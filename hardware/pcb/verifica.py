#!/usr/bin/env python3
"""Verificările de proiectare pe care ERC-ul și DRC-ul nu le fac (PCB-4).

    python3 verifica.py           # regulile; iese cu 1 dacă vreuna e încălcată
    python3 verifica.py --pini    # și tabelul pin → rețea al circuitelor

Citește `circuit.py` (legăturile), schema (numele pinilor din simboluri),
placa (distanțele) și `firmware/boards/include/board_devkit.h` (pinii folosiți
de firmware). Motivele regulilor, cu trimiterile la foile de catalog, sunt în
`REVIEW.md`. Erorile opresc `make.sh`; avertismentele doar se afișează.
"""
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import circuit                                       # noqa: E402
from sexp import parse                               # noqa: E402

SCH = os.path.join(HERE, 'eink-reader-pcb.kicad_sch')
PCB = os.path.join(HERE, 'eink-reader-pcb.kicad_pcb')
DEVKIT = os.path.join(HERE, '../../firmware/boards/include/board_devkit.h')

erori, avert = [], []
def eroare(m): erori.append(m)
def atentie(m): avert.append(m)

PARTS = {p['ref']: p for p in circuit.PARTS}

# ---------------------------------------------------------------- numele pinilor
def pin_names():
    """lib_id → {număr: nume}, din simbolurile copiate în schemă."""
    sch = parse(open(SCH, encoding='utf-8').read())
    libs = next(x for x in sch if isinstance(x, list) and x[0] == 'lib_symbols')
    out = {}
    def walk(node, acc):
        for x in node:
            if not isinstance(x, list):
                continue
            if x[0] == 'pin':
                f = {y[0]: y[1].strip('"') for y in x if isinstance(y, list) and y[0] in ('name', 'number')}
                acc[f['number']] = f['name']
            else:
                walk(x, acc)
    for s in libs[1:]:
        acc = {}
        walk(s, acc)
        out[s[1].strip('"')] = acc
    return out

NAMES = pin_names()

def name_of(ref, pin):
    return NAMES[PARTS[ref]['lib']].get(pin, '?')

def net_of(ref, pin):
    return PARTS[ref]['pins'].get(pin)

def pins_on(net):
    return [(r, n) for r, p in PARTS.items() for n, x in p['pins'].items() if x == net]

# ---------------------------------------------------------------- pinii lăsați liberi
# Fiecare pin neconectat al unui circuit trebuie să fie aici, cu motivul lui.
LIBERI = {
    ('J1', 'A8'): 'SBU1, fără rost la USB 2.0',
    ('J1', 'B8'): 'SBU2, fără rost la USB 2.0',
    ('U2', '14'): 'TMR liber: temporizatoarele implicite (foaia BQ24074, 9.3.6)',
    ('U2', '15'): 'ITERM liber: terminarea implicită, 10 % (foaia BQ24074, 9.3.5)',
    ('U7', '15'): 'IO3, pin de configurare la pornire: liber = sursa JTAG implicită',
    ('U7', '16'): 'IO46, pin de configurare la pornire: liber = coborât intern, pornire din flash',
    ('U7', '26'): 'IO45, pin de configurare la pornire: liber = coborât intern, VDD_SPI 3,3 V',
    ('U7', '28'): 'IO35, ocupat de PSRAM-ul octal (N16R8)',
    ('U7', '29'): 'IO36, ocupat de PSRAM-ul octal (N16R8)',
    ('U7', '30'): 'IO37, ocupat de PSRAM-ul octal (N16R8)',
    **{('U7', str(n)): f'IO{g}, liber (JTAG/rezervă)' for n, g in zip(range(31, 36), range(38, 43))},
}

def check_liberi():
    for ref, p in PARTS.items():
        if ref[0] not in 'UJ':
            continue
        for num in NAMES[p['lib']]:
            if num not in p['pins'] and (ref, num) not in LIBERI:
                eroare(f'{ref}.{num} ({name_of(ref, num)}) e neconectat și nu e în lista pinilor liberi')
    for (ref, num) in LIBERI:
        if net_of(ref, num):
            eroare(f'{ref}.{num} e în lista pinilor liberi, dar e legat la {net_of(ref, num)}')

