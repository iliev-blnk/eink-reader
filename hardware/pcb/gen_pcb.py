"""Generează placa (eink-reader-pcb.kicad_pcb) din circuit.py: conturul, piesele
așezate, rețelele pe pini, planurile; apoi o trasează cu route.py.

    python3 gen_pcb.py            # așază, trasează, umple zonele
    python3 gen_pcb.py --place    # doar așezarea (pentru a o privi)
    python3 gen_pcb.py --no-pour  # trasată, fără turnările GND de pe F.Cu și B.Cu

Placa are mărimea plăcii modulului de afișaj WeAct de 3,7 țoli (105,79 × 53,80 mm),
cu găurile de M2 în aceleași locuri, ca să stea spate în spate. Coordonatele de mai
jos sunt față de colțul din stânga sus al plăcii.
"""
import os, sys
import pcbnew
from pcbnew import VECTOR2I_MM, FromMM, ToMM

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from circuit import PARTS, nets, PLANE_NETS  # noqa: E402
from gen_sch import sym_uuid  # noqa: E402  (căile spre simboluri, ca placa să fie legată de schemă)

PROJECT = 'eink-reader-pcb'
FPDIR = '/usr/share/kicad/footprints'
W, H = 105.79, 53.80
OX, OY = 100.0, 100.0           # colțul plăcii în coordonatele KiCad
EDGE = 0.5                      # cuprul stă la cel puțin atât de margine

# ---------------------------------------------------------------- așezarea
# ref: (x, y, rotație). Pentru piesele lipite de margine, x sau y e 'L'/'R'/'T'/'B':
# piesa se mută până când cuprul ei e la EDGE + 0,05 mm de marginea aceea.
PLACE = {
    'H1': (6.395, 2.9, 0), 'H2': (98.2, 2.9, 0), 'H3': (6.395, 50.9, 0), 'H4': (98.2, 50.9, 0),
    # modulul: antena spre dreapta, peste marginea plăcii (zona fără cupru e la x > 99,79)
    'U7': (93.04, 26.9, 270),
    'C14': (96.2, 14.5, 90), 'C15': (97.9, 14.5, 90), 'R19': (92.8, 14.5, 90), 'C16': (91.1, 14.5, 90),
    'R12': (86.0, 14.5, 90), 'R13': (87.7, 14.5, 90), 'R14': (89.4, 14.5, 90),
    'SW6': (90.0, 8.0, 0),
    'R20': (82.0, 40.0, 90), 'SW5': (84.0, 47.0, 0), 'J5': (86.0, 41.0, 90),
    'R17': (94.0, 40.5, 90), 'R18': (95.7, 40.5, 90),
    'R15': (94.0, 44.0, 90), 'R16': (95.7, 44.0, 90), 'C13': (97.4, 44.0, 90),
    # cardul: fanta spre marginea de sus
    'J4': (70.0, 'T', 0),
    'R21': (64.0, 15.5, 90), 'R22': (66.0, 15.5, 90), 'R23': (68.0, 15.5, 90), 'R24': (70.0, 15.5, 90),
    'U4': (60.0, 9.0, 0), 'C7': (60.0, 12.5, 0), 'R10': (60.0, 14.3, 0), 'C9': (60.0, 5.5, 0), 'C10': (60.0, 2.6, 0),
    # afișajul
    'J3': (36.0, 4.0, 90),
    'U5': (50.0, 9.0, 0), 'C11': (54.0, 9.0, 90), 'C8': (46.5, 9.0, 90), 'R11': (50.0, 12.3, 0),
    # butoanele, cu apăsătorul în afara plăcii
    'SW2': ('L', 20.0, 270), 'SW3': ('L', 34.0, 270), 'SW4': (20.0, 'T', 180),
    'SW1': (28.0, 'B', 0),
    # USB-C: linia „PCB Edge” a amprentei pe marginea de jos
    'J1': (40.0, H - 3.675, 0),
    'U1': (40.0, 43.6, 0), 'R1': (36.3, 43.6, 90), 'R2': (43.7, 43.6, 90),
    # încărcătorul: pinii BQ24074 (VQFN-16) sunt TS, BAT, BAT, CE în stânga, EN2,
    # EN1, PGOOD, VSS jos, CHG, OUT, OUT, ILIM în dreapta, IN, TMR, SYSOFF, ISET sus;
    # fiecare piesă stă pe latura pinului ei
    'U2': (52.0, 41.0, 0), 'C1': (52.6, 37.2, 0), 'R4': (49.0, 37.6, 90),
    'C3': (48.2, 41.6, 90), 'R5': (46.5, 41.6, 90), 'C2': (55.8, 41.4, 90), 'R3': (55.6, 37.6, 90),
    'D1': (49.0, 50.6, 0), 'D2': (52.8, 50.6, 0), 'R6': (49.0, 48.8, 0), 'R7': (52.8, 48.8, 0),
    # regulatorul: TPS63900 (WSON-10) are EN, GND, CFG1, GND, CFG3 în stânga și
    # VOUT, L2, GND, L1, VIN în dreapta
    'U3': (63.5, 42.5, 0), 'L1': (67.2, 42.5, 90), 'C4': (63.5, 39.3, 0), 'C5': (63.5, 45.9, 0),
    'R8': (59.9, 42.2, 180), 'R9': (59.9, 44.2, 180),
    # bateria și indicatorul
    'J2': (69.0, 49.5, 0), 'JP1': (72.0, 45.6, 0), 'TP3': (71.0, 34.6, 0), 'TP4': (75.6, 45.6, 0),
    'U6': (72.0, 38.6, 0), 'C12': (69.2, 38.6, 90),
    # punctele de test
    'TP1': (44.0, 33.0, 0), 'TP2': (48.0, 33.0, 0), 'TP6': (52.0, 33.0, 0), 'TP5': (56.0, 33.0, 0),
}

