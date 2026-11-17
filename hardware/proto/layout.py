#!/usr/bin/env python3
"""Desenul plăcii de prototipare (HW-6), generat din cod.

Placa de prototipare stă în spatele afișajului WeAct, prinsă în găurile lui de M2.
Scriptul așază modulele pe grila de 2,54 mm, ia conexiunile din schema KiCad
(`../eink-reader.kicad_sch`, prin kicad-cli), trasează firele pe fața de lipire
cu A* pe grilă, verifică totul și scrie:

  out/sablon.pdf       șablonul 1:1 pe A4: fața pieselor și fața de lipire
  out/fata.svg/.png    fața pieselor (văzută din spatele cititorului)
  out/spate.svg/.png   fața de lipire, oglindită
  FIRE.md              piesele, lista firelor, ce se măsoară înainte

Coordonatele sunt în mm, în sistemul plăcii proprii (hardware/pcb): x spre
dreapta, y în jos, văzut din spate, cu baretele afișajului sus.
"""
import heapq, math, os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'out')
SCH = os.path.join(HERE, '..', 'eink-reader.kicad_sch')

# ---------------------------------------------------------------- grila
P = 2.54
# Bareta afișajului (pinul 1) la (36, 4), ca în hardware/pcb/gen_pcb.py: grila se
# aliniază pe ea, ca soclul să cadă exact sub pinii modulului.
X0, Y0 = 36.0 - 13 * P, 4.0 - P
NC, NR = 41, 21                                   # găuri: 41 × 21
ROWS = 'ABCDEFGHJKLMNPRSTUVWX'                    # fără I, O, Q
BX0, BY0 = X0 - P / 2, Y0 - P / 2                 # marginile plăcii tăiate
BX1, BY1 = BX0 + NC * P, BY0 + NR * P

def hole(c, r): return (X0 + (c - 1) * P, Y0 + (r - 1) * P)
def hname(c, r): return f'{ROWS[r - 1]}{c}'

# Afișajul și găurile lui de prindere (hardware/pcb/gen_pcb.py, H1–H4).
EPD_W, EPD_H = 105.79, 53.80
DRILL = [(6.395, 2.9), (98.2, 2.9), (6.395, 50.9), (98.2, 50.9)]
DRILL_D = 2.2                                     # M2
KEEP_BODY = 2.2                                   # capul șurubului Ø3,8 + joc
KEEP_HOLE = 2.6                                   # distanțierul: găurile grilei de aici nu se folosesc

# ---------------------------------------------------------------- piesele
# status: 'masurat' (din desenul oficial), 'desen' (din desenul vânzătorului,
# de verificat pe piesă) sau 'estimat' (mărime tipică, de măsurat).
# side: 'piese' = pe fața pieselor; 'ecran' = pe fața dinspre afișaj.
# mount: 'soclu' = pini pe grilă; 'fire' = modul lipit cu bandă, pini legați cu fire scurte.
PARTS = []
PINS = {}                 # id schemă (ex. 'U1.J1.4') → dict(ref, name, hole|None, xy)

def part(ref, title, rect, status, side='piese', mount='soclu', note='', raised=False, extra=None):
    d = dict(ref=ref, title=title, rect=rect, status=status, side=side, mount=mount,
             note=note, raised=raised, extra=extra or [], pins=[])
    PARTS.append(d)
    return d

def pin(p, sch, name, c=None, r=None, xy=None):
    """Un pin cu gaura lui (c, r), sau un pad de modul la xy (atunci gaura e ancora)."""
    h = (c, r) if c else None
    if xy is None: xy = hole(c, r)
    for s in ([sch] if isinstance(sch, str) else sch):
        PINS[s] = dict(ref=p['ref'], name=name, hole=h, xy=xy, part=p)
    p['pins'].append(dict(sch=sch, name=name, hole=h, xy=xy))

# --- U1: ESP32-S3-DevKitC-1, pe două socluri mamă 1×22. USB spre stânga, antena spre dreapta.
# Desenul Espressif (DXF_ESP32-S3-DevKitC-1_V1.1_20220429): 62,74 × 25,40 mm, pinii la
# 1,27 mm de margini (rândurile la 22,86 mm = 9 găuri), ultimul pin la 8,00 mm de capătul USB.
J1_ROW, J3_ROW, PIN22_COL = 12, 21, 7
J1 = ['3V3', '3V3', 'RST', 'IO4', 'IO5', 'IO6', 'IO7', 'IO15', 'IO16', 'IO17', 'IO18', 'IO8',
      'IO3', 'IO46', 'IO9', 'IO10', 'IO11', 'IO12', 'IO13', 'IO14', '5V', 'G']
J3 = ['G', 'TX', 'RX', 'IO1', 'IO2', 'IO42', 'IO41', 'IO40', 'IO39', 'IO38', 'IO37',
      'IO36', 'IO35', 'IO0', 'IO45', 'IO48', 'IO47', 'IO21', 'IO20', 'IO19', 'G', 'G']