# ---------------------------------------------------------------- ESP32-S3
def gpio(name):
    """Numele pinului din simbolul modulului → numărul GPIO."""
    fixed = {'USB_D-': 19, 'USB_D+': 20, 'RXD0': 44, 'TXD0': 43}
    if name in fixed:
        return fixed[name]
    m = re.fullmatch(r'IO(\d+)', name)
    return int(m.group(1)) if m else None

def esp_map():
    """rețea → GPIO, pentru pinii modulului legați la ceva."""
    out = {}
    for num, net in PARTS['U7']['pins'].items():
        g = gpio(name_of('U7', num))
        if g is not None:
            out[net] = g
    return out

def check_esp():
    m = esp_map()
    # USB-ul intern e doar pe GPIO19 (D−) și GPIO20 (D+)
    if m.get('ESP_DN') != 19 or m.get('ESP_DP') != 20:
        eroare(f'USB: D− pe GPIO{m.get("ESP_DN")}, D+ pe GPIO{m.get("ESP_DP")}; trebuie 19 și 20')
    # între modul și mufă câte o rezistență serie de 22–33 Ω (ghidul Espressif, USB)
    for esp, usb in (('ESP_DP', 'USB_DP'), ('ESP_DN', 'USB_DN')):
        rs = [r for r, _ in pins_on(esp) if r.startswith('R')]
        if len(rs) != 1 or set(PARTS[rs[0]]['pins'].values()) != {esp, usb} \
                or PARTS[rs[0]]['value'] not in ('22', '27', '33'):
            eroare(f'{esp}: lipsește rezistența serie de 22–33 Ω spre {usb}')
    # cardul și panoul pe bus-uri separate: altfel unul îl alimentează pe celălalt prin linii
    comune = {n for n in PARTS['J3']['pins'].values()} & {n for n in PARTS['J4']['pins'].values()} - {'GND'}
    if comune:
        eroare(f'cardul și panoul împart {sorted(comune)}: alimentare inversă când una din șine e oprită')
    # PSRAM-ul octal ocupă GPIO35–37
    for net, g in m.items():
        if g in (35, 36, 37):
            eroare(f'{net} pe GPIO{g}: pinul e ocupat de PSRAM-ul octal')
    # ADC2 nu merge cât e pornit WiFi-ul: tensiunile se citesc pe ADC1 (GPIO1–10)
    for net in ('VBAT_SENSE', 'VBUS_SENSE'):
        if not 1 <= m.get(net, 0) <= 10:
            eroare(f'{net} pe GPIO{m.get(net)}: trebuie un pin ADC1 (GPIO1–10)')
    # trezirea din somnul adânc (ext1) și rezistențele RTC: doar GPIO0–21
    for net in ('BTN_PREV', 'BTN_NEXT', 'BTN_MENU', 'FG_ALRT'):
        if not 0 <= m.get(net, -1) <= 21:
            eroare(f'{net} pe GPIO{m.get(net)}: trebuie un pin RTC (GPIO0–21)')
    # GPIO0: BOOT, cu rezistență spre 3,3 V și buton spre masă
    if m.get('BOOT') != 0:
        eroare('BOOT nu e pe GPIO0')
    ups = [r for r, n in pins_on('BOOT') if r.startswith('R') and '+3V3' in PARTS[r]['pins'].values()]
    if not ups:
        eroare('GPIO0 (BOOT) nu are rezistență spre +3V3')
    # EN: RC 10 kΩ / 1 µF, cum recomandă ghidul Espressif
    rc = sorted(PARTS[r]['value'] for r, _ in pins_on('ESP_EN') if r[0] in 'RC')
    if rc != ['10k', '1uF']:
        eroare(f'EN are {rc}; ghidul Espressif cere 10 kΩ spre 3,3 V și 1 µF spre masă')

