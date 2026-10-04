#!/usr/bin/env python3
"""Port this repo's mods from Digitakt mk1 OS 1.53 to another OS release (1.54).

    python3 tools/port_os.py map  --old OS1.53.syx --new OS1.54.syx --elekloader DIR [--mods m ...]
    python3 tools/port_os.py apply --new OS1.54.syx --elekloader DIR [--mods m ...]

`map` finds every firmware address the mods name (their mod.json sites, and the literal addresses in their
C and assembly sources) and works out where it is in the new release, writing tools/os154.json. Each
address is found one of four ways, recorded with it:

  - window: the code or data around it, with its absolute addresses masked out, is found once near the
    same place in the new image (most of 1.54 only moved, by a few hundred bytes);
  - refs:   for RAM and for tables made of pointers: the instructions that use it are found (by window)
    in the new image, and the address they use there is read; every reference must agree;
  - near:   an address inside an instruction or a table (a site's continuation, a field): the nearest
    address before it that maps, with the same move, when the bytes between match too;
  - same:   the SRAM (0x80000000-0x80100000), which 1.54 did not move.

`apply` rewrites the sources to name each address that moved as F_<address> (C) or .LF_<address>
(assembly: a local symbol, so mods never share one), its 1.53 value, defined at the top of each file for
both releases (OS154, from mod.json's ports), and writes each mod.json's "ports": {"1.54":
...} with every site at its new address and its stock bytes read from the new image. It refuses an address
that `map` did not find, so nothing is guessed. The mods' 1.53 builds are the same as before, byte for byte.
"""
import argparse
import collections
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAPFILE = os.path.join(ROOT, 'tools', 'os154.json')
B = 0x40000400
MODS = ['digimono', 'digichain', 'digipoly', 'digiutils', 'digimatrix', 'digieq']
LIT = re.compile(r'0x(4[0-3][0-9a-fA-F]{6})([uUlL]*)\b')     # C suffixes (0x401b3748u) too
NAMED = re.compile(r'F_([0-9a-f]{8})\b')


def sources(m):
    d = json.load(open(os.path.join(ROOT, 'mods', m, 'mod.json')))
    out = []
    for s in d['sources']:
        p = os.path.join(ROOT, 'mods', m, s)
        out.append(p if os.path.exists(p) else os.path.join(ROOT, 'src', s))
    return d, out


def addresses(m):
    d, files = sources(m)
    found = set()
    for f in files:
        text = open(f).read()
        text = strip_block(strip_block(text, BEGIN_C, END_C), BEGIN_S, END_S)   # the moved ones: by name
        found.update(int(x, 16) for x in NAMED.findall(text))
        for line in text.split('\n'):
            code = line.split('|')[0] if f.endswith('.s') else line
            found.update(int(x, 16) for x, _ in LIT.findall(code))
    for s in d['sites']:
        found.add(int(s['addr'], 16))
    return found


class Mapper:
    def __init__(self, old, new):
        self.A, self.N = old, new
        self.endA = B + len(old)

    def _masked(self, img, off, n):
        w = bytearray(img[off:off + n])
        for i in range(0, len(w) - 3, 2):
            v = int.from_bytes(w[i:i + 4], 'big')
            if 0x40000000 <= v < 0x44000000 or 0x80000000 <= v < 0x80100000:
                w[i:i + 4] = b'\0\0\0\0'
        return bytes(w)

    def window(self, a, span=0x3000):
        o = a - B
        for n, pre in ((32, 0), (32, 16), (48, 0), (48, 24), (64, 0), (24, 0), (16, 0)):
            s = o - pre
            if s < 0 or s + n > len(self.A):
                continue
            pat = self._masked(self.A, s, n)
            if len(set(pat)) < 6:                   # too plain to be found once
                continue
            hits = []
            lo, hi = max(0, s - span), min(len(self.N) - n, s + span)
            for t in range(lo & ~1, hi, 2):
                if self._masked(self.N, t, n) == pat:
                    hits.append(t)
                    if len(hits) > 1:
                        break
            if len(hits) == 1:
                return hits[0] + pre + B
        return None

    def refs(self, x):
        """the new address the code uses where the old code used x, when every reference agrees"""
        b, votes, i = x.to_bytes(4, 'big'), collections.Counter(), self.A.find(x.to_bytes(4, 'big'))
        while i >= 0:
            if i % 2 == 0 and i < len(self.A):
                m = self.window(B + i)
                if m is not None:
                    votes[int.from_bytes(self.N[m - B:m - B + 4], 'big')] += 1
            i = self.A.find(b, i + 1)
        if len(votes) == 1:
            y, c = votes.most_common(1)[0]
            return y, c
        return None

    def near(self, a, mapped):
        """a field or a continuation: the nearest address before it that maps, moved the same, if the bytes
        between are the same in both"""
        for back in range(2, 0x400, 2):
            p = a - back
            if p in mapped and mapped[p] is not None:
                q = mapped[p]
            elif B <= p < self.endA and back <= 0x40:
                q = self.window(p)
                if q is None:
                    r = self.refs(p) if back <= 0x100 else None   # a table's start, by the code using it
                    if not r:
                        continue
                    q = r[0]
            elif back <= 0x100:
                r = self.refs(p)
                if not r:
                    continue
                q = r[0]
            else:
                continue
            if B <= a < self.endA:
                old = self._masked(self.A, p - B, back + 4)
                new = self._masked(self.N, q - B, back + 4)
                if old != new:
                    return None
            return q + back, p
        return None

    def map(self, addrs):
        out, how = {}, {}
        for a in sorted(addrs):
            if 0x80000000 <= a < 0x80100000:
                out[a], how[a] = a, 'same'
                continue
            if B <= a < self.endA:
                m = self.window(a)
                if m is not None:
                    out[a], how[a] = m, 'window'
                    continue
            r = self.refs(a)
            if r:
                out[a], how[a] = r[0], 'refs (%d)' % r[1]
                continue
            out[a], how[a] = None, 'not found'
        for a in sorted(addrs):                      # the rest: from the nearest one before them
            if out[a] is None:
                r = self.near(a, out)
                if r:
                    out[a], how[a] = r[0], 'near 0x%08x' % r[1]
        return out, how


