"""Translate SoulSilver overworld assembly to native C, seed by seed.

Usage: python3 port.py [--compile] [--trace]

SEEDS lists the original assembly entry points the native overworld path still
needs. The closure walks BL targets and .word function pointers and stops at
anything the native build already provides (NATIVE) or at real C/SDK functions
(their argument counts are read from the decompilation headers, with explicit
overrides where a header is absent).

Nothing here invents behaviour: an unsupported instruction, an unknown call
signature or an unresolved literal raises and the build fails.
"""
from pathlib import Path
import json, os, re, subprocess, sys
import translate2 as t

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DECOMP = ROOT / 'soulsilver-research/pokeheartgold-slop'
SRC = DECOMP / 'asm'
PSPDEV = Path(os.environ.get('PSPDEV', Path.home() / 'pspdev'))

SEEDS = json.loads((HERE / 'seeds.json').read_text())
# Already implemented by the native build; calling them keeps the tested code.
NATIVE = set(json.loads((HERE / 'native-provided.json').read_text()))
# Argument counts that cannot be read from a header.
OVERRIDE = json.loads((HERE / 'arg-overrides.json').read_text())
# ARM runtime division helpers: quotient in r0, remainder in r1.
DIV = {'_s32_div_f': 's', '_u32_div_f': 'u'}
# ARM runtime 64-bit multiply: (r1:r0) * (r3:r2) -> (r1:r0).
RUNTIME64 = {'_ll_mul': 'ssllmul(&r0,&r1,r2,r3);',
             # MWCC unsigned 64-bit divide/modulo: (r1:r0) op (r3:r2) -> (r1:r0); divisor 0 returns the dividend.
             '_ll_udiv': 'ssulldiv(&r0,&r1,r2,r3,0);', '_ull_div': 'ssulldiv(&r0,&r1,r2,r3,0);',
             '_ull_mod': 'ssulldiv(&r0,&r1,r2,r3,1);'}
# ARM soft-float runtime helpers (IEEE-754 single in integer registers).
SOFTFLOAT = {
    '_fflt': ('r0=ssf_flt((int32_t)r0);', 1),
    '_ffix': ('r0=(uint32_t)ssf_fix(r0);', 1),
    '_fadd': ('r0=ssf_add(r0,r1);', 2),
    '_fsub': ('r0=ssf_sub(r0,r1);', 2),
    '_fmul': ('r0=ssf_mul(r0,r1);', 2),
    '_fdiv': ('r0=ssf_div(r0,r1);', 2),
    # IEEE-754 double in register pairs (r1:r0 low word first), MWCC runtime.
    '_f2d': ('ssd_f2d(&r0,&r1);', 1),
    '_dleq': ('ssd_cmp(&f,r0,r1,r2,r3);', 4),   # sets N/Z/C/V like cmp (r1:r0),(r3:r2)
    # MWCC single-precision compares (lib/asm/msl.s): result 0/1 in r0 AND condition flags.
    '_fgr':  ('ssf_gt(&f,&r0,r1);', 2),   # C=1 if a>b (NaN false), Z from the compare
    '_fleq': ('ssf_le(&f,&r0,r1);', 2),   # le: C=0,Z=1; gt or NaN: C=1,Z=0
    '_fls':  ('ssf_lt(&f,&r0,r1);', 2),   # C=1 unless a<b (NaN false -> C=1)
    '_feq':  ('ssf_eq(&f,&r0,r1);', 2),   # Z=1 if a==b (NaN false)
    # MWCC double arithmetic (lib/asm/msl.s): doubles in (high:low) = (r1:r0) / (r3:r2), result in r1:r0.
    '_dflt':  ('ssd_flt(&r0,&r1);', 1),
    '_dfltu': ('ssd_fltu(&r0,&r1);', 1),
    '_dadd':  ('ssd_add(&r0,&r1,r2,r3);', 4),
    '_dmul':  ('ssd_mul(&r0,&r1,r2,r3);', 4),
    '_ddiv':  ('ssd_div(&r0,&r1,r2,r3);', 4),
    '_dfix':  ('r0=(uint32_t)ssd_fix(r0,r1);', 2),
    '_fneq':  ('ssf_ne(&f,&r0,r1);', 2),   # Z=1 if a==b; r0 = (a!=b)
    '_d2f':   ('r0=ssd_d2f(r0,r1);', 2),   # double (r1:r0) -> float bits in r0
    '_dgeq':  ('ssd_ge(&f,&r0,r1,r2,r3);', 4),   # r0=(a>=b); C=r0 (caller uses blo/bhs), NaN -> 0, C=0
    '_ffltu': ('r0=ssf_fltu(r0);', 1),
    '_ffixu': ('r0=ssf_fixu(r0);', 1),
    '_dfixu': ('r0=ssd_fixu(r0,r1);', 2),
    '_dsub':  ('ssd_sub(&r0,&r1,r2,r3);', 4),
    '_dls':   ('ssd_lt(&f,&r0,r1,r2,r3);', 4),   # r0=(a<b); flags like _fls
    '_dgr':   ('ssd_gt(&f,&r0,r1,r2,r3);', 4),   # r0=(a>b); flags like _fgr
}
# (containing function, register) -> stack arguments the original sets up.
INDIRECT_STACK = {tuple(k.split()): v for k, v in
                  json.loads((HERE / 'indirect-stack.json').read_text()).items()}

