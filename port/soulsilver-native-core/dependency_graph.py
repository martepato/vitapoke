from pathlib import Path
import subprocess,re,json,collections,concurrent.futures
b=Path(__file__).resolve().parent;root=b.parent/'soulsilver-research/pokeheartgold-slop';objdump='@TOOLBIN@objdump'
def read(p):
 t=subprocess.check_output([objdump,'-t','-r',str(p)],text=True);defs={};refs=collections.defaultdict(set);sec=None
 for l in t.splitlines():
  m=re.match(r'^RELOCATION RECORDS FOR \[(.+)\]:',l)
  if m:sec=m[1];continue
  if sec:
   m=re.match(r'^[0-9a-f]+\s+R_\w+\s+(.+)',l)
   if m:refs[sec].add(re.split(r'[+-]0x',m[1])[0]);continue
  m=re.match(r'^[0-9a-f]+\s+(.{7})\s+(\S+)\s+[0-9a-f]+\s+(.+)$',l)
  if m and m[2]!='*UND*':defs[m[3]]=(m[2],m[1].startswith('g'))
 return p.name,defs,refs
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:objs=list(pool.map(read,(b/'objects').glob('*.o')))
byobj={n:(d,r) for n,d,r in objs};globaldefs=collections.defaultdict(list)
for n,d,r in objs:
 for sym,(sec,g) in d.items():
  if g:globaldefs[sym].append((n,sec))
sdk=set();nm='@TOOLBIN@nm'
for lib in ['native-next-app/libsdk-filtered.a','native-probe/libnetwork-probe.a']:
 for l in subprocess.check_output([nm,'-g','--defined-only',str(b.parent/lib)],text=True).splitlines():
  m=re.match(r'^[0-9a-f]+\s+[A-Z]\s+(\S+)',l)
  if m:sdk.add(m[1])
assembly={}
for p in (root/'asm').rglob('*.s'):
 for f in re.findall(r'(?:thumb|arm)_func_start\s+(\w+)',p.read_text()):assembly[f]=str(p.relative_to(root))
for entry in ['NitroMain','InitSystemForTheGame']:
 queue=list(globaldefs[entry]);seen=set();missing=collections.defaultdict(set);externsdk=set()
 while queue:
  node=queue.pop()
  if node in seen:continue
  seen.add(node);n,s=node;d,r=byobj[n]
  for sym in r.get(s,[]):
   if sym in d:queue.append((n,d[sym][0]))
   elif sym in globaldefs:queue+=globaldefs[sym]
   elif sym in sdk:externsdk.add(sym)
   else:missing[sym].add(n+':'+s)
 out={'entry':entry,'reachable_sections':len(seen),'game_objects':len(set(n for n,s in seen)),'sdk_symbols':sorted(externsdk),'unresolved':[{'symbol':k,'assembly':assembly.get(k),'referenced_by':sorted(v)} for k,v in sorted(missing.items())]}
 (b/f'graph-{entry}.json').write_text(json.dumps(out,indent=2));print(entry,len(seen),'sections',len(missing),'unresolved',sum(k in assembly for k in missing),'asm')
