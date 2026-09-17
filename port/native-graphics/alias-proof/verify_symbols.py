from pathlib import Path
import json,subprocess
p=Path(__file__).resolve().parent
out=subprocess.check_output(['@TOOLBIN@nm','-n',str(p/'native_soft2d_probe.elf')],text=True)
symbols={l.split()[2]:int(l.split()[0],16) for l in out.splitlines() if len(l.split())==3 and l.split()[1] in 'BD'}
base=symbols['PSPNative_GfxRegisters']
rows=json.loads((p/'registers.json').read_text())
for name,offset,width,family in rows:assert symbols[name]==base+offset,(name,offset,symbols[name]-base)
print(f'PASS: all {len(rows)} linked symbols match hardware byte offsets; canonical base=0x{base:x}')
