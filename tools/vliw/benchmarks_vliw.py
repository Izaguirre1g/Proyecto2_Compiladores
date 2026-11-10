"""Coordina lotes VLIW delegando la arquitectura y las métricas a B1."""
import argparse
import base64
import csv
import hashlib
import io
import json
import math
import os
from pathlib import Path
import selectors
import signal
import stat
import subprocess
import sys
import tempfile
import time

CATEGORIAS = ('loop_unrolling', 'renombramiento', 'dce', 'saltos_funciones', 'spilling')
RESULTADOS = {0: 'EQUIVALENTE', 2: 'DIFERENTE', 3: 'INCONCLUSO', 1: 'ERROR'}
INFRA = 'ERROR_DE_INFRAESTRUCTURA'
MAX_MANIFIESTO = 2 * 1024 * 1024
MAX_CASOS = 256
MAX_ARCHIVO = 64 * 1024 * 1024
MAX_STDOUT = 1024 * 1024
MAX_STDERR = 64 * 1024
MAX_LOTE = 16 * 1024 * 1024
LIMITE_U64 = (1 << 64) - 1
LIMITACIONES = [
    'B1 certifica equivalencia pública estricta; no equivalencia semántica general.',
    'Renombramiento puede cambiar GPR sin demostrar un defecto semántico del compilador.',
    'Bóveda privada no observable; B2 no modifica el contrato estricto-publico.',
    'Niveles -O y fuente BAL son declaraciones, no procedencia de compilación verificada.',
    'Demostraciones no cumplen los quince casos oficiales.',
    'No se miden compilación, tamaño de código ejecutable ni estadísticas gráficas del IDE.',
]


class ManifiestoInvalido(ValueError):
    pass


class InfraestructuraError(ValueError):
    pass


class PublicacionError(OSError):
    def __init__(self, mensaje, publicados):
        super().__init__(mensaje)
        self.publicados = publicados


def exigir(condicion, mensaje):
    if not condicion:
        raise ValueError(mensaje)


def texto(valor, nombre, maximo=4096, vacio=False):
    exigir(type(valor) is str and (vacio or bool(valor.strip())), nombre + ': texto requerido')
    exigir('\0' not in valor and len(valor) <= maximo, nombre + ': longitud o carácter inválido')
    valor.encode('utf-8', errors='strict')
    return valor


def entero(valor, nombre, maximo=LIMITE_U64, minimo=0):
    # bool hereda de int en Python: el contrato exige un entero JSON real.
    exigir(type(valor) is int and minimo <= valor <= maximo, nombre + ': entero fuera de rango')
    return valor


def campos(objeto, requeridos, opcionales=()):
    exigir(type(objeto) is dict, 'se requiere un objeto JSON')
    exigir(set(requeridos) <= objeto.keys() and objeto.keys() <= set(requeridos) | set(opcionales),
           'campos ausentes o desconocidos: ' + repr(sorted(objeto)))


def booleano(v, nombre):
    exigir(type(v) is bool, nombre + ': booleano requerido')


def numero(v, nombre, no_negativo=False):
    exigir(type(v) in (int, float), nombre + ': número requerido')
    exigir(math.isfinite(v) and (not no_negativo or v >= 0), nombre + ': número inválido')


def decodificar(datos):
    # JSON estándar permite por defecto duplicados y NaN. Rechazarlos evita
    # interpretar silenciosamente otra versión del manifiesto o respuesta B1.
    def objeto(pares):
        d = {}
        for k, v in pares:
            exigir(k not in d, 'clave duplicada: ' + k)
            d[k] = v
        return d

    def constante(v):
        raise ValueError('número no finito: ' + v)

    def real(v):
        n = float(v)
        exigir(math.isfinite(n), 'número no finito')
        return n

    return json.loads(datos.decode('utf-8'), object_pairs_hook=objeto,
                      parse_constant=constante, parse_float=real)


def huella(ruta):
    h = hashlib.sha256()
    total = 0
    with ruta.open('rb') as f:
        for b in iter(lambda: f.read(65536), b''):
            total += len(b)
            exigir(total <= MAX_ARCHIVO, 'archivo creció sobre el límite')
            h.update(b)
    return h.hexdigest()


