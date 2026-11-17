"""Serigrafia de pe spatele plăcii: sigla USV și numele proiectului.

Pe B.SilkS, fața fără piese, care rămâne la vedere când se deschide carcasa (pe
F stă bareta spre afișaj). Totul intră într-un grup, `serigrafie`, care se
înlocuiește la fiecare rulare:

    python3 marking.py      # pe placa existentă, fără retrasare
    (gen_pcb.py o cheamă și el, la generarea plăcii)

Sigla vine din `usv_sigla.json`: sigla de pe usv.ro, vectorizată cu potrace,
în unități de „înălțimea siglei = 1”, cu y în jos.

Coordonatele de aici sunt cum se vede spatele plăcii, de dedesubt: u de la
marginea din dreapta a plăcii (văzută de sus) spre stânga, y în jos.
"""
import json, os, sys
import pcbnew
from pcbnew import VECTOR2I_MM, FromMM

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 105.79, 53.80
OX, OY = 100.0, 100.0
GROUP = 'serigrafie'

LOGO_H = 13.0                   # înălțimea siglei, mm
LOGO_U, LOGO_Y = 62.0, 8.6      # colțul ei din stânga sus, văzut de dedesubt
# (text, u, aliniere, înălțimea literei, grosimea liniei, y-ul liniei de bază); la dreapta
# siglei se poate până la u ≈ 101 (urechile butoanelor SW2/SW3 sunt la 103,7)
LINES = [
    ('Proiect de licență', 80.0, 'L', 1.3, 0.18, 11.9),
    ('Beleniuc Ilie', 80.0, 'L', 1.9, 0.28, 15.9),
    ('Cititor de cărți electronice', 80.0, 'L', 1.0, 0.15, 19.0),
    ('2027 · placa v1', 80.0, 'L', 1.0, 0.15, 21.6),
    ('Universitatea „Ștefan cel Mare” din Suceava', 81.0, 'C', 1.0, 0.15, 25.6),
]
CLEAR = 0.3                     # distanța minimă față de pastile, găuri și margine

def to_board(u, y):
    """Din vederea de dedesubt în coordonatele KiCad (vederea de sus)."""
    return OX + W - u, OY + y

def add_marking(board):
    old = [g for g in board.Groups() if g.GetName() == GROUP]
    for g in old:
        items = list(g.GetItems())
        g.RemoveAll()
        board.Remove(g)
        for it in items:
            board.Remove(it)
    grp = pcbnew.PCB_GROUP(board); grp.SetName(GROUP)
    board.Add(grp)

    logo = json.load(open(os.path.join(HERE, 'usv_sigla.json'), encoding='utf-8'))
    for poly in logo['poligoane']:
        ps = pcbnew.SHAPE_POLY_SET(); ps.NewOutline()
        for i, ring in enumerate(poly):
            if i: ps.NewHole()
            for x, y in ring:
                bx, by = to_board(LOGO_U + x * LOGO_H, LOGO_Y + y * LOGO_H)
                if i == 0: ps.Append(FromMM(bx), FromMM(by))
                else: ps.Append(FromMM(bx), FromMM(by), 0, i - 1)
        ps.Fracture()
        for k in range(ps.OutlineCount()):
            one = pcbnew.SHAPE_POLY_SET(); one.AddOutline(ps.Outline(k))
            s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_POLY)
            s.SetPolyShape(one); s.SetFilled(True); s.SetWidth(0)
            s.SetLayer(pcbnew.B_SilkS)
            board.Add(s); grp.AddItem(s)

    for text, u, just, size, thick, base in LINES:
        t = pcbnew.PCB_TEXT(board)
        t.SetText(text); t.SetLayer(pcbnew.B_SilkS); t.SetMirrored(True)
        t.SetTextSize(VECTOR2I_MM(size, size)); t.SetTextThickness(FromMM(thick))
        # oglindit: „stânga” rămâne începutul textului, adică u mic
        t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT if just == 'L' else pcbnew.GR_TEXT_H_ALIGN_CENTER)
        t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_BOTTOM)
        t.SetPosition(VECTOR2I_MM(*to_board(u, base)))
        board.Add(t); grp.AddItem(t)
    return grp

def check(board, grp):
    """Nimic din serigrafie peste o pastilă, o gaură sau marginea plăcii."""
    errs = []
    inside = pcbnew.BOX2I(VECTOR2I_MM(OX + CLEAR, OY + CLEAR), VECTOR2I_MM(W - 2 * CLEAR, H - 2 * CLEAR))
    obst = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.IsOnLayer(pcbnew.B_Cu) or p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                obst.append((f'{fp.GetReference()}.{p.GetNumber()}', p.GetBoundingBox()))
    for t in board.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA):
            obst.append(('via', t.GetBoundingBox()))
    for it in grp.GetItems():
        bb = it.GetBoundingBox()
        if not inside.Contains(bb):
            errs.append('serigrafia iese din placă')
        sh = it.GetEffectiveShape(pcbnew.B_SilkS)
        for name, ob in obst:
            big = pcbnew.BOX2I(ob.GetPosition(), ob.GetSize()); big.Inflate(FromMM(CLEAR))
            if not big.Intersects(bb):
                continue
            r = pcbnew.SHAPE_RECT(big)
            if sh.Collide(r, 0):
                errs.append(f'serigrafia atinge {name}')
    return sorted(set(errs))

if __name__ == '__main__':
    path = os.path.join(HERE, 'eink-reader-pcb.kicad_pcb')
    board = pcbnew.LoadBoard(path)
    grp = add_marking(board)
    errs = check(board, grp)
    for e in errs:
        print('EROARE:', e)
    if errs:
        sys.exit(1)
    board.Save(path)
    print('serigrafia: sigla și', len(LINES), 'rânduri de text pe B.SilkS')