def lib_path(fpid):
    lib, name = fpid.split(':')
    return f'{FPDIR}/{lib}.pretty', name

def copper_bbox(fp):
    xs0, ys0, xs1, ys1 = [], [], [], []
    for p in fp.Pads():
        if not (p.IsOnLayer(pcbnew.F_Cu) or p.IsOnLayer(pcbnew.B_Cu)) and p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH:
            continue
        b = p.GetBoundingBox()
        xs0.append(b.GetLeft()); ys0.append(b.GetTop()); xs1.append(b.GetRight()); ys1.append(b.GetBottom())
    return ToMM(min(xs0)), ToMM(min(ys0)), ToMM(max(xs1)), ToMM(max(ys1))

def build():
    path = os.path.join(HERE, f'{PROJECT}.kicad_pcb')
    board = pcbnew.NewBoard(path)
    board.SetCopperLayerCount(4)
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(FromMM(1.6))

    # conturul
    rect = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECT)
    rect.SetLayer(pcbnew.Edge_Cuts); rect.SetWidth(FromMM(0.05))
    rect.SetStart(VECTOR2I_MM(OX, OY)); rect.SetEnd(VECTOR2I_MM(OX + W, OY + H))
    board.Add(rect)

    netmap = {}
    for n in sorted(nets()):
        ni = pcbnew.NETINFO_ITEM(board, n)
        board.Add(ni); netmap[n] = ni

    nc, ncnets = unconnected_pins(), {}
    for p in PARTS:
        lp, name = lib_path(p['fp'])
        fp = pcbnew.FootprintLoad(lp, name)
        assert fp, p['fp']
        fp.SetFPID(pcbnew.LIB_ID(*p['fp'].split(':')))
        fp.SetReference(p['ref']); fp.SetValue(p['value'])
        fp.SetPath(pcbnew.KIID_PATH('/' + sym_uuid(p['ref'])))
        for k in ('LCSC', 'MPN'):       # și goale, ca în schemă
            if True:
                fld = pcbnew.PCB_FIELD(fp, pcbnew.FIELD_T_USER, k)
                fld.SetText(p[k.lower()]); fld.SetVisible(False); fld.SetLayer(pcbnew.F_Fab)
                fp.Add(fld)
        fp.Value().SetVisible(False)
        # numele pieselor pe desenul de asamblare (F.Fab), nu pe serigrafie: placa e
        # prea deasă, iar JLCPCB asamblează după fișierul CPL, nu după serigrafie
        ref = fp.Reference()
        ref.SetLayer(pcbnew.F_Fab); ref.SetTextSize(VECTOR2I_MM(0.5, 0.5)); ref.SetTextThickness(FromMM(0.08))
        ref.SetPosition(fp.GetPosition())
        # vias termice de 0,2 mm din amprente (ESP32, QFN): 0,3 mm, gaura minimă fără cost în plus
        for pad in fp.Pads():
            d = pad.GetDrillSize()
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and 0 < d.x < FromMM(0.3):
                pad.SetDrillSize(pcbnew.VECTOR2I(FromMM(0.3), FromMM(0.3)))
                sz = pad.GetSize(pcbnew.F_Cu)
                if sz.x < FromMM(0.6):
                    pad.SetSize(pcbnew.F_Cu, pcbnew.VECTOR2I(FromMM(0.6), FromMM(0.6)))
        # cu găuri mai mari, unele vias din aceeași pastilă ajung la mai puțin de
        # 0,5 mm între găuri: rămân cele care nu se încalcă (de la pastila centrală)
        vias = [q for q in fp.Pads() if q.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and q.GetDrillSize().x <= FromMM(0.3)]
        vias.sort(key=lambda q: (q.GetPosition() - fp.GetPosition()).EuclideanNorm())
        kept = []
        for q in vias:
            if all((q.GetPosition() - k.GetPosition()).EuclideanNorm() >= FromMM(0.8) for k in kept):
                kept.append(q)
            else:
                fp.Remove(q)
        x, y, rot = PLACE[p['ref']]
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(VECTOR2I_MM(OX + (x if isinstance(x, float) else 0), OY + (y if isinstance(y, float) else 0)))
        if isinstance(x, str) or isinstance(y, str):
            x0, y0, x1, y1 = copper_bbox(fp)
            m = EDGE + 0.05
            dx = {'L': OX + m - x0, 'R': OX + W - m - x1}.get(x, 0) if isinstance(x, str) else 0
            dy = {'T': OY + m - y0, 'B': OY + H - m - y1}.get(y, 0) if isinstance(y, str) else 0
            fp.Move(VECTOR2I_MM(dx, dy))
        for pad in fp.Pads():
            n = p['pins'].get(pad.GetNumber())
            if n:
                pad.SetNet(netmap[n])
            elif (p['ref'], pad.GetNumber()) in nc:
                # pin cu „nu se conectează” în schemă: KiCad îi dă o rețea proprie
                name = nc[(p['ref'], pad.GetNumber())]
                if name not in ncnets:
                    ncnets[name] = pcbnew.NETINFO_ITEM(board, name)
                    board.Add(ncnets[name])
                pad.SetNet(ncnets[name])
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and any(
                    q.GetNumber() == pad.GetNumber() and q.GetAttribute() == pcbnew.PAD_ATTRIB_SMD for q in fp.Pads()):
                # viaurile din pastila centrală: legate plin, altfel DRC le vede
                # fără punți termice (sunt sub cuprul pastilei)
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
            if p['ref'] == 'J1' and pad.GetNumber() == 'SH':
                # urechile carcasei USB-C, lângă margine: nu încap două punți termice
                pad.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_FULL)
        # serigrafia care ar ieși din placă (antena modulului, comutatorul de pe margine)
        inside = pcbnew.BOX2I(VECTOR2I_MM(OX + 0.2, OY + 0.2), VECTOR2I_MM(W - 0.4, H - 0.4))
        for g in list(fp.GraphicalItems()):
            if g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS) and not inside.Contains(g.GetBoundingBox()):
                fp.Remove(g)
        if p['ref'] == 'U7':
            trim_antenna_courtyard(fp)
        board.Add(fp)

    # planurile interioare, pe toată placa
    for net, layer in PLANE_NETS.items():
        add_zone(board, netmap[net], board.GetLayerID(layer), 0)
    return board, netmap

