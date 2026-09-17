#!/usr/bin/env python3
"""Archive the SDK objects that native-sdk-probe/compile.py compiled successfully (libnative-sdk-probe.a)."""
import json, os, subprocess, sys
from pathlib import Path
b = Path(sys.argv[1]).resolve(); top = b.parent
roots = {'libntr': top/'native-graphics/libntr', 'libntrsystem': top/'native-probe/libntrsystem'}
objs = []
for x in json.loads((b/'results.json').read_text()):
    if not x['ok']: continue
    src = Path(x['source']); repo = 'libntr' if str(src).startswith(str(roots['libntr'])) else 'libntrsystem'
    objs.append(str(b/'objects'/(repo+'__'+str(src.relative_to(roots[repo])).replace('/', '__')+'.o')))
(b/'libnative-sdk-probe.a').unlink(missing_ok=True)
subprocess.run([os.environ.get('TOOLBIN', '') + 'ar', 'rcs', str(b/'libnative-sdk-probe.a'), *objs], check=True)
print('libnative-sdk-probe.a:', len(objs), 'objects')
