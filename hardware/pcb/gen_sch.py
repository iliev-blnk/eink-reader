"""Generează schema plăcii proprii (eink-reader-pcb.kicad_sch) din circuit.py.

Fiecare pin primește un fir scurt și o etichetă cu numele rețelei (sau simbolul
GND / +3V3); pinii nefolosiți primesc semnul de neconectat. Piesele se așază
automat, pe grupuri. PWR_FLAG se pune pe fiecare rețea cu un pin de alimentare
de intrare și fără niciun pin care s-o alimenteze.
"""
import math, os, sys, uuid as _uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import sexp  # noqa: E402
from circuit import PARTS, nets  # noqa: E402

PROJECT = 'eink-reader-pcb'
SYMDIR = '/usr/share/kicad/symbols'
NS = _uuid.NAMESPACE_URL
ROOT = str(_uuid.uuid5(NS, 'eink-reader-pcb-root'))
def sym_uuid(ref): return str(_uuid.uuid5(NS, 'eink-reader-pcb-sym-' + ref))
_n = 0
def uid():
    global _n; _n += 1
    return str(_uuid.uuid5(NS, f'eink-reader-pcb-{_n}'))

def q(s): return '"' + s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n') + '"'
def f(v): return f'{round(v, 4):g}'
FONT = '(font (size 1.27 1.27))'
G = 2.54
def snap(v): return round(v / G) * G

# ---------------------------------------------------------------- simbolul MAX17048 (nu e în biblioteca KiCad)
def max17048():
    L = [('3', 'VDD', 'power_in', 2.54), ('2', 'CELL', 'passive', 0), ('6', 'QSTRT', 'input', -2.54), ('1', 'CTG', 'passive', -5.08)]
    R = [('8', 'SDA', 'bidirectional', 2.54), ('7', 'SCL', 'input', 0), ('5', '~{ALRT}', 'open_collector', -2.54)]
    B = [('4', 'GND', 'power_in', -2.54), ('9', 'EP', 'passive', 2.54)]
    pins = []
    for num, nm, typ, y in L:
        pins.append(f'(pin {typ} line (at -10.16 {f(y)} 0) (length 2.54) (name {q(nm)} (effects {FONT})) (number {q(num)} (effects {FONT})))')
    for num, nm, typ, y in R:
        pins.append(f'(pin {typ} line (at 10.16 {f(y)} 180) (length 2.54) (name {q(nm)} (effects {FONT})) (number {q(num)} (effects {FONT})))')
    for num, nm, typ, x in B:
        pins.append(f'(pin {typ} line (at {f(x)} -10.16 90) (length 2.54) (name {q(nm)} (effects {FONT})) (number {q(num)} (effects {FONT})))')
    s = ('(symbol "MAX17048" (pin_names (offset 1.016)) (exclude_from_sim no) (in_bom yes) (on_board yes)'
         f'(property "Reference" "U" (at -7.62 6.35 0) (effects {FONT} (justify left)))'
         f'(property "Value" "MAX17048" (at -7.62 -12.7 0) (effects {FONT} (justify left)))'
         f'(property "Footprint" "Package_DFN_QFN:TDFN-8-1EP_2x2mm_P0.5mm_EP0.8x1.2mm" (at 0 0 0) (effects {FONT} (hide yes)))'
         f'(property "Datasheet" "https://www.analog.com/media/en/technical-documentation/data-sheets/MAX17048-MAX17049.pdf" (at 0 0 0) (effects {FONT} (hide yes)))'
         f'(property "Description" "Indicator de încărcare 1S cu ModelGauge, I2C, 3 uA" (at 0 0 0) (effects {FONT} (hide yes)))'
         '(symbol "MAX17048_0_1" (rectangle (start -7.62 5.08) (end 7.62 -7.62) (stroke (width 0.254) (type default)) (fill (type background))))'
         '(symbol "MAX17048_1_1" ' + ' '.join(pins) + '))')
    return s