def check_firmware():
    """Pinii comuni cu prototipul de pe DevKit trebuie să rămână aceiași, ca
    firmware-ul să meargă pe placă doar cu un antet nou (PCB-8)."""
    # pe DevKit bus-ul e comun; pe placă pinii lui au rămas cardului
    nume = {'BAT_ADC_GPIO': 'VBAT_SENSE', 'SPI_SCK': 'SD_SCK', 'SPI_MOSI': 'SD_MOSI', 'SPI_MISO': 'SD_MISO'}
    m = esp_map()
    src = open(DEVKIT, encoding='utf-8').read()
    for k, g in re.findall(r'#define BOARD_(\w+)\s+GPIO_NUM_(\d+)', src):
        net = nume.get(k, k)
        if net not in m:
            atentie(f'firmware: BOARD_{k} (GPIO{g}) nu are rețea „{net}” pe placă')
        elif m[net] != int(g):
            atentie(f'firmware: BOARD_{k} e GPIO{g} pe DevKit, dar {net} e GPIO{m[net]} pe placă')
    d = re.search(r'BOARD_BAT_DIVIDER\s+([\d.]+)', src)
    r15, r16 = (float(PARTS[r]['value'].rstrip('M')) for r in ('R15', 'R16'))
    if d and abs((r15 + r16) / r16 - float(d.group(1))) > 1e-6:
        eroare(f'divizorul bateriei e {(r15 + r16) / r16:g} pe placă și {d.group(1)} în firmware')

# ---------------------------------------------------------------- polaritatea
def check_polaritate():
    # LED-urile: catodul (pinul 1 al Device:LED) pe ieșirea open-drain, anodul spre VSYS prin rezistență
    for d, od in (('D1', 'PGOOD_N'), ('D2', 'CHG_N')):
        if name_of(d, '1') != 'K' or net_of(d, '1') != od:
            eroare(f'{d}: catodul nu e pe {od}')
    # USBLC6: VBUS pe pinul 5, masa pe 2, liniile de date pe perechile 1/6 și 3/4
    if (net_of('U1', '5'), net_of('U1', '2')) != ('VBUS', 'GND'):
        eroare('U1: VBUS și GND nu sunt pe pinii 5 și 2')
    if {net_of('U1', '1'), net_of('U1', '6')} != {'USB_DP'} or {net_of('U1', '3'), net_of('U1', '4')} != {'USB_DN'}:
        eroare('U1: perechile 1/6 și 3/4 nu sunt câte o linie de date')
    # USB-C: câte 5,1 kΩ spre masă pe fiecare CC (sursa vede un consumator)
    for cc in ('CC1', 'CC2'):
        rs = [PARTS[r]['value'] for r, _ in pins_on(cc) if r.startswith('R')]
        if rs != ['5.1k']:
            eroare(f'{cc}: are {rs}, trebuie o singură rezistență de 5,1 kΩ spre masă')
    # încărcătorul: /CE pe masă (încărcarea pornită), ILIM legat (altfel nu încarcă)
    if net_of('U2', '4') != 'GND':
        eroare('U2: /CE nu e pe masă, încărcarea e oprită')
    for pin, nm in (('12', 'ILIM'), ('16', 'ISET'), ('1', 'TS')):
        if not any(r.startswith('R') for r, _ in pins_on(net_of('U2', pin) or '-')):
            eroare(f'U2: {nm} nu are rezistența lui')
    # TPS22917: condensatorul CT merge la VIN (foaia TPS22917, 9.3.2)
    for u in ('U4', 'U5'):
        ct = net_of(u, '4')
        caps = [r for r, _ in pins_on(ct) if r.startswith('C')]
        if not caps or '+3V3' not in PARTS[caps[0]]['pins'].values():
            eroare(f'{u}: condensatorul de pe CT nu e legat la VIN (+3V3)')

# ---------------------------------------------------------------- placa
DECUPLARE = [  # condensator, circuit, pin, distanța maximă (mm)
    ('C15', 'U7', '2', 4), ('C14', 'U7', '2', 4), ('C4', 'U3', '10', 4), ('C5', 'U3', '6', 4),
    ('C1', 'U2', '13', 4), ('C2', 'U2', '10', 4), ('C3', 'U2', '2', 4), ('C12', 'U6', '3', 4),
    ('C9', 'U4', '6', 4), ('C11', 'U5', '6', 4),
    # pinul 4 al cardului e sub carcasa metalică a soclului; mai aproape nu se poate
    ('C10', 'J4', '4', 12),
]
PUNCTE_TEST = ['VBUS', 'VSYS', 'BAT_J', 'VBAT', '+3V3', '3V3_SD', '3V3_EPD', 'GND']