def archivo(ruta):
    s = ruta.stat()
    exigir(stat.S_ISREG(s.st_mode) and s.st_size <= MAX_ARCHIVO, 'archivo no regular o demasiado grande: ' + str(ruta))
    return {'ruta': str(ruta), 'sha256': huella(ruta)}


def leer_manifiesto(ruta):
    try:
        ruta = Path(ruta).resolve(strict=True)
        exigir(stat.S_ISREG(ruta.stat().st_mode), 'manifiesto no regular')
        with ruta.open('rb') as f:
            datos = f.read(MAX_MANIFIESTO + 1)
        exigir(len(datos) <= MAX_MANIFIESTO, 'manifiesto excede 2 MiB')
        m = decodificar(datos)
        campos(m, ('version', 'casos'))
        exigir(type(m['version']) is int and m['version'] == 1, 'versión de manifiesto incompatible')
        exigir(type(m['casos']) is list and len(m['casos']) <= MAX_CASOS, 'lista de casos inválida o excesiva')
        ids = set()
        for c in m['casos']:
            campos(c, ('id', 'categoria', 'referencia', 'candidato', 'modo', 'presupuesto'), ('descripcion', 'procedencia'))
            texto(c['id'], 'id', 96)
            exigir(c['id'] not in ids, 'id duplicado: ' + c['id'])
            ids.add(c['id'])
            exigir(c['categoria'] in (*CATEGORIAS, 'demostracion'), 'categoría desconocida')
            exigir(c['modo'] in ('finita', 'fisica'), 'modo inválido')
            entero(c['presupuesto'], 'presupuesto')
            texto(c.get('descripcion', ''), 'descripcion', vacio=True)
            for lado in ('referencia', 'candidato'):
                campos(c[lado], ('formato', 'ruta'))
                exigir(c[lado]['formato'] in ('mem', 'balb'), 'formato inválido')
                nombre = texto(c[lado]['ruta'], 'ruta')
                # El manifiesto conserva su significado aunque el usuario lance
                # B2 desde otro cwd. resolve también detecta alias de symlinks.
                c[lado].update(archivo((ruta.parent / nombre).resolve(strict=True)))
            proc = c.get('procedencia', {})
            campos(proc, (), ('fuente_bal', 'nivel_referencia', 'nivel_candidato'))
            for nivel in ('nivel_referencia', 'nivel_candidato'):
                if nivel in proc:
                    exigir(proc[nivel] in ('-O0', '-O1', '-O2'), 'nivel declarado inválido')
            if 'fuente_bal' in proc:
                proc['fuente_bal'] = archivo((ruta.parent / texto(proc['fuente_bal'], 'fuente_bal')).resolve(strict=True))
            c['procedencia'] = dict(proc, verificada=False)
        return {'ruta': str(ruta), 'sha256': hashlib.sha256(datos).hexdigest(), 'casos': m['casos']}
    except (OSError, ValueError, TypeError, RecursionError, OverflowError) as e:
        raise ManifiestoInvalido(str(e)) from e


