"""Trasarea automată a plăcii, pe o grilă de 0,125 mm, cu A* în C (astar.c).

Straturile exterioare F.Cu și B.Cu sunt pentru piste. In1.Cu e planul GND, iar
In2.Cu planul +3V3. Pinii SMD ai acestor două rețele primesc doar câte o pistă
scurtă până la o via spre plan (fan-out). Pinii THT ating planurile direct.

Pentru fiecare clasă de lățime (pista de semnal 0,15 mm, cea de 0,25 mm, cea de
putere 0,4 mm, viaul de 0,6 mm) și fiecare strat, o matrice spune a cui e fiecare
celulă. Valoarea e -1 dacă celula e liberă, id-ul rețelei dacă linia de mijloc a
unei piste de clasa aceea ar ajunge acolo prea aproape de cuprul acelei singure
rețele, și -2 dacă ar fi prea aproape de cuprul a două rețele sau de ceva interzis
(margine, gaură, zona antenei). O pistă a rețelei n poate trece printr-o celulă
cu valoarea -1 sau n. Distanțele se calculează exact față de forma pastilelor
(poligoanele din KiCad), cu o rezervă de 0,03 mm pentru drumul dintre centrele a
două celule. DRC-ul din kicad-cli verifică la sfârșit totul.

O rețea cu mai mulți pini se trasează ca un arbore: A* pornește din tot ce e deja
legat și se oprește la cel mai apropiat pin rămas. Întâi câteva treceri complete
(PASSES), cu rețelele eșuate mutate la început. Pe cea mai bună, fiecare rețea
rămasă se caută „moale”: pistele celorlalte se pot traversa, cu un cost. Rețelele
de pe drumul găsit se smulg, rețeaua se trasează, apoi ele se retrasează. Celulele
disputate devin tot mai scumpe (ca în PathFinder), deci rețelele nu se dau la
nesfârșit afară una pe alta.
"""
import ctypes, os, random, subprocess, sys, time
import numpy as np
import pcbnew
from pcbnew import FromMM, ToMM, VECTOR2I_MM

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from circuit import POWER_NETS, PLANE_NETS  # noqa: E402

G = 0.125                      # pasul grilei, mm
CLR = 0.15                     # distanța minimă cupru–cupru
HOLE_CLR = 0.25                # cupru–gaură
HOLE_HOLE = 0.5                # gaură–gaură
EDGE = 0.5                     # cupru–margine
MARGIN = 0.03                  # rezerva pentru pista dintre centrele celulelor
VIA_D, VIA_DRILL = 0.6, 0.3
VIA_COST = 60                  # un via costă cât 0,75 mm de pistă
LAYER_COST = (0, 3)            # B.Cu puțin mai scump: rămâne mai mult loc pentru turnarea GND
PASSES = 6
SOFT_COST = 400                # trecerea prin cuprul altei rețele, la căutarea moale
HIST_STEP = 30                 # cât se scumpește o celulă disputată
RIPUP = 200                    # câte rip-up-uri cel mult
# clasa: jumătatea lățimii obiectului care se mută pe grilă
R = {'sig': 0.075, 'mid': 0.125, 'pwr': 0.2, 'via': VIA_D / 2}
WIDTH = {'sig': 0.15, 'mid': 0.25, 'pwr': 0.4}
LAYERS = (pcbnew.F_Cu, pcbnew.B_Cu)


def load_astar():
    src = os.path.join(HERE, 'astar.c')
    out = os.path.join(HERE, 'build', 'astar.so')
    if not os.path.exists(out) or os.path.getmtime(out) < os.path.getmtime(src):
        os.makedirs(os.path.dirname(out), exist_ok=True)
        subprocess.run(['cc', '-O2', '-shared', '-fPIC', '-o', out, src], check=True)
    lib = ctypes.CDLL(out)
    u8 = np.ctypeslib.ndpointer(np.uint8, flags='C_CONTIGUOUS')
    i32 = np.ctypeslib.ndpointer(np.int32, flags='C_CONTIGUOUS')
    lib.astar.argtypes = [ctypes.c_int, ctypes.c_int, u8, u8, u8, u8, i32, ctypes.c_int, ctypes.c_int, i32,
                          i32, ctypes.c_int32, i32, ctypes.c_int]
    lib.astar.restype = ctypes.c_int
    return lib