def trim_antenna_courtyard(fp):
    """Curtea modulului ESP32-S3 cuprinde și zona din jurul antenei, pe toată
    înălțimea plăcii, deci peste găurile de prindere H2 și H4 din colțuri. Partea
    aceea a curții se scurtează până lângă găuri (2,55 mm de la margine). Zona fără
    cupru a antenei din amprentă rămâne întreagă: găurile sunt NPTH, fără cupru, la
    15 mm de antenă, unde le are și placa afișajului."""
    y0, y1 = FromMM(OY + 5.45), FromMM(OY + H - 5.45)
    xa = FromMM(OX + 99.7)
    for g in fp.GraphicalItems():
        if g.GetLayer() != pcbnew.F_CrtYd:
            continue
        for get, put in ((g.GetStart, g.SetStart), (g.GetEnd, g.SetEnd)):
            q = get()
            if q.x >= xa:
                put(pcbnew.VECTOR2I(q.x, min(max(q.y, y0), y1)))

def unconnected_pins():
    """(ref, pin) → numele rețelei „unconnected-(…)” pe care KiCad o dă unui pin
    marcat „nu se conectează”, luat din netlista schemei (kicad-cli)."""
    import subprocess, tempfile
    import xml.etree.ElementTree as ET
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, 'net.xml')
        subprocess.run(['kicad-cli', 'sch', 'export', 'netlist', '--format', 'kicadxml', '-o', out,
                        os.path.join(HERE, f"{PROJECT}.kicad_sch")], check=True, capture_output=True)
        root = ET.parse(out).getroot()
    res = {}
    for net in root.iter('net'):
        if net.get('name').startswith('unconnected-'):
            for node in net.iter('node'):
                res[(node.get('ref'), node.get('pin'))] = net.get('name')
    return res