def validar_b1(b, codigo):
    # Se valida el esquema real B1 v1, no otra comparación de GPR, memoria o
    # IPC. Las métricas opcionales conservan null; nunca se completan con cero.
    campos(b, ('version', 'contrato', 'resultado_comparacion', 'motivo', 'referencia', 'candidato',
               'iniciales_compatibles', 'bytes_iniciales_distintos', 'primer_byte_inicial',
               'diferencias_arquitectonicas', 'error', 'diferencias_metricas', 'limites'))
    exigir(type(b['version']) is int and b['version'] == 1 and b['contrato'] == 'estricto-publico', 'contrato/version B1 incompatible')
    # Los retornos 2/3 son resultados válidos, no excepciones del proceso.
    # Solo su desacuerdo con el JSON convierte la respuesta en infraestructura.
    exigir(codigo in RESULTADOS and RESULTADOS[codigo] == b['resultado_comparacion'], 'código de salida y resultado B1 inconsistentes')
    entero(b['motivo'], 'motivo', 6)
    booleano(b['iniciales_compatibles'], 'iniciales_compatibles')
    entero(b['bytes_iniciales_distintos'], 'bytes iniciales', 65536)
    entero(b['primer_byte_inicial'], 'primer byte inicial', 65535)
    for lado in ('referencia', 'candidato'):
        p = b[lado]
        campos(p, ('ruta', 'creada', 'ejecutada', 'estado', 'metricas', 'excepciones'))
        texto(p['ruta'], 'ruta B1')
        booleano(p['creada'], 'creada'); booleano(p['ejecutada'], 'ejecutada')
        if p['estado'] is not None:
            entero(p['estado'], 'estado', 10)
        exigir(p['creada'] == (p['estado'] is not None), 'estado B1 sin sesión correspondiente')
        exigir(not p['ejecutada'] or p['creada'], 'ejecución sin sesión')
        if p['metricas'] is not None:
            campos(p['metricas'], ('ciclos', 'bundles', 'instrucciones', 'ipc', 'pc_diagnostico', 'estado_crypto', 'traps'))
            for k in ('ciclos', 'bundles', 'instrucciones', 'traps'):
                entero(p['metricas'][k], k)
            entero(p['metricas']['pc_diagnostico'], 'pc', (1 << 32) - 1)
            entero(p['metricas']['estado_crypto'], 'crypto', (1 << 32) - 1)
            numero(p['metricas']['ipc'], 'ipc', True)
        exigir(type(p['excepciones']) is list, 'historial inválido')
        for t in p['excepciones']:
            campos(t, ('ciclo', 'pc', 'vector'))
            entero(t['ciclo'], 'ciclo trap'); entero(t['pc'], 'pc trap', (1 << 32) - 1)
            entero(t['vector'], 'vector', (1 << 32) - 1)
    a = b['diferencias_arquitectonicas']
    campos(a, ('comparadas', 'registros', 'primer_registro', 'valores_registro', 'bytes', 'primer_byte',
               'valores_byte', 'crypto', 'cantidad_traps', 'vector', 'primer_vector', 'valores_vector'))
    for k in ('comparadas', 'crypto', 'cantidad_traps', 'vector'):
        booleano(a[k], k)
    for k, mx in (('registros', 32), ('primer_registro', 31), ('bytes', 65536), ('primer_byte', 65535), ('primer_vector', LIMITE_U64)):
        entero(a[k], k, mx)
    for k, mx in (('valores_registro', (1 << 32) - 1), ('valores_byte', 255), ('valores_vector', (1 << 32) - 1)):
        exigir(type(a[k]) is list and len(a[k]) == 2, k + ' inválido')
        for v in a[k]:
            entero(v, k, mx)
    campos(b['error'], ('lado', 'ejecucion', 'metricas'))
    entero(b['error']['lado'], 'lado', 1, -1)
    entero(b['error']['ejecucion'], 'error M2', 6); entero(b['error']['metricas'], 'error M1', 9)
    d = b['diferencias_metricas']
    if d is not None:
        campos(d, ('ciclos', 'bundles', 'instrucciones', 'ipc', 'porcentaje_definido', 'porcentaje_ciclos'))
        for k in ('ciclos', 'bundles', 'instrucciones'):
            campos(d[k], ('negativa', 'magnitud'))
            booleano(d[k]['negativa'], 'signo'); entero(d[k]['magnitud'], 'magnitud')
        numero(d['ipc'], 'delta ipc'); booleano(d['porcentaje_definido'], 'porcentaje_definido')
        if d['porcentaje_definido']:
            numero(d['porcentaje_ciclos'], 'porcentaje')
        else:
            exigir(d['porcentaje_ciclos'] is None, 'porcentaje no definido debe ser null')
        exigir(all(b[l]['metricas'] is not None for l in ('referencia', 'candidato')), 'deltas sin métricas')
    exigir(type(b['limites']) is list and all(type(x) is str for x in b['limites']), 'límites B1 inválidos')
    r = b['resultado_comparacion']
    if r in ('EQUIVALENTE', 'DIFERENTE'):
        exigir(b['iniciales_compatibles'] and a['comparadas'] and d is not None and b['motivo'] == 0 and
               all(b[l]['creada'] and b[l]['ejecutada'] and b[l]['estado'] == 1 for l in ('referencia', 'candidato')),
               'comparación final B1 incompleta')
        diferencias = bool(a['registros'] or a['bytes'] or a['crypto'] or a['cantidad_traps'] or a['vector'])
        exigir(diferencias == (r == 'DIFERENTE'), 'clasificación y diagnóstico B1 inconsistentes')
    elif r == 'INCONCLUSO':
        exigir(not a['comparadas'] and b['motivo'] in (1, 2), 'inconcluso inconsistente')
        if b['motivo'] == 1:
            exigir(not b['iniciales_compatibles'] and d is None and not any(b[l]['ejecutada'] for l in ('referencia', 'candidato')), 'iniciales B1 inconsistentes')
        else:
            exigir(b['iniciales_compatibles'] and d is not None and
                   all(b[l]['estado'] in (1, 2) for l in ('referencia', 'candidato')) and
                   any(b[l]['estado'] == 2 for l in ('referencia', 'candidato')), 'límite B1 inconsistente')
    else:
        exigir(b['motivo'] in (3, 4, 5, 6), 'ERROR B1 sin causa')
    return b