# ------------------------------------------------------------------ geometria
def seg_dist(px, py, ax, ay, bx, by):
    """Distanța de la fiecare punct (px, py) la cel mai apropiat segment AB."""
    dx, dy = bx - ax, by - ay
    l2 = np.where(dx * dx + dy * dy > 0, dx * dx + dy * dy, 1.0)
    t = np.clip(((px[:, None] - ax) * dx + (py[:, None] - ay) * dy) / l2, 0, 1)
    qx, qy = ax + t * dx - px[:, None], ay + t * dy - py[:, None]
    return np.sqrt((qx * qx + qy * qy).min(axis=1))


def poly_dist(px, py, polys):
    """Distanța cu semn până la o reuniune de poligoane (negativă înăuntru)."""
    d = np.full(px.shape, np.inf)
    inside = np.zeros(px.shape, bool)
    for p in polys:
        a, b = p, np.roll(p, -1, axis=0)
        d = np.minimum(d, seg_dist(px, py, a[:, 0], a[:, 1], b[:, 0], b[:, 1]))
        ay, by = a[:, 1], b[:, 1]
        cross = (ay > py[:, None]) != (by > py[:, None])
        with np.errstate(divide='ignore', invalid='ignore'):
            xi = (b[:, 0] - a[:, 0]) * (py[:, None] - ay) / (by - ay) + a[:, 0]
        inside ^= (cross & (px[:, None] < xi)).sum(axis=1) % 2 == 1
    return np.where(inside, -d, d)


def shape_polys(shape_poly_set):
    out = []
    for k in range(shape_poly_set.OutlineCount()):
        o = shape_poly_set.COutline(k)
        out.append(np.array([(ToMM(o.CPoint(i).x), ToMM(o.CPoint(i).y)) for i in range(o.PointCount())]))
    return out


# ------------------------------------------------------------------ grila
class Grid:
    def __init__(self, ox, oy, w, h):
        self.ox, self.oy, self.w, self.h = ox, oy, w, h
        self.nx, self.ny = int(w / G) + 1, int(h / G) + 1
        self.owner = {c: np.full((2, self.ny, self.nx), -1, np.int32) for c in R}
        self.hole = np.zeros((self.ny, self.nx), bool)      # aici nu poate sta centrul unui via
        self.smd = np.zeros((self.ny, self.nx), bool)       # nici aici (via în pastilă)

    def copy(self):
        g = Grid.__new__(Grid)
        g.__dict__.update(self.__dict__)
        g.owner = {c: a.copy() for c, a in self.owner.items()}
        g.hole, g.smd = self.hole.copy(), self.smd.copy()
        return g

    def xy(self, i, j):
        return self.ox + i * G, self.oy + j * G

    def region(self, x0, y0, x1, y1, r):
        i0 = max(0, int(np.floor((x0 - r - self.ox) / G)))
        i1 = min(self.nx, int(np.ceil((x1 + r - self.ox) / G)) + 1)
        j0 = max(0, int(np.floor((y0 - r - self.oy) / G)))
        j1 = min(self.ny, int(np.ceil((y1 + r - self.oy) / G)) + 1)
        if i0 >= i1 or j0 >= j1:
            return None
        xs = self.ox + np.arange(i0, i1) * G
        ys = self.oy + np.arange(j0, j1) * G
        px, py = np.meshgrid(xs, ys)
        return (slice(j0, j1), slice(i0, i1)), px.ravel(), py.ravel(), px.shape

    def dist(self, geom, reach):
        """(regiunea, distanțele) pentru o formă: ('poly', [poligoane]),
        ('seg', (ax, ay, bx, by, jumătatea lățimii)) sau ('circle', (cx, cy, r))."""
        kind, g = geom
        if kind == 'poly':
            allp = np.vstack(g)
            box = allp[:, 0].min(), allp[:, 1].min(), allp[:, 0].max(), allp[:, 1].max()
        elif kind == 'seg':
            ax, ay, bx, by, hw = g
            box = min(ax, bx) - hw, min(ay, by) - hw, max(ax, bx) + hw, max(ay, by) + hw
        else:
            cx, cy, r = g
            box = cx - r, cy - r, cx + r, cy + r
        reg = self.region(*box, reach)
        if reg is None:
            return None, None
        sl, px, py, shape = reg
        if kind == 'poly':
            d = poly_dist(px, py, g)
        elif kind == 'seg':
            d = seg_dist(px, py, np.array([ax]), np.array([ay]), np.array([bx]), np.array([by])) - hw
        else:
            d = np.hypot(px - cx, py - cy) - r
        return sl, d.reshape(shape)

    def mark(self, layers, net, geom, clr=CLR, classes=tuple(R)):
        reach = max(R[c] for c in classes) + clr + MARGIN
        sl, d = self.dist(geom, reach)
        if sl is None:
            return
        for c in classes:
            m = d < R[c] + clr + MARGIN
            for l in layers:
                sub = self.owner[c][l][sl]
                if net == -2:
                    sub[m] = -2
                else:
                    cur = sub[m]
                    sub[m] = np.where((cur == -1) | (cur == net), net, -2)

    def mark_hole(self, cx, cy, r):
        sl, d = self.dist(('circle', (cx, cy, r)), HOLE_HOLE + VIA_DRILL / 2 + MARGIN)
        if sl is not None:
            self.hole[sl] |= d < HOLE_HOLE + VIA_DRILL / 2 + MARGIN

    def free(self, c, net):
        o = self.owner[c]
        return ((o == -1) | (o == net)).astype(np.uint8)

    def via_ok(self, net):
        o = self.owner['via']
        ok = ((o[0] == -1) | (o[0] == net)) & ((o[1] == -1) | (o[1] == net)) & ~self.hole & ~self.smd
        return ok.astype(np.uint8)