# TPS22917 din biblioteca KiCad are QOD de tip open collector, iar ERC refuză
# legarea lui de VOUT, deși foaia de catalog cere exact asta pentru descărcarea
# rapidă. Copia proprie are QOD pasiv; altfel e identică.
def tps22917():
    t = sexp.find_lib_symbol(f'{SYMDIR}/Power_Management.kicad_sym', 'TPS22917DBV')
    def walk(node):
        for x in node:
            if isinstance(x, list):
                if x[0] == 'pin' and find(x, 'name')[1].strip('"') == 'QOD':
                    assert x[1] == 'open_collector'; x[1] = 'passive'
                walk(x)
    walk(t)
    t = list(t); t[1] = '"TPS22917DBV"'
    return t

# ---------------------------------------------------------------- simbolurile din biblioteci
def find(node, key):
    return next((a for a in node if isinstance(a, list) and a and a[0] == key), None)

def body_style_ok(name):
    return name.strip('"').rsplit('_', 1)[-1] in ('0', '1')

def pin_list(sym):
    out = []
    def walk(node):
        for x in node:
            if not isinstance(x, list): continue
            if x[0] == 'symbol' and not body_style_ok(x[1]): continue
            if x[0] == 'pin':
                at = find(x, 'at')
                out.append(dict(num=find(x, 'number')[1].strip('"'), name=find(x, 'name')[1].strip('"'), type=x[1],
                                x=float(at[1]), y=float(at[2]), ang=int(float(at[3])) % 360 if len(at) > 3 else 0))
            else:
                walk(x)
    walk(sym)
    return out

def body_bbox(sym):
    pts = []
    def walk(node):
        for x in node:
            if not isinstance(x, list): continue
            if x[0] == 'symbol' and not body_style_ok(x[1]): continue
            if x[0] in ('rectangle', 'polyline', 'arc', 'bezier'):
                for a in x:
                    if isinstance(a, list) and a[0] in ('start', 'end', 'mid'): pts.append((float(a[1]), float(a[2])))
                    if isinstance(a, list) and a[0] == 'pts':
                        pts.extend((float(p[1]), float(p[2])) for p in a if isinstance(p, list))
            elif x[0] == 'circle':
                c, r = find(x, 'center'), float(find(x, 'radius')[1])
                pts.extend([(float(c[1]) - r, float(c[2]) - r), (float(c[1]) + r, float(c[2]) + r)])
            elif x[0] == 'pin':
                at = find(x, 'at'); pts.append((float(at[1]), float(at[2])))
            else:
                walk(x)
    walk(sym)
    xs, ys = [p[0] for p in pts] or [0], [p[1] for p in pts] or [0]
    return min(xs), max(xs), min(ys), max(ys)

LIB = {}
for p in PARTS + [{'lib': 'power:GND'}, {'lib': 'power:+3V3'}, {'lib': 'power:PWR_FLAG'}]:
    lid = p['lib']
    if lid in LIB: continue
    if lid == 'EinkReader:MAX17048':
        t = sexp.parse(max17048())
    elif lid == 'EinkReader:TPS22917DBV':
        t = tps22917()
    else:
        lib, name = lid.split(':')
        t = sexp.find_lib_symbol(f'{SYMDIR}/{lib}.kicad_sym', name)
        assert not find(t, 'extends'), lid
    t = list(t); t[1] = q(lid)
    LIB[lid] = dict(tree=t, pins=pin_list(t), bbox=body_bbox(t))

# ---------------------------------------------------------------- verificări
for p in PARTS:
    nums = {pp['num'] for pp in LIB[p['lib']]['pins']}
    bad = set(p['pins']) - nums
    assert not bad, (p['ref'], bad)

# ---------------------------------------------------------------- elementele foii
items = []
pwr_n = 0

def prop(k, v, at, hide=False, just=None, ang=0):
    e = FONT + (f' (justify {just})' if just else '') + (' (hide yes)' if hide else '')
    return f'(property {q(k)} {q(v)} (at {f(at[0])} {f(at[1])} {ang}) (effects {e}))'

