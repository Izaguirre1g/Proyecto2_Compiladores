"""Construye un puente físico independiente sin Flex/Bison, y lo ejecuta.

No usa fixtures que escriban máscaras, DAG ni bundles a mano: ejercita
las APIs reales del proyecto con C11 estricto y una IR que fuerza spilling.
"""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    'tests/test_ir_vliw.c', 'src/ir_vliw.c', 'src/ir_spilling.c',
    'src/ir_spilling_marco.c', 'src/ir_spilling_backend.c',
    'src/Generacion_Codigo/funciones_marco.c',
    'src/Representacion_Intermedia/ir.c', 'src/Analisis_Sintactico/ast.c',
    'src/Calendarizacion_VLIW/vliw_proyeccion_fisica.c',
    'src/Calendarizacion_VLIW/vliw_dag_fisico.c',
    'src/Calendarizacion_VLIW/vliw_cfg_fisico.c',
    'src/Calendarizacion_VLIW/vliw_metadatos.c',
    'src/Simulador_VLIW/sim_decode.c',
    'src/Calendarizacion_VLIW/vliw_integracion.c',
    'src/Codificacion_Binaria/codificacion_binaria.c',
    'src/Calendarizacion_VLIW/vliw_cadena_fisica.c',
    'src/Calendarizacion_VLIW/vliw_scheduler.c',
    'src/Calendarizacion_VLIW/vliw_emisor.c',
    'src/Calendarizacion_VLIW/vliw_visualizador.c',
]


def main():
    with tempfile.TemporaryDirectory(prefix='ir-spilling-vliw-') as temp:
        exe = Path(temp) / 'prueba'
        cmd = [os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
               '-Wpedantic', '-Werror', '-O2', '-Iinclude',
               '-Isrc/Analisis_Sintactico', *SOURCES, '-o', str(exe)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        subprocess.run([str(exe)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
