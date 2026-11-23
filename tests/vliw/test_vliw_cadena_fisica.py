"""Verifica la cadena consumidora física completa y artefactos reales B1/B2.

Las entradas son fixtures ISA explícitos, no resultados de un backend BAL.
Todos los ejecutables, imágenes y reportes se construyen en temporales externos.
"""
from pathlib import Path
import csv
import io
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
import unittest
sys.dont_write_bytecode = True
import test_vliw_proyeccion_fisica as proy

ROOT = Path(__file__).resolve().parents[2]
PROPIAS = proy.PROPIAS + ['src/Calendarizacion_VLIW/vliw_cadena_fisica.c',
    'src/Calendarizacion_VLIW/vliw_scheduler.c', 'src/Calendarizacion_VLIW/vliw_emisor.c',
    'src/Calendarizacion_VLIW/vliw_visualizador.c']
FUENTES = PROPIAS + ['tests/vliw/test_vliw_cadena_fisica.c']
WRAPPERS = ['malloc', 'calloc', 'realloc', 'vliw_planificar', 'vliw_verificar',
            'vliw_emitir', 'cb_ensamblar', 'vliw_visualizar', 'tmpfile']
MINIMO = r'''/* El vacío conserva todas las fases aplicables sin inventar código NOP. */
#include "vliw_cadena_fisica.h"
int main(void) {
    VliwCfgEntrada f={0}; f.fronteras_acreditadas=true;
    VliwCadenaEntrada e={.flujo=&f}; VliwCadena *c=0;
    if(vliw_cadena_construir(&e,&c,0))return 1;
    if(c->codificado || c->codigo.cantidad || c->plan->cantidad_bundles)return 2;
    vliw_cadena_liberar(&c); return c!=0;
}
'''