def jcol(k): return PIN22_COL + 22 - k            # pinul k (1..22) → coloana
x_usb = hole(PIN22_COL, 1)[0] - 8.00
x_ant = hole(jcol(1), 1)[0] + 1.40               # 62,74 − 8,00 − 21 × 2,54
y_top = hole(1, J1_ROW)[1] - 1.27
u1 = part('U1', 'ESP32-S3-DevKitC-1', (x_usb, y_top, x_ant, y_top + 25.40), 'masurat',
          note='pe 2 socluri mamă 1×22; USB spre stânga', raised=True)
# Antena modulului WROOM iese peste capătul plăcii; ~6,3 mm, citit la scară din desen.
ANT = (x_ant, y_top + 12.70 - 9.0, x_ant + 6.3, y_top + 12.70 + 9.0)
u1['extra'].append(('antena', ANT, 'estimat'))
for k, n in enumerate(J1, 1): pin(u1, f'U1.J1.{k}', n, jcol(k), J1_ROW)
for k, n in enumerate(J3, 1): pin(u1, f'U1.J3.{k}', n, jcol(k), J3_ROW)
# Pinii de G și cei doi de 3V3 sunt legați în DevKit; pe placă se leagă doar unul din fiecare,
# ca rețelele să rămână întregi și cu DevKit-ul scos (la verificarea dinainte de a-l pune).
INTERNAL = {'GND': ['U1.J3.1', 'U1.J3.21', 'U1.J3.22'], '+3V3': ['U1.J1.2']}

# --- U4: afișajul, printr-un soclu mamă 1×8 pe fața dinspre ecran.
epd = part('U4', 'soclu afișaj 1×8', (hole(14, 2)[0] - 1.27, hole(14, 2)[1] - 1.27,
                                      hole(21, 2)[0] + 1.27, hole(21, 2)[1] + 1.27),
           'desen', side='ecran', note='pe fața dinspre ecran, lipit pe fața pieselor')
for k, n in enumerate(['BUSY', 'RES', 'D/C', 'CS', 'SCL', 'SDA', 'GND', 'VCC'], 1):
    pin(epd, f'U4.{k}', n, 13 + k, 2)

# --- U5: modulul microSD, pe bareta lui de 6 pini; fanta cardului spre marginea de sus.
SD_ROW, SD_COL = 18, 34
sdx = (hole(SD_COL, 1)[0] + hole(SD_COL + 5, 1)[0]) / 2
sdy = hole(1, SD_ROW)[1]
u5 = part('U5', 'modul microSD', (sdx - 12.0, sdy + 2.5 - 42.0, sdx + 12.0, sdy + 2.5), 'estimat',
          note='42 × 24 mm, tipic; bareta de 6 pini jos, fanta sus', raised=True)
for k, n in enumerate(['GND', 'VCC', 'MISO', 'MOSI', 'SCK', 'CS'], 1):
    pin(u5, f'U5.{k}', n, SD_COL + k - 1, SD_ROW)

# --- U2: TP4056 cu USB-C și protecție DW01, cu USB-ul la marginea din stânga.
# Padurile de ieșire la capătul din dreapta; IN+/IN− nu se folosesc (se încarcă prin USB-C).
tx0, ty0 = BX0 + 0.2, 6.9
u2 = part('U2', 'TP4056', (tx0, ty0, tx0 + 28.0, ty0 + 17.5), 'estimat', mount='fire',
          note='28 × 17,5 mm, tipic; bandă dublu adezivă, USB-C la margine')
px = tx0 + 26.5
for sch, n, dy, c, r in [('U2.5', 'OUT+', 1.5, 12, 4), ('U2.3', 'B+', 5.5, None, None),
                         ('U2.4', 'B−', 12.0, None, None), ('U2.6', 'OUT−', 16.0, 12, 9)]:
    pin(u2, sch, n, c, r, xy=(px, ty0 + dy)) if c else pin(u2, sch, n, xy=(px, ty0 + dy))
# Bateria: cablul JST se lipește direct pe B+ / B−, fără găuri în placă.
PINS['BT1.1'] = dict(ref='BT1', name='+ (roșu)', hole=None, xy=None, part=None)
PINS['BT1.2'] = dict(ref='BT1', name='− (negru)', hole=None, xy=None, part=None)

# --- U3: modulul buck-boost TPS63020, ieșire 3,3 V.
ux0, uy0 = 58.0, 7.0
u3 = part('U3', 'TPS63020', (ux0, uy0, ux0 + 20.0, uy0 + 14.0), 'estimat', mount='fire',
          note='20 × 14 mm, tipic; bandă dublu adezivă')
for sch, n, dy, r in [('U3.1', 'VIN', 2.5, 4), ('U3.2', 'GND', 7.0, 6), ('U3.3', 'VOUT', 11.5, 8)]:
    pin(u3, sch, n, 22, r, xy=(ux0 + 1.5, uy0 + dy))

# --- SW1: comutatorul glisant SS12D00, la marginea din stânga.
sw1 = part('SW1', 'SS12D00', (hole(3, 1)[0] - 1.75, hole(1, 14)[1] - 2.0,
                              hole(3, 1)[0] + 1.75, hole(1, 16)[1] + 2.0), 'estimat',
           note='3 pini; se folosesc mijlocul și cel de sus')