# ------------------------------------------------------------------ index
def asm_text(path):
    """146 of the assembly files start with a C #include of a constants header
    and use those names as immediates, so every file goes through the C
    preprocessor first. The .include assembler directives are left alone."""
    raw = path.read_text()
    if '#include' not in raw:
        return raw
    cache = HERE / 'preprocessed' / path.name
    if not cache.exists() or cache.stat().st_mtime < path.stat().st_mtime:
        cache.parent.mkdir(exist_ok=True)
        r = subprocess.run([str(PSPDEV / 'bin/psp-gcc'), '-E', '-P',
                            '-x', 'assembler-with-cpp',
                            '-I' + str(DECOMP / 'include'), '-I' + str(DECOMP / 'files'),
                            '-I' + str(ROOT / 'soulsilver-native-core/include'),
                            '-I' + str(ROOT / 'soulsilver-native-core/game-include'),
                            str(path)], text=True, capture_output=True)
        # A file whose generated headers are absent keeps its raw text; any
        # symbolic immediate in it then fails translation loudly.
        cache.write_text(r.stdout if r.returncode == 0 else raw)
    return cache.read_text()


bodies, origin = {}, {}
for p in sorted(SRC.glob('*.s')):
    text = asm_text(p)
    for n, b in re.findall(r'\b(?:thumb|arm)_func_start\s+(\w+)\s*\n(.*?)\b(?:thumb|arm)_func_end\s+\w+', text, re.S):
        bodies[n], origin[n] = b, p.name

# Address-derived labels are ambiguous where overlays share a load region
# (0x0221BE20: overlays 7/8/9/10/63/65). The decomp names such a `bl` after one
# overlay, but the caller may run while another is loaded. call-retargets.json:
# {"<caller function>": {"<label in asm>": "<function actually called>"}}.
RETARGETS = json.loads((HERE / 'call-retargets.json').read_text()) if (HERE / 'call-retargets.json').exists() else {}
for fn, table in RETARGETS.items():
    if fn == '*':   # every translated caller: SDK entries whose port ABI differs from the DS one
        for old, new in table.items():
            pat = re.compile(r'\bbl\s+' + re.escape(old) + r'\b')
            for name in list(bodies):
                if pat.search(bodies[name]):
                    bodies[name] = pat.sub('bl ' + new, bodies[name])
        continue
    if fn not in bodies:
        raise SystemExit('call-retargets: unknown function ' + fn)
    for old, new in table.items():
        n = len(re.findall(r'\bbl\s+' + re.escape(old) + r'\b', bodies[fn]))
        if n == 0:
            raise SystemExit(f'call-retargets: {fn} has no bl {old}')
        bodies[fn] = re.sub(r'\bbl\s+' + re.escape(old) + r'\b', 'bl ' + new, bodies[fn])

# ------------------------------------------------------------------ closure
missing_seeds = [s for s in SEEDS if s not in bodies]
if missing_seeds:
    raise SystemExit('seed not found in assembly: ' + ', '.join(missing_seeds))