def place_symbol(lid, ref, value, X, Y, rot=0, ref_at=None, val_at=None, hide_ref=False, hide_val=False,
                 extra=(), uuid=None, rot_text=0):
    pins = LIB[lid]['pins']
    ref_at = ref_at or (X, Y - 5.08); val_at = val_at or (X, Y + 5.08)
    rj = ref_at[2] if len(ref_at) > 2 else None
    vj = val_at[2] if len(val_at) > 2 else None
    s = (f'(symbol (lib_id {q(lid)}) (at {f(X)} {f(Y)} {rot}) (unit 1) (exclude_from_sim no) '
         f'(in_bom {"no" if ref.startswith(("#", "H", "TP", "JP")) else "yes"}) (on_board yes) (dnp no) (uuid {q(uuid or uid())})\n  '
         + prop('Reference', ref, ref_at, hide_ref, rj, rot_text) + '\n  ' + prop('Value', value, val_at, hide_val, vj, rot_text)
         + ''.join('\n  ' + prop(k, v, (X, Y), True) for k, v in extra)
         + '\n  ' + ' '.join(f'(pin {q(pp["num"])} (uuid {q(uid())}))' for pp in pins)
         + f'\n  (instances (project {q(PROJECT)} (path {q("/" + ROOT)} (reference {q(ref)}) (unit 1)))))')
    items.append(s)

def wire(a, b):
    items.append(f'(wire (pts (xy {f(a[0])} {f(a[1])}) (xy {f(b[0])} {f(b[1])})) '
                 f'(stroke (width 0) (type default)) (uuid {q(uid())}))')

LABEL = {'left': (180, 'right'), 'right': (0, 'left'), 'up': (90, 'left'), 'down': (270, 'right')}
def label(name, x, y, side):
    ang, just = LABEL[side]
    # etichete globale: rețeaua se numește exact `name`, ca pe placă (o etichetă
    # locală ar da „/name”)
    items.append(f'(global_label {q(name)} (shape passive) (at {f(x)} {f(y)} {ang}) (fields_autoplaced yes) '
                 f'(effects {FONT} (justify {just})) (uuid {q(uid())}))')

def pwr(kind, x, y, direction):
    """Simbol de alimentare cu pinul la (x,y), desenat spre `direction`. Un GND culcat
    nu-și arată textul: ar călca rândul vecin, iar simbolul e destul."""
    global pwr_n; pwr_n += 1
    lid = {'gnd': 'power:GND', '3v3': 'power:+3V3', 'flag': 'power:PWR_FLAG'}[kind]
    val = {'gnd': 'GND', '3v3': '+3V3', 'flag': 'PWR_FLAG'}[kind]
    native = 'down' if kind == 'gnd' else 'up'
    order = ['up', 'left', 'down', 'right']
    rot = ((order.index(direction) - order.index(native)) % 4) * 90
    off = {'up': (0, -5.08, None), 'down': (0, 5.08, None), 'left': (-4.0, 0, 'right'), 'right': (4.0, 0, 'left')}[direction]
    just = off[2]
    if just and rot == 90:
        just = {'left': 'right', 'right': 'left'}[just]
    va = (x + off[0], y + off[1]) + ((just,) if just else ())
    place_symbol(lid, f'#{"FLG" if kind == "flag" else "PWR"}{pwr_n:03d}', val, x, y, rot,
                 ref_at=(x, y), val_at=va, hide_ref=True, rot_text=rot % 180,
                 hide_val=(kind == 'gnd' and direction in ('left', 'right')))

def nc(p): items.append(f'(no_connect (at {f(p[0])} {f(p[1])}) (uuid {q(uid())}))')

def text(s, x, y, size=1.27, bold=False):
    fnt = f'(font (size {size} {size}){" (bold yes)" if bold else ""})'
    items.append(f'(text {q(s)} (exclude_from_sim no) (at {f(x)} {f(y)} 0) (effects {fnt} (justify left top)) (uuid {q(uid())}))')