pin(sw1, 'SW1.2', 'VSYS', 3, 14)
pin(sw1, 'SW1.1', 'comun', 3, 15)
pin(sw1, 'SW1.x', 'liber', 3, 16)

# --- J_BTN: baretă tată 1×4 spre plăcuța butoanelor (SW2–SW4, fiecare între linia lui și GND).
jb = part('JBTN', 'baretă butoane 1×4', (hole(22, 10)[0] - 1.27, hole(22, 10)[1] - 1.27,
                                         hole(25, 10)[0] + 1.27, hole(25, 10)[1] + 1.27),
          'standard', note='cablu cu 4 fire spre plăcuța cu cele 3 butoane')
pin(jb, ['SW2.2', 'SW3.2', 'SW4.2'], 'GND', 22, 10)
pin(jb, 'SW4.1', 'MENIU', 23, 10)
pin(jb, 'SW3.1', 'ÎNAINTE', 24, 10)
pin(jb, 'SW2.1', 'ÎNAPOI', 25, 10)

# --- Divizorul bateriei, culcat sub DevKit (încape: DevKit-ul stă ~8,5 mm deasupra).
for ref, title, a, b, an, bn, sa, sb in [
        ('R1', '100k', (22, 15), (26, 15), 'VSYS', 'SENS', 'R1.1', 'R1.2'),
        ('R2', '100k', (26, 17), (22, 17), 'SENS', 'GND', 'R2.1', 'R2.2'),
        ('C1', '100nF', (26, 19), (24, 19), 'SENS', 'GND', 'C1.1', 'C1.2')]:
    xa, ya = hole(*a); xb, yb = hole(*b)
    d = part(ref, f'{ref} {title}', (min(xa, xb) + 1.2, ya - 1.1, max(xa, xb) - 1.2, ya + 1.1),
             'standard', note='culcat, sub DevKit')
    pin(d, sa, an, *a); pin(d, sb, bn, *b)

# ---------------------------------------------------------------- rețelele, din schemă
def schematic_nets():
    with tempfile.TemporaryDirectory() as td:
        nf = os.path.join(td, 'n.net')
        subprocess.run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadsexpr',
                        '-o', nf, SCH], check=True, capture_output=True)
        t = open(nf, encoding='utf-8').read()
    t = t[t.index('(nets'):]
    nets = {}
    for blk in t.split('(net\n')[1:]:
        name = re.search(r'\(name "([^"]*)"\)', blk).group(1).lstrip('/')
        nodes = [f'{r}.{p}' for r, p in re.findall(r'\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', blk)]
        if len(nodes) > 1: nets[name] = nodes
    return nets

# ---------------------------------------------------------------- trasarea
def route(nets):
    """Fiecare rețea: arbore între găurile ei, fire izolate trasate pe grilă cu A*.
    Un fir nu trece peste gaura altei rețele; poate trece peste alt fir (e izolat), dar
    încrucișările costă, ca desenul să rămână ușor de urmărit."""
    pinhole = {}                                  # gaură → rețea ('' = pin nefolosit)
    for p in PARTS:
        for q in p['pins']:
            if q['hole']: pinhole[q['hole']] = ''
    for net, nodes in nets.items():
        for s in nodes:
            h = PINS.get(s, {}).get('hole')
            if h: pinhole[h] = net
    blocked = {(c, r) for c in range(1, NC + 1) for r in range(1, NR + 1)
               if min(math.dist(hole(c, r), d) for d in DRILL) < KEEP_HOLE}
    used = {}                                     # gaură → rețeaua firului care trece pe acolo
    def antenna_cost(n):
        x, y = hole(*n)
        return 3 if ANT[0] - 1 <= x <= ANT[2] and ANT[1] <= y <= ANT[3] else 0

    def astar(starts, goals, net):
        goals = set(goals)
        pq = [(0, 0, s, None, (s,)) for s in starts]
        seen = {}
        while pq:
            f, g, n, d, path = heapq.heappop(pq)
            if n in goals: return g, list(path)
            if seen.get((n, d), 1e9) <= g: continue
            seen[(n, d)] = g
            for dd in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                m = (n[0] + dd[0], n[1] + dd[1])
                if not (1 <= m[0] <= NC and 1 <= m[1] <= NR) or m in blocked: continue
                if pinhole.get(m, net) != net: continue
                cross = 4 if used.get(m, net) != net else 0
                ng = g + 1 + (1.5 if d and d != dd else 0) + antenna_cost(m) + cross
                h = min(abs(m[0] - q[0]) + abs(m[1] - q[1]) for q in goals)
                heapq.heappush(pq, (ng + h, ng, m, dd, path + (m,)))
        return None

    wires, order = [], sorted(nets, key=lambda n: (n not in POWER, n))
    for net in order:
        holes = []
        for s in nets[net]:
            h = PINS.get(s, {}).get('hole')
            if h and h not in holes and s not in sum(INTERNAL.values(), []): holes.append(h)
        if len(holes) < 2: continue
        tree, rest = [holes[0]], holes[1:]
        while rest:
            best = None
            for h in rest:
                r = astar([h], tree, net)
                if r and (best is None or r[0] < best[0]): best = (r[0], h, r[1])
            if best is None: sys.exit(f'EROARE: rețeaua {net} nu se poate trasa')
            _, h, path = best
            for n in path: used.setdefault(n, net)
            wires.append(dict(net=net, path=path))
            tree.append(h); rest.remove(h)
    return wires, pinhole, blocked

