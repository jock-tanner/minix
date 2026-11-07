#!/usr/bin/env python3
"""Cross-check the result sizes of functions: the callee's `ret n` against what each
caller does after `cal` (asp, then lfr n or nothing). On the Stackette the callee
leaves its result on the stack, so a mismatch unbalances the parameter stack.
usage: retcheck.py file.k..."""
import subprocess, sys, re, collections
DEC='/tmp/ackwork/ack/.obj/staging/lib/ack/em_decode'
params={}
defs=collections.defaultdict(set)   # name -> {(ret sizes), file}
sites=collections.defaultdict(list) # name -> [(size, file)]
asites=collections.defaultdict(list)
for f in sys.argv[1:]:
    txt=subprocess.run([DEC,f],capture_output=True,text=True).stdout.splitlines()
    cur=None
    for i,l in enumerate(txt):
        w=l.split()
        if not w: continue
        if w[0]=='pro':
            cur=w[1].lstrip('$').split(',')[0]; defs[cur]|=set()
            seen=set()
        elif w[0]=='mes' and cur and w[1].split(',')[0]=='9':
            params[cur]=int(w[1].split(',')[1])
        elif w[0]=='ret' and cur:
            defs[cur].add((int(w[1]),f))
        elif w[0]=='end':
            cur=None
        elif w[0]=='cal':
            name=w[1].lstrip('$')
            j=i+1
            # skip label lines and `asp`
            size=0
            argb=0
            if j<len(txt) and txt[j].split()[:1]==['asp']:
                argb=int(txt[j].split()[1]); j+=1
            if j<len(txt) and txt[j].split()[:1]==['lfr']: size=int(txt[j].split()[1])
            sites[name].append((size,f))
            asites[name].append((argb,f))
bad=0
for name,d in sorted(defs.items()):
    if not d: continue
    rs=set(r for r,_ in d)
    ss=set(s for s,_ in sites.get(name,[]))
    if ss and (ss-rs or len(rs)>1):
        bad+=1
        print('%s: defined ret %s (%s); called expecting %s (%s)'%(name,sorted(rs),sorted(set(f for _,f in d))[:2],sorted(ss),sorted(set(f for s,f in sites[name] if s not in rs))[:3]))
print('result mismatches:',bad)
nb=0
for name,p in sorted(params.items()):
    for a,f in asites.get(name,[]):
        if p>=0 and a!=p:
            nb+=1; print('args: %s takes %d bytes, a call in %s passes %d'%(name,p,f,a))
print('argument mismatches:',nb)