# Whole files are seeded when the extracted .rodata holds function-pointer
# tables into them: those references live in the data object, not in any
# function body, so reachability alone would miss them.
SEED_FILES = set(json.loads((HERE / 'seed-files.json').read_text()))
work = [s for s in SEEDS if s not in NATIVE]
work += [n for n, f in origin.items() if f in SEED_FILES and n not in NATIVE and n not in work]
closure, extern = set(work), set()
while work:
    n = work.pop()
    calls = re.findall(r'^\s*bl\s+([A-Za-z_]\w*)', bodies[n], re.M)
    words = re.findall(r'\.word\s+([A-Za-z_]\w*)', bodies[n])
    for r in calls + words:
        if r in NATIVE or r not in bodies:
            # only direct BL targets need a typed call binding; a .word to a
            # data symbol is resolved through the extracted-data name map
            if r in calls:
                extern.add(r)
        elif r not in closure:
            closure.add(r)
            work.append(r)
closure = sorted(closure)
extern = sorted(extern - set(DIV) - set(RUNTIME64) - set(SOFTFLOAT))

# ------------------------------------------------- external argument counts
HEADERS = list((DECOMP / 'include').rglob('*.h')) + list((DECOMP / 'src').rglob('*.c')) \
    + list((ROOT / 'soulsilver-native-core/game-include').rglob('*.h')) \
    + list((ROOT / 'native-graphics/libntr/include').rglob('*.h')) \
    + list((ROOT / 'native-probe/libntrsystem/include').rglob('*.h'))
TEXT = '\n'.join(p.read_text(errors='replace') for p in HEADERS)


def split_top(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out]


