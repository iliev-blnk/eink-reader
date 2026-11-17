/* A* pe o grilă cu două straturi (F.Cu, B.Cu), pentru route.py.
 *
 * Celulele sunt indexate l * ny * nx + y * nx + x. free[] spune unde poate sta
 * linia de mijloc a pistei rețelei curente (cuprul celorlalte rețele, umflat cu
 * jumătatea lățimii și cu distanța minimă, e deja scos în Python), via_ok[] unde
 * poate sta un via (un singur strat, valabil pentru amândouă). Pornește din toate
 * celulele din start[] (arborele rețelei construit până acum) și se oprește la
 * prima celulă din target[]. Mutări: 8 vecini pe același strat (diagonala numai
 * dacă amândouă celulele ortogonale sunt libere, ca pista să nu taie un colț) și
 * viaul pe celălalt strat. Un via nou nu poate sta mai aproape de viaurile
 * de mai devreme ale aceluiași drum decât via_gap2 (distanța dintre centre, la
 * pătrat, în celule): găurile lor ar fi prea apropiate pentru burghiu. Viaurile
 * rețelelor deja trasate sunt scoase din via_ok[] în Python.
 *
 * Euristica: distanța octilă până la cel mai apropiat dreptunghi-țintă, deci
 * admisibilă; costurile sunt 10 pe ortogonală, 14 pe diagonală, plus cost[] al
 * celulei în care se intră (≥ 0: congestia și, la căutarea „moale”, cuprul altor
 * rețele care se poate smulge).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int32_t f; int32_t i; } item_t;
typedef struct { item_t *a; int n, cap; } heap_t;

static int push(heap_t *h, int32_t f, int32_t i)
{
    if (h->n == h->cap) {
        int cap = h->cap ? h->cap * 2 : 4096;
        item_t *a = realloc(h->a, (size_t)cap * sizeof *a);
        if (!a) return 0;
        h->a = a, h->cap = cap;
    }
    int k = h->n++;
    while (k > 0) {
        int p = (k - 1) / 2;
        if (h->a[p].f <= f) break;
        h->a[k] = h->a[p];
        k = p;
    }
    h->a[k] = (item_t){f, i};
    return 1;
}

static item_t pop(heap_t *h)
{
    item_t top = h->a[0], last = h->a[--h->n];
    int k = 0;
    for (;;) {
        int c = 2 * k + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && h->a[c + 1].f < h->a[c].f) c++;
        if (h->a[c].f >= last.f) break;
        h->a[k] = h->a[c];
        k = c;
    }
    if (h->n) h->a[k] = last;
    return top;
}

static int32_t octile(int dx, int dy)
{
    dx = abs(dx), dy = abs(dy);
    return dx > dy ? 10 * (dx - dy) + 14 * dy : 10 * (dy - dx) + 14 * dx;
}

/* Drumul care ajunge în celula i are, în urmă, un via la mai puțin de
 * sqrt(gap2) celule? Se merge înapoi cel mult 128 de pași. */
static int via_near(const int32_t *from, int i, int plane, int nx, int gap2)
{
    int r0 = i % plane, x0 = r0 % nx, y0 = r0 / nx;
    for (int k = 0, a = i; k < 128 && from[a] != -1; k++) {
        int b = from[a];
        if (b / plane != a / plane) {
            int r = a % plane, dx = r % nx - x0, dy = r / nx - y0;
            if (dx * dx + dy * dy < gap2) return 1;
        }
        a = b;
    }
    return 0;
}

/* boxes: nbox × (x0, y0, x1, y1), în celule. layer_cost[l]: cost în plus pe pas,
 * în aceleași unități (10 = încă o dată lungimea pasului). Întoarce lungimea drumului scrisă în path[] (indici de
 * celule, de la țintă la start), 0 dacă nu există drum, -1 dacă path[] e prea mic. */
int astar(int nx, int ny, const uint8_t *free_, const uint8_t *via_ok, const uint8_t *start,
          const uint8_t *target, const int32_t *boxes, int nbox, int via_cost, int via_gap2, const int32_t *layer_cost,
          const int32_t *cost, int32_t max_cost, int32_t *path, int path_cap)
{
    int plane = nx * ny, n = 2 * plane;
    int32_t *g = malloc((size_t)n * sizeof *g);
    int32_t *from = malloc((size_t)n * sizeof *from);
    heap_t h = {0};
    int result = 0, goal = -1;
    if (!g || !from) goto out;
    for (int i = 0; i < n; i++) g[i] = INT32_MAX;

#define H(x, y) ({ int32_t best_ = INT32_MAX; \
    for (int b_ = 0; b_ < nbox; b_++) { \
        const int32_t *q_ = boxes + 4 * b_; \
        int dx_ = (x) < q_[0] ? q_[0] - (x) : (x) > q_[2] ? (x) - q_[2] : 0; \
        int dy_ = (y) < q_[1] ? q_[1] - (y) : (y) > q_[3] ? (y) - q_[3] : 0; \
        int32_t o_ = octile(dx_, dy_); if (o_ < best_) best_ = o_; } best_; })

    for (int i = 0; i < n; i++) {
        if (start[i] && free_[i]) {
            g[i] = 0, from[i] = -1;
            int r = i % plane;
            if (!push(&h, H(r % nx, r / nx), i)) goto out;
        }
    }
    static const int dxs[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dys[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    while (h.n) {
        item_t it = pop(&h);
        int i = it.i, l = i / plane, r = i % plane, x = r % nx, y = r / nx;
        int32_t gi = g[i];
        if (it.f - H(x, y) > gi) continue;      // intrare veche din heap
        if (target[i]) { goal = i; break; }
        if (gi > max_cost) continue;
        for (int d = 0; d < 8; d++) {
            int x2 = x + dxs[d], y2 = y + dys[d];
            if (x2 < 0 || y2 < 0 || x2 >= nx || y2 >= ny) continue;
            int j = l * plane + y2 * nx + x2;
            if (!free_[j]) continue;
            if (d >= 4 && (!free_[l * plane + y * nx + x2] || !free_[l * plane + y2 * nx + x])) continue;
            int32_t c = gi + (d < 4 ? 10 + layer_cost[l] : 14 + layer_cost[l] * 14 / 10) + cost[j];
            if (c < g[j]) {
                g[j] = c, from[j] = i;
                if (!push(&h, c + H(x2, y2), j)) goto out;
            }
        }
        if (via_ok[r] && !via_near(from, i, plane, nx, via_gap2)) {
            int j = (1 - l) * plane + r;
            int32_t c = gi + via_cost + cost[j];
            if (free_[j] && c < g[j]) {
                g[j] = c, from[j] = i;
                if (!push(&h, c + H(x, y), j)) goto out;
            }
        }
    }
    if (goal >= 0) {
        int k = 0;
        for (int i = goal; i != -1; i = from[i]) {
            if (k == path_cap) { result = -1; goto out; }
            path[k++] = i;
        }
        result = k;
    }
out:
    free(g), free(from), free(h.a);
    return result;
#undef H
}