def load_main(elekloader, path):
    sys.path.insert(0, elekloader)
    from elekloader import formats
    s, d, r = formats.load(path)
    return bytes(formats.main_image(s, d)), r.version


def cmd_map(a):
    old, vo = load_main(a.elekloader, a.old)
    new, vn = load_main(a.elekloader, a.new)
    mp = Mapper(old, new)
    want = set()
    per = {}
    for m in a.mods:
        per[m] = addresses(m)
        want |= per[m]
    if a.extra:
        per['(tests)'] = {int(x, 16) for x in a.extra}
        want |= per['(tests)']
    out, how = mp.map(want)
    doc = json.load(open(MAPFILE)) if os.path.exists(MAPFILE) else {'from': vo, 'to': vn, 'map': {}}
    for x in sorted(want):
        k = '0x%08x' % x
        if out[x] is None and doc['map'].get(k, {}).get('new'):
            out[x] = int(doc['map'][k]['new'], 16)     # found before (or by hand): keep it
            continue
        doc['map'][k] = {'new': None if out[x] is None else '0x%08x' % out[x], 'how': how[x]}
    json.dump(doc, open(MAPFILE, 'w'), indent=1, sort_keys=True)
    bad = [x for x in want if out[x] is None]
    for m in per:
        n = len(per[m])
        miss = sorted(x for x in per[m] if out[x] is None)
        print('%-11s %3d addresses, %3d found%s' % (m, n, n - len(miss),
              (': missing ' + ' '.join('0x%08x' % x for x in miss)) if miss else ''))
    print('%s -> %s: %d addresses, %d not found; written to %s' % (vo, vn, len(want), len(bad), MAPFILE))
    return 1 if bad else 0


def fw(addr, os_version):
    """for the tests: addr (OS 1.53's) on os_version, from tools/os154.json"""
    if os_version == '1.53':
        return addr
    doc = json.load(open(MAPFILE))
    if doc['to'] != os_version:
        raise SystemExit('tools/os154.json maps 1.53 to %s, not %s' % (doc['to'], os_version))
    e = doc['map'].get('0x%08x' % addr)
    if not e or not e['new']:
        raise SystemExit('0x%08x is not mapped for %s: tools/port_os.py map --extra 0x%08x' % (addr, os_version, addr))
    return int(e['new'], 16)


BEGIN_C = '/* ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ---- */'
END_C = '/* ---- end of the moved addresses ---- */'
BEGIN_S = '| ---- firmware addresses that moved in OS 1.54 (tools/port_os.py, tools/os154.json) ----'
END_S = '| ---- end of the moved addresses ----'


def strip_block(text, begin, end):
    i = text.find(begin)
    if i < 0:
        return text
    j = text.index(end, i) + len(end)
    return text[:i] + text[j:].lstrip('\n')


