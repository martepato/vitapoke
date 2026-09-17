from pathlib import Path
import re,json,subprocess,concurrent.futures
b=Path(__file__).resolve().parent;out=b/'networkobjects';out.mkdir(exist_ok=True)
incs=[b.parent/'native-backup-probe/include',b.parent/'native-fonts/include',b.parent/'native-input/include',b.parent/'native-startup/include',b/'backing/include',b.parent/'native-graphics/alias-proof/include',b/'generated',b.parent/'native-graphics/libntr/include',b/'libntrsystem/include',b/'libntrwifi/include',b/'libntrdwc/include',b/'libntrdwc/include/gs',b/'libntrdwc/include/base',b/'libvct/include']
incs.insert(0,Path('@SDKINC@'))
flags=['@TOOLBIN@gcc','-ffile-prefix-map='+str(b.parent)+'=/pspoke/build/tree/test-out/deterministic-path-token-----','-O2',*'@TARGETCC@'.split(),'-ffunction-sections','-fdata-sections','-std=gnu99','-DSDK_PORT','-DSDK_X86','-DSDK_CODE_X86','-D@SDKBUILD@','-DSDK_VERSION_MAJOR=4','-DSDK_TS','-DSDK_4M','-DSDK_FINALROM','-DNNS_FINALROM','-D_NITRO','-DSDK_CW_FORCE_EXPORT_SUPPORT','-DLINK_PPWLOBBY']+['-I'+str(i) for i in incs]
files=[]
for repo in ['libntrdwc','libntrwifi','libvct']:
 for m in (b/repo).rglob('meson.build'):
  for item in re.findall(r"'([^']+\.c)'",m.read_text()):
   p=m.parent/item
   if p.exists():files.append(p)
files=sorted(p for p in set(files) if '/standalone/' not in str(p) and ('/libraries/stubs/' not in str(p) or '/libraries/stubs/md5/' in str(p)))
# wchar.h is force-included because two of the authentication sources call wcslen without declaring
# it, and rely on the platform's own headers having done so. The PSP build got it from pspkernel.h.
def comp(p):
 name=str(p.relative_to(b)).replace('/','__');f=flags+['-I'+str(p.parent),'-I'+str(p.parent.parent/'include'),'-I'+str(p.parent.parent),'-include','wchar.h']
 if p.name=='network.c':f+=['-include','sys/socket.h','-include','netinet/in.h','-include','arpa/inet.h']
 # GameSpy's nonport.c defines getlocalhost for every platform except the two it has a branch for
 # (_PS3 and _PSP), so the PSP build got the port's own version and this one does not. The port's
 # version is the one to keep: it is part of the explicit "networking is not available" boundary in
 # port/native-offline/offline.c, and it answers by saying so instead of going looking for an
 # interface that is not there. Renaming the definition here leaves the library's two callers --
 # both online matchmaking, unreachable in this port -- bound to that one.
 if p.name=='nonport.c':f+=['-Dgetlocalhost=GSI_unused_getlocalhost']
 dest=out/(name+'.o')
 if dest.exists():dest.unlink()
 c=subprocess.run(f+['-c',str(p),'-o',str(dest)],capture_output=True,text=True);(out/(name+'.log')).write_text(c.stderr)
 return {'file':str(p.relative_to(b)),'ok':c.returncode==0,'errors':[x for x in c.stderr.splitlines() if 'error:' in x]}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:res=list(pool.map(comp,files))
(b/'network-results.json').write_text(json.dumps(res,indent=2));print('NETWORK',sum(x['ok'] for x in res),'of',len(res))
for x in res:
 if not x['ok']:print(x['file'], '\n'.join(x['errors'][:2]))
a=b/'libnetwork-probe.a'
if a.exists():a.unlink()
subprocess.run(['@TOOLBIN@ar','rcs',str(a)]+[str(out/(str(p.relative_to(b)).replace('/','__')+'.o')) for p in files if not str(p.relative_to(b)).startswith('libvct/')],check=True)
