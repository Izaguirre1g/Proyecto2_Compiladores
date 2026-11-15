"""Verifica el contrato físico I1-A1 mediante fixtures C explícitos.

Compila el validador y las bibliotecas existentes en un directorio temporal;
no utiliza BAL, CLI ni un productor provisional. Los grupos C comprueban ISA,
rechazos, recursos e integración real S7/S8-A, sin certificar semántica BAL.
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


class IntegracionTests(unittest.TestCase):
    # Construye ejecutables normales y con fallos controlados. CC puede incluir
    # flags de sanitizadores; la configuración conserva advertencias como errores.
    @classmethod
    def setUpClass(cls):
        cls.temporal = tempfile.TemporaryDirectory(prefix='pruebas-i1a1-')
        cls.addClassCleanup(cls.temporal.cleanup)
        cls.directorio = Path(cls.temporal.name)
        cls.registros = []
        cls.escenarios = cls.comprobaciones = 0
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-Iinclude']
        fuentes = [
            'tests/vliw/test_vliw_integracion.c',
            'src/Calendarizacion_VLIW/vliw_integracion.c',
            'src/Calendarizacion_VLIW/vliw_scheduler.c',
            'src/Calendarizacion_VLIW/vliw_emisor.c',
            'src/Codificacion_Binaria/codificacion_binaria.c',
            'src/Simulador_VLIW/sim_decode.c',
        ]
        cls.normal = cls.directorio / 'integracion'
        cls.fallos = cls.directorio / 'recursos'
        cls.ejecutar(cc + flags + fuentes + ['-o', str(cls.normal)])
        envolturas = ['malloc', 'calloc', 'realloc', 'tmpfile', 'fflush', 'fseek', 'fclose']
        cls.ejecutar(cc + flags + ['-DINTEGRACION_FALLOS'] + fuentes +
                     ['-Wl,--wrap=' + nombre for nombre in envolturas] + ['-o', str(cls.fallos)])

    # Ejecuta un comando real y conserva stdout, stderr, código y orden. Un fallo
    # de compilación o ejecución detiene la prueba y mantiene el diagnóstico.
    @classmethod
    def ejecutar(cls, comando):
        proceso = subprocess.run(comando, cwd=ROOT, capture_output=True, text=True, timeout=180)
        cls.registros.append(dict(comando=comando, codigo=proceso.returncode,
                                  stdout=proceso.stdout, stderr=proceso.stderr))
        if proceso.returncode:
            raise AssertionError(f'{comando}: salida {proceso.returncode}\n'
                                 f'{proceso.stdout}\n{proceso.stderr}')
        return proceso.stdout

    # Exige que el ejecutable haya recorrido escenarios y aserciones reales.
    # El contador complementa unittest: siete métodos no significan siete casos C.
    def grupo(self, nombre, recursos=False):
        texto = self.ejecutar([str(self.fallos if recursos else self.normal), nombre])
        resumen = re.search(r'PASS ' + nombre + r': escenarios=(\d+); comprobaciones=(\d+)', texto)
        self.assertIsNotNone(resumen, texto)
        escenarios, comprobaciones = map(int, resumen.groups())
        self.assertGreater(escenarios, 0, texto)
        self.assertGreater(comprobaciones, escenarios, texto)
        type(self).escenarios += escenarios
        type(self).comprobaciones += comprobaciones
        print(texto, end='')

    # Las 34 operaciones y sus máscaras son datos explícitos de prueba. Los
    # negativos distinguen ISA inexistente, x0, nombres virtuales y par Feistel.
    def test_01_isa(self):
        self.grupo('isa')

    # Los rechazos estructurales deben ocurrir sin recorrer regiones inexistentes
    # y sin escribir sobre texto, intervalos, punteros o declaración del productor.
    def test_02_estructura(self):
        self.grupo('estructura')

    # El DAG físico debe respetar N+3 y cubrir riesgos visibles, incluso cuando
    # el productor haya omitido una arista; caminos transitivos sí son válidos.
    def test_03_dependencias(self):
        self.grupo('dependencias')

    # Tomado, caída, llamada, retorno y backedge se contrastan con el CFG; ningún
    # destino dinámico se transforma en una dirección estática inventada.
    def test_04_destinos(self):
        self.grupo('destinos')

    # Las cotas evitan overflow antes de acceder a datos. La entrada máxima
    # local no implica que S7 pueda empaquetarla en la capacidad de IMEM.
    def test_05_limites(self):
        self.grupo('limites')

    # El fixture atraviesa bibliotecas reales y se inspeccionan slots/NOP del
    # resultado; un plan alterado debe seguir siendo rechazado por S7.
    def test_06_integracion(self):
        self.grupo('integracion')

    # Cada reserva fallida y cada fallo de flujo debe liberar recursos, conservar
    # entradas y permitir una validación posterior sana; no se cambia producción.
    def test_07_recursos(self):
        self.grupo('recursos', recursos=True)

    # Exporta evidencia solo si se pidió una ruta externa. Los temporales son
    # propiedad de la clase y addClassCleanup los retira también ante errores.
    @classmethod
    def tearDownClass(cls):
        print(f'I1-A1: {cls.escenarios} escenarios C; {cls.comprobaciones} comprobaciones C.')
        ruta = os.environ.get('I1A1_EVIDENCIA')
        if ruta:
            destino = Path(ruta).resolve()
            if destino.is_relative_to(ROOT):
                raise AssertionError('la evidencia debe estar fuera del repositorio')
            destino.write_text(json.dumps(dict(escenarios=cls.escenarios,
                comprobaciones=cls.comprobaciones, comandos=cls.registros),
                ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