def add_zone(board, net, layer, prio, margin=0.0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer); z.SetNet(net); z.SetAssignedPriority(prio)
    z.SetLocalClearance(FromMM(0.2)); z.SetMinThickness(FromMM(0.2))
    # pastilele SMD lipite plin, cele THT cu punți termice (se lipesc cu letconul)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)
    z.SetThermalReliefGap(FromMM(0.3)); z.SetThermalReliefSpokeWidth(FromMM(0.4))
    o = z.Outline(); o.NewOutline()
    for x, y in ((0, 0), (W, 0), (W, H), (0, H)):
        o.Append(FromMM(OX + x), FromMM(OY + y))
    board.Add(z)
    return z

if __name__ == '__main__':
    board, netmap = build()
    if '--place' not in sys.argv:
        import route
        res = route.route_board(board, netmap, OX, OY, W, H)
        print('trasare:', res)
        if '--no-pour' not in sys.argv:
            # GND și pe straturile exterioare, în ce rămâne liber
            for l in (pcbnew.F_Cu, pcbnew.B_Cu):
                add_zone(board, netmap['GND'], l, 1)
    # sigla USV și numele proiectului pe spate (marking.py)
    import marking
    errs = marking.check(board, marking.add_marking(board))
    if errs:
        sys.exit('serigrafia: ' + '; '.join(errs))
    # originea fișierelor de fabricație (Gerber, găuri, CPL): colțul din stânga jos
    ds = board.GetDesignSettings()
    ds.SetAuxOrigin(VECTOR2I_MM(OX, OY + H))
    out = os.path.join(HERE, f"{PROJECT}.kicad_pcb")
    board.Save(out)
    if '--place' not in sys.argv and '--no-pour' not in sys.argv:
        # turnările le umple kicad-cli: ZONE_FILLER din Python nu încarcă regulile
        # plăcii (distanța față de găurile NPTH și de margine ar fi 0)
        import subprocess, tempfile
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run(['kicad-cli', 'pcb', 'drc', '--refill-zones', '--save-board',
                            '-o', os.path.join(tmp, 'drc.rpt'), out], check=True, capture_output=True)
    print('placa:', out, len(PARTS), 'piese')
