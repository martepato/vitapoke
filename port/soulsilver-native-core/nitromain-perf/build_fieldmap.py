from pathlib import Path
import json,re,subprocess
b=Path(__file__).resolve().parent;o=b/'fieldmap_native.o'
cmd=json.loads((b.parent/'compile-command.json').read_text())
subprocess.run(cmd+['-c',str(b/'fieldmap_native.c'),'-o',str(o)],check=True)
sec=subprocess.check_output(['@TOOLBIN@objdump','-h',str(o)],text=True);args=[]
for s in re.findall(r'^\s*\d+\s+(\S+)\s+[0-9a-f]+',sec,re.M):
 kind='bss' if s.startswith(('.bss','.sbss')) else 'data' if s.startswith(('.data','.sdata')) else None
 if kind:args+=['--rename-section',s+'=.ssov.1.'+kind+s]
subprocess.run(['@TOOLBIN@objcopy',*args,str(o)],check=True)