def proceso(args, timeout):
    salidas = [bytearray(), bytearray()]
    motivo = None
    with subprocess.Popen(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, start_new_session=True) as p:
        sel = None
        limite = time.monotonic() + timeout
        try:
            sel = selectors.DefaultSelector()
            for i, f in enumerate((p.stdout, p.stderr)):
                sel.register(f, selectors.EVENT_READ, i)
            while sel.get_map():
                restante = limite - time.monotonic()
                if restante <= 0:
                    motivo = 'timeout del anfitrión'
                    break
                for clave, _ in sel.select(min(restante, 0.1)):
                    b = os.read(clave.fileobj.fileno(), 65536)
                    if not b:
                        sel.unregister(clave.fileobj)
                        continue
                    i = clave.data
                    maximo = MAX_STDOUT if i == 0 else MAX_STDERR
                    espacio = maximo - len(salidas[i])
                    salidas[i].extend(b[:espacio])
                    if len(b) > espacio:
                        motivo = 'salida del comparador excede el límite'
                        break
                if motivo:
                    break
            if not motivo:
                try:
                    p.wait(timeout=max(0.001, limite - time.monotonic()))
                except subprocess.TimeoutExpired:
                    motivo = 'timeout del anfitrión'
        except OSError as e:
            motivo = 'error al capturar salida: ' + str(e)
        finally:
            # El grupo propio permite terminar también hijos que conserven
            # los pipes. Incluso en error se recoge al proceso antes de salir.
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            p.wait()
            if sel is not None:
                sel.close()
    return p.returncode, bytes(salidas[0]), bytes(salidas[1]), motivo


def ejecutar_caso(c, comparador, timeout, disponible):
    args = [comparador['ruta'], c['referencia']['formato'], c['referencia']['ruta'],
            c['candidato']['formato'], c['candidato']['ruta'], c['modo'], str(c['presupuesto']), 'json']
    r = dict(c, resultado=INFRA, intentado=False, proceso_iniciado=False, comando=args, b1=None,
             diagnostico={'codigo': None, 'stderr': '', 'error': None}, rendimiento_valido=False)
    out = err = b''
    try:
        exigir(disponible >= MAX_STDOUT + MAX_STDERR, 'límite acumulado de evidencia del lote')
        for f in (comparador, c['referencia'], c['candidato']):
            exigir(archivo(Path(f['ruta']))['sha256'] == f['sha256'], 'entrada/comparador cambió antes de ejecutar')
        r['intentado'] = True
        codigo, out, err, fallo = proceso(args, timeout)
        r['proceso_iniciado'] = True
        r['diagnostico'].update(codigo=codigo, stderr=err.decode('utf-8', errors='backslashreplace'))
        r['diagnostico']['stdout_sha256'] = hashlib.sha256(out).hexdigest()
        exigir(fallo is None, fallo)
        b = validar_b1(decodificar(out), codigo)
        exigir(all(b[l]['ruta'] == c[l]['ruta'] for l in ('referencia', 'candidato')),
               'B1 identificó archivos distintos de los solicitados')
        for f in (comparador, c['referencia'], c['candidato']):
            exigir(archivo(Path(f['ruta']))['sha256'] == f['sha256'], 'entrada/comparador cambió durante la comparación')
        r['b1'] = b
        r['resultado'] = b['resultado_comparacion']
        # Solo esta certificación completa habilita estadísticas de rendimiento.
        # DIFERENTE/INCONCLUSO conservan sus métricas como datos diagnósticos.
        r['rendimiento_valido'] = r['resultado'] == 'EQUIVALENTE'
    except (OSError, ValueError, TypeError, RecursionError, OverflowError) as e:
        r['diagnostico']['error'] = str(e)
        r['diagnostico']['stdout_no_validado_base64'] = base64.b64encode(out).decode('ascii')
    return r, len(out) + len(err)