def box(x1, y1, x2, y2, title):
    items.append(f'(rectangle (start {f(x1)} {f(y1)}) (end {f(x2)} {f(y2)}) '
                 f'(stroke (width 0.2) (type dash) (color 120 120 120 1)) (fill (type none)) (uuid {q(uid())}))')
    text(title, x1 + 2.54, y1 + 2.54, 2.0, True)

STUB = 2.54
def tw(s): return 1.0 * len(s) + 2.0                # lățimea aproximativă a unei etichete

def stub_dir(ang):
    return {0: 'left', 180: 'right', 90: 'down', 270: 'up'}[ang]
VEC = {'left': (-1, 0), 'right': (1, 0), 'up': (0, -1), 'down': (0, 1)}

def endpoint_label(net, end, d):
    if net == 'GND': pwr('gnd', *end, d if d != 'up' else 'down') if d != 'up' else _gnd_up(end)
    elif net == '+3V3': pwr('3v3', *end, d)
    else: label(net, *end, d)

def _gnd_up(end):
    # un pin care iese în sus și merge la GND: firul face un cot spre dreapta, ca simbolul să arate în jos
    e2 = (end[0] + G, end[1]); wire(end, e2); pwr('gnd', *e2, 'down')

def text_spot(p):
    """Unde stau referința și valoarea unei piese mici: în dreapta, dacă acolo nu
    iese niciun pin, altfel deasupra (aliniate la dreapta, dacă și sus iese un pin)."""
    lib = LIB[p['lib']]
    x0, x1, y0, y1 = lib['bbox']
    if x1 - x0 > 12: return 'big'
    sides = {stub_dir(pp['ang']) for pp in lib['pins']}
    if 'right' not in sides: return 'right'
    return 'above-r' if 'up' in sides else 'above'

def text_w(p): return 1.0 * max(len(p['ref']), len(p['value'])) + 1.0

def extents(p):
    """Cât ocupă o piesă față de originea ei (coordonatele foii): x0, x1, y0, y1."""
    lib = LIB[p['lib']]
    x0, x1, y0, y1 = lib['bbox']
    ex = [x0, x1, -y1, -y0]
    for pp in lib['pins']:
        d = stub_dir(pp['ang']); px, py = pp['x'], -pp['y']
        net = p['pins'].get(pp['num'])
        L = STUB + (tw(net) if net and net not in ('GND', '+3V3') else 6.0)
        vx, vy = VEC[d]
        ex[0] = min(ex[0], px + vx * L); ex[1] = max(ex[1], px + vx * L)
        ex[2] = min(ex[2], py + vy * L); ex[3] = max(ex[3], py + vy * L)
    spot = text_spot(p)
    if spot == 'right': ex[1] = max(ex[1], x1 + 1.27 + text_w(p))
    elif spot == 'above': ex[1] = max(ex[1], x0 + text_w(p)); ex[2] = min(ex[2], -y1 - 6.35)
    elif spot == 'above-r': ex[0] = min(ex[0], x0 - text_w(p)); ex[2] = min(ex[2], -y1 - 6.35)
    ex[2] -= 2.54; ex[3] += 5.08
    return ex

