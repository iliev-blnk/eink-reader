"""Generează schema cititorului e-ink (+ biblioteca de simboluri)."""
import math, uuid as _uuid
import sexp

PROJECT = 'eink-reader'
SYMDIR = '/usr/share/kicad/symbols'
ROOT = str(_uuid.uuid5(_uuid.NAMESPACE_URL, 'eink-reader-root'))
_n = 0
def uid():
    global _n; _n += 1
    return str(_uuid.uuid5(_uuid.NAMESPACE_URL, f'eink-reader-{_n}'))

def q(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
def f(v): return f'{round(v, 4):g}'
FONT = '(font (size 1.27 1.27))'

# ---------------------------------------------------------------- simbolurile proprii ale modulelor
P = 2.54
def module(name, desc, left, right, width, top, plen=2.54):
    """left/right: liste de (număr, nume, tip) sau None pentru un gol, de sus în jos."""
    pins, n_rows = [], max(len(left), len(right))
    for side, lst in (('L', left), ('R', right)):
        for i, p in enumerate(lst):
            if p: pins.append((side, top - i * P) + p)
    bottom = top - (n_rows - 1) * P
    hw = width / 2
    body = (f'(rectangle (start {f(-hw)} {f(top + P)}) (end {f(hw)} {f(bottom - P)}) '
            f'(stroke (width 0.254) (type default)) (fill (type background)))')
    pin_s = []
    for side, y, num, nm, typ in pins:
        x, ang = (-hw - plen, 0) if side == 'L' else (hw + plen, 180)
        pin_s.append(f'(pin {typ} line (at {f(x)} {f(y)} {ang}) (length {f(plen)}) '
                     f'(name {q(nm)} (effects {FONT})) (number {q(num)} (effects {FONT})))')
    sym = (f'(symbol {q(name)} (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)\n'
           f'  (property "Reference" "U" (at {f(-hw)} {f(top + P + 1.27)} 0) (effects {FONT} (justify left)))\n'
           f'  (property "Value" {q(name)} (at {f(-hw)} {f(bottom - P - 1.27)} 0) (effects {FONT} (justify left)))\n'
           f'  (property "Footprint" "" (at 0 0 0) (effects {FONT} (hide yes)))\n'
           f'  (property "Datasheet" "" (at 0 0 0) (effects {FONT} (hide yes)))\n'
           f'  (property "Description" {q(desc)} (at 0 0 0) (effects {FONT} (hide yes)))\n'
           f'  (symbol {q(name + "_0_1")} {body})\n'
           f'  (symbol {q(name + "_1_1")}\n    ' + '\n    '.join(pin_s) + '))')
    pinpos = {n: ((-hw - plen if sd == 'L' else hw + plen), y) for sd, y, n, _, _ in pins}
    return sym, pinpos, (hw, top, bottom)

BI, PI, PO, IN, OUT, PAS = 'bidirectional', 'power_in', 'power_out', 'input', 'output', 'passive'
J1 = ['3V3', '3V3', 'RST', 'IO4', 'IO5', 'IO6', 'IO7', 'IO15', 'IO16', 'IO17', 'IO18', 'IO8',
      'IO3', 'IO46', 'IO9', 'IO10', 'IO11', 'IO12', 'IO13', 'IO14', '5V', 'G']
J3 = ['G', 'TX/IO43', 'RX/IO44', 'IO1', 'IO2', 'IO42', 'IO41', 'IO40', 'IO39', 'IO38/LED', 'IO37',
      'IO36', 'IO35', 'IO0/BOOT', 'IO45', 'IO48', 'IO47', 'IO21', 'IO20/USB+', 'IO19/USB-', 'G', 'G']
def jtype(n): return PI if n in ('3V3', '5V', 'G') else (IN if n == 'RST' else BI)

CUSTOM = {}
def add(key, *a):
    CUSTOM[key] = module(key, *a)
add('ESP32-S3-DevKitC-1', 'Espressif ESP32-S3-DevKitC-1 (N16R8), headers J1/J3',
    [(f'J1.{i+1}', n, jtype(n)) for i, n in enumerate(J1)],
    [(f'J3.{i+1}', n, jtype(n)) for i, n in enumerate(J3)], 30.48, 27.94, 5.08)
add('WeAct_EPD_3.7', 'WeAct Studio 3.7in e-paper module, 416x240, UC8253, SPI',
    [('1', 'BUSY', OUT), ('2', 'RES', IN), ('3', 'D/C', IN), ('4', 'CS', IN),
     ('5', 'SCL', IN), ('6', 'SDA', IN), ('7', 'GND', PI), ('8', 'VCC', PI)], [], 20.32, 8.89)
add('MicroSD_Module', 'microSD card breakout, SPI mode',
    [('1', 'GND', PI), ('2', 'VCC', PI), ('3', 'MISO', OUT), ('4', 'MOSI', IN),
     ('5', 'SCK', IN), ('6', 'CS', IN)], [], 20.32, 6.35)
add('TP4056_Module', 'TP4056 1A Li-ion charger with DW01 protection',
    [('1', 'IN+', PAS), ('2', 'IN-', PAS), None, ('3', 'B+', PAS), ('4', 'B-', PAS)],
    [None, ('5', 'OUT+', PO), None, ('6', 'OUT-', PAS)], 20.32, 5.08)
add('TPS63020_Module', 'TPS63020 buck-boost module, fixed 3.3 V out',
    [('1', 'VIN', PI), None, ('2', 'GND', PI)], [('3', 'VOUT', PO)], 20.32, 2.54)

LIBS = {'Device:R': ('Device', 'R'), 'Device:C': ('Device', 'C'),
        'Device:Battery_Cell': ('Device', 'Battery_Cell'), 'Switch:SW_Push': ('Switch', 'SW_Push'),
        'Switch:SW_SPST': ('Switch', 'SW_SPST'), 'power:GND': ('power', 'GND'),
        'power:+3V3': ('power', '+3V3'), 'power:PWR_FLAG': ('power', 'PWR_FLAG')}
LIBTREE, LIBPINS = {}, {}
for lid, (lib, name) in LIBS.items():
    t = sexp.find_lib_symbol(f'{SYMDIR}/{lib}.kicad_sym', name)
    t[1] = q(lid)
    LIBTREE[lid] = t
    LIBPINS[lid] = {num: (x, y) for num, _, x, y in sexp.pins(t)}
for k, (_, pins, _) in CUSTOM.items():
    LIBPINS['EinkReader:' + k] = pins

# ---------------------------------------------------------------- elementele foii
items = []
refs = {}
def xf(X, Y, rot, px, py):
    a = math.radians(rot)
    rx, ry = px * math.cos(a) - py * math.sin(a), px * math.sin(a) + py * math.cos(a)
    return (round(X + rx, 4), round(Y - ry, 4))

pwr_n = 0
def place(lib_id, ref, value, X, Y, rot=0, fang=0, ref_at=None, val_at=None, hide_ref=False, hide_val=False):
    pins = LIBPINS[lib_id]
    ref_at = ref_at or (X, Y - 5.08); val_at = val_at or (X, Y + 5.08)
    def prop(k, v, at, hide=False, just=None):
        e = FONT + (f' (justify {just})' if just else '') + (' (hide yes)' if hide else '')
        return f'(property {q(k)} {q(v)} (at {f(at[0])} {f(at[1])} {fang}) (effects {e}))'
    rj, vj = (ref_at[2] if len(ref_at) > 2 else None), (val_at[2] if len(val_at) > 2 else None)
    s = (f'(symbol (lib_id {q(lib_id)}) (at {f(X)} {f(Y)} {rot}) (unit 1) (exclude_from_sim no) '
         f'(in_bom {"no" if ref.startswith("#") else "yes"}) (on_board yes) (dnp no) (uuid {q(uid())})\n  '
         + prop('Reference', ref, ref_at, hide_ref, rj) + '\n  ' + prop('Value', value, val_at, hide_val, vj)
         + '\n  ' + prop('Footprint', '', (X, Y), True) + '\n  ' + prop('Datasheet', '', (X, Y), True) + '\n  '
         + ' '.join(f'(pin {q(n)} (uuid {q(uid())}))' for n in pins)
         + f'\n  (instances (project {q(PROJECT)} (path {q("/" + ROOT)} (reference {q(ref)}) (unit 1)))))')
    items.append(s)
    pos = {n: xf(X, Y, rot, *p) for n, p in pins.items()}
    refs[ref] = pos
    return pos

def pwr(kind, x, y, direction='down'):
    """Simbol de alimentare cu pinul la (x,y) și desenul îndreptat spre `direction`."""
    global pwr_n; pwr_n += 1
    lib = {'gnd': 'power:GND', '3v3': 'power:+3V3', 'flag': 'power:PWR_FLAG'}[kind]
    val = {'gnd': 'GND', '3v3': '+3V3', 'flag': 'PWR_FLAG'}[kind]
    # direcția nativă: GND arată în jos, +3V3/PWR_FLAG arată în sus
    native = 'down' if kind == 'gnd' else 'up'
    order = ['up', 'left', 'down', 'right']           # pași de 90 de grade în sens trigonometric
    rot = ((order.index(direction) - order.index(native)) % 4) * 90
    off = {'up': (0, -5.08, None), 'down': (0, 5.08, None), 'left': (-4.0, 0, 'right'), 'right': (4.0, 0, 'left')}[direction]
    just = off[2]
    if just and rot == 90:  # KiCad citește câmpul invers la 90 de grade și întoarce alinierea
        just = {'left': 'right', 'right': 'left'}[just]
    va = (x + off[0], y + off[1]) + ((just,) if just else ())
    place(lib, f'#{"FLG" if kind == "flag" else "PWR"}0{pwr_n:02d}', val, x, y, rot, rot % 180,
          ref_at=(x, y), val_at=va, hide_ref=True)

def wire(*pts):
    for a, b in zip(pts, pts[1:]):
        items.append(f'(wire (pts (xy {f(a[0])} {f(a[1])}) (xy {f(b[0])} {f(b[1])})) '
                     f'(stroke (width 0) (type default)) (uuid {q(uid())}))')

def label(name, x, y, side):
    """Etichetă locală de rețea ancorată la (x,y); textul merge spre `side` ('left' sau 'right')."""
    ang, just = (180, 'right') if side == 'left' else (0, 'left')
    items.append(f'(label {q(name)} (at {f(x)} {f(y)} {ang}) (effects {FONT} (justify {just} bottom)) '
                 f'(uuid {q(uid())}))')

def nc(p): items.append(f'(no_connect (at {f(p[0])} {f(p[1])}) (uuid {q(uid())}))')
def junction(p): items.append(f'(junction (at {f(p[0])} {f(p[1])}) (diameter 0) (color 0 0 0 0) (uuid {q(uid())}))')

def text(s, x, y, size=1.27, bold=False, just='left top'):
    fnt = f'(font (size {size} {size}){" (bold yes)" if bold else ""})'
    items.append(f'(text {q(s)} (exclude_from_sim no) (at {f(x)} {f(y)} 0) (effects {fnt} (justify {just})) '
                 f'(uuid {q(uid())}))')

def box(x1, y1, x2, y2, title):
    items.append(f'(rectangle (start {f(x1)} {f(y1)}) (end {f(x2)} {f(y2)}) '
                 f'(stroke (width 0.2) (type dash) (color 120 120 120 1)) (fill (type none)) (uuid {q(uid())}))')
    text(title, x1 + 2.54, y1 + 2.54, 2.0, True)

def stub_label(p, net, side, length=5.08):
    end = (p[0] - length, p[1]) if side == 'left' else (p[0] + length, p[1])
    wire(p, end); label(net, *end, side)

def stub_pwr(p, kind, side, length=5.08):
    end = (p[0] - length, p[1]) if side == 'left' else (p[0] + length, p[1])
    wire(p, end); pwr(kind, *end, side)

def mod(key, ref, X, Y):
    hw, top, bottom = CUSTOM[key][2]
    return place('EinkReader:' + key, ref, key.replace('_', ' '), X, Y,
                 ref_at=(X - hw, Y - top - P - 1.27, 'left'), val_at=(X - hw, Y - bottom + P + 1.27, 'left'))

# ================================================================= ALIMENTAREA
box(15.24, 20.32, 160.02, 125.73, 'POWER')
bt = place('Device:Battery_Cell', 'BT1', 'LiPo 3.7 V', 30.48, 68.58,
           ref_at=(27.94, 66.04, 'right'), val_at=(27.94, 68.58, 'right'))
u2 = mod('TP4056_Module', 'U2', 63.5, 60.96)
sw1 = place('Switch:SW_SPST', 'SW1', 'POWER', 88.9, 58.42, ref_at=(88.9, 50.8), val_at=(88.9, 53.975))
u3 = mod('TPS63020_Module', 'U3', 119.38, 60.96)

wire(bt['1'], u2['3'])                                            # BAT+ -> B+
wire(u2['4'], (40.64, u2['4'][1]), (40.64, bt['2'][1]), bt['2'])  # B- -> BAT-
nc(u2['1']); nc(u2['2'])
text('Charging input:\nUSB port on the\nTP4056 module', 25.4, 45.72)
wire(u2['5'], sw1['1'])                                           # OUT+ -> întrerupător
stub_pwr(u2['6'], 'gnd', 'right', 2.54)
wire(sw1['2'], (101.6, 58.42), u3['1']); label('VSYS', 95.25, 58.42, 'right')
pwr('flag', 101.6, 58.42, 'up'); junction((101.6, 58.42))
stub_pwr(u3['2'], 'gnd', 'left', 2.54)
stub_pwr(u3['3'], '3v3', 'right', 5.08)
text('Charger GND = OUT−, not B−\n(B− sits behind the protection FETs)', 50.8, 76.2)

# divizorul pentru măsurarea bateriei
r1 = place('Device:R', 'R1', '100k', 127, 91.44, ref_at=(129.54, 90.17, 'left'), val_at=(129.54, 92.71, 'left'))
r2 = place('Device:R', 'R2', '100k', 127, 104.14, ref_at=(129.54, 102.87, 'left'), val_at=(129.54, 105.41, 'left'))
c1 = place('Device:C', 'C1', '100nF', 137.16, 104.14, ref_at=(139.7, 102.87, 'left'), val_at=(139.7, 105.41, 'left'))
wire(r1['1'], (127, 85.09)); label('VSYS', 127, 85.09, 'right')
node = (127, 97.79)
wire(r1['2'], node); wire(node, r2['1']); wire(node, (137.16, 97.79))
wire((137.16, 97.79), c1['1']); wire((137.16, 97.79), (144.78, 97.79))
junction(node); junction((137.16, 97.79))
label('VBAT_SENSE', 144.78, 97.79, 'right')
pwr('gnd', *r2['2']); pwr('gnd', *c1['2'])
pwr('gnd', 152.4, 107.95); pwr('flag', 152.4, 107.95, 'up')
text('Battery sense: VSYS/2 → IO1 (ADC1_CH0)\n4.2 V full → 2.1 V at the pin.\nAfter SW1, so it draws nothing when off.', 50.8, 92.71)

# ================================================================= MICROCONTROLERUL
box(165.1, 20.32, 279.4, 139.7, 'MCU')
u1 = mod('ESP32-S3-DevKitC-1', 'U1', 222.25, 83.82)
J1NET = {3: 'BTN_PREV', 4: 'BTN_NEXT', 5: 'BTN_MENU',
         10: 'EPD_BUSY', 11: 'EPD_RST', 14: 'EPD_DC', 15: 'EPD_CS', 16: 'SPI_MOSI', 17: 'SPI_SCK',
         18: 'SPI_MISO', 19: 'SD_CS'}
J3NET = {3: 'VBAT_SENSE'}
for i in range(22):
    p = u1[f'J1.{i+1}']
    if i in J1NET: stub_label(p, J1NET[i], 'left')
    elif i == 21: stub_pwr(p, 'gnd', 'left')
    elif i not in (0, 1): nc(p)
a, b = u1['J1.1'], u1['J1.2']
wire(a, (a[0] - 5.08, a[1])); wire(b, (b[0] - 5.08, b[1])); wire((a[0] - 5.08, a[1]), (b[0] - 5.08, b[1]))
pwr('3v3', a[0] - 5.08, a[1], 'left')
for i in range(22):
    p = u1[f'J3.{i+1}']
    if i in J3NET: stub_label(p, J3NET[i], 'right')
    elif i in (0, 20): stub_pwr(p, 'gnd', 'right')
    elif i != 21: nc(p)
c, d = u1['J3.21'], u1['J3.22']
wire(d, (d[0] + 5.08, d[1])); wire((d[0] + 5.08, d[1]), (c[0] + 5.08, c[1]))

# ================================================================= ECRANUL
box(289.56, 20.32, 403.86, 72.39, 'DISPLAY')
u4 = mod('WeAct_EPD_3.7', 'U4', 354.33, 45.72)
for num, net in zip('123456', ['EPD_BUSY', 'EPD_RST', 'EPD_DC', 'EPD_CS', 'SPI_SCK', 'SPI_MOSI']):
    stub_label(u4[num], net, 'left', 15.24)
stub_pwr(u4['7'], 'gnd', 'left', 5.08)
stub_pwr(u4['8'], '3v3', 'left', 5.08)
text('3.7" 416×240, UC8253\nVCC 3.3–5 V, logic 3.3 V\nWrite-only: no MISO', 370.84, 34.29)

# ================================================================= STOCAREA
box(289.56, 80.01, 403.86, 125.73, 'STORAGE')
u5 = mod('MicroSD_Module', 'U5', 354.33, 104.14)
stub_pwr(u5['1'], 'gnd', 'left', 5.08)
stub_pwr(u5['2'], '3v3', 'left', 5.08)
for num, net in zip('3456', ['SPI_MISO', 'SPI_MOSI', 'SPI_SCK', 'SD_CS']):
    stub_label(u5[num], net, 'left', 15.24)
text('microSDHC, FAT32\nShares SPI2 with the\ndisplay, own CS', 370.84, 95.25)

# ================================================================= BUTOANELE
box(15.24, 133.35, 160.02, 196.85, 'BUTTONS')
BTNS = [('SW2', 'BTN_PREV', 'PREV PAGE'), ('SW3', 'BTN_NEXT', 'NEXT PAGE'), ('SW4', 'BTN_MENU', 'MENU / OK')]
for k, (ref, net, fn) in enumerate(BTNS):
    X = 55.88; Y = 152.4 + k * 12.7
    s = place('Switch:SW_Push', ref, fn, X, Y, ref_at=(X, Y - 5.08), val_at=(X, Y + 2.54))
    stub_label(s['1'], net, 'left')
    stub_pwr(s['2'], 'gnd', 'right', 2.54)
text('Active-low to GND, internal pull-ups (no resistors).\nAll on RTC GPIOs: any button can wake from deep sleep (ext1).',
     22.86, 184.15)

# ================================================================= NOTE
box(165.1, 147.32, 403.86, 196.85, 'NOTES')
text('Parts not in the kit: SW1 slide switch, SW2–SW4 tactile buttons, R1/R2 100k, C1 100nF.\n\n'
     '1.  Never power the board from the DevKit USB while SW1 is ON. Espressif lists USB, the 5V pin and the 3V3 pin\n'
     '     as mutually exclusive supplies, and here TPS63020 drives 3V3 directly. Switch off before flashing.\n'
     '2.  TP4056: use the 6-pad version with DW01 protection. System ground is OUT−. Charge through its own USB port;\n'
     '     the device can run while charging (OUT+ follows the charger).\n'
     '3.  microSD module: if it carries an AMS1117 regulator it expects 5 V. At 3.3 V either buy a 3.3 V-direct module\n'
     '     or bridge the regulator IN to OUT.\n'
     '4.  Left unconnected on purpose: IO35–37 (octal PSRAM on N16R8), IO0/IO3/IO45/IO46 (strapping),\n'
     '     IO19/IO20 (native USB), IO43/IO44 (UART0 console), IO38 (on-board RGB LED).\n'
     '5.  Display and microSD share SPI2: SCK IO12, MOSI IO11, MISO IO13. Separate chip selects IO10 / IO14.\n'
     '     Keep the SD on its own CS and deselect both before init.\n'
     '6.  Battery: 1S LiPo, 3.0–4.2 V. TPS63020 runs from 1.8–5.5 V, so the 3.3 V rail holds\n'
     '     all the way down to cut-off.',
     170.18, 156.21)
SIG = ['SPI_SCK', 'SPI_MOSI', 'SPI_MISO', 'EPD_CS', 'EPD_DC', 'EPD_RST', 'EPD_BUSY', 'SD_CS',
       'BTN_PREV', 'BTN_NEXT', 'BTN_MENU', 'VBAT_SENSE']
GP = ['12', '11', '13', '10', '9', '8', '18', '14', '4', '5', '6', '1']
text('GPIO MAP', 350.52, 156.21, 1.27, True)
text('\n'.join(SIG), 350.52, 160.02)
text('\n'.join('IO' + g for g in GP), 378.46, 160.02)

# ---------------------------------------------------------------- scrierea
lib_syms = [sexp.dump(t, 2) for t in LIBTREE.values()]
for k, (sym, _, _) in CUSTOM.items():
    lib_syms.append(sym.replace(f'(symbol {q(k)}', f'(symbol {q("EinkReader:" + k)}', 1))
sch = (f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "10.0")\n'
       f'(uuid {q(ROOT)})\n(paper "A3")\n'
       f'(title_block (title "E-ink reader") (date "2026-10-25") (rev "1.0") (company "Diploma project")\n'
       f'  (comment 1 "Module-level wiring: ESP32-S3-DevKitC-1, WeAct 3.7in EPD, microSD, TP4056, TPS63020")\n'
       f'  (comment 2 "Author: Ilie Beleniuc"))\n'
       f'(lib_symbols\n' + '\n'.join(lib_syms) + '\n)\n' + '\n'.join(items) +
       f'\n(sheet_instances (path "/" (page "1")))\n(embedded_fonts no)\n)\n')
open(f'{PROJECT}.kicad_sch', 'w').write(sch)
open('eink-reader.kicad_sym', 'w').write('(kicad_symbol_lib (version 20241209) (generator "eink-reader-gen")\n'
                                       + '\n'.join(s for s, _, _ in CUSTOM.values()) + '\n)\n')
open('sym-lib-table', 'w').write('(sym_lib_table (version 7)\n  (lib (name "EinkReader")(type "KiCad")'
                                 '(uri "${KIPRJMOD}/eink-reader.kicad_sym")(options "")(descr "E-ink reader modules"))\n'
                                 + ''.join(f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_SYMBOL_DIR}}/{n}.kicad_sym")(options "")(descr ""))\n'
                                           for n in ('Device', 'Switch', 'power')) + ')\n')
import json
json.dump({'meta': {'filename': f'{PROJECT}.kicad_pro', 'version': 3}}, open(f'{PROJECT}.kicad_pro', 'w'), indent=2)
print('ok', len(items), 'items')
