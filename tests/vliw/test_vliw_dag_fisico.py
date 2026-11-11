"""Prueba DAG físicos independientes mediante fixtures C y consumidores reales.

Compila con advertencias estrictas en temporales externos. Conserva códigos,
tiempos y conteos reales; no modifica Makefile ni utiliza un productor BAL.
"""
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROPIAS = ['src/Calendarizacion_VLIW/vliw_dag_fisico.c',
           'src/Calendarizacion_VLIW/vliw_cfg_fisico.c',
           'src/Calendarizacion_VLIW/vliw_metadatos.c',
           'src/Simulador_VLIW/sim_decode.c']
FUENTES = PROPIAS + ['tests/vliw/test_vliw_dag_fisico.c',
    'src/Calendarizacion_VLIW/vliw_integracion.c',
    'src/Calendarizacion_VLIW/vliw_scheduler.c',
    'src/Calendarizacion_VLIW/vliw_emisor.c',
    'src/Codificacion_Binaria/codificacion_binaria.c']
MINIMO = r'''/* Enlace aislado: no requiere CB/S7 ni backend para construir el vacío. */
#include "vliw_dag_fisico.h"
int main(void) {
    VliwCfgEntrada f={0}; f.fronteras_acreditadas=true;
    VliwCfg *cfg=0;
    if(vliw_cfg_construir(&f,&cfg,0)!=VLIW_CFG_OK)return 1;
    VliwDagEntrada e={.flujo=&f,.cfg=cfg}; VliwDag *g=0;
    if(vliw_dag_construir(&e,&g,0)!=VLIW_DAG_OK)return 2;
    if(g->cantidad_dependencias || g->cantidad_operaciones)return 3;
    vliw_cfg_liberar(&cfg); vliw_dag_liberar(&g);return g!=0;
}
'''


class DagFisicoTests(unittest.TestCase):
    # Compila producción mínima y fixtures completos. CC admite instrumentar
    # todas las fuentes enlazadas; ninguna salida se escribe en el repositorio.
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='dag-fisico-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.registros = []
        cls.escenarios = cls.comprobaciones = 0
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-Iinclude']
        cls.normal, cls.recursos, cls.minimo = [cls.dir / n for n in ('normal', 'recursos', 'minimo')]
        fuente = cls.dir / 'minimo.c'
        fuente.write_text(MINIMO)
        cls.ejecutar(cc + flags + PROPIAS + [str(fuente), '-o', str(cls.minimo)])
        cls.ejecutar(cc + flags + FUENTES + ['-o', str(cls.normal)])
        cls.ejecutar(cc + flags + ['-DDAG_FALLOS'] + FUENTES +
                     ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-Wl,--wrap=realloc', '-o', str(cls.recursos)])

    # Un error conserva comando, salida y duración, y falla la prueba. No se
    # transforma un rechazo inesperado en SKIP ni se acepta evidencia histórica.
    @classmethod
    def ejecutar(cls, comando):
        inicio = time.monotonic()
        p = subprocess.run(list(map(str, comando)), cwd=ROOT, capture_output=True,
                           text=True, timeout=240)
        cls.registros.append(dict(comando=list(map(str, comando)), codigo=p.returncode,
            segundos=time.monotonic() - inicio, stdout=p.stdout, stderr=p.stderr))
        if p.returncode:
            raise AssertionError(f'{comando}: código {p.returncode}\n{p.stdout}\n{p.stderr}')
        return p.stdout

    # Exige escenarios no vacíos y suma lo que el driver realmente ejecutó.
    # Los oráculos y el cierre independiente están en C, no en estos conteos.
    def grupo(self, nombre, recursos=False):
        texto = self.ejecutar([self.recursos if recursos else self.normal, nombre])
        m = re.search(r'PASS ' + nombre + r': escenarios=(\d+); comprobaciones=(\d+)', texto)
        self.assertIsNotNone(m, texto)
        escenarios, comprobaciones = map(int, m.groups())
        self.assertGreater(escenarios, 0)
        self.assertGreater(comprobaciones, escenarios)
        type(self).escenarios += escenarios
        type(self).comprobaciones += comprobaciones
        print(texto, end='')

    # La API aislada construye y libera el vacío sin enlazar consumidores S7.
    def test_00_enlace_minimo(self):
        self.ejecutar([self.minimo])

    # Independencia y x0 impiden relaciones falsas o inferencias ABI por x1.
    def test_01_basicos(self):
        self.grupo('basicos')

    # RAW N+3 y escrituras intermedias contrastan cobertura global entre bloques.
    def test_02_registros(self):
        self.grupo('registros')

    # Memoria conserva todos los pares LSU, incluidos load/load y offsets disjuntos.
    def test_03_memoria(self):
        self.grupo('memoria')

    # Un camino de peso dos no satisface RAW3; los backedges permanecen en CFG.
    def test_04_control(self):
        self.grupo('control')

    # CRIPTO, Feistel dual y efectos globales acreditados deben conservar barrera3.
    def test_05_barreras(self):
        self.grupo('barreras')

    # Call/ret explícitos requieren crédito ABI sin inventar máscaras del callee.
    def test_06_abi(self):
        self.grupo('abi')

    # Mutaciones aisladas ejercitan coherencia con S2, CFG y generación vigente.
    def test_07_rechazos(self):
        self.grupo('rechazos')

    # Salida ocupada y cantidades absurdas se rechazan antes de recorrer memoria.
    def test_08_argumentos(self):
        self.grupo('argumentos')

    # Floyd independiente verifica aristas y cobertura en cien flujos deterministas.
    def test_09_combinaciones(self):
        self.grupo('combinaciones')

    # Proyección de fixtures completos usa las aristas generadas sin reemplazarlas.
    def test_10_integracion(self):
        self.grupo('integracion')

    # Máximo estructural y camino imposible separan capacidad DAG y plan final.
    def test_11_limites(self):
        self.grupo('limites')

    # Fallar cada reserva prueba limpieza parcial, inmutabilidad y recuperación.
    def test_12_recursos(self):
        self.grupo('recursos', recursos=True)

    # Exporta evidencia opcional fuera del proyecto. Los recursos de compilación
    # pertenecen al temporal de clase y se eliminan incluso si una prueba falla.
    @classmethod
    def tearDownClass(cls):
        print(f'DAG: {cls.escenarios} escenarios C; {cls.comprobaciones} comprobaciones C.')
        if os.environ.get('I1DAG_EVIDENCIA'):
            p = Path(os.environ['I1DAG_EVIDENCIA']).resolve()
            if p.is_relative_to(ROOT):
                raise AssertionError('la evidencia debe estar fuera del repositorio')
            p.write_text(json.dumps(dict(escenarios=cls.escenarios, comprobaciones=cls.comprobaciones,
                comandos=cls.registros), ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
