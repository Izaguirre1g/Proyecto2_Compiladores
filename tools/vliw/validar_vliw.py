#!/usr/bin/env python3
"""Reconstruye y prueba la contribución VLIW en una copia temporal del proyecto."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
# Evita crear __pycache__ al importar el resolvedor desde el árbol original.
sys.dont_write_bytecode = True
from rutas_vliw import top_rtl

ROOT = Path(__file__).resolve().parents[2]


def omitir_generados(carpeta, nombres):
    """Excluye Git, cachés y ejecutables anteriores, sin tocar sus originales."""
    omitidos = set(nombres) & {'.git', '__pycache__', 'build', 'balc', 'balc-ide', 'balc-ll1'}
    # Cada suite se ejecuta sobre la copia con las herramientas reales. El Makefile general solo
    # se añade a la pasada completa normal; los modos con sanitizadores instrumentan los
    # ejecutables propios que compilan las suites.
    for nombre in nombres:
        ruta = Path(carpeta) / nombre
        if ruta.suffix == '.o':
            omitidos.add(nombre)
        elif ruta.is_file() and not ruta.suffix:
            with ruta.open('rb') as archivo:
                if archivo.read(4) == b'\x7fELF':
                    omitidos.add(nombre)
    return omitidos


def ejecutar(orden, copia, entorno):
    """Ejecuta una suite real; conserva código, métodos y omisiones reportadas."""
    inicio = time.monotonic()
    proceso = subprocess.run(orden, cwd=copia, env=entorno, capture_output=True, text=True)
    texto = proceso.stdout + proceso.stderr
    # Interpretamos el resumen unittest real y conservamos stdout y stderr completos como
    # evidencia. Un código cero con omisiones se informa SKIP, porque no demuestra todas las
    # dependencias solicitadas.
    metodos = re.findall(r'Ran (\d+) tests? in', texto)
    omitidos = re.findall(r'skipped=(\d+)' , texto)
    resultado = dict(comando=orden, codigo=proceso.returncode,
                     segundos=round(time.monotonic() - inicio, 3),
                     metodos=int(metodos[-1]) if metodos else None,
                     skip=int(omitidos[-1]) if omitidos else 0, salida=texto)
    estado = 'FAIL' if proceso.returncode else ('SKIP' if resultado['skip'] else 'PASS')
    print(f"{estado}: {' '.join(orden)}; salida={proceso.returncode}; "
          f"métodos={resultado['metodos']}; SKIP={resultado['skip']}", flush=True)
    if proceso.returncode:
        print(texto, end='')
    return resultado


def main():
    """Selecciona suites y dependencias; devuelve error si falla o se omite algo."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rapida', action='store_true', help='S1, S7 y S8-C; no sustituye la regresión completa')
    parser.add_argument('--arquitectura', type=Path, default=ROOT.parent / 'Proyecto2_Arquitectura1')
    parser.add_argument('--sanitizador', choices=('address', 'undefined', 'address,undefined'))
    parser.add_argument('--fugas', action='store_true', help='activa LSan al usar AddressSanitizer')
    parser.add_argument('--evidencia', type=Path, help='JSON opcional fuera del repositorio')
    args = parser.parse_args()
    if args.evidencia and args.evidencia.resolve().is_relative_to(ROOT):
        parser.error('la evidencia debe guardarse fuera del repositorio')
    arquitectura = args.arquitectura.resolve()
    if not top_rtl(arquitectura).is_file():
        print('RTL BLOQUEADO: se requiere top en rtl/ o rtl-p2/; '
              'las suites independientes continuarán.', flush=True)
    nombres = sorted(p.name for p in (ROOT / 'tests/vliw').glob('test_*.py'))
    if args.rapida:
        nombres = [n for n in nombres if n in ('test_sim_cargador.py', 'test_vliw_scheduler.py', 'test_vliw_visualizador.py')]
    if not nombres:
        parser.error('no se encontraron las suites VLIW')
    registros = []
    # La raíz copiada conserva fuentes propias y compartidas. Ninguna suite
    # regenera lexer/parser ni binarios en el repositorio que invocó el script.
    with tempfile.TemporaryDirectory(prefix='validacion-vliw-') as temporal:
        copia = Path(temporal) / 'codigo'
        shutil.copytree(ROOT, copia, ignore=omitir_generados)
        entorno = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ARQUITECTURA_ROOT=str(arquitectura))
        compilacion = ejecutar(['make', '-B', 'balc'], copia, entorno)
        registros.append(compilacion)
        if compilacion['codigo'] == 0:
            if args.sanitizador:
                # Los sanitizadores instrumentan las bibliotecas y sondas
                # propias compiladas por cada suite. balc ya se reconstruyó.
                entorno['CC'] = os.environ.get('CC', 'cc') + (
                    ' -g -fno-omit-frame-pointer -fno-pie -no-pie -fsanitize=' + args.sanitizador)
                entorno['ASAN_OPTIONS'] = 'detect_leaks=' + ('1' if args.fugas else '0') + ':halt_on_error=1'
                entorno['UBSAN_OPTIONS'] = 'halt_on_error=1:print_stacktrace=1'
            for clave in ('B3', 'B4', 'B5'):
                entorno[clave + '_BALC'] = str(copia / 'balc')
            for nombre in nombres:
                registros.append(ejecutar([sys.executable, 'tests/vliw/' + nombre, '-v'], copia, entorno))
            if not args.rapida and not args.sanitizador:
                registros.append(ejecutar(['make', '-B', '-k', 'test'], copia, entorno))
    # Un SKIP no acredita contraste RTL ni una regresión completa exitosa.
    fallos = sum(r['codigo'] != 0 for r in registros)
    omitidos = sum(r['skip'] for r in registros)
    print(f"Resumen: {len(registros)-fallos} comandos sin error; FAIL={fallos}; SKIP={omitidos}")
    if args.evidencia:
        args.evidencia.write_text(json.dumps(registros, ensure_ascii=False, indent=2) + '\n')
    return 1 if fallos else 2 if omitidos else 0


if __name__ == '__main__':
    raise SystemExit(main())