POWER = ['GND', '+3V3', 'VSYS', 'Net-(SW1-A)']
NETNAME = {'+3V3': '3V3', 'Net-(SW1-A)': 'OUT+', 'Net-(BT1-+)': 'BAT+', 'Net-(BT1--)': 'BAT−'}
COLOR = {'GND': '#222222', '+3V3': '#C0392B', 'VSYS': '#D35400', 'Net-(SW1-A)': '#B9770E',
         'VBAT_SENSE': '#7D3C98'}
SIGCOL = ['#1F618D', '#117A65', '#2874A6', '#148F77', '#5B2C6F', '#1A5276', '#0E6655', '#6C3483']

def ncolor(net, i): return COLOR.get(net, SIGCOL[i % len(SIGCOL)])
def nname(net): return NETNAME.get(net, net)

# ---------------------------------------------------------------- verificări
def check(nets, wires, pinhole, blocked):
    errs, warns = [], []
    # 1. fiecare pin de soclu cade pe o gaură, nicio gaură cu doi pini
    seen = {}
    for p in PARTS:
        for q in p['pins']:
            if not q['hole']: continue
            if q['hole'] in seen: errs.append(f'gaura {hname(*q["hole"])} are doi pini')
            seen[q['hole']] = p['ref']
            if not (1 <= q['hole'][0] <= NC and 1 <= q['hole'][1] <= NR):
                errs.append(f'{p["ref"]}.{q["name"]} în afara plăcii')
            if q['hole'] in blocked:
                errs.append(f'{p["ref"]}.{q["name"]} pe o gaură stricată de gaura de prindere')
    # 2. corpurile: în placă, fără suprapuneri pe aceeași față, departe de șuruburi
    def ov(a, b): return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
    for p in PARTS:
        x0, y0, x1, y1 = p['rect']
        if x0 < BX0 - 0.3 or y0 < BY0 - 0.3 or x1 > BX1 + 0.3 or y1 > BY1 + 0.3:
            errs.append(f'{p["ref"]} iese din placă')
        for d in DRILL:
            cx, cy = min(max(d[0], x0), x1), min(max(d[1], y0), y1)
            if math.dist((cx, cy), d) < KEEP_BODY:
                errs.append(f'{p["ref"]} stă peste șurubul de la {d}')
    for i, a in enumerate(PARTS):
        for b in PARTS[i + 1:]:
            if a['side'] != b['side'] or not ov(a['rect'], b['rect']): continue
            if {a['ref'], b['ref']} & {'R1', 'R2', 'C1'} and 'U1' in (a['ref'], b['ref']): continue
            errs.append(f'{a["ref"]} și {b["ref"]} se suprapun')
        if a['ref'] != 'U1' and a['side'] == 'piese' and ov(a['rect'], ANT):
            errs.append(f'{a["ref"]} stă sub antenă')
    # 3. ancorele modulelor cu fire: în afara corpului, la cel mult 6 mm de pad
    for p in PARTS:
        if p['mount'] != 'fire': continue
        for q in p['pins']:
            if not q['hole']: continue
            x, y = hole(*q['hole'])
            x0, y0, x1, y1 = p['rect']
            if x0 < x < x1 and y0 < y < y1: errs.append(f'ancora {p["ref"]}.{q["name"]} e sub modul')
            if math.dist((x, y), q['xy']) > 6: warns.append(f'piciorul {p["ref"]}.{q["name"]} e lung')
    # 4. firele realizează exact rețelele din schemă
    parent = {}
    def find(a):
        while parent.setdefault(a, a) != a: a = parent[a]
        return a
    for w in wires:
        parent[find(w['path'][0])] = find(w['path'][-1])      # firul e izolat: leagă doar capetele
        for n in w['path']:
            if pinhole.get(n, w['net']) != w['net']: errs.append(f'firul {w["net"]} atinge {hname(*n)}')
    internal = sum(INTERNAL.values(), [])
    on_board = 0
    for net, nodes in nets.items():
        hs = [PINS[s]['hole'] for s in nodes if s in PINS and PINS[s]['hole'] and s not in internal]
        missing = [s for s in nodes if s not in PINS]
        if missing: errs.append(f'{net}: pini din schemă fără loc pe placă: {missing}')
        if len({find(h) for h in hs}) > 1: errs.append(f'{net} nu e legată complet')
        on_board += len(hs) > 1
    groups = {}
    for net, nodes in nets.items():
        for s in nodes:
            h = PINS.get(s, {}).get('hole')
            if h: groups.setdefault(find(h), set()).add(net)
    for g, ns in groups.items():
        if len(ns) > 1: errs.append(f'scurtcircuit între {sorted(ns)}')
    # 5. firele de sub antenă (pe fața de lipire, deci departe de ea, dar de știut)
    under = sum(1 for w in wires for n in w['path']
                if ANT[0] <= hole(*n)[0] <= ANT[2] and ANT[1] <= hole(*n)[1] <= ANT[3])
    if under: warns.append(f'{under} puncte de fir sub antenă')
    return errs, warns, on_board