def place_part(p, X, Y):
    lib = LIB[p['lib']]
    x0, x1, y0, y1 = lib['bbox']
    spot = text_spot(p)
    if spot == 'big':
        # valoarea sub simbolurile GND ale pinilor de jos, dacă are
        low = max([-y0 + 2.54] + [-pp['y'] + STUB + 7.62 for pp in lib['pins'] if stub_dir(pp['ang']) == 'down'])
        ref_at = (X + x0, Y - y1 - 1.27, 'left'); val_at = (X + x0, Y + low, 'left')
    elif spot == 'right':
        ref_at = (X + x1 + 1.27, Y - 1.27, 'left'); val_at = (X + x1 + 1.27, Y + 1.27, 'left')
    elif spot == 'above':
        ref_at = (X + x0, Y - y1 - 4.445, 'left'); val_at = (X + x0, Y - y1 - 1.905, 'left')
    else:
        ref_at = (X + x0 - 0.635, Y - y1 - 4.445, 'right'); val_at = (X + x0 - 0.635, Y - y1 - 1.905, 'right')
    extra = [('Footprint', p['fp']), ('Datasheet', ''), ('LCSC', p['lcsc']), ('MPN', p['mpn'])]
    # Footprint și Datasheet sunt deja câmpuri; le scriem o singură dată
    s_extra = [e for e in extra if e[0] not in ('Footprint', 'Datasheet')]
    place_symbol(p['lib'], p['ref'], p['value'], X, Y, 0, ref_at, val_at,
                 extra=[('Footprint', p['fp']), ('Datasheet', '')] + s_extra, uuid=sym_uuid(p['ref']))
    seen = {}
    for pp in lib['pins']:
        pos = (round(X + pp['x'], 4), round(Y - pp['y'], 4))
        net = p['pins'].get(pp['num'])
        if pos in seen:                              # pini suprapuși (ex. GND-urile modulului)
            assert seen[pos] == net, (p['ref'], pp['num'])
            continue
        seen[pos] = net
        if net is None:
            nc(pos); continue
        d = stub_dir(pp['ang']); vx, vy = VEC[d]
        end = (pos[0] + vx * STUB, pos[1] + vy * STUB)
        wire(pos, end)
        endpoint_label(net, end, d)

# ---------------------------------------------------------------- așezarea pe grupuri
GROUPS = [('usb', 'USB-C ȘI PROTECȚIA'), ('charger', 'ÎNCĂRCĂTORUL ȘI CĂILE DE ALIMENTARE'),
          ('battery', 'BATERIA ȘI INDICATORUL DE ÎNCĂRCARE'), ('regulator', 'REGULATORUL DE 3,3 V'),
          ('loadsw', 'COMUTATOARELE PENTRU CARD ȘI AFIȘAJ'), ('mcu', 'MICROCONTROLERUL'),
          ('buttons', 'BUTOANELE'), ('epd', 'AFIȘAJUL'), ('sd', 'CARDUL microSD'), ('mech', 'GĂURI DE PRINDERE')]
PAPER_W, PAPER_H = 594, 420            # A2
MARGIN, GROUP_W, GAP = 20.32, 175.26, 5.08
gx, gy, row_h = MARGIN, MARGIN, 0
for key, title in GROUPS:
    parts = [p for p in PARTS if p['group'] == key]
    # mai întâi piesele mari (IC-urile), apoi pasivele
    parts.sort(key=lambda p: -(LIB[p['lib']]['bbox'][1] - LIB[p['lib']]['bbox'][0]))
    cells = [(p, extents(p)) for p in parts]
    # o primă trecere ca să aflăm înălțimea grupului
    lay, cx, cy, rh = [], 0, 12.7, 0
    for p, (x0, x1, y0, y1) in cells:
        w, h = x1 - x0, y1 - y0
        if cx > 0 and cx + w > GROUP_W - 5.08:
            cx, cy, rh = 0, cy + rh + GAP, 0
        lay.append((p, cx - x0, cy - y0))
        cx += w + GAP; rh = max(rh, h)
    gh = cy + rh + 5.08
    gw = GROUP_W
    if gx + gw > PAPER_W - MARGIN:
        gx, gy, row_h = MARGIN, gy + row_h + 7.62, 0
    box(gx, gy, gx + gw, gy + gh, title)
    for p, ox, oy in lay:
        place_part(p, snap(gx + 5.08 + ox), snap(gy + oy))
    gx += gw + 7.62; row_h = max(row_h, gh)