def firmado(d):
    # Solo cambia la representación signo/magnitud entregada por B1; no
    # calcula otra diferencia entre las métricas de las dos ejecuciones.
    return -d['magnitud'] if d['negativa'] else d['magnitud']


def agregar(casos):
    conteos = {x: sum(c['resultado'] == x for c in casos) for x in (*RESULTADOS.values(), INFRA)}
    rendimiento = {}
    for modo in ('finita', 'fisica'):
        validos = [c for c in casos if c['rendimiento_valido'] and c['modo'] == modo]
        # Son sumas de mediciones compatibles, no promedios de IPC ni tiempo
        # real. Python conserva enteros exactos al superar el rango uint64_t.
        a = sum(c['b1']['referencia']['metricas']['ciclos'] for c in validos) if validos else None
        b = sum(c['b1']['candidato']['metricas']['ciclos'] for c in validos) if validos else None
        d = sum(firmado(c['b1']['diferencias_metricas']['ciclos']) for c in validos) if validos else None
        rendimiento[modo] = {'casos': len(validos), 'ciclos_referencia': a, 'ciclos_candidato': b,
                             'diferencia_total_ciclos': d, 'reduccion_porcentual_referencia': -100 * d / a if a else None}
    return {'registrados': len(casos), 'ejecutados': sum(c['intentado'] for c in casos),
            'procesos_iniciados': sum(c['proceso_iniciado'] for c in casos),
            'comparables': sum(c['resultado'] in ('EQUIVALENTE', 'DIFERENTE') for c in casos),
            'conteos': conteos, 'rendimiento_validado_por_modo': rendimiento}


def ejecutar_lote(m, comparador, timeout=30, exigir_cobertura=False):
    if type(timeout) not in (int, float) or not math.isfinite(timeout) or not 0 < timeout <= 3600:
        raise InfraestructuraError('timeout debe estar entre cero exclusivo y 3600 segundos')
    try:
        exe = archivo(Path(comparador).resolve(strict=True))
        exigir(os.access(exe['ruta'], os.X_OK), 'comparador no ejecutable')
    except (OSError, ValueError) as e:
        raise InfraestructuraError(str(e)) from e
    resultados = []
    restante = MAX_LOTE
    for c in m['casos']:
        r, usados = ejecutar_caso(c, exe, timeout, restante)
        resultados.append(r)
        restante -= usados
    categorias = {}
    for nombre in (*CATEGORIAS, 'demostracion'):
        grupo = [c for c in resultados if c['categoria'] == nombre]
        a = agregar(grupo)
        if nombre in CATEGORIAS:
            a.update(requeridos=3, faltantes=max(0, 3 - len(grupo)), excedentes=max(0, len(grupo) - 3))
        categorias[nombre] = a
    oficiales = [c for c in resultados if c['categoria'] in CATEGORIAS]
    cobertura = {'requeridos': 15, 'registrados': len(oficiales),
                 'ejecutados': sum(c['intentado'] for c in oficiales),
                 'comparables': sum(c['resultado'] in ('EQUIVALENTE', 'DIFERENTE') for c in oficiales),
                 'equivalentes_estrictos': sum(c['rendimiento_valido'] for c in oficiales),
                 'faltantes': sum(categorias[n]['faltantes'] for n in CATEGORIAS),
                 'completa_registrada': all(categorias[n]['registrados'] == 3 for n in CATEGORIAS),
                 'completa_estricta': all(categorias[n]['registrados'] == 3 and categorias[n]['conteos']['EQUIVALENTE'] == 3 for n in CATEGORIAS),
                 'exigida': exigir_cobertura}
    return {'version': 1, 'contrato': 'estricto-publico',
            'manifiesto': {'ruta': m['ruta'], 'sha256': m['sha256']},
            'comparador': exe, 'timeout_anfitrion_segundos': timeout,
            'cobertura': cobertura, 'resumen': agregar(resultados), 'categorias': categorias,
            'casos': resultados, 'limitaciones': LIMITACIONES}