# ---------------------------------------------------------------- desenul
FONT = "font-family='DejaVu Sans, Liberation Sans, Arial, sans-serif'"
STATUS_TXT = {'masurat': 'măsurat (desen oficial)', 'standard': 'piesă standard, pas 2,54', 'desen': 'din desen, de verificat pe piesă',
              'estimat': 'estimat, de măsurat'}

def svg_page(mirror, wires, nets, title, sub):
    OX, OY = 14.0, 34.0                           # colțul afișajului pe pagină
    def X(x): return OX + ((BX0 + BX1 - x) if mirror else x)
    def Y(y): return OY + y
    o = []
    def t(x, y, s, size=2.0, anchor='start', w='normal', fill='#222', rot=0):
        tr = f" transform='rotate({rot} {x:.2f} {y:.2f})'" if rot else ''
        o.append(f"<text x='{x:.2f}' y='{y:.2f}' font-size='{size}' text-anchor='{anchor}' "
                 f"font-weight='{w}' fill='{fill}' {FONT}{tr}>{s}</text>")
    def rect(r, stroke='#222', fill='none', sw=0.25, dash=None, op=1):
        x0, x1 = sorted((X(r[0]), X(r[2])))
        da = f" stroke-dasharray='{dash}'" if dash else ''
        o.append(f"<rect x='{x0:.2f}' y='{Y(r[1]):.2f}' width='{x1 - x0:.2f}' height='{r[3] - r[1]:.2f}' "
                 f"stroke='{stroke}' fill='{fill}' stroke-width='{sw}' fill-opacity='{op}'{da}/>")
        return (x0 + x1) / 2, Y((r[1] + r[3]) / 2)
    t(OX, 12, title, 4.2, w='bold')
    t(OX, 18, sub, 2.4, fill='#444')
    # afișajul (contur) și placa tăiată
    rect((0, 0, EPD_W, EPD_H), '#888', dash='1.2 0.8', sw=0.2)
    t(X(EPD_W if not mirror else 0) + (-0.5 if not mirror else 0.5), Y(EPD_H) + 3.0,
      'conturul afișajului, 105,79 × 53,80', 1.6, 'end' if not mirror else 'start', fill='#888')
    rect((BX0, BY0, BX1, BY1), '#000', '#F3F1EA', 0.4)
    # numerotarea găurilor
    for c in range(1, NC + 1):
        if c % 2 == 1 or c == NC: t(X(hole(c, 1)[0]), Y(BY0) - 1.0, str(c), 1.3, 'middle', fill='#666')
    for r in range(1, NR + 1):
        xl = X(BX0 if not mirror else BX1)
        t(xl - 1.2 if not mirror else xl + 1.2, Y(hole(1, r)[1]) + 0.45, ROWS[r - 1], 1.3,
          'end' if not mirror else 'start', fill='#666')
    # găurile grilei
    for c in range(1, NC + 1):
        for r in range(1, NR + 1):
            x, y = hole(c, r)
            o.append(f"<circle cx='{X(x):.2f}' cy='{Y(y):.2f}' r='0.45' fill='none' stroke='#B8B2A2' stroke-width='0.15'/>")
    # găurile de prindere
    for d in DRILL:
        cx, cy = X(d[0]), Y(d[1])
        o.append(f"<circle cx='{cx:.2f}' cy='{cy:.2f}' r='{DRILL_D / 2}' fill='#fff' stroke='#000' stroke-width='0.3'/>")
        o.append(f"<path d='M{cx - 2:.2f} {cy:.2f}h4M{cx:.2f} {cy - 2:.2f}v4' stroke='#000' stroke-width='0.15'/>")
    # piesele
    for p in PARTS:
        on_this = (p['side'] == 'piese') != mirror
        dash = None if p['status'] in ('masurat', 'standard') else ('1.5 0.8' if p['status'] == 'desen' else '0.6 0.6')
        if p['ref'] in ('R1', 'R2', 'C1'):
            stroke = '#555'
        else:
            stroke = '#1B1B1B'
        if on_this or p['ref'] in ('U1', 'U5'):
            cx, cy = rect(p['rect'], stroke if on_this else '#999', '#FFFFFF' if on_this else 'none',
                          0.35 if on_this else 0.2, dash, 0.55 if p['ref'] in ('U1',) else 0.85)
        else:
            cx, cy = rect(p['rect'], '#999', 'none', 0.2, '0.4 0.6')
        for name, r, st in p['extra']:
            ax, ay = rect(r, '#999', 'url(#hatch)', 0.2, '0.6 0.6')
            t(ax, ay + 0.6, 'antenă', 1.5, 'middle', fill='#777')
        if p['ref'] in ('R1', 'R2', 'C1'):
            t(cx, cy - 1.5, p['title'], 1.3, 'middle', fill='#555')
        elif p['ref'] == 'U1':
            t(cx, cy - 4.5, 'U1  ESP32-S3-DevKitC-1', 2.4, 'middle', 'bold')
            t(cx, cy - 1.6, 'pe socluri, ~8,5 mm deasupra', 1.6, 'middle', fill='#444')
            ux = X(p['rect'][0])
            t(ux + (1.2 if not mirror else -1.2), cy + 9.5, 'USB', 1.6, 'start' if not mirror else 'end', fill='#444')
        elif p['ref'] == 'U4':
            ex = X(p['rect'][0]) if not mirror else X(p['rect'][0])
            t(ex - 1.2 if not mirror else ex + 1.2, cy + 0.5, 'U4 soclu afișaj' + ('' if on_this else ' (fața cealaltă)'),
              1.5, 'end' if not mirror else 'start', fill='#222' if on_this else '#888')
        elif p['ref'] == 'JBTN':
            ex = X(p['rect'][0])
            t(ex - 1.2 if not mirror else ex + 1.2, cy + 0.5, 'J_BTN', 1.5, 'end' if not mirror else 'start', 'bold')
        elif p['ref'] == 'SW1':
            t(cx, Y(p['rect'][1]) - 0.8, 'SW1', 1.6, 'middle', 'bold')
        else:
            t(cx, cy - 1.0, f'{p["ref"]}  {p["title"]}', 2.2, 'middle', 'bold', '#222' if on_this else '#888')
            t(cx, cy + 1.8, STATUS_TXT[p['status']], 1.4, 'middle', fill='#666')
            if p['ref'] == 'U5': t(cx, Y(p['rect'][1]) + 2.6, 'fanta cardului ↑', 1.5, 'middle', fill='#444')
            if p['ref'] == 'U2': t(X(p['rect'][0]) + (1.0 if not mirror else -1.0), cy + 6.0, 'USB-C',
                                   1.5, 'start' if not mirror else 'end', fill='#444')
    # firele (pe fața de lipire: pline; pe fața pieselor: șterse)
    netidx = {n: i for i, n in enumerate(sorted(nets))}
    for k, w in enumerate(wires, 1):
        col = ncolor(w['net'], netidx[w['net']])
        d = 'M' + ' L'.join(f'{X(hole(*n)[0]):.2f} {Y(hole(*n)[1]):.2f}' for n in w['path'])
        wd = 0.75 if w['net'] in POWER else 0.5
        o.append(f"<path d='{d}' fill='none' stroke='{col}' stroke-width='{wd}' stroke-linejoin='round' "
                 f"stroke-linecap='round' opacity='{0.9 if mirror else 0.18}'/>")
        if mirror:
            mid = w['path'][len(w['path']) // 2]
            mx, my = X(hole(*mid)[0]), Y(hole(*mid)[1])
            o.append(f"<circle cx='{mx:.2f}' cy='{my:.2f}' r='1.05' fill='#fff' stroke='{col}' stroke-width='0.25'/>")
            t(mx, my + 0.5, str(k), 1.25, 'middle', 'bold', col)
    # picioarele modulelor cu fire (fața pieselor)
    for p in PARTS:
        if p['mount'] != 'fire': continue
        for q in p['pins']:
            px_, py_ = X(q['xy'][0]), Y(q['xy'][1])
            o.append(f"<rect x='{px_ - 0.8:.2f}' y='{py_ - 0.8:.2f}' width='1.6' height='1.6' fill='#C9A227' stroke='#000' stroke-width='0.15'/>")
            lab_dx = -1.3 if not mirror else 1.3
            t(px_ + lab_dx, py_ + 0.5, q['name'], 1.3, 'end' if not mirror else 'start', fill='#333')
            if q['hole'] and not mirror:
                hx, hy = X(hole(*q['hole'])[0]), Y(hole(*q['hole'])[1])
                o.append(f"<path d='M{px_:.2f} {py_:.2f}L{hx:.2f} {hy:.2f}' stroke='#C9A227' stroke-width='0.5'/>")
    # pinii folosiți
    used_pins = {PINS[s]['hole'] for nodes in nets.values() for s in nodes if s in PINS and PINS[s]['hole']}
    for p in PARTS:
        for q in p['pins']:
            if not q['hole']: continue
            x, y = X(hole(*q['hole'])[0]), Y(hole(*q['hole'])[1])
            u = q['hole'] in used_pins
            o.append(f"<circle cx='{x:.2f}' cy='{y:.2f}' r='{0.75 if u else 0.6}' fill='{'#000' if u else '#fff'}' "
                     f"stroke='#000' stroke-width='0.2'/>")
            if u and p['ref'] in ('U1',):
                above = q['hole'][1] == J1_ROW
                t(x + 0.45, y + (-1.4 if above else 2.0), q['name'], 1.15, 'end' if above else 'start',
                  fill='#000', rot=90 if above else 90)
            elif u and p['ref'] in ('U4', 'U5', 'JBTN', 'SW1'):
                below = p['ref'] in ('U4',)
                if p['ref'] == 'SW1':
                    t(x + (2.2 if not mirror else -2.2), y + 0.45, q['name'], 1.2, 'start' if not mirror else 'end')
                else:
                    t(x + 0.45, y + (2.0 if below else -1.4), q['name'], 1.15, 'start' if below else 'end', rot=90)
    # legenda și scara
    LX = 132.0
    def legend_line(y, s, size=2.0, w='normal', fill='#222'): t(LX, y, s, size, w=w, fill=fill)
    o.append(f"<path d='M{LX} 34 h50 M{LX} 32.8 v2.4 M{LX + 50} 32.8 v2.4' stroke='#000' stroke-width='0.3'/>")
    t(LX + 25, 31.5, 'verifică cu rigla: 50 mm (scara 1:1)', 1.8, 'middle')
    yy = 42
    if not mirror:
        lines = [('Fața pieselor, văzută din spatele cititorului.', 'bold'),
                 ('Ecranul e dedesubt, cu bareta lui sus (rândul B).', None),
                 ('', None),
                 ('Contur plin: măsurat. Liniuțe lungi: din desen.', None),
                 ('Puncte: mărime tipică, se măsoară pe piesa ta.', None),
                 ('', None),
                 ('Pătrate galbene: padurile modulelor lipite cu bandă;', None),
                 ('linia galbenă e firul scurt până în gaura de lângă.', None),
                 ('', None),
                 ('Găurile mari: Ø2,2 mm, prin ele trec șuruburile M2', None),
                 ('ale afișajului, cu distanțiere între cele două plăci.', None),
                 ('', None),
                 ('Rezistoarele și condensatorul stau culcate sub DevKit.', None),
                 ('Firele (șterse aici) sunt pe fața de lipire: pagina 2.', None)]
    else:
        lines = [('Fața de lipire, oglindită: așa o vezi când o întorci.', 'bold'),
                 ('Ea stă spre ecran. Firele se pun aici, în ordinea numerelor.', None),
                 ('', None),
                 ('Fire groase (AWG 24–26): GND, 3V3, VSYS, OUT+.', None),
                 ('Restul, fir subțire izolat (AWG 28–30).', None),
                 ('Culorile sunt doar pentru desen.', None),
                 ('', None),
                 ('Două găuri vecine din aceeași rețea: o punte de cositor.', None),
                 ('Firele nu trec niciodată prin gaura altei rețele.', None),
                 ('', None),
                 ('Lista completă, cu lungimi: hardware/proto/FIRE.md', None)]
    for s, w in lines:
        legend_line(yy, s, 2.0, w or 'normal'); yy += 3.6
    head = ("<svg xmlns='http://www.w3.org/2000/svg' width='297mm' height='210mm' viewBox='0 0 297 210'>"
            "<defs><pattern id='hatch' width='1.2' height='1.2' patternUnits='userSpaceOnUse' "
            "patternTransform='rotate(45)'><path d='M0 0v1.2' stroke='#BBB' stroke-width='0.3'/></pattern></defs>"
            "<rect width='297' height='210' fill='#fff'/>")
    return head + ''.join(o) + '</svg>'

# ---------------------------------------------------------------- lista firelor
def wire_len(path): return math.ceil(((len(path) - 1) * P + 8) / 5) * 5     # + 4 mm la fiecare capăt

def endpoint(h, net, nets):
    for s in nets[net]:
        if PINS.get(s, {}).get('hole') == h:
            q = PINS[s]
            return f'{q["ref"]} {q["name"]}'
    return '?'

def fire_md(nets, wires, warns, on_board):
    L = ['# Placa de prototipare: piese și fire', '',
         'Generat de `layout.py` (nu se editează de mână). Desenul 1:1: `out/sablon.pdf`, '
         'pagina 1 fața pieselor, pagina 2 fața de lipire.', '',
         f'Placa: {NC} × {NR} găuri ({NC * P:.2f} × {NR * P:.2f} mm), tăiată dintr-o placă de 9 × 15 cm '
         'sau mai mare. Găurile se numesc rând + coloană: rândurile A–X (fără I, O, Q) de sus în jos, '
         'coloanele 1–41 de la stânga la dreapta, văzut dinspre piese.', '',
         '## Piesele', '', '| Piesă | Cum se prinde | Mărimea |', '|---|---|---|']
    for p in PARTS:
        if p['ref'] in ('R2', 'C1'): continue
        name = 'R1, R2 100k, C1 100nF' if p['ref'] == 'R1' else f'{p["ref"]} {p["title"]}'
        L.append(f'| {name} | {p["note"]} | {STATUS_TXT[p["status"]]} |')
    L += ['', 'Bateria nu are găuri în placă: cablul JST se lipește direct pe B+ (roșu) și B− (negru) '
          'ale TP4056, **după** verificarea polarității (HW-5).', '',
          '## Firele de pe fața de lipire', '',
          'În ordinea de lipire: întâi alimentarea, apoi semnalele.', '',
          '| Nr. | Rețea | De la | La | Lungime |', '|---|---|---|---|---|']
    for k, w in enumerate(wires, 1):
        a, b = w['path'][0], w['path'][-1]
        ln = 'punte de cositor' if len(w['path']) == 2 else f'~{wire_len(w["path"])} mm'
        L.append(f'| {k} | {nname(w["net"])} | {hname(*a)} ({endpoint(a, w["net"], nets)}) | '
                 f'{hname(*b)} ({endpoint(b, w["net"], nets)}) | {ln} |')
    L += ['', '## Firele scurte de pe fața pieselor', '',
          '| Modul | Pad | Gaura |', '|---|---|---|']
    for p in PARTS:
        if p['mount'] != 'fire': continue
        for q in p['pins']:
            if q['hole']: L.append(f'| {p["ref"]} {p["title"]} | {q["name"]} | {hname(*q["hole"])} |')
    L += ['', '## Plăcuța butoanelor', '',
          'Trei butoane tactile pe o bucată mică de placă, legate la J_BTN cu un cablu cu 4 fire: '
          'fiecare buton între linia lui și GND (pull-up-urile sunt interne). Unde stă plăcuța se '
          'hotărăște cu carcasa; pentru masă poate atârna.', '',
          '| J_BTN | Gaura | Butonul | GPIO |', '|---|---|---|---|',
          '| 1 | K22 | GND, comun la toate trei | |',
          '| 2 | K23 | MENIU | IO6 |', '| 3 | K24 | ÎNAINTE | IO5 |', '| 4 | K25 | ÎNAPOI | IO4 |',
          '', '## De măsurat pe piesele tale, înainte de lipire', '',
          'Valorile marcate „estimat” sunt mărimi tipice. Se măsoară cu rigla sau șublerul și se '
          'trec în `layout.py`; scriptul refăcut spune dacă tot mai încape.', '',
          '1. **Bareta afișajului**: unde e pinul 1 (BUSY) față de colțul plăcii WeAct, ce fel de '
          'baretă are (tată, mamă sau doar găuri) și cât de lungi sunt pinii. De aici ies poziția '
          'soclului și înălțimea distanțierelor (cu soclu mamă de 8,5 mm, ~11 mm).',
          '2. **Modulul microSD**: lungimea, lățimea, unde e bareta de 6 pini și ordinea pinilor ei. '
          'Și dacă are regulatorul AMS1117 (nota 3 din schemă).',
          '3. **TP4056**: mărimea și unde sunt padurile OUT+, B+, B−, OUT−.',
          '4. **TPS63020**: mărimea și unde sunt VIN, GND, VOUT.',
          '5. **SS12D00**: pasul pinilor (2,54 sau 2,0 mm).',
          '6. **Găurile de prindere ale afișajului**: distanța dintre ele (aici 91,8 × 48,0 mm, din desen).',
          '', '## Înainte de prima alimentare', '',
          '1. Fără DevKit, fără card, fără baterie: cu multimetrul pe continuitate, fiecare rețea din '
          'lista de mai sus sună de la un capăt la altul, iar 3V3, VSYS și OUT+ **nu** sună cu GND.',
          '2. Polaritatea cablului bateriei față de B+ / B− (HW-5).',
          '3. Bateria pusă, SW1 pornit, încă fără DevKit: 3,3 V între K28 (soclul J1, pinul 3V3) și '
          'K7 (G). Apoi SW1 oprit, DevKit-ul în socluri, afișajul și cardul puse.',
          '4. Restul e lista de punere în funcțiune (HW-8) și aplicația din `firmware/`.', '']
    L += ['## Verificări', '',
          f'- Rețelele vin din schemă (`kicad-cli`); {on_board} se leagă pe placă, toate complete, fără scurtcircuite.',
          '- Fiecare pin de soclu cade pe o gaură; nicio gaură cu doi pini; nicio piesă peste șuruburi sau sub antenă.']
    for w in warns: L.append(f'- Atenție: {w}.')
    L.append('')
    return '\n'.join(L)

# ---------------------------------------------------------------- main
def main():
    nets = schematic_nets()
    wires, pinhole, blocked = route(nets)
    errs, warns, on_board = check(nets, wires, pinhole, blocked)
    for w in warns: print('atenție:', w)
    if errs:
        for e in errs: print('EROARE:', e)
        sys.exit(1)
    os.makedirs(OUT, exist_ok=True)
    pages = []
    for mirror, fn, title, sub in [
            (False, 'fata', 'Placa de prototipare: fața pieselor',
             'Cititorul e-ink, HW-6. Văzută din spate; ecranul e dedesubt. Pagina 1 din 2.'),
            (True, 'spate', 'Placa de prototipare: fața de lipire',
             'Oglindită, cum o vezi întoarsă. Firele, numerotate ca în FIRE.md. Pagina 2 din 2.')]:
        path = os.path.join(OUT, fn + '.svg')
        with open(path, 'w', encoding='utf-8') as fh: fh.write(svg_page(mirror, wires, nets, title, sub))
        subprocess.run(['rsvg-convert', '-d', '220', '-p', '220', '-f', 'png', '-o',
                        os.path.join(OUT, fn + '.png'), path], check=True)
        pages.append(path)
    subprocess.run(['rsvg-convert', '-f', 'pdf', '-o', os.path.join(OUT, 'sablon.pdf')] + pages, check=True)
    with open(os.path.join(HERE, 'FIRE.md'), 'w', encoding='utf-8') as fh:
        fh.write(fire_md(nets, wires, warns, on_board))
    print(f'{len(nets)} rețele din schemă, {on_board} pe placă, {len(wires)} fire, 0 erori')

if __name__ == '__main__':
    main()