# ------------------------------------------------------------------ placa
class Router:
    def __init__(self, board, netmap, ox, oy, w, h):
        self.board, self.netmap = board, netmap
        self.ids = {n: k for k, n in enumerate(sorted(netmap))}
        self.names = {k: n for n, k in self.ids.items()}
        self.lib = load_astar()
        self.pads = []          # (pad, net id, {strat: poligoane}, celulele-țintă)
        base = Grid(ox, oy, w, h)
        self.static(base, board)
        self.base = base
        self.zero = np.zeros((2, base.ny, base.nx), np.int32)
        self.hist = self.zero.copy()      # congestia: cât de des a fost disputată o celulă

    def static(self, g, board):
        # marginea plăcii
        for c in R:
            r = R[c] + EDGE + MARGIN
            ys, xs = np.mgrid[0:g.ny, 0:g.nx]
            x, y = xs * G, ys * G
            near = (x < r) | (y < r) | (x > g.w - r) | (y > g.h - r)
            g.owner[c][:, near] = -2
        for fp in board.GetFootprints():
            # zonele interzise din amprente (antena modulului)
            for z in fp.Zones():
                if z.GetIsRuleArea() and z.GetDoNotAllowTracks():
                    ls = [k for k, l in enumerate(LAYERS) if z.GetLayerSet().Contains(l)]
                    g.mark(ls, -2, ('poly', shape_polys(z.Outline())), clr=0)
            if fp.GetReference() == 'U7':
                self.module_floor(g, fp)
            for pad in fp.Pads():
                pos = pad.GetPosition()
                cx, cy = ToMM(pos.x), ToMM(pos.y)
                dr = pad.GetDrillSize()
                if dr.x > 0:
                    r = ToMM(max(dr.x, dr.y)) / 2
                    g.mark_hole(cx, cy, r)
                    if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                        g.mark([0, 1], -2, ('circle', (cx, cy, r)), clr=HOLE_CLR)
                        continue
                net = pad.GetNetname()
                nid = self.ids.get(net, -2) if net else -2
                polys, cells = {}, {}
                for k, l in enumerate(LAYERS):
                    if not pad.IsOnLayer(l):
                        continue
                    pl = shape_polys(pad.GetEffectivePolygon(l, pcbnew.ERROR_INSIDE))
                    polys[k] = pl
                    g.mark([k], nid, ('poly', pl))
                    sl, d = g.dist(('poly', pl), 0.1)
                    cells[k] = (sl, d <= 0.05)
                    if dr.x == 0:
                        sl2, d2 = g.dist(('poly', pl), R['via'] + 0.05)
                        g.smd[sl2] |= d2 < R['via'] + 0.05
                if nid >= 0:
                    self.pads.append((pad, nid, polys, cells))

    def module_floor(self, g, fp):
        """Sub corpul modulului ESP32-S3, pe F.Cu, nu trece nimic: dedesubt are
        pastile și metal. Pe B.Cu se poate."""
        bb = {}
        for p in fp.Pads():
            if p.GetNumber().isdigit() and 1 <= int(p.GetNumber()) <= 40:
                b = p.GetBoundingBox()
                bb[int(p.GetNumber())] = (ToMM(b.GetLeft()), ToMM(b.GetTop()), ToMM(b.GetRight()), ToMM(b.GetBottom()))
        x0 = max(bb[n][2] for n in range(15, 27)) + 0.3
        y0 = max(bb[n][3] for n in range(1, 15)) + 0.3
        y1 = min(bb[n][1] for n in range(27, 41)) - 0.3
        x1 = max(bb[n][2] for n in bb)
        rect = np.array([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
        g.mark([0], -2, ('poly', [rect]), clr=0)
        self.floor = (x0, y0, x1, y1)

    # -------------------------------------------------------------- căutarea
    def search(self, g, free, via_ok, start, target, max_cost=1 << 30, cost=None):
        idx = np.flatnonzero(target.reshape(2, -1).any(axis=0))
        if idx.size == 0:
            return None
        ys, xs = np.divmod(idx, g.nx)
        # dreptunghiurile-țintă: unul singur, ca euristica să fie ieftină și admisibilă
        boxes = np.array([[xs.min(), ys.min(), xs.max(), ys.max()]], np.int32)
        path = np.zeros(200000, np.int32)
        n = self.lib.astar(g.nx, g.ny, np.ascontiguousarray(free), np.ascontiguousarray(via_ok),
                           np.ascontiguousarray(start), np.ascontiguousarray(target), boxes, len(boxes),
                           VIA_COST, np.array(LAYER_COST, np.int32),
                           np.ascontiguousarray(self.zero if cost is None else cost, np.int32),
                           max_cost, path, len(path))
        if n <= 0:
            return None
        plane = g.nx * g.ny
        p = path[:n]
        return [(int(i // plane), int(i % plane % g.nx), int(i % plane // g.nx)) for i in p]

    def pad_cells(self, g, entry, cls):
        pad, nid, polys, cells = entry
        m = np.zeros((2, g.ny, g.nx), np.uint8)
        for k, (sl, mm) in cells.items():
            m[k][sl] |= mm
        return m & g.free(cls, nid)

    def commit(self, g, nid, cls, path):
        """Pista (și viaurile) din drumul găsit, scrise în grilă și ținute minte."""
        segs, vias = [], []
        run = [path[0]]
        for a, b in zip(path, path[1:]):
            if a[0] != b[0]:
                vias.append((b[1], b[2]))
                segs += polyline(run)
                run = [b]
            else:
                run.append(b)
        segs += polyline(run)
        hw = WIDTH[cls] / 2
        out = []
        for l, (i0, j0), (i1, j1) in segs:
            ax, ay = g.xy(i0, j0)
            bx, by = g.xy(i1, j1)
            g.mark([l], nid, ('seg', (ax, ay, bx, by, hw)))
            out.append(('track', nid, l, ax, ay, bx, by, WIDTH[cls]))
        for i, j in vias:
            x, y = g.xy(i, j)
            g.mark([0, 1], nid, ('circle', (x, y, VIA_D / 2)))
            g.mark_hole(x, y, VIA_DRILL / 2)
            out.append(('via', nid, x, y))
        return out

    def fanout(self, g, items):
        """Pinii SMD ai planurilor: o pistă scurtă pe F.Cu până la cea mai apropiată
        via care are loc, sau până la pastila centrală a aceleiași capsule dacă
        aceea are vias proprii spre plan (pinii GND de lângă ea, ca de obicei la
        QFN)."""
        fails = []
        anchored = {}            # amprentă → pastilele ei deja legate de plan
        for entry in self.pads:
            pad, nid, polys, cells = entry
            fp = pad.GetParentFootprint()
            if self.names[nid] in PLANE_NETS and pad.GetDrillSize().x == 0 and any(
                    q.GetNumber() == pad.GetNumber() and q.GetDrillSize().x > 0 for q in fp.Pads()):
                anchored.setdefault(fp.GetReference(), []).append(entry)
        for entry in self.pads:
            pad, nid, polys, cells = entry
            net = self.names[nid]
            if net not in PLANE_NETS or pad.GetDrillSize().x > 0:
                continue
            fp = pad.GetParentFootprint()
            if any(q.GetNumber() == pad.GetNumber() and q.GetDrillSize().x > 0 for q in fp.Pads()):
                continue        # pastila centrală cu vias termice proprii
            for cls in ('pwr', 'mid', 'sig'):
                start = self.pad_cells(g, entry, cls)
                start[1] = 0
                if not start.any():
                    continue
                free = g.free(cls, nid)
                free[1] = 0
                vok = g.via_ok(nid)
                target = np.zeros_like(free)
                target[0] = vok & free[0]
                ep = np.zeros_like(free)
                for e in anchored.get(fp.GetReference(), []):
                    if e[1] == nid:
                        ep |= self.pad_cells(g, e, cls)
                ep[1] = 0
                path = self.search(g, free, np.zeros_like(vok), start, target | ep, max_cost=320)
                if path:
                    items += self.commit(g, nid, cls, path)
                    l, i, j = path[0]
                    if not ep[l, j, i]:
                        x, y = g.xy(i, j)
                        g.mark([0, 1], nid, ('circle', (x, y, VIA_D / 2)))
                        g.mark_hole(x, y, VIA_DRILL / 2)
                        items.append(('via', nid, x, y))
                    break
            else:
                fails.append(f'{fp.GetReference()}.{pad.GetNumber()}')
        return fails

    def route_net(self, g, nid, items, classes=None):
        entries = [e for e in self.pads if e[1] == nid]
        net = self.names[nid]
        classes = classes or (('pwr', 'mid') if net in POWER_NETS else ('sig',))
        for cls in classes:
            trial = g.copy()
            got = []
            ok = self.route_tree(trial, nid, cls, entries, got, cost=self.hist)
            if ok:
                g.__dict__.update(trial.__dict__)
                items += got
                return True
        return False

    def route_tree(self, g, nid, cls, entries, items, cost=None, soft=None, paths=None):
        """soft: grila fără pistele trasate (doar pastile, margini, fan-out); atunci
        cuprul celorlalte rețele din g se poate traversa, cu un cost mare."""
        tree = self.pad_cells(soft or g, entries[0], cls)
        if not tree.any():
            return False
        rest = list(entries[1:])
        while rest:
            gg = soft or g
            target = np.zeros_like(tree)
            masks = [self.pad_cells(gg, e, cls) for e in rest]
            for m in masks:
                target |= m
            if soft is not None:
                free = soft.free(cls, nid)
                hard = g.free(cls, nid)
                c = cost + np.where(hard == 0, SOFT_COST, 0).astype(np.int32)
                path = self.search(g, free, g.via_ok(nid), tree, target, cost=c)
            else:
                free = g.free(cls, nid)
                path = self.search(g, free, g.via_ok(nid), tree, target, cost=cost)
            if not path:
                return False
            l, i, j = path[0]
            k = next(k for k, m in enumerate(masks) if m[l, j, i])
            if paths is not None:
                paths.append(path)
            if soft is None:
                items += self.commit(g, nid, cls, path)
            for l2, i2, j2 in path:
                tree[l2, j2, i2] = 1
            tree |= masks[k]
            rest.pop(k)
        return True

    # -------------------------------------------------------------- rip-up
    def replay(self, g, items):
        for it in items:
            if it[0] == 'track':
                _, nid, l, ax, ay, bx, by, w = it
                g.mark([l], nid, ('seg', (ax, ay, bx, by, w / 2)))
            else:
                _, nid, x, y = it
                g.mark([0, 1], nid, ('circle', (x, y, VIA_D / 2)))
                g.mark_hole(x, y, VIA_DRILL / 2)

    def blockers(self, g, nid, cls, paths, by_net):
        """Rețelele al căror cupru stă pe drumul găsit prin căutarea moale."""
        hard = g.free(cls, nid)
        pts = {0: [], 1: []}
        for path in paths:
            for l, i, j in path:
                if not hard[l, j, i]:
                    pts[l].append(g.xy(i, j))
        reach = R[cls] + CLR + MARGIN
        found = set()
        for l in (0, 1):
            if not pts[l]:
                continue
            p = np.array(pts[l])
            for n, its in by_net.items():
                if n == nid or self.names[n] in PLANE_NETS or n in found:
                    continue
                for it in its:
                    if it[0] == 'track':
                        if it[2] != l:
                            continue
                        d = seg_dist(p[:, 0], p[:, 1], np.array([it[3]]), np.array([it[4]]),
                                     np.array([it[5]]), np.array([it[6]])) - it[7] / 2
                    else:
                        d = np.hypot(p[:, 0] - it[2], p[:, 1] - it[3]) - VIA_D / 2
                    if (d < reach).any():
                        found.add(n)
                        break
        return found, pts

    def ripup(self, by_net, fails, budget):
        """Pentru fiecare rețea eșuată: drumul ei ignorând pistele celorlalte (cu
        cost), smulge rețelele de pe drum, o trasează, apoi le retrasează pe ele.
        Celulele disputate devin mai scumpe (istoria din PathFinder), ca două
        rețele să nu se tot dea afară una pe alta."""
        fixed = [it for n, its in by_net.items() if self.names[n] in PLANE_NETS for it in its]
        soft = self.base.copy()
        self.replay(soft, fixed)
        queue = list(fails)
        dead = []

        def rebuild():
            g = soft.copy()
            self.replay(g, [it for n, its in by_net.items() if self.names[n] not in PLANE_NETS for it in its])
            return g

        g = rebuild()
        for it in range(budget):
            if not queue:
                break
            n = queue.pop(0)
            net = self.names[n]
            entries = [e for e in self.pads if e[1] == n]
            classes = ('pwr', 'mid') if net in POWER_NETS else ('sig',)
            for cls in classes:
                paths = []
                if self.route_tree(g, n, cls, entries, [], cost=self.hist, soft=soft, paths=paths):
                    break
            else:
                dead.append(n)          # nu are loc nici fără celelalte piste
                continue
            found, pts = self.blockers(g, n, cls, paths, by_net)
            for l in (0, 1):
                for x, y in pts[l]:
                    i, j = round((x - g.ox) / G), round((y - g.oy) / G)
                    self.hist[l, max(0, j - 2):j + 3, max(0, i - 2):i + 3] += HIST_STEP
            for b in found:
                by_net.pop(b, None)
            g = rebuild()
            got = []
            if self.route_net(g, n, got, classes=(cls,)) or self.route_net(g, n, got):
                by_net[n] = got
            else:
                queue.append(n)
            for b in sorted(found, key=lambda b: -len(self.names[b])):
                got = []
                if self.route_net(g, b, got):
                    by_net[b] = got
                else:
                    queue.append(b)
            print(f'  rip-up {it + 1}: {net} ← {", ".join(self.names[b] for b in found) or "-"}; '
                  f'rămase {len(queue)}' + (f' ({", ".join(self.names[q] for q in queue)})' if queue else ''),
                  flush=True)
        return queue + dead

    # -------------------------------------------------------------- tot
    def run(self):
        signal = sorted({e[1] for e in self.pads if self.names[e[1]] not in PLANE_NETS})
        count = {n: sum(1 for e in self.pads if e[1] == n) for n in signal}
        signal = [n for n in signal if count[n] > 1]

        def span(n):
            ps = [e[0].GetPosition() for e in self.pads if e[1] == n]
            xs, ys = [ToMM(p.x) for p in ps], [ToMM(p.y) for p in ps]
            return max(xs) - min(xs) + max(ys) - min(ys)
        power = sorted((n for n in signal if self.names[n] in POWER_NETS), key=span)
        rest = sorted((n for n in signal if self.names[n] not in POWER_NETS), key=span)
        # puterea întâi (pistele late ies greu dintre pinii la 0,5 mm), apoi
        # viaurile spre planuri (FANOUT), apoi semnalele; ce a eșuat trece în față
        # la trecerea următoare, oricare ar fi clasa, iar dacă nu se mai câștigă
        # nimic, ordinea restului se amestecă
        FANOUT = -1
        order = power + [FANOUT] + rest
        rng = random.Random(1)
        best = None
        for p in range(PASSES):
            t0 = time.time()
            g = self.base.copy()
            items = []
            fails = []
            ffails = []
            for n in order:
                if n == FANOUT:
                    ffails = self.fanout(g, items)
                    if ffails:
                        fails.append(FANOUT)
                elif not self.route_net(g, n, items):
                    fails.append(n)
            nf = sum(1 for n in fails if n != FANOUT)
            print(f'  trecerea {p + 1}: {len(signal) - nf}/{len(signal)} rețele, '
                  f'fan-out ratat {len(ffails)}, {time.time() - t0:.1f} s'
                  + (': ' + ', '.join(self.names[n] for n in fails if n != FANOUT) if nf else ''), flush=True)
            score = nf + 10 * len(ffails)   # fan-out-ul nu se mai repară după
            if best is None or score < best[0]:
                best = (score, items, [n for n in fails if n != FANOUT], ffails)
                stale = 0
            else:
                stale += 1
            if not score:
                break
            others = [n for n in order if n not in fails]
            if stale >= 2:
                rng.shuffle(others)
            order = fails + others
        score, items, fails, ffails = best
        if fails:
            by_net = {}
            for it in items:
                by_net.setdefault(it[1], []).append(it)
            fails = self.ripup(by_net, fails, RIPUP)
            items = [it for its in by_net.values() for it in its]
        return score, items, fails, ffails

    def apply(self, items):
        """Pistele și viaurile, puse pe placă."""
        b = self.board
        for it in items:
            net = self.netmap[self.names[it[1]]]
            if it[0] == 'track':
                _, _, l, ax, ay, bx, by, w = it
                t = pcbnew.PCB_TRACK(b)
                t.SetStart(VECTOR2I_MM(ax, ay)); t.SetEnd(VECTOR2I_MM(bx, by))
                t.SetWidth(FromMM(w)); t.SetLayer(LAYERS[l]); t.SetNet(net)
                b.Add(t)
            else:
                _, _, x, y = it
                v = pcbnew.PCB_VIA(b)
                v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
                v.SetPosition(VECTOR2I_MM(x, y)); v.SetWidth(FromMM(VIA_D)); v.SetDrill(FromMM(VIA_DRILL))
                v.SetNet(net)
                b.Add(v)


def polyline(run):
    """Celulele unui drum pe un strat → segmente, unite cât timp direcția e aceeași."""
    if len(run) < 2:
        return []
    out = []
    l = run[0][0]
    start = run[0]
    prev_dir = None
    for a, b in zip(run, run[1:]):
        d = (b[1] - a[1], b[2] - a[2])
        if prev_dir is not None and d != prev_dir:
            out.append((l, (start[1], start[2]), (a[1], a[2])))
            start = a
        prev_dir = d
    out.append((l, (start[1], start[2]), (run[-1][1], run[-1][2])))
    return out


def route_board(board, netmap, ox, oy, w, h):
    r = Router(board, netmap, ox, oy, w, h)
    nfail, items, fails, ffails = r.run()
    r.apply(items)
    tracks = [it for it in items if it[0] == 'track']
    length = sum(np.hypot(t[5] - t[3], t[6] - t[4]) for t in tracks)
    vias = sum(1 for it in items if it[0] == 'via')
    return {'fails': [r.names[n] for n in fails], 'fanout_fails': ffails, 'tracks': len(tracks),
            'length_mm': round(float(length), 1), 'vias': vias}
