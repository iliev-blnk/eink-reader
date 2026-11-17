"""Grafice SVG simple, doar cu biblioteca standard.

Două feluri, cât le trebuie experimentelor: bare grupate și linii (sau
histograme desenate ca linii). Textul e în română, cu virgulă zecimală.
"""
import html
import math

W, H = 640, 360
PAD_L, PAD_R, PAD_T, PAD_B = 64, 16, 64, 56
# Culori distincte și pentru cine nu vede bine roșul și verdele (Okabe–Ito).
COLORS = ["#0072B2", "#E69F00", "#009E73", "#CC79A7", "#56B4E9", "#D55E00", "#000000"]
DASHES = ["", "6 3", "2 2", "8 3 2 3"]


def num(v, nd=2):
    s = f"{v:.{nd}f}".rstrip("0").rstrip(".") if nd else f"{v:.0f}"
    if s == "-0":
        s = "0"
    return s.replace(".", ",").replace("-", "−")


def nice_ticks(lo, hi, n=5):
    if hi <= lo:
        hi = lo + 1
    raw = (hi - lo) / n
    mag = 10 ** math.floor(math.log10(raw))
    step = min((m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw), default=mag * 10)
    start = math.floor(lo / step) * step
    ticks, t = [], start
    while True:
        ticks.append(round(t, 10))
        if t >= hi - step * 1e-9:
            return ticks
        t += step


class Plot:
    def __init__(self, title, xlabel, ylabel, w=W, h=H):
        self.title, self.xlabel, self.ylabel, self.w, self.h = title, xlabel, ylabel, w, h
        self.parts = []

    def frame(self, x0, x1, y0, y1, xticks=None, yticks=None, xfmt=num, yfmt=num):
        self.x0, self.x1, self.y0, self.y1 = x0, x1, y0, y1
        self.pw, self.ph = self.w - PAD_L - PAD_R, self.h - PAD_T - PAD_B
        p = self.parts
        for t in yticks or []:
            y = self.Y(t)
            p.append(f'<line x1="{PAD_L}" y1="{y:.1f}" x2="{PAD_L + self.pw}" y2="{y:.1f}" stroke="#ddd"/>')
            p.append(f'<text x="{PAD_L - 6}" y="{y + 4:.1f}" text-anchor="end">{yfmt(t)}</text>')
        for t in xticks or []:
            x = self.X(t)
            p.append(f'<line x1="{x:.1f}" y1="{PAD_T + self.ph}" x2="{x:.1f}" y2="{PAD_T + self.ph + 4}" stroke="#555"/>')
            p.append(f'<text x="{x:.1f}" y="{PAD_T + self.ph + 18}" text-anchor="middle">{xfmt(t)}</text>')
        p.append(f'<rect x="{PAD_L}" y="{PAD_T}" width="{self.pw}" height="{self.ph}" fill="none" stroke="#555"/>')

    def X(self, v):
        return PAD_L + (v - self.x0) / (self.x1 - self.x0) * self.pw

    def Y(self, v):
        return PAD_T + self.ph - (v - self.y0) / (self.y1 - self.y0) * self.ph

    def line(self, xs, ys, i, width=2):
        pts = " ".join(f"{self.X(x):.1f},{self.Y(y):.1f}" for x, y in zip(xs, ys))
        dash = DASHES[i % len(DASHES)]
        self.parts.append(f'<polyline points="{pts}" fill="none" stroke="{COLORS[i % len(COLORS)]}" '
                          f'stroke-width="{width}" stroke-dasharray="{dash}"/>')

    def vline(self, x, label=""):
        X = self.X(x)
        self.parts.append(f'<line x1="{X:.1f}" y1="{PAD_T}" x2="{X:.1f}" y2="{PAD_T + self.ph}" stroke="#888" stroke-dasharray="3 3"/>')
        if label:
            self.parts.append(f'<text x="{X + 4:.1f}" y="{PAD_T + 12}" fill="#555">{html.escape(label)}</text>')

    def hline(self, y, label=""):
        Y = self.Y(y)
        self.parts.append(f'<line x1="{PAD_L}" y1="{Y:.1f}" x2="{PAD_L + self.pw}" y2="{Y:.1f}" stroke="#888" stroke-dasharray="3 3"/>')
        if label:
            self.parts.append(f'<text x="{PAD_L + self.pw - 4}" y="{Y - 4:.1f}" text-anchor="end" fill="#555">{html.escape(label)}</text>')

    def legend(self, names, kinds=None):
        """Pe un rând (sau două) între titlu și grafic, ca să nu acopere datele."""
        x, y = PAD_L, 40
        for i, n in enumerate(names):
            width = 24 + 6.6 * len(n) + 16
            if x + width > self.w - PAD_R:
                x, y = PAD_L, y + 14

            c = COLORS[i % len(COLORS)]
            if kinds == "bar":
                self.parts.append(f'<rect x="{x}" y="{y - 9}" width="14" height="10" fill="{c}"/>')
            else:
                dash = DASHES[i % len(DASHES)]
                self.parts.append(f'<line x1="{x}" y1="{y - 4}" x2="{x + 18}" y2="{y - 4}" stroke="{c}" stroke-width="2" stroke-dasharray="{dash}"/>')
            self.parts.append(f'<text x="{x + 24}" y="{y}">{html.escape(n)}</text>')
            x += width

    def svg(self):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}" height="{self.h}" '
                f'viewBox="0 0 {self.w} {self.h}" font-family="DejaVu Sans, sans-serif" font-size="12">'
                f'<rect width="100%" height="100%" fill="#fff"/>'
                f'<text x="{self.w / 2}" y="22" text-anchor="middle" font-size="14" font-weight="bold">{html.escape(self.title)}</text>'
                f'<text x="{PAD_L + (self.w - PAD_L - PAD_R) / 2}" y="{self.h - 12}" text-anchor="middle">{html.escape(self.xlabel)}</text>'
                f'<text transform="translate(16 {PAD_T + (self.h - PAD_T - PAD_B) / 2}) rotate(-90)" text-anchor="middle">{html.escape(self.ylabel)}</text>')
        return head + "".join(self.parts) + "</svg>\n"

    def save(self, path):
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.svg())