class CadenaFisicaTests(unittest.TestCase):
    # Compila la cadena y las dependencias reales. La variante de recursos
    # intercepta adquisiciones y fases solo en escenarios adversariales armados.
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='cadena-fisica-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.registros, cls.evaluaciones = [], []
        cls.escenarios = cls.comprobaciones = 0
        cls.imagenes = None
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-Iinclude']
        cls.normal, cls.recursos, cls.minimo = [cls.dir / n for n in ('normal', 'recursos', 'minimo')]
        fuente = cls.dir / 'minimo.c'
        fuente.write_text(MINIMO)
        cls.ejecutar(cc + flags + PROPIAS + [str(fuente), '-o', str(cls.minimo)])
        cls.ejecutar(cc + flags + FUENTES + ['-o', str(cls.normal)])
        cls.ejecutar(cc + flags + ['-DFIS_FALLOS'] + FUENTES +
                     ['-Wl,--wrap=' + nombre for nombre in WRAPPERS] + ['-o', str(cls.recursos)])
        cls.b1 = cls.dir / 'comparar_vliw'
        fuentes_b1 = ['tools/vliw/comparar_vliw.c'] + ['src/Simulador_VLIW/sim_' + n + '.c' for n in
            ('comparador', 'ejecucion', 'metricas', 'pipeline', 'alu', 'lsu', 'bru', 'crypto', 'decode', 'cargador')]
        cls.ejecutar(cc + flags + fuentes_b1 + ['-o', str(cls.b1)])

    # Conserva el retorno real y las salidas, incluso cuando una comparación
    # B1/B2 informa diferencia o inconclusión. Cada caso exige su propio código.
    @classmethod
    def ejecutar(cls, comando, esperado=0):
        inicio = time.monotonic()
        p = subprocess.run(list(map(str, comando)), cwd=ROOT, capture_output=True,
                           text=True, timeout=300, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        cls.registros.append(dict(comando=list(map(str, comando)), codigo=p.returncode,
            esperado=esperado, segundos=time.monotonic() - inicio, stdout=p.stdout, stderr=p.stderr))
        if p.returncode != esperado:
            raise AssertionError(f'{comando}: código {p.returncode}, esperado {esperado}\n{p.stdout}\n{p.stderr}')
        return p.stdout

    # Los números proceden del driver y no son un oráculo de funcionalidad.
    # La corrección de sus resultados se prueba por campos y etapas en C.
    def grupo(self, nombre, recursos=False, extra=()):
        texto = self.ejecutar([self.recursos if recursos else self.normal, nombre, *extra])
        m = re.search(r'PASS ' + nombre + r': escenarios=(\d+); comprobaciones=(\d+)', texto)
        self.assertIsNotNone(m, texto)
        escenarios, comprobaciones = map(int, m.groups())
        self.assertGreater(escenarios, 0)
        self.assertGreater(comprobaciones, escenarios)
        type(self).escenarios += escenarios
        type(self).comprobaciones += comprobaciones
        print(texto, end='')

    # La generación se realiza una vez por clase y siempre fuera del checkout.
    # Las pruebas B1/B2 pueden ejecutarse aisladamente sin depender del orden.
    def artefactos(self):
        if type(self).imagenes is None:
            # Cada intento dispone de un directorio nuevo: un fallo no permite
            # consumir archivos parciales de una preparación anterior.
            d = Path(tempfile.mkdtemp(prefix='imagenes-', dir=self.dir))
            self.grupo('artefactos', extra=[d])
            type(self).imagenes = d
        return type(self).imagenes

    # El vacío posee resultado liberable, pero no una imagen ejecutable ficticia.
    def test_00_enlace_minimo(self):
        self.ejecutar([self.minimo])

    # La salida no puede retener textos o tablas temporales de su productor.
    def test_01_positivas(self):
        self.grupo('positivas')

    # Distancias N+3 y conflictos de slots se comprueban sobre el plan real.
    def test_02_riesgos(self):
        self.grupo('riesgos')

    # Backedges/call/ret conservan forma y destinos; créditos ausentes rechazan.
    def test_03_control(self):
        self.grupo('control')

    # CRIPTO exige barreras y pares completos, sin simular AUTH satisfecho.
    def test_04_cripto(self):
        self.grupo('cripto')

    # Las categorías y fases deben identificar dónde se detuvo cada entrada.
    def test_05_negativas(self):
        self.grupo('negativas')

    # Capacidad de DAG, capacidad del plan y alcance de salto son restricciones distintas.
    def test_06_limites(self):
        self.grupo('limites')

    # Cada reserva observada se falla y luego se repite la cadena sana.
    def test_07_recursos(self):
        self.grupo('recursos', recursos=True)

    # Rechazos tardíos de APIs y E/S no pueden dejar resultados publicados.
    def test_08_fases(self):
        self.grupo('fases', recursos=True)

    # MEM/binario deben representar exactamente los mismos bundles y slots.
    # Su tamaño se mide en bytes de código, no en bytes del archivo de texto.
    def test_09_artefactos_cb(self):
        d = self.artefactos()
        for nombre in ('reference', 'scheduled'):
            lineas = (d / (nombre + '.mem')).read_text().splitlines()
            binario = (d / (nombre + '.bin')).read_bytes()
            self.assertTrue(lineas)
            self.assertEqual(len(binario), 16 * len(lineas))
            self.assertEqual(binario, b''.join(bytes.fromhex(l)[::-1] for l in lineas))
        visual = json.loads((d / 'visual.json').read_text())
        self.assertIsInstance(visual, dict)

    # La comparación estricta real debe certificar resultados públicos iguales.
    # Solo después de EQUIVALENTE se consideran sus ciclos/IPC medidos.
    def test_10_simulador_b1_real(self):
        d = self.artefactos()
        texto = self.ejecutar([self.b1, 'mem', d/'reference.mem', 'mem', d/'scheduled.mem', 'finita', 300, 'json'])
        b = json.loads(texto)
        self.assertEqual(b['resultado_comparacion'], 'EQUIVALENTE')
        for lado in ('referencia', 'candidato'):
            self.assertEqual(b[lado]['estado'], 1)
            self.assertEqual(b[lado]['metricas']['instrucciones'], 4)
            self.assertGreater(b[lado]['metricas']['ciclos'], 0)
        self.assertLessEqual(b['candidato']['metricas']['ciclos'], b['referencia']['metricas']['ciclos'])
        type(self).evaluaciones.append(dict(tipo='B1', resultado=b))

    # B2 consume las imágenes de la cadena sin convertir esta demostración en
    # cuota oficial de optimizaciones BAL. Conserva el documento B1 real.
    def test_11_lote_b2_real(self):
        d = self.artefactos()
        manifiesto = d/'manifest.json'
        manifiesto.write_text(json.dumps(dict(version=1, casos=[dict(id='cadena-fisica', categoria='demostracion',
            referencia=dict(formato='mem', ruta='reference.mem'), candidato=dict(formato='mem', ruta='scheduled.mem'),
            modo='finita', presupuesto=300)])))
        self.ejecutar([sys.executable, ROOT/'tools/vliw/benchmarks_vliw.py', '--manifest', manifiesto,
            '--comparador', self.b1, '--json', d/'b2.json', '--csv', d/'b2.csv', '--timeout', '30'])
        b = json.loads((d/'b2.json').read_text())
        self.assertEqual(b['casos'][0]['resultado'], 'EQUIVALENTE')
        self.assertTrue(b['casos'][0]['rendimiento_valido'])
        self.assertEqual(b['cobertura']['registrados'], 0)
        filas = list(csv.DictReader(io.StringIO((d/'b2.csv').read_text())))
        self.assertEqual(len(filas), 1)
        self.assertEqual(filas[0]['resultado'], 'EQUIVALENTE')
        type(self).evaluaciones.append(dict(tipo='B2', resultado=b))

    # Agotar presupuesto no demuestra terminación ni equivalencia: B1 conserva
    # INCONCLUSO y su retorno 3. La prueba acredita el rechazo de la comparación.
    def test_12_presupuesto_inconcluso(self):
        d = self.artefactos()
        b = json.loads(self.ejecutar([self.b1, 'mem', d/'reference.mem', 'mem', d/'scheduled.mem',
                                     'finita', 0, 'json'], esperado=3))
        self.assertEqual(b['resultado_comparacion'], 'INCONCLUSO')
        self.assertFalse(b['diferencias_arquitectonicas']['comparadas'])
        type(self).evaluaciones.append(dict(tipo='B1-inconcluso', resultado=b))

    # Evidencia externa con cantidades efectivamente ejecutadas. No conserva
    # los artefactos de clase después de limpiar el temporal; sí comandos/salidas.
    @classmethod
    def tearDownClass(cls):
        print(f'CADENA: {cls.escenarios} escenarios C; {cls.comprobaciones} comprobaciones C.')
        if os.environ.get('CADENA_EVIDENCIA'):
            p = Path(os.environ['CADENA_EVIDENCIA']).resolve()
            if p.is_relative_to(ROOT):
                raise AssertionError('la evidencia debe estar fuera del repositorio')
            p.write_text(json.dumps(dict(escenarios=cls.escenarios, comprobaciones=cls.comprobaciones,
                comandos=cls.registros, evaluaciones=cls.evaluaciones), ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
