"""Verifica proyección física independiente con CB/S2, CFG/DAG e I1-A1 reales.

Compila C11 estricto en temporales externos; no altera Makefile ni binarios
versionados. Los fixtures aportan solo garantías locales, no procedencia BAL.
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
PROPIAS = ['src/Calendarizacion_VLIW/vliw_proyeccion_fisica.c',
           'src/Calendarizacion_VLIW/vliw_dag_fisico.c',
           'src/Calendarizacion_VLIW/vliw_cfg_fisico.c',
           'src/Calendarizacion_VLIW/vliw_metadatos.c',
           'src/Simulador_VLIW/sim_decode.c',
           'src/Calendarizacion_VLIW/vliw_integracion.c',
           'src/Codificacion_Binaria/codificacion_binaria.c']
FUENTES = PROPIAS + ['tests/vliw/test_vliw_proyeccion_fisica.c']
MINIMO = r'''/* El vacío no requiere backend, S7 ni un productor ficticio. */
#include "vliw_proyeccion_fisica.h"
int main(void) {
    VliwCfgEntrada f={0}; f.fronteras_acreditadas=true;
    VliwCfg *cfg=0; VliwDag *dag=0; VliwProyeccion *p=0;
    if(vliw_cfg_construir(&f,&cfg,0))return 1;
    VliwDagEntrada de={.flujo=&f,.cfg=cfg};
    if(vliw_dag_construir(&de,&dag,0))return 2;
    VliwProyeccionEntrada e={.fisica=de,.dag=dag};
    if(vliw_proyeccion_construir(&e,&p,0))return 3;
    vliw_proyeccion_liberar(&p); vliw_dag_liberar(&dag); vliw_cfg_liberar(&cfg);
    return p!=0;
}
'''


class ProyeccionFisicaTests(unittest.TestCase):
    # Ambas variantes utilizan las mismas fuentes reales; solo recursos activa
    # wrappers de reserva. La sonda mínima acredita dependencias de enlace.
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='proyeccion-fisica-')
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
        cls.ejecutar(cc + flags + ['-DFIS_FALLOS'] + FUENTES +
                     ['-Wl,--wrap=malloc', '-Wl,--wrap=calloc', '-Wl,--wrap=realloc', '-o', str(cls.recursos)])

    # Conserva comando, salida, código y tiempo; un rechazo inesperado falla
    # realmente la suite. No convierte errores instrumentados en omisiones.
    @classmethod
    def ejecutar(cls, comando):
        inicio = time.monotonic()
        p = subprocess.run(list(map(str, comando)), cwd=ROOT, capture_output=True,
                           text=True, timeout=300)
        cls.registros.append(dict(comando=list(map(str, comando)), codigo=p.returncode,
            segundos=time.monotonic() - inicio, stdout=p.stdout, stderr=p.stderr))
        if p.returncode:
            raise AssertionError(f'{comando}: código {p.returncode}\n{p.stdout}\n{p.stderr}')
        return p.stdout

    # Se cuentan los escenarios y comprobaciones ejecutados por C; el enlace
    # mínimo se registra como método Python, sin inventar otro escenario C.
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

    # El resultado vacío se construye y libera con las dependencias declaradas.
    def test_00_enlace_minimo(self):
        self.ejecutar([self.minimo])

    # La copia debe sobrevivir al borrado de los préstamos, incluidos textos.
    def test_01_positivas(self):
        self.grupo('positivas')

    # RAW3/WAR/WAW y pares Feistel conservan aristas y distancias originales.
    def test_02_riesgos(self):
        self.grupo('riesgos')

    # Backedges y call/ret conservan destinos sin inferir ABI desde registros.
    def test_03_control(self):
        self.grupo('control')

    # Metadatos, textos y garantías contradictorios deben impedir publicación.
    def test_04_negativas(self):
        self.grupo('negativas')

    # Falla cada reserva observada y repite sano, sin eliminar la ruta de CB.
    def test_05_recursos(self):
        self.grupo('recursos', recursos=True)

    # La evidencia opcional queda fuera del proyecto. Los ejecutables y fuentes
    # temporales pertenecen a la clase y se limpian incluso ante fallos.
    @classmethod
    def tearDownClass(cls):
        print(f'PROYECCION: {cls.escenarios} escenarios C; {cls.comprobaciones} comprobaciones C.')
        if os.environ.get('PROYECCION_EVIDENCIA'):
            p = Path(os.environ['PROYECCION_EVIDENCIA']).resolve()
            if p.is_relative_to(ROOT):
                raise AssertionError('la evidencia debe estar fuera del repositorio')
            p.write_text(json.dumps(dict(escenarios=cls.escenarios, comprobaciones=cls.comprobaciones,
                comandos=cls.registros), ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
