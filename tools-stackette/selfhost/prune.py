# Drop declarations that nothing refers to from the header parts of a
# preprocessed C file: prune.py in.i out.i
# Items from the main file (the first linemarker's file) are always kept.
import re
import sys

TOKEN = re.compile(r'[A-Za-z_][A-Za-z_0-9]*|[0-9][0-9A-Za-z_.]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\S')
MARK = re.compile(r'^#\s*(\d+)\s+"([^"]*)"')
IDENT = re.compile(r'[A-Za-z_]')
KEYWORDS = {'struct', 'union', 'enum', 'typedef'}


def declared_names(toks):
    names = set()
    brace = 0
    paren = 0
    for i, t in enumerate(toks):
        if t == '{':
            brace += 1
            if paren == 0 and brace == 1 and i > 0 and IDENT.match(toks[i - 1]) and i > 1 and toks[i - 2] in ('struct', 'union', 'enum'):
                names.add(toks[i - 1])
            continue
        if t == '}':
            brace -= 1
            continue
        if t == '(':
            paren += 1
            continue
        if t == ')':
            paren -= 1
            continue
        if not IDENT.match(t):
            continue
        prev = toks[i - 1] if i > 0 else ''
        nxt = toks[i + 1] if i + 1 < len(toks) else ''
        if prev in ('struct', 'union', 'enum'):
            names.add(t)
            continue
        if brace == 1 and toks[0:1] != ['typedef'] and _in_enum(toks, i):
            if nxt in (',', '}', '='):
                names.add(t)
            continue
        if brace > 0:
            continue
        if paren == 0 and nxt in (';', ',', '[', '(', '=', ')') and t not in ('sizeof', 'int', 'char', 'short', 'long', 'unsigned', 'signed', 'float', 'double', 'void', 'const', 'volatile', 'static', 'extern', 'register', 'auto'):
            if nxt == ')' and paren == 0:
                continue
            names.add(t)
        elif paren == 1 and prev == '*' and i > 1 and toks[i - 2] == '(' and nxt == ')':
            names.add(t)
    return names


def _in_enum(toks, i):
    depth = 0
    for j in range(i, -1, -1):
        if toks[j] == '}':
            depth += 1
        elif toks[j] == '{':
            if depth == 0:
                return j >= 2 and toks[j - 2] == 'enum' or (j >= 1 and toks[j - 1] == 'enum')
            depth -= 1
    return False


def split(lines):
    # segments: ('main', [(lineno, text)]) and ('hdr', file, lineno, text)
    segs = []
    main = None
    cur = None
    lineno = 1
    item = None
    for raw in lines:
        raw = raw.rstrip('\n')
        m = MARK.match(raw)
        if m:
            lineno = int(m.group(1))
            cur = m.group(2)
            if main is None:
                main = cur
            continue
        if cur == main and item is None:
            if raw.strip():
                if segs and segs[-1][0] == 'main':
                    segs[-1][1].append((lineno, raw))
                else:
                    segs.append(('main', [(lineno, raw)]))
            lineno += 1
            continue
        if cur == main and item is not None:
            # main-file text inside a header item; give up pruning that item
            item['text'].append(raw)
            item['broken'] = True
            lineno += 1
            continue
        if item is None:
            item = {'file': cur, 'line': lineno, 'text': [], 'toks': [], 'depth': 0, 'body': False, 'broken': False}
        item['text'].append(raw)
        for t in TOKEN.findall(raw):
            if t.startswith('#'):
                continue
            item['toks'].append(t)
            if t == '{':
                if item['depth'] == 0 and len(item['toks']) > 1 and item['toks'][-2] == ')':
                    item['body'] = True
                item['depth'] += 1
            elif t == '}':
                item['depth'] -= 1
                if item['depth'] == 0 and item['body']:
                    segs.append(('hdr', item))
                    item = None
                    break
            elif t == ';' and item['depth'] == 0:
                segs.append(('hdr', item))
                item = None
                break
        lineno += 1
    if item is not None:
        segs.append(('hdr', item))
    return segs


def prune(lines):
    segs = split(lines)
    used = set()
    for s in segs:
        if s[0] == 'main':
            for _, text in s[1]:
                used.update(TOKEN.findall(text))
    items = [s[1] for s in segs if s[0] == 'hdr']
    for it in items:
        it['decl'] = declared_names(it['toks'])
        it['keep'] = it['broken'] or not it['decl']
    changed = True
    while changed:
        changed = False
        for it in items:
            if not it['keep'] and it['decl'] & used:
                it['keep'] = True
            else:
                continue
            changed = True
        for it in items:
            if it['keep'] and not it.get('counted'):
                it['counted'] = True
                used.update(t for t in it['toks'] if IDENT.match(t) and t not in it['decl'])
                changed = True
    out = []
    lastmain = None
    for s in segs:
        if s[0] == 'main':
            for ln, text in s[1]:
                if lastmain != ln - 1:
                    out.append('# %d "main"' % ln)
                out.append(text)
                lastmain = ln
        elif s[1]['keep']:
            it = s[1]
            out.append('# %d "%s"' % (it['line'], it['file']))
            out.extend(it['text'])
            lastmain = None
    return out


def main():
    lines = open(sys.argv[1]).read().split('\n')
    # recover the main file name for the markers
    first = MARK.match(lines[0])
    res = prune(lines)
    name = first.group(2) if first else 'main'
    res = [r.replace('"main"', '"%s"' % name) for r in res]
    open(sys.argv[2], 'w').write('\n'.join(res) + '\n')


main()