COLUMNAS = ('id', 'categoria', 'resultado', 'motivo', 'ciclos_referencia', 'ciclos_candidato',
            'diferencia_ciclos', 'ipc_referencia', 'ipc_candidato', 'diferencia_ipc', 'porcentaje_ciclos',
            'instrucciones_referencia', 'instrucciones_candidato', 'diferencia_instrucciones',
            'bundles_referencia', 'bundles_candidato', 'diferencia_bundles', 'rendimiento_valido',
            'referencia', 'candidato', 'sha256_referencia', 'sha256_candidato',
            'nivel_referencia_declarado', 'nivel_candidato_declarado', 'procedencia_verificada', 'descripcion')


def csv_seguro(v):
    # Las comillas CSV no impiden fórmulas en una hoja de cálculo. Solo se
    # prefijan textos peligrosos; los números conservan su signo y valor real.
    if isinstance(v, str) and (v.lstrip().startswith(('=', '+', '-', '@')) or v.startswith(('\t', '\r', '\n'))):
        return "'" + v
    return v


def generar_csv(reporte):
    salida = io.StringIO(newline='')
    w = csv.DictWriter(salida, fieldnames=COLUMNAS, lineterminator='\n')
    w.writeheader()
    for c in reporte['casos']:
        b = c['b1']
        fila = {'id': c['id'], 'categoria': c['categoria'], 'resultado': c['resultado'],
                'motivo': b['motivo'] if b else c['diagnostico']['error'],
                'rendimiento_valido': c['rendimiento_valido'], 'referencia': c['referencia']['ruta'],
                'candidato': c['candidato']['ruta'], 'sha256_referencia': c['referencia']['sha256'],
                'sha256_candidato': c['candidato']['sha256'], 'procedencia_verificada': False,
                'descripcion': c.get('descripcion', '')}
        for lado in ('referencia', 'candidato'):
            fila['nivel_' + lado + '_declarado'] = c['procedencia'].get('nivel_' + lado)
            metricas = b[lado]['metricas'] if b else None
            for k in ('ciclos', 'ipc', 'instrucciones', 'bundles'):
                fila[k + '_' + lado] = metricas[k] if metricas is not None else None
        d = b['diferencias_metricas'] if b else None
        for k in ('ciclos', 'instrucciones', 'bundles'):
            fila['diferencia_' + k] = firmado(d[k]) if d else None
        fila['diferencia_ipc'] = d['ipc'] if d else None
        fila['porcentaje_ciclos'] = d['porcentaje_ciclos'] if d else None
        w.writerow({k: csv_seguro(v) for k, v in fila.items()})
    return salida.getvalue()


def destinos_validos(destinos, protegidos):
    rutas = [Path(p).resolve() for p in destinos]
    exigir(len(set(rutas)) == len(rutas), 'destinos JSON/CSV coinciden')
    for i, p in enumerate(rutas):
        exigir(p.parent.is_dir(), 'directorio de salida inexistente: ' + str(p.parent))
        exigir(not p.exists() or p.is_file(), 'destino no regular')
        for q in [*map(Path, protegidos), *rutas[:i]]:
            exigir(p != q.resolve() and not (p.exists() and q.exists() and os.path.samefile(p, q)), 'destino sobrescribe una entrada u otro reporte')
    return rutas


