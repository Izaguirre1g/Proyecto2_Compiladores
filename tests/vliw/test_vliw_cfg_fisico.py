"""Prueba CFG físicos con fixtures C, sin productor BAL ni cambios al repositorio.

Compila en temporales, conserva códigos/diagnósticos y cuenta escenarios reales.
El enlace mínimo utiliza solo constructor y S2; la cadena S7/S8-A usa dependencias
manuales explícitas en el driver complementario, nunca un DAG supuesto.
"""
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MINIMO = r'''
/* Enlace mínimo sin CB, S7 o backend: un flujo vacío produce CFG liberable. */
#include "vliw_cfg_fisico.h"
int main(void) {
    VliwCfgEntrada e={0}; e.fronteras_acreditadas=true;
    VliwCfg *c=0;
    if(vliw_cfg_construir(&e,&c,0)!=VLIW_CFG_OK) return 1;
    if(c->cantidad_bloques || c->cantidad_operaciones) return 2;
    vliw_cfg_liberar(&c); return c!=0;
}
'''


class CfgFisicoTests(unittest.TestCase):
    # Construye normal, variante de recursos y enlace mínimo. Las flags CC
    # permiten instrumentar todas las fuentes C sin modificar Makefile.
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='cfg-fisico-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.registros = []
        cls.escenarios = cls.comprobaciones = 0
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-Iinclude']
        propias = ['src/Calendarizacion_VLIW/vliw_cfg_fisico.c', 'src/Simulador_VLIW/sim_decode.c']
        fuente = cls.dir / 'minimo.c'
        fuente.write_text(MINIMO)
        cls.minimo = cls.dir / 'minimo'
        cls.normal = cls.dir / 'normal'
        cls.recursos = cls.dir / 'recursos'
        cls.ejecutar(cc + flags + propias + [str(fuente), '-o', str(cls.minimo)])
        fuentes = propias + ['tests/vliw/test_vliw_cfg_fisico.c',
            'src/Calendarizacion_VLIW/vliw_integracion.c',
            'src/Calendarizacion_VLIW/vliw_scheduler.c',
            'src/Calendarizacion_VLIW/vliw_emisor.c',
            'src/Codificacion_Binaria/codificacion_binaria.c']
        cls.ejecutar(cc + flags + fuentes + ['-o', str(cls.normal)])
        cls.ejecutar(cc + flags + ['-DCFG_FALLOS'] + fuentes +
            ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-o', str(cls.recursos)])

    # Un fallo real conserva comando/stdout/stderr y detiene la prueba; no se
    # convierte en omisión ni se acepta un código diferente del esperado.
    @classmethod
    def ejecutar(cls, comando):
        p = subprocess.run(list(map(str, comando)), cwd=ROOT, capture_output=True,
                           text=True, timeout=180)
        cls.registros.append(dict(comando=list(map(str, comando)), codigo=p.returncode,
                                  stdout=p.stdout, stderr=p.stderr))
        if p.returncode:
            raise AssertionError(f'{comando}: código {p.returncode}\n{p.stdout}\n{p.stderr}')
        return p.stdout

    # Contrasta el resumen del driver para evitar acreditar grupos vacíos.
    # Los oráculos de bloques y sucesores están escritos manualmente en C.
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

    # Demuestra que producción no necesita CB/S7 para particionar un flujo.
    def test_00_enlace_minimo(self):
        self.ejecutar([self.minimo])

    # Vacío, secuencias, alias y regiones contrastan cobertura sin huecos.
    def test_01_secuencias(self):
        self.grupo('secuencias')

    # Backedges y condicionales comprueban identidad lógica y caída deduplicada.
    def test_02_saltos(self):
        self.grupo('saltos')

    # Funciones/calls/retornos conservan lo dinámico y su cobertura declarada.
    def test_03_dinamicos(self):
        self.grupo('dinamicos')

    # Cada rechazo comprueba entradas intactas y ausencia de publicación parcial.
    def test_04_rechazos(self):
        self.grupo('rechazos')

    # El máximo usa almacenamiento real y no promete capacidad del futuro plan.
    def test_05_limites(self):
        self.grupo('limites')

    # Solo fixtures completos aportan sus dependencias a I1-A1/S7/S8-A reales.
    def test_06_integracion(self):
        self.grupo('integracion')

    # Cada reserva fallida debe liberar parciales y permitir repetir sano.
    def test_07_recursos(self):
        self.grupo('recursos', recursos=True)

    # La evidencia opcional se exporta fuera del proyecto y sin generar cachés
    # ni artefactos C en el árbol. Los temporales se liberan también ante fallo.
    @classmethod
    def tearDownClass(cls):
        print(f'CFG: {cls.escenarios} escenarios C; {cls.comprobaciones} comprobaciones C.')
        if os.environ.get('I1CFG_EVIDENCIA'):
            p = Path(os.environ['I1CFG_EVIDENCIA']).resolve()
            if p.is_relative_to(ROOT):
                raise AssertionError('la evidencia debe estar fuera del repositorio')
            p.write_text(json.dumps(dict(escenarios=cls.escenarios, comprobaciones=cls.comprobaciones,
                comandos=cls.registros), ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