# PWR_FLAG: rețelele cu pini power_in și fără nicio sursă
ptype = {}
for p in PARTS:
    for pp in LIB[p['lib']]['pins']:
        n = p['pins'].get(pp['num'])
        if n: ptype.setdefault(n, set()).add(pp['type'])
need = sorted(n for n, t in ptype.items() if 'power_in' in t and 'power_out' not in t)
fx, fy = gx, gy
if fx + 120 > PAPER_W - MARGIN: fx, fy = MARGIN, gy + row_h + 7.62
box(fx, fy, fx + 120, fy + 20.32 + 7.62 * len(need), 'FLAGURI DE ALIMENTARE')
for i, n in enumerate(need):
    x, y = snap(fx + 20.32), snap(fy + 15.24 + 7.62 * i)
    # PWR_FLAG cu pinul spre dreapta, apoi fir și eticheta rețelei
    pwr('flag', x, y, 'left')
    wire((x, y), (x + 7.62, y))
    endpoint_label(n, (x + 7.62, y), 'right')
row_h = max(row_h, 20.32 + 7.62 * len(need))
text('Placa proprie v1, CIORNĂ. Generată de hardware/pcb/gen_sch.py din circuit.py; nu se editează de mână.\n'
     'Valorile, motivele și bugetul de consum: hardware/pcb/README.md. GPIO-urile păstrează harta de pe DevKit,\n'
     'plus I2C (IO16 SDA, IO17 SCL), FG_ALRT IO7, SD_PWR_EN IO15, EPD_PWR_EN IO21, VBUS_SENSE IO2.',
     fx + 120 + 7.62, fy + 2.54)

# ---------------------------------------------------------------- scrierea
lib_syms = [sexp.dump(v['tree'], 2) for v in LIB.values()]
sch = (f'(kicad_sch (version 20250114) (generator "eeschema") (generator_version "10.0")\n'
       f'(uuid {q(ROOT)})\n(paper "A2")\n'
       f'(title_block (title "Cititor e-ink: placa proprie v1 (CIORNĂ)") (date "2026-11-17") (rev "v1-ciornă") '
       f'(company "Proiect de diplomă")\n'
       f'  (comment 1 "ESP32-S3-WROOM-1, BQ24074, TPS63900, MAX17048, TPS22917 x2, USB-C, microSD, WeAct EPD 3.7")\n'
       f'  (comment 2 "Autor: Ilie Beleniuc"))\n'
       f'(lib_symbols\n' + '\n'.join(lib_syms) + '\n)\n' + '\n'.join(items) +
       f'\n(sheet_instances (path "/" (page "1")))\n(embedded_fonts no)\n)\n')
os.chdir(HERE)
open(f'{PROJECT}.kicad_sch', 'w').write(sch)
open('eink-reader-pcb.kicad_sym', 'w').write('(kicad_symbol_lib (version 20241209) (generator "eink-reader-gen")\n'
                                           + max17048() + '\n' + sexp.dump(tps22917(), 1) + '\n)\n')
fplibs = sorted({p['fp'].split(':')[0] for p in PARTS})
open('fp-lib-table', 'w').write(
    '(fp_lib_table (version 7)\n' + ''.join(
        f'  (lib (name "{n}")(type "KiCad")(uri "${{KICAD10_FOOTPRINT_DIR}}/{n}.pretty")(options "")(descr ""))\n'
        for n in fplibs) + ')\n')
libs = sorted({p['lib'].split(':')[0] for p in PARTS} | {'power'})
open('sym-lib-table', 'w').write(
    '(sym_lib_table (version 7)\n' + ''.join(
        f'  (lib (name "{n}")(type "KiCad")(uri "{"${KIPRJMOD}/eink-reader-pcb.kicad_sym" if n == "EinkReader" else "${KICAD10_SYMBOL_DIR}/" + n + ".kicad_sym"}")(options "")(descr ""))\n'
        for n in libs) + ')\n')
import project  # noqa: E402
project.write_pro()
print('schema:', len(PARTS), 'piese,', len(items), 'elemente; PWR_FLAG pe', need)