def publicar(reporte, json_path, csv_path, protegidos):
    publicados = []
    temporales = []
    try:
        destinos = destinos_validos((json_path, csv_path), protegidos)
        contenidos = (json.dumps(reporte, ensure_ascii=True, allow_nan=False, sort_keys=True, indent=2) + '\n', generar_csv(reporte))
        # Se preparan ambos archivos antes de reemplazar. Cada replace es
        # atómico individualmente; un fallo del segundo no revierte el primero.
        for destino, contenido in zip(destinos, contenidos):
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', newline='', dir=destino.parent,
                                             prefix='.b2-', delete=False) as f:
                temporales.append(Path(f.name))
                f.write(contenido); f.flush(); os.fsync(f.fileno())
        for temporal, destino in zip(temporales, destinos):
            os.replace(temporal, destino)
            publicados.append(str(destino))
    except (OSError, ValueError, TypeError) as e:
        raise PublicacionError(str(e), publicados) from e
    finally:
        for t in temporales:
            t.unlink(missing_ok=True)
    return publicados


def codigo_lote(r):
    # Un error tiene prioridad sobre cobertura o comparaciones válidas. Una
    # ejecución equivalente no puede ocultar que otro caso del lote falló.
    c = r['resumen']['conteos']
    if c[INFRA]:
        return 1
    if c['ERROR']:
        return 6
    if r['cobertura']['exigida'] and not r['cobertura']['completa_registrada']:
        return 5
    if c['DIFERENTE']:
        return 2
    if c['INCONCLUSO']:
        return 3
    return 0


class Argumentos(argparse.ArgumentParser):
    def error(self, message):
        raise ValueError(message)


def main(argv=None):
    p = Argumentos(description='Benchmarks VLIW mediante el comparador público B1')
    for nombre in ('manifest', 'comparador', 'json', 'csv'):
        p.add_argument('--' + nombre, required=True)
    p.add_argument('--timeout', type=float, default=30)
    p.add_argument('--exigir-cobertura', action='store_true')
    try:
        a = p.parse_args(argv)
        exigir(math.isfinite(a.timeout) and 0 < a.timeout <= 3600, 'timeout inválido')
    except ValueError as e:
        print('argumentos: ' + str(e), file=sys.stderr)
        return 64
    try:
        m = leer_manifiesto(a.manifest)
    except ManifiestoInvalido as e:
        print('manifiesto: ' + str(e), file=sys.stderr)
        return 4
    try:
        protegidos = [m['ruta'], a.comparador]
        for c in m['casos']:
            protegidos += [c[l]['ruta'] for l in ('referencia', 'candidato')]
            if 'fuente_bal' in c['procedencia']:
                protegidos.append(c['procedencia']['fuente_bal']['ruta'])
        destinos_validos((a.json, a.csv), protegidos)
        r = ejecutar_lote(m, a.comparador, a.timeout, a.exigir_cobertura)
        publicar(r, a.json, a.csv, protegidos)
    except (OSError, ValueError) as e:
        print('infraestructura: ' + str(e), file=sys.stderr)
        if isinstance(e, PublicacionError):
            print('Archivos publicados antes del error: ' + repr(e.publicados), file=sys.stderr)
        return 1
    print('Benchmarks VLIW')
    print('Casos registrados:', r['resumen']['registrados'], '| Intentados:', r['resumen']['ejecutados'])
    for estado, cantidad in r['resumen']['conteos'].items():
        print(estado + ':', cantidad)
    c = r['cobertura']
    print('Cobertura oficial registrada:', c['registrados'], '/ 15; equivalentes estrictos:', c['equivalentes_estrictos'], '; faltantes:', c['faltantes'])
    print('Renombramiento: contrato de 32 GPR físicos; DIFERENTE no confirma un defecto semántico.')
    print('JSON:', a.json, '| CSV:', a.csv)
    return codigo_lote(r)


if __name__ == '__main__':
    sys.exit(main())