def lines(path, title, xlabel, ylabel, series, xticks=None, ylo=None, yhi=None, marks=(), xfmt=num, yfmt=num):
    """series: [(nume, xs, ys)]. marks: [("v"|"h", valoare, eticheta)]."""
    xs = [x for _, a, _ in series for x in a]
    ys = [y for _, _, b in series for y in b]
    lo = min(ys) if ylo is None else ylo
    hi = max(ys) if yhi is None else yhi
    yt = nice_ticks(lo, hi)
    p = Plot(title, xlabel, ylabel)
    p.frame(min(xs), max(xs), yt[0], yt[-1], xticks or nice_ticks(min(xs), max(xs)), yt, xfmt, yfmt)
    for i, (_, a, b) in enumerate(series):
        p.line(a, b, i)
    for kind, v, lab in marks:
        (p.vline if kind == "v" else p.hline)(v, lab)
    p.legend([n for n, _, _ in series])
    p.save(path)


def bars(path, title, ylabel, groups, names, values, yhi=None, fmt=None):
    """groups: etichetele grupurilor; names: seriile; values[serie][grup]."""
    hi = yhi if yhi is not None else 1.08 * max(max(v) for v in values)
    yt = nice_ticks(min(0, min(min(v) for v in values)), hi)
    p = Plot(title, "", ylabel, w=max(W, 90 * len(groups) + PAD_L + PAD_R))
    p.frame(0, len(groups), yt[0], yt[-1], None, yt)
    gw = p.pw / len(groups)
    bw = gw * 0.8 / len(names)
    for g, label in enumerate(groups):
        x = PAD_L + g * gw + gw * 0.1
        for s in range(len(names)):
            v = values[s][g]
            y0, y1 = p.Y(max(v, 0)), p.Y(min(v, 0))
            p.parts.append(f'<rect x="{x + s * bw:.1f}" y="{y0:.1f}" width="{bw - 1:.1f}" height="{max(y1 - y0, 0.5):.1f}" '
                           f'fill="{COLORS[s % len(COLORS)]}"><title>{html.escape(names[s])}: {num(v)}</title></rect>')
            if fmt:
                p.parts.append(f'<text x="{x + s * bw + bw / 2:.1f}" y="{y0 - 3:.1f}" text-anchor="middle" font-size="9">{fmt(v)}</text>')
        p.parts.append(f'<text x="{PAD_L + g * gw + gw / 2:.1f}" y="{PAD_T + p.ph + 18}" text-anchor="middle">{html.escape(label)}</text>')
    p.legend(names, "bar")
    p.save(path)