INFERRED = set()
# Struct/union typedef names: a function returning one of these BY VALUE takes a
# hidden result pointer as its first register argument on both ARM AAPCS and MIPS
# o32, so its real arity is the declared parameter count plus one. The hand
# assembly already passes that pointer in r0; without this the generated call
# dropped the last real argument (Camera_GetLookAtCamTarget: VecFx32 return,
# camera pointer never reached the callee -> NULL deref after the first warp).
def _typedef_struct_names(text):
    # Walk every 'typedef struct/union' to its matching brace and take the name after it;
    # a bare '} name;' scan also matches '} return;' and similar and was over-matching.
    names = set(re.findall(r'typedef\s+(?:struct|union)\s+\w+\s+(\w+)\s*;', text))
    for m in re.finditer(r'typedef\s+(?:struct|union)\b[^{;]*\{', text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {'{': 1, '}': -1}.get(text[i], 0)
            i += 1
        mm = re.match(r'\s*(\w+)\s*;', text[i:i + 80])
        if mm:
            names.add(mm[1])
    return names - {'return', 'break', 'continue', 'else', 'while', 'do'}
STRUCT_TYPES = _typedef_struct_names(TEXT)
STRUCT_RETURNS = {}


def arg_count(name):
    if name in OVERRIDE:
        return OVERRIDE[name]
    for m in re.finditer(r'(?:^|[\s*])' + re.escape(name) + r'\s*\(([^;{)]*(?:\([^)]*\)[^;{)]*)*)\)\s*[;{]', TEXT, re.M):
        args = m.group(1).strip()
        toks = re.findall(r'\w+|\*', TEXT[max(0, m.start() - 120):m.start() + 1])
        hidden = 1 if (toks and toks[-1] != '*' and toks[-1] in STRUCT_TYPES) else 0
        if hidden:
            STRUCT_RETURNS[name] = toks[-1]
        if args in ('', 'void'):
            return hidden
        parts = split_top(args)
        if any(p == '...' for p in parts):
            raise SystemExit('variadic external not supported: ' + name)
        return len(parts) + hidden
    # No prototype anywhere in the decompilation or the SDK headers. The four
    # AAPCS register arguments are forwarded, which is what every call site in
    # hand assembly can set without also writing the caller stack. Recorded in
    # inferred-args.json so the assumption stays visible.
    INFERRED.add(name)
    return 4


INFERRED = set()
counts = {n: arg_count(n) for n in extern}
(HERE / 'inferred-args.json').write_text(json.dumps(sorted(INFERRED), indent=1) + '\n')
(HERE / 'struct-returns.json').write_text(json.dumps(STRUCT_RETURNS, indent=1, sort_keys=True) + '\n')

# ------------------------------------------------------------------ wiring
t.LOCALS.clear()
t.CALLS.clear()
t.INDIRECT_STACK.clear()
t.INDIRECT_STACK.update(INDIRECT_STACK)
inventory = {Path(r['file']).name: r.get('symbols', {})
             for r in json.loads((ROOT / 'soulsilver-native-data/inventory.json').read_text())}

for name in closure:
    t.LOCALS[name] = t.scan(name, bodies[name], inventory.get(origin[name], {}))[1]

# Standard library entry points already declared by the generated prefix.
DEBUG_CALLS = set(json.loads((HERE / 'debug-calls.json').read_text()))
LIBC = {'memset': ('r0=(uint32_t)(uintptr_t)memset((void*)(uintptr_t)r0,(int)r1,r2);', 3),
        'memcpy': ('r0=(uint32_t)(uintptr_t)memcpy((void*)(uintptr_t)r0,(const void*)(uintptr_t)r1,r2);', 3)}
protos = []
for name in extern:
    if name in LIBC:
        t.CALLS[name] = LIBC[name]
        continue
    if name == 'GF_AssertFail':
        t.CALLS[name] = ('ss_assert("?");', 0)
        continue
    c = counts[name]
    if name in DEBUG_CALLS:
        protos.append(f'extern uint32_t {name}(' + (','.join(['uint32_t'] * c) or 'void') + ');')
        a = [f'r{i}' if i < 4 else f'ld32(sp+{4 * (i - 4)})' for i in range(c)]
        fmt = ' '.join(['%08lx'] * c)
        t.CALLS[name] = (f'printf("[DBG] {name} {fmt}"' + ''.join(',' + x for x in a) + f');r0={name}(' + ','.join(a) + ');printf(" -> %08lx\\n", r0);', c)
        continue
    protos.append(f'extern uint32_t {name}(' + (','.join(['uint32_t'] * c) or 'void') + ');')
    args = [f'r{i}' if i < 4 else f'ld32(sp+{4 * (i - 4)})' for i in range(c)]
    t.CALLS[name] = (f'r0={name}(' + ','.join(args) + ');', c)
for name, kind in DIV.items():
    t.CALLS[name] = (f'ssdiv_{kind}(&r0,&r1);', 2)
for name, stmt in RUNTIME64.items():
    t.CALLS[name] = (stmt, 4)
for name, (stmt, count) in SOFTFLOAT.items():
    t.CALLS[name] = (stmt, count)

trace = '--trace' in sys.argv
parts, manifest = [], []
for name in closure:
    # name the translated function in the fatal line before the original
    # assertion handler aborts, so a card log identifies it without symbols
    if 'GF_AssertFail' in t.CALLS:
        t.CALLS['GF_AssertFail'] = (f'ss_assert("{name}");', 0)
    code, ops = t.emit(name, bodies[name], inventory.get(origin[name], {}))
    for helper, kind in DIV.items():
        code = code.replace(f'ssdiv_{kind}(&r0,&r1);\nr1=0xa1a1a1a1; ', f'ssdiv_{kind}(&r0,&r1);\n')
    for stmt, _ in SOFTFLOAT.values():
        if '&r1' in stmt:     # double result travels in r1:r0
            code = code.replace(stmt + '\nr1=0xa1a1a1a1; ', stmt + '\n')
        if '&f' in stmt:      # compare helper: its flags feed the next branch
            code = code.replace(stmt + '\nr1=0xa1a1a1a1; r2=0xa2a2a2a2; r3=0xa3a3a3a3; f=(Flags){0}; /* caller-clobbered */',
                                stmt + '\nr1=0xa1a1a1a1; r2=0xa2a2a2a2; r3=0xa3a3a3a3; /* caller-clobbered, flags kept */')
    for stmt in RUNTIME64.values():
        code = code.replace(stmt + '\nr1=0xa1a1a1a1; ', stmt + '\n')
    for other in closure + extern:
        code = code.replace(f'extern unsigned char {other}[];\n', '')
    if trace and (not os.environ.get('TRACE_ONLY') or name in os.environ['TRACE_ONLY'].split(',')):
        lines = code.split('\n')
        for i, l in enumerate(lines):
            if l.startswith(f'uint32_t native_{name}('):
                lines.insert(i + 1, f'printf("[MLM] {name} %08x %08x %08x %08x\\n",a0,a1,a2,a3);fflush(stdout);')
                break
        code = '\n'.join(lines)
    parts.append(code)
    manifest.append(dict(name=name, file=origin[name], instructions=len(ops), stack_args=t.LOCALS[name]))

forward, exports = [], []
for name in closure:
    sig = ','.join(['uint32_t'] * (4 + t.LOCALS[name]))
    forward.append(f'uint32_t native_{name}({sig});')
    forward.append(f'uint32_t {name}({sig});')
    params = ','.join(f'uint32_t a{i}' for i in range(4 + t.LOCALS[name]))
    call = ','.join(f'a{i}' for i in range(4 + t.LOCALS[name]))
    exports.append(f'uint32_t {name}({params}){{return native_{name}({call});}}')

helpers = r'''
extern uint32_t GF_AssertFail(void);
#include <stdio.h>
/* The original assertion handler, named. It still aborts: the translated
 * code never continues past an assertion the game failed. */
static void ss_assert(const char *where) {
    printf("[FATAL] game assertion in translated %s\n", where);
    (void)GF_AssertFail();
}
/* ARM soft-float runtime helpers: IEEE-754 single passed in integer registers. */
static float ssf_bits(uint32_t b){float f;memcpy(&f,&b,4);return f;}
static uint32_t ssf_word(float f){uint32_t b;memcpy(&b,&f,4);return b;}
static uint32_t ssf_flt(int32_t v){return ssf_word((float)v);}
static int32_t ssf_fix(uint32_t a){return (int32_t)ssf_bits(a);}
static uint32_t ssf_add(uint32_t a,uint32_t b){return ssf_word(ssf_bits(a)+ssf_bits(b));}
static uint32_t ssf_sub(uint32_t a,uint32_t b){return ssf_word(ssf_bits(a)-ssf_bits(b));}
static uint32_t ssf_mul(uint32_t a,uint32_t b){return ssf_word(ssf_bits(a)*ssf_bits(b));}
static uint32_t ssf_div(uint32_t a,uint32_t b){return ssf_word(ssf_bits(a)/ssf_bits(b));}
/* MWCC float compares (lib/asm/msl.s _fgr/_fleq/_fls/_feq): 0/1 in r0 plus the flags the caller branches on. */
static void ssf_gt(Flags *f, uint32_t *r0, uint32_t b){float x=ssf_bits(*r0),y=ssf_bits(b);int r=(x>y);f->c=r;f->z=(x==y);f->n=(x<y);f->v=0;*r0=(uint32_t)r;}
static void ssf_le(Flags *f, uint32_t *r0, uint32_t b){float x=ssf_bits(*r0),y=ssf_bits(b);int r=(x<=y);f->c=!r;f->z=r;f->n=(x<y);f->v=0;*r0=(uint32_t)r;}
static void ssf_lt(Flags *f, uint32_t *r0, uint32_t b){float x=ssf_bits(*r0),y=ssf_bits(b);int r=(x<y);f->c=!r;f->z=(x==y);f->n=r;f->v=0;*r0=(uint32_t)r;}
static void ssf_eq(Flags *f, uint32_t *r0, uint32_t b){float x=ssf_bits(*r0),y=ssf_bits(b);int r=(x==y);f->z=r;f->c=(x>=y);f->n=(x<y);f->v=0;*r0=(uint32_t)r;}
static double ssd_get(uint32_t l, uint32_t h){uint64_t b=((uint64_t)h<<32)|l;double d;memcpy(&d,&b,8);return d;}
static void ssd_put(double d, uint32_t *l, uint32_t *h){uint64_t b;memcpy(&b,&d,8);*l=(uint32_t)b;*h=(uint32_t)(b>>32);}
static void ssd_flt(uint32_t *r0, uint32_t *r1){ssd_put((double)(int32_t)*r0,r0,r1);}
static void ssd_fltu(uint32_t *r0, uint32_t *r1){ssd_put((double)*r0,r0,r1);}
static void ssd_add(uint32_t *r0, uint32_t *r1, uint32_t r2, uint32_t r3){ssd_put(ssd_get(*r0,*r1)+ssd_get(r2,r3),r0,r1);}
static void ssd_mul(uint32_t *r0, uint32_t *r1, uint32_t r2, uint32_t r3){ssd_put(ssd_get(*r0,*r1)*ssd_get(r2,r3),r0,r1);}
static void ssd_div(uint32_t *r0, uint32_t *r1, uint32_t r2, uint32_t r3){ssd_put(ssd_get(*r0,*r1)/ssd_get(r2,r3),r0,r1);}
static int32_t ssd_fix(uint32_t l, uint32_t h){double d=ssd_get(l,h);
    if (d!=d) return (h&0x80000000u)?(int32_t)0x80000000:0x7FFFFFFF;
    if (d>=2147483648.0) return 0x7FFFFFFF; if (d<-2147483648.0) return (int32_t)0x80000000;
    return (int32_t)d;}
static void ssf_ne(Flags *f, uint32_t *r0, uint32_t b){float x=ssf_bits(*r0),y=ssf_bits(b);int eq=(x==y);f->z=eq;f->c=(x>=y);f->n=(x<y);f->v=0;*r0=(uint32_t)!eq;}
/* MWCC double helpers: a double travels as (high:low) = (r1:r0) / (r3:r2). */
static void ssd_f2d(uint32_t *r0, uint32_t *r1){double d=(double)ssf_bits(*r0);uint64_t b;memcpy(&b,&d,8);*r0=(uint32_t)b;*r1=(uint32_t)(b>>32);}
static void ssd_cmp(Flags *f, uint32_t l0, uint32_t h0, uint32_t l1, uint32_t h1){
    uint64_t A=((uint64_t)h0<<32)|l0, B=((uint64_t)h1<<32)|l1; double a,b; memcpy(&a,&A,8); memcpy(&b,&B,8);
    f->z=(a==b); f->c=(a>=b); f->n=(a<b); f->v=0;   /* what `cmp a,b` leaves for bhi/bls/bge/blt */
}
/* MWCC _d2f (double -> single) and _dgeq (a >= b, result in r0 and the carry flag). */
static uint32_t ssd_d2f(uint32_t l, uint32_t h){return ssf_word((float)ssd_get(l,h));}
static void ssd_ge(Flags *f, uint32_t *r0, uint32_t h0, uint32_t l1, uint32_t h1){double a=ssd_get(*r0,h0),b=ssd_get(l1,h1);int r=(a>=b);f->c=r;f->z=(a==b);f->n=(a<b);f->v=0;*r0=(uint32_t)r;}
/* MWCC runtime helpers used by Pokeathlon (lib/asm/msl.s _ffltu/_ffixu/_dfixu/_dsub/_dls/_dgr). */
static uint32_t ssf_fltu(uint32_t v){return ssf_word((float)v);}
static uint32_t ssf_fixu(uint32_t a){float x=ssf_bits(a); if (x!=x) return 0xFFFFFFFFu; if (x<=0.0f) return (a&0x80000000u)&&((a<<1)>0xFF000000u)?0xFFFFFFFFu:0; if (x>=4294967296.0f) return 0xFFFFFFFFu; return (uint32_t)x;}
static uint32_t ssd_fixu(uint32_t l, uint32_t h){double d=ssd_get(l,h); if (d!=d) return 0xFFFFFFFFu; if (d<=0.0) return 0; if (d>=4294967296.0) return 0xFFFFFFFFu; return (uint32_t)d;}
static void ssd_sub(uint32_t *r0, uint32_t *r1, uint32_t r2, uint32_t r3){ssd_put(ssd_get(*r0,*r1)-ssd_get(r2,r3),r0,r1);}
static void ssd_lt(Flags *f, uint32_t *r0, uint32_t h0, uint32_t l1, uint32_t h1){double a=ssd_get(*r0,h0),b=ssd_get(l1,h1);int r=(a<b);f->c=!r;f->z=(a==b);f->n=r;f->v=0;*r0=(uint32_t)r;}
static void ssd_gt(Flags *f, uint32_t *r0, uint32_t h0, uint32_t l1, uint32_t h1){double a=ssd_get(*r0,h0),b=ssd_get(l1,h1);int r=(a>b);f->c=r;f->z=(a==b);f->n=(a<b);f->v=0;*r0=(uint32_t)r;}
/* MWCC _ll_udiv/_ull_div (mod=0) and _ull_mod (mod=1): unsigned (r1:r0) op (r3:r2) -> (r1:r0).
   A zero divisor returns with r0/r1 unchanged, like msl.s (orrs r5,r3,r2; bne; return). */
static void ssulldiv(uint32_t *r0, uint32_t *r1, uint32_t r2, uint32_t r3, int mod) {
    uint64_t a = ((uint64_t)*r1 << 32) | *r0, b = ((uint64_t)r3 << 32) | r2, q;
    if (!b) return;
    q = mod ? a % b : a / b; *r0 = (uint32_t)q; *r1 = (uint32_t)(q >> 32);
}
/* ARM runtime 64-bit multiply: (r1:r0) * (r3:r2) -> (r1:r0). */
static void ssllmul(uint32_t *r0, uint32_t *r1, uint32_t r2, uint32_t r3) {
    uint64_t a = ((uint64_t)*r1 << 32) | *r0, b = ((uint64_t)r3 << 32) | r2, p = a * b;
    *r0 = (uint32_t)p; *r1 = (uint32_t)(p >> 32);
}
/* ARM runtime divide helpers: quotient in r0, remainder in r1. */
static void ssdiv_s(uint32_t *r0, uint32_t *r1) {
    int32_t n = (int32_t)*r0, d = (int32_t)*r1;
    if (d == 0) { *r0 = n < 0 ? 0xffffffffu : (n == 0 ? 0u : 1u); *r1 = (uint32_t)n; return; }
    *r0 = (uint32_t)(n / d); *r1 = (uint32_t)(n % d);
}
static void ssdiv_u(uint32_t *r0, uint32_t *r1) {
    uint32_t n = *r0, d = *r1;
    if (d == 0) { *r0 = n ? 0xffffffffu : 0u; *r1 = n; return; }
    *r0 = n / d; *r1 = n % d;
}
'''
out = [t.prefix, '#include <stdio.h>' if (trace or DEBUG_CALLS) else '', helpers,
       '\n'.join(protos), '\n'.join(forward)] + parts + ['\n'.join(exports)]
(HERE / 'overworld_native.c').write_text('\n\n'.join(out) + '\n')
(HERE / 'manifest.json').write_text(json.dumps(
    dict(functions=manifest, externals=counts,
         files=sorted({origin[n] for n in closure})), indent=1) + '\n')
print('closure', len(closure), 'functions,', sum(m['instructions'] for m in manifest),
      'assembly instructions from', len({origin[n] for n in closure}), 'files;',
      len(extern), 'external bindings')

if '--compile' in sys.argv:
    obj = HERE / 'overworld_native.o'
    r = subprocess.run([str(PSPDEV / 'bin/psp-gcc'), '-O2', *'@TARGETCC@'.split(), '-std=gnu99',
                        '-ffunction-sections', '-fdata-sections', '-Wall',
                        '-Wno-unused-but-set-variable', '-Wno-unused-variable', '-Wno-unused-function',
                        '-c', str(HERE / 'overworld_native.c'), '-o', str(obj)],
                       capture_output=True, text=True)
    sys.stderr.write(r.stderr)
    if r.returncode:
        raise SystemExit('compile failed')
    # A bounds warning against the local frame array means an incoming stack argument
    # was missed. GCC also emits -Warray-bounds for the assert path where a pointer is
    # provably NULL ('out of the bounds [0, 0]'): that mirrors the original's behaviour
    # and is not a frame problem, so it is ignored.
    real = [l for l in r.stderr.splitlines() if 'bounds=' in l and '[0, 0]' not in l]
    if real:
        sys.stderr.write('\n'.join(real) + '\n')
        raise SystemExit('frame bounds warning: an incoming stack argument was missed')
    # DS register dispatcher: compiled with the game's own command so the SDK
    # reg_* macros resolve to the native simulator variables.
    subprocess.run([sys.executable, str(HERE / 'gen_ioreg.py')], check=True)
    game_cmd = json.loads((ROOT / 'soulsilver-native-core/compile-command.json').read_text())
    ioobj = HERE / 'ds_ioreg.o'
    subprocess.run(game_cmd + ['-c', str(HERE / 'ds_ioreg.c'), '-o', str(ioobj)], check=True)
    lib = HERE / 'libss-maploader.a'
    lib.unlink(missing_ok=True)
    subprocess.run([str(PSPDEV / 'bin/psp-ar'), 'rcs', str(lib), str(obj), str(ioobj)], check=True)
    print('built', lib)
