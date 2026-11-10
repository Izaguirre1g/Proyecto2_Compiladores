"""Valida BAL → balc → BALB/MEM → M2/M1 sin alterar los componentes existentes."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import struct
import sys
import tempfile
import time

# B2 ya resuelve JSON estricto y procesos POSIX con captura y timeout acotados.
# Se importa desde una ruta fija, nunca desde un manifiesto o programa BAL.
sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location('transporte_b2', Path(__file__).with_name('benchmarks_vliw.py'))
b2 = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(b2)
MAX_BYTES = 2 * 1024 * 1024
LIMITACIONES = [
    'La terminación y la corrección semántica son comprobaciones distintas.',
    'El IPC se conserva de M1; no se recalcula.',
    'La huella DMEM no demuestra igualdad byte a byte; esa comparación corresponde a B1.',
    'La CLI M2 no exporta DMEM completa ni métricas parciales tras errores de ejecución.',
    'Los oráculos declarados deben provenir de especificaciones independientes; B3 no certifica su autoría.',
    'Los pares BALB/MEM son demostraciones de formatos, nunca evidencia de optimización.',
    'El tiempo de compilación es una observación de pared, no una estadística ni ciclos VLIW.',
]


def leer(ruta, limite=MAX_BYTES):
    # No se leen dispositivos/FIFO ni se sigue un artefacto que escape mediante
    # symlink. Los límites preceden a la reserva y se comprueban tras la lectura.
    b2.exigir(not ruta.is_symlink() and ruta.is_file(), 'archivo regular requerido: '+str(ruta))
    b2.exigir(ruta.stat().st_size <= limite, 'archivo demasiado grande')
    with ruta.open('rb') as f:
        datos = f.read(limite + 1)
    b2.exigir(len(datos) <= limite, 'archivo creció sobre el límite')
    return datos


def identidad(ruta):
    datos = leer(ruta, b2.MAX_ARCHIVO)
    return dict(ruta=str(ruta), sha256=hashlib.sha256(datos).hexdigest(), bytes=len(datos))


def validar_formato(binario, mem):
    """Contrasta estructura y representación; S1 sigue siendo el cargador real."""
    raw, texto = leer(binario), leer(mem)
    b2.exigir(len(raw) >= 48, 'BALB truncado antes del encabezado')
    firma, version, cabecera, total, entrada, co, cs, ca, do, ds, da, flags = struct.unpack_from('<4s11I', raw)
    b2.exigir((firma, version, cabecera) == (b'BALB', 1, 48), 'firma, versión o encabezado BALB inválido')
    b2.exigir(total == len(raw) and co == 48 and cs > 0 and cs % 16 == 0,
              'tamaños BALB incompatibles')
    b2.exigir(do == co + cs and do + ds == total and cs <= 16384 and ds <= 16384,
              'secciones BALB truncadas o fuera de límites')
    b2.exigir(entrada == ca == 0 and da <= 65536 and ds <= 65536-da and flags in (0, 1),
              'direcciones o flags BALB incompatibles con S1')
    lineas = [s.strip() for s in texto.decode('ascii').splitlines() if s.strip()]
    b2.exigir(bool(lineas) and len(lineas) <= 1024 and
              all(re.fullmatch('[0-9a-fA-F]{32}', s) for s in lineas), 'MEM inválido')
    # MEM imprime el entero de 128 bits con CRIPTO primero. BALB guarda ALU
    # primero en little-endian; convertir el entero evita invertir solo slots.
    codigo_mem = b''.join(int(s, 16).to_bytes(16, 'little') for s in lineas)
    b2.exigir(raw[co:do] == codigo_mem, 'código BALB y MEM diferente')
    # MEM inicia DMEM en cero. Se compara la sección completa, no su hash;
    # que el arranque vuelva a escribirla no iguala las condiciones iniciales.
    return dict(disponible=True, valido=True, codigo_bytes=cs, bundles=cs//16,
                datos_bytes=ds, base_datos=da, flags=flags,
                codigo_correspondiente=True, iniciales_compatibles=not any(raw[do:]))


def validar_metricas(m, codigo, presupuesto):
    b2.exigir(type(m) is dict and type(m.get('version')) is int and m['version'] == 1 and
              m.get('naturaleza') == 'dinamica', 'contrato M1 incompatible')
    b2.exigir(codigo in (0, 2) and m.get('estado') == ('TERMINADO' if codigo == 0 else 'LIMITE'),
              'estado M1 y retorno M2 inconsistentes')
    for k in ('ciclos', 'completados', 'instrucciones_utiles', 'traps'):
        b2.entero(m.get(k), k)
    b2.exigir(m['ciclos'] <= presupuesto, 'M2 excedió el presupuesto')
    b2.numero(m.get('ipc'), 'IPC', True)
    a = m.get('arquitectura')
    b2.exigir(type(a) is dict, 'arquitectura M1 ausente')
    for k in ('pc', 'estado_crypto', 'trap_count32', 'trap_pc'):
        b2.entero(a.get(k), k, (1 << 32)-1)
    b2.exigir(type(a.get('registros')) is list and len(a['registros']) == 32, 'GPR inválidos')
    for v in a['registros']:
        b2.entero(v, 'GPR', (1 << 32)-1)
    b2.exigir(type(a.get('dmem_fnv1a64')) is str and
              re.fullmatch('[0-9a-f]{16}', a['dmem_fnv1a64']), 'huella DMEM inválida')
    ts = m.get('excepciones')
    b2.exigir(type(ts) is list and len(ts) == m['traps'], 'historial M1 incompleto')
    anterior = 0
    for t in ts:
        b2.campos(t, ('ciclo', 'pc', 'vector'))
        b2.entero(t['ciclo'], 'ciclo trap', m['ciclos'], anterior)
        anterior = t['ciclo']
        for k in ('pc', 'vector'):
            b2.entero(t[k], k, (1 << 32)-1)
    return m


def cargar_oraculos(ruta):
    if ruta is None:
        return {}
    o = b2.decodificar(leer(ruta))
    b2.campos(o, ('version', 'fuentes'))
    b2.exigir(type(o['version']) is int and o['version'] == 1 and type(o['fuentes']) is dict,
              'contrato de oráculos inválido')
    for sha, c in o['fuentes'].items():
        b2.exigir(re.fullmatch('[0-9a-f]{64}', sha), 'SHA fuente inválido')
        b2.campos(c, ('procedencia', 'alcance', 'registros'), ('estado_crypto', 'traps'))
        b2.texto(c['procedencia'], 'procedencia'); b2.texto(c['alcance'], 'alcance')
        b2.exigir(type(c['registros']) is dict, 'registros esperados inválidos')
        for k, v in c['registros'].items():
            b2.exigir(re.fullmatch('0|[1-9]|[12][0-9]|3[01]', k), 'índice GPR inválido')
            b2.entero(v, 'valor GPR', (1 << 32)-1)
        b2.exigir(c['registros'] or 'estado_crypto' in c or 'traps' in c, 'oráculo vacío')
        for k in ('estado_crypto', 'traps'):
            if k in c:
                b2.entero(c[k], k, (1 << 32)-1 if k == 'estado_crypto' else b2.LIMITE_U64)
    return o['fuentes']


def comprobar_oraculo(m, contrato):
    # No se deriva ningún esperado del resultado ejecutado. El SHA vincula el
    # contrato externo al fuente exacto; alcance limita lo que se ha demostrado.
    if contrato is None:
        return dict(estado='ORACULO_NO_DISPONIBLE', disponible=False)
    fallos = []
    for k, esperado in contrato['registros'].items():
        if m['arquitectura']['registros'][int(k)] != esperado:
            fallos.append('x'+k)
    for k in ('estado_crypto', 'traps'):
        if k in contrato and (m['arquitectura'][k] if k == 'estado_crypto' else m[k]) != contrato[k]:
            fallos.append(k)
    return dict(disponible=True, estado='ORACULO_FALLIDO' if fallos else 'ORACULO_SATISFECHO',
                contrato=contrato, diferencias=fallos)


def invocar(args, timeout, directorio, nombre, anfitrion):
    inicio = time.monotonic()
    # El cwd también es propio: una salida auxiliar relativa o un volcado del
    # proceso externo no debe terminar en el directorio desde el que se lanzó B3.
    anterior = Path.cwd()
    try:
        os.chdir(directorio)
        codigo, out, err, motivo = b2.proceso(list(map(str, args)), timeout)
    finally:
        os.chdir(anterior)
    # Bytes originales y comandos exactos se conservan aunque no haya JSON o
    # el proceso falle. Rutas temporales y tiempo variable van fuera del caso.
    (directorio/(nombre+'.stdout')).write_bytes(out)
    (directorio/(nombre+'.stderr')).write_bytes(err)
    anfitrion.append(dict(comando=list(map(str, args)), cwd=str(directorio),
                          codigo=codigo, motivo=motivo,
                          segundos_pared=time.monotonic()-inicio,
                          stdout=nombre+'.stdout', stderr=nombre+'.stderr'))
    return codigo, out, err, motivo


def copiar_fuentes(fuente, destino):
    # bring resuelve módulos hermanos. Copiar todos los .bal de esa carpeta
    # conserva dependencias sin duplicar el parser ni tocar los originales.
    # No se recorren padres ni symlinks; importaciones anidadas también son
    # nombres hermanos en el contrato actual de semantic_modules.
    destino.mkdir()
    inventario = []
    total = 0
    for p in sorted(fuente.parent.iterdir()):
        if p.suffix != '.bal':
            continue
        b2.exigir(len(inventario) < 256, 'demasiadas dependencias locales')
        datos = leer(p)
        total += len(datos)
        b2.exigir(total <= 16*1024*1024, 'fuentes locales exceden 16 MiB')
        (destino/p.name).write_bytes(datos)
        inventario.append(dict(nombre=p.name, sha256=hashlib.sha256(datos).hexdigest()))
    return inventario


def caso(fuente, raiz, id_, config, oraculos, anfitrion):
    d = raiz/id_; d.mkdir()
    c = dict(id=id_, fuente=identidad(fuente), compilacion=dict(disponible=False),
             artefactos={}, validacion_formato=dict(disponible=False), ejecucion={},
             metricas={}, oraculo={}, resultado='ERROR_INFRAESTRUCTURA', limitaciones=LIMITACIONES)
    host = []; anfitrion[id_] = host
    try:
        c['fuente']['dependencias'] = copiar_fuentes(fuente, d/'fuentes')
        # Solo esta copia puede recibir salidas auxiliares de -s. Cada caso
        # empieza vacío; nunca se reutilizan binarios de otra compilación.
        p = d/'programa.bin'; mem = d/'programa.mem'
        ret, _, _, motivo = invocar([config.balc, '-s', '-o', p, d/'fuentes'/fuente.name],
                                    config.timeout, d, 'compilador', host)
        c['compilacion'] = dict(disponible=True, codigo=ret, estado='COMPILACION_FALLIDA')
        if motivo:
            c['diagnostico'] = motivo
            return c
        if ret != 0:
            c['resultado'] = 'COMPILACION_FALLIDA'
            return c
        for nombre, ruta in (('balb', p), ('mem', mem)):
            if ruta.exists():
                c['artefactos'][nombre] = dict(identidad(ruta), ruta=id_+'/'+ruta.name)
        if len(c['artefactos']) != 2:
            c['compilacion']['estado'] = 'SALIDA_INCOMPLETA'
            c['resultado'] = 'FORMATO_INVALIDO'
            return c
        try:
            c['validacion_formato'] = validar_formato(p, mem)
        except (ValueError, UnicodeError) as e:
            c['compilacion']['estado'] = 'SALIDA_INVALIDA'
            c['validacion_formato'] = dict(disponible=True, valido=False, diagnostico=str(e))
            c['resultado'] = 'FORMATO_INVALIDO'
            return c
        c['compilacion']['estado'] = 'COMPILACION_EXITOSA'
        for nombre, ruta in (('balb', p), ('mem', mem)):
            ret, out, err, motivo = invocar([config.simulador, nombre, ruta, 'finita',
                                            str(config.presupuesto), 'json'], config.timeout, d, nombre, host)
            estado = dict(disponible=True, codigo=ret, estado='ERROR', carga='NO_VERIFICADA')
            c['ejecucion'][nombre] = estado
            c['metricas'][nombre] = None
            c['oraculo'][nombre] = dict(disponible=False, estado='ORACULO_NO_DISPONIBLE')
            if motivo:
                estado['diagnostico'] = motivo
                estado['infraestructura'] = True
                continue
            if ret == 1:
                # La CLI actual separa por su diagnóstico textual la ejecución
                # posterior a cargar. No existe código público distinto de carga.
                estado['carga'] = 'PASS' if err.startswith(b'ejecucion:') else 'FAIL_O_NO_OBSERVABLE'
                continue
            try:
                m = validar_metricas(b2.decodificar(out), ret, config.presupuesto)
            except (ValueError, TypeError, KeyError, OverflowError) as e:
                estado.update(infraestructura=True, diagnostico=str(e))
                continue
            c['metricas'][nombre] = m
            estado.update(estado=m['estado'], carga='PASS')
            c['oraculo'][nombre] = comprobar_oraculo(m, oraculos.get(c['fuente']['sha256']))
        estados = list(c['ejecucion'].values())
        if any(e.get('infraestructura') for e in estados):
            c['resultado'] = 'ERROR_INFRAESTRUCTURA'
        elif any(e['estado'] == 'ERROR' for e in estados):
            c['resultado'] = 'EJECUCION_FALLIDA'
        elif any(e['estado'] == 'LIMITE' for e in estados):
            c['resultado'] = 'NO_TERMINADO'
        elif any(o['estado'] == 'ORACULO_FALLIDO' for o in c['oraculo'].values()):
            c['resultado'] = 'ORACULO_FALLIDO'
        else:
            c['resultado'] = ('VALIDADO' if all(o['disponible'] for o in c['oraculo'].values())
                              else 'VALIDADO_SIN_ORACULO')
        # Detecta cambios de fuentes durante el recorrido, sin restaurarlas ni
        # borrar cambios ajenos. No constituye un bloqueo ante carreras externas.
        b2.exigir(identidad(fuente) == {k: c['fuente'][k] for k in ('ruta', 'sha256', 'bytes')},
                  'el fuente cambió durante la validación')
    except (OSError, ValueError, OverflowError) as e:
        c.update(resultado='ERROR_INFRAESTRUCTURA', diagnostico=str(e))
    return c


def publicar(config):
    fuentes = [p.resolve(strict=True) for p in config.fuente]
    b2.exigir(1 <= len(fuentes) <= 64 and len(set(fuentes)) == len(fuentes), 'fuentes repetidas o cantidad inválida')
    for p in fuentes:
        b2.exigir(p.suffix == '.bal', 'se requiere extensión .bal')
        identidad(p)
    herramientas = {}
    for k in ('balc', 'simulador'):
        p = getattr(config, k).resolve(strict=True)
        herramientas[k] = identidad(p)
        b2.exigir(os.access(p, os.X_OK), 'ejecutable sin permiso: '+str(p))
        setattr(config, k, p)
    oraculos = cargar_oraculos(config.oraculos)
    salida = config.salida.absolute()
    # Solo se publica un directorio nuevo. No se borra ni reemplaza un destino
    # existente y no se usa una carpeta de fuentes como área de preparación.
    b2.exigir(not salida.exists() and not salida.is_symlink(), 'salida ya existe')
    padre = salida.parent.resolve(strict=True)
    b2.exigir(padre.is_dir(), 'padre de salida inválido')
    for p in fuentes:
        b2.exigir(not padre.is_relative_to(p.parent), 'salida dentro de carpeta fuente')
    anfitrion = {}
    # La carpeta temporal es propia y externa a las fuentes. Su limpieza no
    # afecta directorios ajenos; renombrarla publica evidencia y reporte juntos.
    with tempfile.TemporaryDirectory(prefix='.b3-', dir=padre) as temporal:
        raiz = Path(temporal)/'evidencia'; raiz.mkdir()
        casos = [caso(p, raiz, 'caso-%03d'%i, config, oraculos, anfitrion)
                 for i, p in enumerate(fuentes, 1)]
        for k, original in herramientas.items():
            b2.exigir(identidad(getattr(config, k)) == original, 'herramienta cambió durante la ejecución')
        pares = []
        for c in casos:
            if c['validacion_formato'].get('iniciales_compatibles') and c['compilacion']['estado'] == 'COMPILACION_EXITOSA':
                pares.append(dict(id=c['id'], categoria='demostracion',
                                  descripcion='BALB/MEM: interoperabilidad; sin optimización ni oráculo semántico',
                                  referencia=dict(formato='balb', ruta=c['artefactos']['balb']['ruta']),
                                  candidato=dict(formato='mem', ruta=c['artefactos']['mem']['ruta']),
                                  modo='finita', presupuesto=config.presupuesto))
        reporte = dict(version=1, herramientas=herramientas, presupuesto=config.presupuesto,
                       casos=casos, limitaciones=LIMITACIONES,
                       b1_b2=dict(manifiesto='manifiesto-b2.json' if pares else None,
                                  pares_demostracion=len(pares), optimizaciones='PENDIENTES'))
        # Las salidas de procesos tienen bytes exactos en archivos separados.
        # El reporte estable excluye duraciones, comandos con temporales y stdout.
        for nombre, datos in (('reporte.json', reporte), ('anfitrion.json', anfitrion),
                              ('manifiesto-b2.json', dict(version=1, casos=pares))):
            with (raiz/nombre).open('x', encoding='utf-8') as f:
                json.dump(datos, f, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False)
                f.write('\n'); f.flush(); os.fsync(f.fileno())
        # rename no reemplaza una carpeta poblada. Se vuelve a exigir ausencia
        # para detectar destinos creados durante la compilación (sin limpieza ajena).
        b2.exigir(not salida.exists() and not salida.is_symlink(), 'salida apareció durante la ejecución')
        os.rename(raiz, salida)
    return reporte


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    for nombre in ('balc', 'simulador', 'salida'):
        p.add_argument('--'+nombre, type=Path, required=True)
    p.add_argument('--fuente', type=Path, action='append', required=True)
    p.add_argument('--presupuesto', required=True)
    p.add_argument('--timeout', type=float, default=30)
    p.add_argument('--oraculos', type=Path)
    try:
        a = p.parse_args(argv)
        if not re.fullmatch('[0-9]{1,20}', a.presupuesto):
            p.error('presupuesto debe ser uint64 decimal')
        a.presupuesto = int(a.presupuesto)
        if a.presupuesto > b2.LIMITE_U64 or not math.isfinite(a.timeout) or not 0 < a.timeout <= 3600:
            p.error('presupuesto o timeout fuera de rango')
    except SystemExit as e:
        return 0 if e.code == 0 else 64
    try:
        r = publicar(a)
        print(str(a.salida.absolute()/'reporte.json'))
        resultados = {c['resultado'] for c in r['casos']}
        return (1 if 'ERROR_INFRAESTRUCTURA' in resultados else
                2 if resultados - {'VALIDADO', 'VALIDADO_SIN_ORACULO'} else 0)
    except (OSError, ValueError, OverflowError, RecursionError) as e:
        print('validacion BAL: '+str(e), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
