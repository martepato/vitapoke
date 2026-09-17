from pathlib import Path
import subprocess,concurrent.futures,re,json
base=Path(__file__).resolve().parent; top=base.parent
roots=[top/'native-graphics/libntr',top/'native-probe/libntrsystem']
inc=[top/'native-probe/generated']+[r/'include' for r in roots]
for r in roots:
 inc.extend(p for p in (r/'libraries').glob('*/include') if p.is_dir())
flags=['-ffile-prefix-map='+str(top)+'=/pspoke/build/tree/test-out/deterministic-path-token-----','-O2',*'@TARGETCC@'.split(),'-std=gnu99','-ffunction-sections','-fdata-sections','-DSDK_VERSION_MAJOR=4','-include',str(roots[0]/'include/pch/nitro_pch.h'),'-DSDK_PORT','-DSDK_X86','-DSDK_TS','-DSDK_4M','-DSDK_FINALROM','-DNNS_FINALROM','-D_NITRO','-DSDL_MAIN_HANDLED']+['-I'+str(p) for p in inc]
entries=[]
for root in roots:
 for manifest in sorted((root/'libraries').glob('*/src/meson.build')):
  if manifest.parts[-3] in ('sim','sim7','wl_arm7','wm_arm7','exi','nvram','pad','syscall','camera','dsp','el','scfg','wxc'):continue
  # Select SDK4 and primary CPU source blocks; ARM7 lists are separate libraries.
  active=[True]; lines=[]; arm7=False
  for line in manifest.read_text().splitlines():
   q=line.strip()
   if q.startswith('if'):
    cond='sdk_version_major == 5' not in q and "build_target == 'arm'" not in q
    active.append(active[-1] and cond);continue
   if q=='else':active[-1]=active[-2] and not active[-1];continue
   if q=='endif':active.pop();continue
   if 'files(' in q:arm7='arm7' in q
   if active[-1] and not arm7:lines.append(line)
  for name in dict.fromkeys(re.findall(r"'([^']+\.c)'",'\n'.join(lines))):
   source=manifest.parent/name
   if source.is_file():entries.append((root.name,source))
objdir=base/'objects';objdir.mkdir(exist_ok=True)
def compile(entry):
 repo,src=entry; name=repo+'__'+str(src.relative_to(next(r for r in roots if r.name==repo))).replace('/','__')
 obj=objdir/(name+'.o')
 if obj.exists():obj.unlink()
 result=subprocess.run(['@TOOLBIN@gcc']+flags+['-c',str(src),'-o',str(obj)],capture_output=True,text=True)
 (objdir/(name+'.log')).write_text(result.stderr)
 return dict(source=str(src),ok=result.returncode==0,errors=[l for l in result.stderr.splitlines() if 'error:' in l])
with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(compile,entries))
(base/'results.json').write_text(json.dumps(results,indent=2))
print('Compiled',sum(x['ok'] for x in results),'/',len(results))
for x in results:
 if not x['ok']:print(Path(x['source']).name,':','; '.join(x['errors'][:2]))
