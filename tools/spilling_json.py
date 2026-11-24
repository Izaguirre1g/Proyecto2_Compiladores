#!/usr/bin/env python3
"""Genera <fuente>.spilling.json para tools/visualizar_spilling.html.

Uso (desde la raiz del proyecto):
    python3 tools/spilling_json.py ejemplo_profe/03_SpillingDeRegistros.bal

Que hace:
  1. Ejecuta `./balc -O2` sobre una copia del fuente en una carpeta temporal,
     asi junto al .bal solo queda el JSON (no .asm/.bin/.mem/.ir).
  2. Lee la IR que entra a la pasada de spilling (.o2-despues.ir) y la que sale
     (.o2-spilling.ir).
  3. Reproduce el algoritmo de src/ir_spilling.c (asignacion local por bloque,
     LRU, k registros) y registra cada evento: que temporal entra, cual se
     desaloja, en que ranura $spillN se guarda y cuando se vuelve a cargar.
  4. Comprueba que la simulacion produce exactamente la misma IR que balc.
  5. Calcula vida de los temporales y el grafo de interferencia.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
K_BALC_O2 = 3  # ir_vliw_programa(ast, tabla, 3, ...) en balc -O2

RE_TEMP = re.compile(r'^\$t\d+$')
RE_LINEA = re.compile(r'^(\d+) \| (.*) ; linea (\d+)$')
RE_BLOQUE = re.compile(r'^\[B(\d+)\](?: ->(.*))?$')


RE_VAR = re.compile(r'^__bal_v(\d+)$')


def es_temporal(x):
    return bool(x) and bool(RE_TEMP.match(x))


def es_variable(x):
    """Variables del programa: el productor -O2 las nombra __bal_vN (N = simbolo N)."""
    return bool(x) and bool(RE_VAR.match(x))


def clave_nombre(x):
    """Ordena variables primero y luego temporales, por numero."""
    m = RE_VAR.match(x)
    return (0, int(m[1])) if m else (1, int(x[2:]))


# ---------------------------------------------------------------- IR texto
def parsear_instr(t):
    """Texto de ir_imprimir -> (op, dst, a, b, operador)."""
    m = None
    if t.startswith('func '): return ('FUNC', t[5:], None, None, None)
    if t.startswith('endfunc '): return ('END_FUNC', t[8:], None, None, None)
    if t.startswith('param_decl '): return ('PARAM_DECL', t[11:], None, None, None)
    if t.startswith('goto '): return ('JUMP', t[5:], None, None, None)
    m = re.match(r'^(ifFalse|ifTrue) (.*) goto (\S+)$', t)
    if m: return ('JFALSE' if m[1] == 'ifFalse' else 'JTRUE', m[3], m[2], None, None)
    if t.startswith('arg '): return ('ARG', None, t[4:], None, None)
    if t == 'return': return ('RETURN', None, None, None, None)
    if t.startswith('return '): return ('RETURN', None, t[7:], None, None)
    m = re.match(r'^array (.*), dimensiones (.*)$', t)
    if m: return ('ARRAY_DECL', m[1], m[2], None, None)
    m = re.match(r'^dim (.*)\[(.*)\] = (.*)$', t)
    if m: return ('ARRAY_DIM', m[1], m[2], m[3], None)
    m = re.match(r'^spill_store (\S+), (.*)$', t)
    if m: return ('SPILL_STORE', m[1], m[2], None, None)
    m = re.match(r'^store (\S+), (.*)$', t)
    if m: return ('STORE', m[1], m[2], None, None)
    m = re.match(r'^list (\S+)$', t)
    if m: return ('LIST_DECL', m[1], None, None, None)
    m = re.match(r'^(add|remove) (\S+), (.*)$', t)
    if m: return ('LIST_ADD' if m[1] == 'add' else 'LIST_REMOVE', m[2], m[3], None, None)
    if re.match(r'^\S+:$', t): return ('LABEL', t[:-1], None, None, None)
    m = re.match(r'^(\S+) = (.*)$', t)
    if not m: raise ValueError('instruccion IR no reconocida: ' + t)
    d, rhs = m[1], m[2]
    m = re.match(r'^call (\S+), (.*)$', rhs)
    if m: return ('CALL', d, m[1], m[2], None)
    if rhs.startswith('spill_load '): return ('SPILL_LOAD', d, rhs[11:], None, None)
    if rhs.startswith('load '): return ('LOAD', d, rhs[5:], None, None)
    if rhs.startswith('size '): return ('SIZE', d, rhs[5:], None, None)
    m = re.match(r'^ref (.*)\[(.*)\]$', rhs)
    if m: return ('INDEX_REF', d, m[1], m[2], None)
    if rhs[:1] in ('"', "'"): return ('COPY', d, rhs, None, None)
    p = rhs.split(' ')
    if len(p) == 2: return ('UNARY', d, p[1], None, p[0])
    if len(p) == 3: return ('BIN', d, p[0], p[2], p[1])
    return ('COPY', d, rhs, None, None)


def formatear(op, d, a, b, o):
    """Mismo formato que ir_imprimir (src/Representacion_Intermedia/ir.c)."""
    return {
        'FUNC': lambda: f'func {d}', 'END_FUNC': lambda: f'endfunc {d}',
        'PARAM_DECL': lambda: f'param_decl {d}', 'COPY': lambda: f'{d} = {a}',
        'BIN': lambda: f'{d} = {a} {o} {b}', 'UNARY': lambda: f'{d} = {o} {a}',
        'LABEL': lambda: f'{d}:', 'JUMP': lambda: f'goto {d}',
        'JFALSE': lambda: f'ifFalse {a} goto {d}', 'JTRUE': lambda: f'ifTrue {a} goto {d}',
        'ARG': lambda: f'arg {a}', 'CALL': lambda: f'{d} = call {a}, {b}',
        'RETURN': lambda: f'return {a}' if a else 'return',
        'ARRAY_DECL': lambda: f'array {d}, dimensiones {a}',
        'ARRAY_DIM': lambda: f'dim {d}[{a}] = {b}', 'INDEX_REF': lambda: f'{d} = ref {a}[{b}]',
        'LOAD': lambda: f'{d} = load {a}', 'STORE': lambda: f'store {d}, {a}',
        'LIST_DECL': lambda: f'list {d}', 'LIST_ADD': lambda: f'add {d}, {a}',
        'LIST_REMOVE': lambda: f'remove {d}, {a}', 'SIZE': lambda: f'{d} = size {a}',
        'SPILL_LOAD': lambda: f'{d} = spill_load {a}',
        'SPILL_STORE': lambda: f'spill_store {d}, {a}',
    }[op]()


def campos(op):
    """Igual que campos() en src/ir_spilling.c: leer_dst, leer_a, leer_b, escribir_dst."""
    if op in ('COPY', 'UNARY', 'LOAD', 'SIZE'): return 0, 1, 0, 1
    if op in ('BIN', 'INDEX_REF'): return 0, 1, 1, 1
    if op in ('JFALSE', 'JTRUE', 'ARG', 'RETURN'): return 0, 1, 0, 0
    if op == 'CALL': return 0, 0, 0, 1
    if op in ('STORE', 'LIST_ADD', 'LIST_REMOVE'): return 1, 1, 0, 0
    if op == 'ARRAY_DIM': return 0, 0, 1, 0
    return 0, 0, 0, 0


def leer_ir(ruta):
    bloques, instrs = [], []
    with open(ruta, encoding='utf-8') as f:
        for linea in f:
            linea = linea.rstrip('\n')
            if not linea.strip(): continue
            m = RE_BLOQUE.match(linea)
            if m:
                suc = [int(x[1:]) for x in (m[2] or '').split()]
                bloques.append({'id': int(m[1]), 'inicio': len(instrs), 'sucesores': suc})
                continue
            m = RE_LINEA.match(linea)
            if not m: raise ValueError(f'{ruta}: linea no reconocida: {linea}')
            texto = m[2]
            p = parsear_instr(texto)
            if formatear(*p) != texto:
                raise ValueError(f'{ruta}: no se pudo reconstruir: {texto}')
            instrs.append({'texto': texto, 'op': p, 'linea': int(m[3]), 'bloque': bloques[-1]['id']})
    for i, b in enumerate(bloques):
        b['fin'] = bloques[i + 1]['inicio'] if i + 1 < len(bloques) else len(instrs)
    return bloques, instrs


# ------------------------------------------------- simulacion de ir_spilling.c
def simular(bloques, instrs, k):
    temporales, ranura = [], {}

    def registrar(x):
        if es_temporal(x) and x not in ranura:
            ranura[x] = f'$spill{len(temporales)}'
            temporales.append(x)

    for ins in instrs:
        op, d, a, b, _ = ins['op']
        rd, ra, rb, wd = campos(op)
        if rd or wd: registrar(d)
        if ra: registrar(a)
        if rb: registrar(b)

    regs = [{'t': None, 'uso': 0, 'sucio': False} for _ in range(k)]
    reloj = [0]
    salida, met = [], {'ranuras': len(temporales), 'cargas': 0, 'almacenes': 0, 'desalojos': 0}
    eventos_por_instr = []
    eventos = None
    actual = {'i': None, 'linea': 0}

    def emitir(texto, linea, origen, tipo=None):
        salida.append({'texto': texto, 'linea': linea, 'origen': origen, 'tipo': tipo})

    def guardar(r, motivo, por=None):
        R = regs[r]
        if R['t'] is None or not R['sucio']: return
        emitir(f'spill_store {ranura[R["t"]]}, $r{r}', actual['linea'], actual['i'], 'store')
        met['almacenes'] += 1
        R['sucio'] = False
        eventos.append({'tipo': 'store', 'temporal': R['t'], 'ranura': ranura[R['t']],
                        'registro': r, 'motivo': motivo, 'por': por})

    def elegir(prot):
        e = None
        for i in range(k):
            if prot[i]: continue
            if regs[i]['t'] is None: return i
            if e is None or regs[i]['uso'] < regs[e]['uso']: e = i
        return e

    def adquirir(nombre, lectura, prot):
        for i in range(k):
            if regs[i]['t'] == nombre:
                reloj[0] += 1; regs[i]['uso'] = reloj[0]; prot[i] = 1
                return i
        r = elegir(prot)
        if r is None: raise RuntimeError('sin registro libre')
        viejo = regs[r]['t']
        if viejo is not None:
            met['desalojos'] += 1
            eventos.append({'tipo': 'desalojo', 'temporal': viejo, 'registro': r, 'por': nombre,
                            'sucio': regs[r]['sucio'], 'ranura': ranura[viejo]})
        guardar(r, 'desalojo', nombre)
        reloj[0] += 1
        regs[r] = {'t': nombre, 'uso': reloj[0], 'sucio': False}
        prot[r] = 1
        if lectura:
            emitir(f'$r{r} = spill_load {ranura[nombre]}', actual['linea'], actual['i'], 'load')
            met['cargas'] += 1
            eventos.append({'tipo': 'load', 'temporal': nombre, 'ranura': ranura[nombre], 'registro': r})
        return r

    def vaciar(motivo):
        for i in range(k): guardar(i, motivo)

    def invalidar():
        for R in regs: R.update(t=None, sucio=False, uso=0)

    def leer(x, prot):
        if not es_temporal(x): return x
        return f'$r{adquirir(x, True, prot)}'

    def procesar(ins):
        op, d, a, b, o = ins['op']
        rd, ra, rb, wd = campos(op)
        if op == 'CALL':
            vaciar('llamada'); invalidar()
        prot = [0] * k
        if rd: d = leer(d, prot)
        if ra: a = leer(a, prot)
        if rb: b = leer(b, prot)
        ri = None
        if wd and es_temporal(ins['op'][1]):
            ri = adquirir(ins['op'][1], False, prot)
            d = f'$r{ri}'
        emitir(formatear(op, d, a, b, o), ins['linea'], actual['i'])
        if ri is not None: regs[ri]['sucio'] = True

    terminales = ('JUMP', 'JFALSE', 'JTRUE', 'RETURN', 'END_FUNC')
    for B in bloques:
        invalidar()
        for i in range(B['inicio'], B['fin']):
            ins = instrs[i]
            actual['i'], actual['linea'] = i, ins['linea']
            eventos = []
            ultimo = i + 1 == B['fin']
            terminal = ins['op'][0] in terminales
            if ultimo and terminal: vaciar('fin de bloque')
            procesar(ins)
            if ultimo and not terminal: vaciar('fin de bloque')
            eventos_por_instr.append({
                'eventos': eventos,
                'registros': [{'t': R['t'], 'sucio': R['sucio']} for R in regs],
            })
    return temporales, ranura, salida, met, eventos_por_instr


# ------------------------------------------------------------- vida / grafo
def vida(bloques, instrs):
    uses, defs = [], []
    for ins in instrs:
        op, d, a, b, _ = ins['op']
        rd, ra, rb, wd = campos(op)
        u = {x for x, f in ((d, rd), (a, ra), (b, rb)) if f and es_temporal(x)}
        df = {d} if wd and es_temporal(d) else set()
        uses.append(u); defs.append(df)
    n = len(bloques)
    gen, kill = [set() for _ in range(n)], [set() for _ in range(n)]
    for j, B in enumerate(bloques):
        for i in range(B['inicio'], B['fin']):
            gen[j] |= uses[i] - kill[j]
            kill[j] |= defs[i]
    lin, lout = [set() for _ in range(n)], [set() for _ in range(n)]
    cambio = True
    while cambio:
        cambio = False
        for j in reversed(range(n)):
            o = set().union(*[lin[s] for s in bloques[j]['sucesores'] if s < n]) if bloques[j]['sucesores'] else set()
            nuevo = gen[j] | (o - kill[j])
            if o != lout[j] or nuevo != lin[j]:
                lout[j], lin[j], cambio = o, nuevo, True
    vivos_desp = [None] * len(instrs)
    aristas = set()
    for j, B in enumerate(bloques):
        viva = set(lout[j])
        for i in reversed(range(B['inicio'], B['fin'])):
            vivos_desp[i] = sorted(viva, key=lambda t: int(t[2:]))
            for d in defs[i]:
                for t in viva:
                    if t != d: aristas.add(tuple(sorted((d, t), key=lambda t: int(t[2:]))))
            viva = (viva - defs[i]) | uses[i]
    # Los temporales vivos a la vez al entrar a un bloque tambien interfieren.
    for j in range(n):
        l = sorted(lin[j], key=lambda t: int(t[2:]))
        for x in range(len(l)):
            for y in range(x + 1, len(l)): aristas.add((l[x], l[y]))
    return uses, defs, vivos_desp, aristas, lin, lout


def in_out_variables(bloques, instrs):
    """IN/OUT de cada bloque contando variables del programa (__bal_vN) y temporales.

    Los temporales del frontend no cruzan bloques; lo que fluye entre bloques son
    las variables, por eso el IN/OUT del CFG se calcula sobre ambas."""
    es_nombre = lambda x: es_temporal(x) or es_variable(x)
    n = len(bloques)
    gen, kill = [set() for _ in range(n)], [set() for _ in range(n)]
    for j, B in enumerate(bloques):
        for i in range(B['inicio'], B['fin']):
            op, d, a, b, _ = instrs[i]['op']
            rd, ra, rb, wd = campos(op)
            if op in ('ARRAY_DECL', 'LIST_DECL'): wd = 1  # declarar crea el objeto
            # En "ref X[i]" y "dim X[i]" el arreglo X tambien se lee.
            if op == 'ARRAY_DIM': rd = 1
            usa = {x for x, f in ((d, rd), (a, ra), (b, rb)) if f and es_nombre(x)}
            gen[j] |= usa - kill[j]
            if wd and es_nombre(d): kill[j].add(d)
    lin, lout = [set() for _ in range(n)], [set() for _ in range(n)]
    orden = lambda s: sorted(s, key=clave_nombre)
    # Iteracion 0: todo vacio. Cada iteracion recorre los bloques de atras hacia
    # adelante; se repite hasta que ningun IN/OUT cambia (punto fijo).
    iteraciones = [[{'in': [], 'out': []} for _ in range(n)]]
    cambio = True
    while cambio:
        cambio = False
        for j in reversed(range(n)):
            o = set()
            for s in bloques[j]['sucesores']:
                if s < n: o |= lin[s]
            nuevo = gen[j] | (o - kill[j])
            if o != lout[j] or nuevo != lin[j]:
                lout[j], lin[j], cambio = o, nuevo, True
        iteraciones.append([{'in': orden(lin[j]), 'out': orden(lout[j])} for j in range(n)])
    return ([orden(s) for s in lin], [orden(s) for s in lout],
            [orden(s) for s in gen], [orden(s) for s in kill], iteraciones)


# ------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('fuente', help='archivo .bal')
    ap.add_argument('-o', '--salida', help='ruta del JSON (por defecto <fuente>.spilling.json)')
    ap.add_argument('-k', type=int, default=K_BALC_O2,
                    help=f'registros fisicos a simular (balc -O2 usa {K_BALC_O2}); con otro k no se puede validar contra balc')
    args = ap.parse_args()
    if not (3 <= args.k <= 32): sys.exit('k debe estar entre 3 y 32 (igual que ir_aplicar_spilling)')
    fuente = os.path.abspath(args.fuente)
    if not fuente.endswith('.bal') or not os.path.isfile(fuente): sys.exit('se necesita un archivo .bal existente')
    balc = os.path.join(RAIZ, 'balc')
    if not os.access(balc, os.X_OK): sys.exit('no existe ./balc; compila primero con make')
    salida_json = args.salida or fuente[:-4] + '.spilling.json'
    base = os.path.basename(fuente)[:-4]

    with tempfile.TemporaryDirectory(prefix='spilling_') as tmp:
        # Copia los .bal de la carpeta por si el fuente importa otros archivos.
        carpeta = os.path.dirname(fuente)
        for f in os.listdir(carpeta):
            if f.endswith('.bal'): shutil.copy2(os.path.join(carpeta, f), tmp)
        p = subprocess.run([balc, '-O2', os.path.join(tmp, base + '.bal')], cwd=RAIZ,
                           capture_output=True, text=True)
        if p.returncode != 0:
            sys.stderr.write(p.stdout + p.stderr)
            sys.exit(f'balc -O2 fallo (codigo {p.returncode})')
        resumen = next((l for l in (p.stdout + p.stderr).splitlines() if l.startswith('[O2]')), '')
        bloques, instrs = leer_ir(os.path.join(tmp, base + '.o2-despues.ir'))
        _, real = leer_ir(os.path.join(tmp, base + '.o2-spilling.ir'))
        # .o2.json: objetos[].id es el N de __bal_vN (mismo indice de identidades).
        variables = {}
        try:
            with open(os.path.join(tmp, base + '.o2.json'), encoding='utf-8') as f:
                for o in json.load(f).get('objetos', []):
                    variables[f'__bal_v{o["id"]}'] = {'nombre': o['nombre'], 'funcion': o.get('funcion'),
                                                      'linea': o.get('linea')}
        except (OSError, ValueError, KeyError):
            pass

    temporales, ranura, salida, met, por_instr = simular(bloques, instrs, args.k)
    validado = None
    if args.k == K_BALC_O2:
        a = [s['texto'] for s in salida]
        b = [r['texto'] for r in real]
        if a != b:
            i = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
            sys.exit(f'la simulacion no coincide con balc en la instruccion {i}: '
                     f'{a[i] if i < len(a) else "-"} | {b[i] if i < len(b) else "-"}')
        m = re.search(r'spills=(\d+) loads=(\d+) stores=(\d+)', resumen)
        if m and (int(m[1]), int(m[2]), int(m[3])) != (met['ranuras'], met['cargas'], met['almacenes']):
            sys.exit('las metricas simuladas no coinciden con las de balc')
        validado = True

    uses, defs, vivos, aristas, lin, lout = vida(bloques, instrs)
    v_in, v_out, v_gen, v_kill, iteraciones = in_out_variables(bloques, instrs)
    # Funcion a la que pertenece cada bloque (para saber que variables le tocan).
    funcion_de, actual_fn = {}, None
    for ins in instrs:
        if ins['op'][0] == 'FUNC': actual_fn = ins['op'][1]
        funcion_de.setdefault(ins['bloque'], actual_fn)
    for lista in (v_in, v_out, v_gen, v_kill):
        for s in lista:
            for x in s:
                if es_variable(x) and x not in variables:
                    variables[x] = {'nombre': x, 'funcion': None, 'linea': None}

    # Datos por temporal
    info = {t: {'nombre': t, 'ranura': ranura[t], 'defs': [], 'usos': [], 'bloques': set(),
                'cargas': 0, 'almacenes': 0, 'desalojos': 0, 'registros': set(), 'grado': 0}
            for t in temporales}
    for i, ins in enumerate(instrs):
        for t in defs[i]: info[t]['defs'].append(i); info[t]['bloques'].add(ins['bloque'])
        for t in uses[i]: info[t]['usos'].append(i); info[t]['bloques'].add(ins['bloque'])
        for e in por_instr[i]['eventos']:
            t = info[e['temporal']]
            if e['tipo'] == 'load': t['cargas'] += 1
            elif e['tipo'] == 'store': t['almacenes'] += 1
            else: t['desalojos'] += 1
        for r, R in enumerate(por_instr[i]['registros']):
            if R['t']: info[R['t']]['registros'].add(r)
    for x, y in aristas:
        info[x]['grado'] += 1; info[y]['grado'] += 1
    for t in info.values():
        t['bloques'] = sorted(t['bloques']); t['registros'] = sorted(t['registros'])
        t['spill'] = t['almacenes'] > 0 or t['cargas'] > 0
        t['entre_bloques'] = len(t['bloques']) > 1

    # Datos por bloque
    salida_por_origen = {}
    for s in salida:
        salida_por_origen.setdefault(s['origen'], []).append({'texto': s['texto'], 'tipo': s['tipo']})
    bl = []
    for B in bloques:
        rng = range(B['inicio'], B['fin'])
        ev = [e for i in rng for e in por_instr[i]['eventos']]
        bl.append({
            'id': B['id'], 'inicio': B['inicio'], 'fin': B['fin'], 'sucesores': B['sucesores'],
            'cargas': sum(e['tipo'] == 'load' for e in ev),
            'almacenes': sum(e['tipo'] == 'store' for e in ev),
            'desalojos': sum(e['tipo'] == 'desalojo' for e in ev),
            'presion_max': max((len(vivos[i]) for i in rng), default=0),
            'vivos_entrada': sorted(lin[B['id']], key=lambda t: int(t[2:])),
            'vivos_salida': sorted(lout[B['id']], key=lambda t: int(t[2:])),
            'in': v_in[B['id']], 'out': v_out[B['id']],
            'gen': v_gen[B['id']], 'kill': v_kill[B['id']], 'funcion': funcion_de.get(B['id']),
            'resumen': [instrs[i]['texto'] for i in rng][:3],
        })

    pasos = [{
        'i': i, 'bloque': ins['bloque'], 'linea': ins['linea'], 'texto': ins['texto'],
        'usa': sorted(uses[i]), 'define': sorted(defs[i]), 'vivos': vivos[i],
        'registros': por_instr[i]['registros'], 'eventos': por_instr[i]['eventos'],
        'genera': salida_por_origen.get(i, []),
    } for i, ins in enumerate(instrs)]

    try:
        with open(fuente, encoding='utf-8') as f: codigo = f.read().splitlines()
    except UnicodeDecodeError:
        codigo = []

    datos = {
        'formato': 'bal-spilling', 'version': 1, 'archivo': os.path.basename(fuente),
        'k': args.k, 'registros': [f'$r{i}' for i in range(args.k)],
        'algoritmo': 'local por bloque, reemplazo LRU (src/ir_spilling.c)',
        'validado_contra_balc': validado, 'resumen_balc': resumen,
        'metricas': {**met, 'temporales': len(temporales), 'interferencias': len(aristas),
                     'presion_max': max((len(v) for v in vivos), default=0),
                     'instrucciones_antes': len(instrs), 'instrucciones_despues': len(salida)},
        'temporales': [info[t] for t in temporales],
        'interferencias': sorted(aristas, key=lambda e: (int(e[0][2:]), int(e[1][2:]))),
        'bloques': bl, 'pasos': pasos, 'fuente': codigo,
        'variables': dict(sorted(variables.items(), key=lambda kv: clave_nombre(kv[0]))),
        'iteraciones_vida': iteraciones,
    }
    with open(salida_json, 'w', encoding='utf-8') as f:
        json.dump(datos, f, ensure_ascii=False, separators=(',', ':'))
    print(f'JSON: {os.path.relpath(salida_json)}')
    print(f'k={args.k} temporales={len(temporales)} cargas={met["cargas"]} almacenes={met["almacenes"]} '
          f'desalojos={met["desalojos"]} interferencias={len(aristas)} '
          f'validado={"si" if validado else "no (k distinto de 3)"}')


if __name__ == '__main__':
    main()