def rewrite(path, moved):
    """name each moved address F_<1.53 address> in the code (not in comments) and define it at the top"""
    asm = path.endswith('.s')
    text = open(path).read()
    text = strip_block(text, BEGIN_S if asm else BEGIN_C, END_S if asm else END_C)
    used = set()
    out = []
    in_comment = False
    for line in text.split('\n'):
        if asm:
            code, sep, rest = line.partition('|')
        else:
            code, rest, sep = line, '', ''
            if in_comment or line.lstrip().startswith(('*', '/*', '//')):
                in_comment = ('/*' in line or in_comment) and '*/' not in line
                out.append(line)
                continue
            k = min([x for x in (line.find('/*'), line.find('//')) if x >= 0] or [len(line)])
            code, rest = line[:k], line[k:]
            in_comment = '/*' in rest and '*/' not in rest

        def sub(mo):
            a = int(mo.group(1), 16)
            if a in moved:
                used.add(a)
                if asm:
                    return '.LF_%08x' % a
                return ('((unsigned)F_%08x)' if 'u' in mo.group(2).lower() else 'F_%08x') % a   # keep its type
            return mo.group(0)
        used.update(int(x, 16) for x in NAMED.findall(code) if int(x, 16) in moved)   # named by an earlier run
        out.append(LIT.sub(sub, code) + sep + rest)
    body = '\n'.join(out)
    if not used:
        open(path, 'w').write(body)
        return 0
    if asm:
        blk = [BEGIN_S, '        .ifdef  OS154']
        blk += ['        .equ    .LF_%08x, 0x%08x' % (a, moved[a]) for a in sorted(used)]
        blk += ['        .else']
        blk += ['        .equ    .LF_%08x, 0x%08x' % (a, a) for a in sorted(used)]
        blk += ['        .endif', END_S, '']
    else:
        blk = [BEGIN_C, '#ifdef OS154']
        blk += ['#define F_%08x 0x%08x' % (a, moved[a]) for a in sorted(used)]
        blk += ['#else']
        blk += ['#define F_%08x 0x%08x' % (a, a) for a in sorted(used)]
        blk += ['#endif', END_C, '']
    # after the file's opening comment
    lines = body.split('\n')
    i = 0
    if asm:
        while i < len(lines) and lines[i].startswith('|'):
            i += 1
    elif lines and lines[0].startswith('/*'):
        while i < len(lines) and '*/' not in lines[i]:
            i += 1
        i += 1
    lines[i:i] = ([''] if i and lines[i - 1].strip() else []) + blk
    open(path, 'w').write('\n'.join(lines))
    return len(used)


def port_sites(d, mp, new_img):
    """the mod's sites at their new addresses, stock bytes read from the new image and checked: the same
    instruction (or data), only its absolute addresses may differ"""
    out = []
    for s in d['sites']:
        a = int(s['addr'], 16)
        n = mp[a]
        stock = bytes.fromhex(s['stock'])
        got = new_img[n - B:n - B + len(stock)]
        mk = Mapper(stock, got)
        if mk._masked(stock, 0, len(stock)) != mk._masked(got, 0, len(got)):
            raise SystemExit('site 0x%08x: the new image has %s at 0x%08x, not %s' % (a, got.hex(), n, stock.hex()))
        t = dict(s)
        t['addr'] = '0x%08x' % n
        t['stock'] = got.hex()
        if s['op'] == 'bytes' and stock != got:
            raise SystemExit('site 0x%08x: bytes changed (%s -> %s); port it by hand' % (a, stock.hex(), got.hex()))
        out.append(t)
    return out


def cmd_apply(a):
    doc = json.load(open(MAPFILE))
    full = {int(k, 16): (int(v['new'], 16) if v['new'] else None) for k, v in doc['map'].items()}
    new_img, _ = load_main(a.elekloader, a.new)
    for m in a.mods:
        want = addresses(m)
        miss = [x for x in want if full.get(x) is None]
        if miss:
            raise SystemExit('%s: not mapped: %s (run map; port these by hand)' % (m, ' '.join('0x%08x' % x for x in miss)))
        moved = {x: full[x] for x in want if full[x] != x}
        d, files = sources(m)
        n = sum(rewrite(f, moved) for f in files)
        port = {'sites': port_sites(d, full, new_img),
                'defsym': dict(d.get('defsym', {}), OS154=1),
                'cflags': list(d.get('cflags', [])) + ['-DOS154']}
        d.setdefault('device', 'digitakt-mk1')
        d.setdefault('os', doc['from'])           # without it the SDK builds the top level for any file
        d['ports'] = {doc['to']: port}
        p = os.path.join(ROOT, 'mods', m, 'mod.json')
        json.dump(d, open(p, 'w'), indent=1)
        open(p, 'a').write('\n')
        print('%-11s %d moved addresses named in %d places; %d sites ported' % (m, len(moved), n, len(port['sites'])))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('map')
    p.add_argument('--old', required=True)
    p.add_argument('--new', required=True)
    p.add_argument('--elekloader', required=True)
    p.add_argument('--mods', nargs='+', default=MODS)
    p.add_argument('--extra', nargs='*', default=[], help='more addresses to map (the tests\' own)')
    p = sub.add_parser('apply')
    p.add_argument('--new', required=True, help='the new release\'s official .syx (sites\' stock bytes)')
    p.add_argument('--elekloader', required=True)
    p.add_argument('--mods', nargs='+', default=MODS)
    a = ap.parse_args()
    sys.exit(cmd_map(a) if a.cmd == 'map' else cmd_apply(a))


if __name__ == '__main__':
    main()
