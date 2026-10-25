"""Tiny s-expression reader/writer for KiCad files."""
import re

TOK = re.compile(r'\s*(\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+)')

def parse(text):
    stack, cur = [], []
    for m in TOK.finditer(text):
        t = m.group(1)
        if t == '(':
            stack.append(cur); cur = []
        elif t == ')':
            done = cur; cur = stack.pop(); cur.append(done)
        else:
            cur.append(t)
    return cur[0]

def dump(node, ind=0):
    if not isinstance(node, list):
        return node
    simple = all(not isinstance(x, list) for x in node)
    if simple:
        return '(' + ' '.join(node) + ')'
    pad = '\t' * (ind + 1)
    head = []
    for x in node:
        if isinstance(x, list): break
        head.append(x)
    s = '(' + ' '.join(head)
    for x in node[len(head):]:
        s += '\n' + pad + (dump(x, ind + 1) if isinstance(x, list) else x)
    return s + '\n' + '\t' * ind + ')'

def find_lib_symbol(path, name):
    lib = parse(open(path).read())
    for x in lib:
        if isinstance(x, list) and x[0] == 'symbol' and x[1] == f'"{name}"':
            return x
    raise KeyError(name)

def pins(sym):
    """Yield (number, name, x, y) for every pin in a (lib) symbol tree."""
    for x in sym:
        if isinstance(x, list):
            if x[0] == 'pin':
                at = next(a for a in x if isinstance(a, list) and a[0] == 'at')
                num = next(a for a in x if isinstance(a, list) and a[0] == 'number')[1].strip('"')
                nm = next(a for a in x if isinstance(a, list) and a[0] == 'name')[1].strip('"')
                yield num, nm, float(at[1]), float(at[2])
            else:
                yield from pins(x)