def check_placa():
    try:
        import pcbnew
    except ImportError:
        atentie('fără pcbnew: verificările pe placă sar')
        return
    from pcbnew import ToMM, FromMM, VECTOR2I
    b = pcbnew.LoadBoard(PCB)
    fp = {f.GetReference(): f for f in b.GetFootprints()}
    def pad(ref, num):
        return next(p for p in fp[ref].Pads() if p.GetNumber() == num)
    def mm(p):
        q = p.GetPosition()
        return ToMM(q.x), ToMM(q.y)
    for c, ic, pin, lim in DECUPLARE:
        (x1, y1), (x2, y2) = mm(pad(c, '1')), mm(pad(ic, pin))
        d = math.hypot(x1 - x2, y1 - y2)
        if d > lim:
            eroare(f'{c} e la {d:.1f} mm de {ic}.{pin} (maxim {lim} mm)')
    # antena modulului: niciun cupru sub ea, pe niciun strat
    edge = b.GetBoardEdgesBoundingBox()
    x0, x1 = ToMM(edge.GetRight()) - 5.9, ToMM(edge.GetRight()) - 0.1
    u7 = fp['U7'].GetPosition()
    y0, y1 = ToMM(u7.y) - 8.5, ToMM(u7.y) + 8.5
    pts = [VECTOR2I(FromMM(x0 + (x1 - x0) * i / 6), FromMM(y0 + (y1 - y0) * j / 8))
           for i in range(7) for j in range(9)]
    for z in b.Zones():
        for layer in z.GetLayerSet().Seq():
            poly = z.GetFilledPolysList(layer)
            if any(poly.Contains(p) for p in pts):
                eroare(f'cupru ({z.GetNetname()}, {b.GetLayerName(layer)}) sub antena modulului')
    for t in b.GetTracks():
        for q in (t.GetStart(), t.GetEnd()):
            if x0 <= ToMM(q.x) <= x1 and y0 <= ToMM(q.y) <= y1:
                eroare(f'traseu {t.GetNetname()} sub antena modulului')
                break
    # perechea USB: lungimile și diferența (USB 2.0 full speed iartă mult)
    lung = {}
    for net in ('USB_DP', 'USB_DN'):         # cu bucata dintre rezistența serie și modul
        tr = [t for t in b.GetTracks() if t.GetNetname() in (net, 'ESP' + net[3:])]
        lung[net] = (sum(ToMM(t.GetLength()) for t in tr if t.Type() != pcbnew.PCB_VIA_T),
                     sum(1 for t in tr if t.Type() == pcbnew.PCB_VIA_T))
    dp, dn = lung['USB_DP'], lung['USB_DN']
    if max(dp[0], dn[0]) > 100 or abs(dp[0] - dn[0]) > 10:
        eroare(f'USB: D+ {dp[0]:.0f} mm, D− {dn[0]:.0f} mm; prea lungi sau prea diferite')
    elif max(dp[0], dn[0]) > 40 or dp[1] + dn[1] > 4:
        atentie(f'USB: D+ {dp[0]:.0f} mm/{dp[1]} via, D− {dn[0]:.0f} mm/{dn[1]} via; '
                'merge la full speed, dar e de scurtat la v2')
    # punctele de test pe fiecare șină
    tps = {PARTS[r]['pins']['1'] for r in PARTS if r.startswith('TP')}
    for net in PUNCTE_TEST:
        if net not in tps:
            atentie(f'fără punct de test pe {net} (se măsoară pe condensatorul de ieșire)')

# ---------------------------------------------------------------- tabelul
def tabel():
    for ref, p in PARTS.items():
        if ref[0] not in 'UJ' and ref != 'SW1':
            continue
        print(f'{ref} {p["value"]}')
        for num in sorted(NAMES[p['lib']], key=lambda k: (len(k), k)):
            net = p['pins'].get(num) or f'— {LIBERI.get((ref, num), "neconectat")}'
            print(f'   {num:>4} {name_of(ref, num):<10} {net}')

if __name__ == '__main__':
    if '--pini' in sys.argv:
        tabel()
    for f in (check_liberi, check_esp, check_firmware, check_polaritate, check_placa):
        f()
    for m in avert:
        print('   avertisment:', m)
    for m in erori:
        print('   EROARE:', m)
    print(f'Verificări de proiectare: {len(erori)} erori, {len(avert)} avertismente')
    sys.exit(1 if erori else 0)
