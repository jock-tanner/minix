#!/usr/bin/env python3
'''
Prepares the assembly files of a program with overlays (doc/overlays.md).

The code space of a process has 64 KiB: a root of 48 KiB that is always there
and a window of 16 KiB where one overlay at a time is mapped. The functions of
an overlay are in an own section (.ovl0, .ovl1, ...) that the link puts at the
address of the window, one after the other in the image. A call to a function of
another overlay goes through a stub in the root, `trap $5` and the data the
kernel needs; the stub has the name of the function, so everything that refers
to the function by name (the root, the other overlays, a pointer in the data)
gets it. The calls inside an overlay go straight to the function, which has the
name NAME.oK in the file of its own overlay.

    ovlprep.py -o outdir [-t thunks.s] group0 file.s ... + group1 file.s ... [+ ...]

writes outdir/<file>.s, the changed copy of every file, and thunks.s, the stubs
for the functions of all overlays (assembled with the root).

A static function whose address is taken (a callback) is made a global one, named
after its file, and gets a stub like the others. The code of the files that are not in a group stays where it is, in .text. A
file is assembled from the compiler's output (ack -mstackette -c.s).
'''
import argparse
import os
import re
import sys

HEADER = '.sect .text; .sect .rom; .sect .data; .sect .bss'
LABEL = re.compile(r'^([A-Za-z_.][A-Za-z_.0-9]*):')
EXTERN = re.compile(r'^\.extern\s+([A-Za-z_.][A-Za-z_.0-9]*)\s*$')
SECT = re.compile(r'^\.sect\s+(\.\w+)')
CALL = re.compile(r'^(\s*)(calla)(\s+)([A-Za-z_.][A-Za-z_.0-9]*)(\s*)$')
IDENT = re.compile(r'[A-Za-z_.][A-Za-z_.0-9]*')


def functions(lines):
    '''The global functions of a file: the names that are .extern and have a
    label in the text, and the local ones (for the check of their use).'''
    ext = set()
    funcs = set()
    local = set()
    section = None
    for line in lines:
        m = EXTERN.match(line)
        if m:
            ext.add(m.group(1))
            continue
        m = SECT.match(line)
        if m:
            section = m.group(1)
            continue
        m = LABEL.match(line)
        if m and section == '.text' and not m.group(1).startswith('.') \
                and m.group(1).startswith('_'):
            funcs.add(m.group(1))
    for f in funcs:
        if f not in ext:
            local.add(f)
    return funcs - local, local


def promote(lines, local, base):
    '''The static functions whose address is taken (a callback) become global
    functions, named after the file, so that they get a stub.'''
    esc = set()
    for line in lines:
        if line.startswith(('.sect', '.extern')) or LABEL.match(line):
            continue
        if CALL.match(line):
            continue
        for tok in IDENT.findall(line):
            if tok in local:
                esc.add(tok)
    if not esc:
        return lines, set(), local
    names = {t: '%s.%s' % (t, base) for t in esc}
    pat = re.compile(r'(?<![A-Za-z_.0-9])(%s)(?![A-Za-z_.0-9])'
                     % '|'.join(re.escape(t) for t in esc))
    out = [pat.sub(lambda m: names[m.group(1)], x) for x in lines]
    for i, line in enumerate(out):
        if line.startswith(HEADER):
            out[i + 1:i + 1] = ['.extern %s' % n for n in names.values()]
            break
    return out, set(names.values()), local - esc


def rewrite(lines, k, mine, local, name):
    '''The file of the group k: the code to the section of the overlay, the
    names of its own functions apart from the stubs.'''
    out = []
    declared = False
    for line in lines:
        if line.startswith(HEADER) and not declared:
            # the section 4 is .end of the library, the overlays come after it
            sects = '; .sect .end' + ''.join('; .sect .ovl%d' % i for i in range(k + 1))
            line = HEADER + sects + line[len(HEADER):]
            declared = True
        m = SECT.match(line)
        if m and m.group(1) == '.text' and not line.startswith(HEADER):
            line = '.sect .ovl%d' % k
        m = EXTERN.match(line)
        if m and m.group(1) in mine:
            line = '.extern %s.o%d' % (m.group(1), k)
        m = LABEL.match(line)
        if m and m.group(1) in mine:
            line = '%s.o%d:%s' % (m.group(1), k, line[m.end():])
        m = CALL.match(line)
        if m and m.group(4) in mine:
            line = '%s%s%s%s.o%d%s' % (m.group(1), m.group(2), m.group(3),
                                      m.group(4), k, m.group(5))
        out.append(line)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('-o', '--outdir', required=True)
    ap.add_argument('-t', '--thunks', default=None)
    ap.add_argument('args', nargs='+')
    a = ap.parse_args()
    groups = []
    cur = None
    for x in a.args:
        if x == '+':
            cur = None
        elif cur is None:
            cur = (x, [])
            groups.append(cur)
        else:
            cur[1].append(x)
    if len(groups) < 2:
        sys.exit('at least two overlays: with one the code fits in the 64 KiB')
    os.makedirs(a.outdir, exist_ok=True)
    files = []
    owner = {}
    for k, (gname, paths) in enumerate(groups):
        for p in paths:
            lines = open(p).read().split('\n')
            glob, loc = functions(lines)
            base = os.path.splitext(os.path.basename(p))[0]
            lines, more, loc = promote(lines, loc, base)
            glob = glob | more
            for f in glob:
                if f in owner:
                    sys.exit('%s: %s is also in %s' % (p, f, owner[f][0]))
                owner[f] = (gname, k)
            files.append((k, p, lines, glob, loc))
    for k, p, lines, glob, loc in files:
        out = rewrite(lines, k, glob, loc, p)
        dst = os.path.join(a.outdir, os.path.basename(p))
        open(dst, 'w').write('\n'.join(out))
    thunks = [HEADER, '.sect .text', '.align 2', '__ovlret:', '\ttrap $6']
    for f in sorted(owner):
        k = owner[f][1]
        thunks += [
            '.extern %s' % f,
            '.align 2',
            '%s:' % f,
            '\ttrap $5',
            '.data2 %d' % k,
            '.data2 %s.o%d' % (f, k),
            '.data2 __ovlret',
        ]
    thunks.append('')
    t = a.thunks or os.path.join(a.outdir, 'ovl_thunks.s')
    open(t, 'w').write('\n'.join(thunks))
    print('%d functions in %d overlays' % (len(owner), len(groups)),
          file=sys.stderr)


if __name__ == '__main__':
    main()
