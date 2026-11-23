"""Prueba metadatos parciales con S2, fixtures físicos y CB como codificador.

Compila en temporales externos, cuenta escenarios C y conserva errores reales.
La prueba de enlace mínimo usa solo extractor+S2; CB existe únicamente para los
fixtures diferenciales. No se construyen VliwEntrada, CFG, DAG ni programas BAL.
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
/* Sonda de enlace: ninguna dependencia CB, S7 o I1-A1 en producción. */
#include "vliw_metadatos.h"
/* Extrae una suma física conocida y comprueba su máscara; no reserva recursos.
 * Devuelve error si el enlace mínimo o la extracción no cumplen el contrato. */
int main(void) {
    VliwMetadatosParciales m;
    uint32_t palabra=(UINT32_C(0x40)<<25)|(UINT32_C(4)<<11)|7u;
    if (vliw_metadatos_extraer(palabra,0,&m,0)!=SIM_DECODE_OK) return 1;
    return m.operacion!=SIM_OP_SUMAI || m.lee!=0 || m.escribe!=(UINT64_C(1)<<4) || !m.pendientes;
}
'''


class MetadatosTests(unittest.TestCase):
    # Construye una sonda mínima y el driver de escenarios. Las fuentes de
    # producción solo dependen de S2; CC admite flags de sanitizadores C.
    @classmethod
    def setUpClass(cls):
        cls.temporal = tempfile.TemporaryDirectory(prefix='pruebas-i1a2a-')
        cls.addClassCleanup(cls.temporal.cleanup)
        cls.directorio = Path(cls.temporal.name)
        cls.registros = []
        cls.escenarios = cls.comprobaciones = 0
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-Iinclude']
        fuentes = ['src/Calendarizacion_VLIW/vliw_metadatos.c', 'src/Simulador_VLIW/sim_decode.c']
        fuente_minima = cls.directorio / 'minimo.c'
        fuente_minima.write_text(MINIMO)
        cls.minimo = cls.directorio / 'minimo'
        cls.binario = cls.directorio / 'metadatos'
        cls.ejecutar(cc + flags + fuentes + [str(fuente_minima), '-o', str(cls.minimo)])
        cls.ejecutar(cc + flags + fuentes + ['src/Codificacion_Binaria/codificacion_binaria.c',
            'tests/vliw/test_vliw_metadatos.c', '-o', str(cls.binario)])

    # Conserva comando, stdout, stderr y código reales. Un error de compilación
    # o de ejecución debe fallar con su diagnóstico; no se transforma en SKIP.
    @classmethod
    def ejecutar(cls, comando):
        p = subprocess.run(comando, cwd=ROOT, capture_output=True, text=True, timeout=120)
        cls.registros.append(dict(comando=comando, codigo=p.returncode, stdout=p.stdout, stderr=p.stderr))
        if p.returncode:
            raise AssertionError(f'{comando}: código {p.returncode}\n{p.stdout}\n{p.stderr}')
        return p.stdout

    # Exige el resumen del driver para distinguir grupos reales de procesos
    # que simplemente terminan con0 sin ejecutar sus verificaciones.
    def grupo(self, nombre):
        texto = self.ejecutar([str(self.binario), nombre])
        resumen = re.search(r'PASS ' + nombre + r': escenarios=(\d+); comprobaciones=(\d+)', texto)
        self.assertIsNotNone(resumen, texto)
        escenarios, comprobaciones = map(int, resumen.groups())
        self.assertGreater(escenarios, 0)
        self.assertGreater(comprobaciones, escenarios)
        type(self).escenarios += escenarios
        type(self).comprobaciones += comprobaciones
        print(texto, end='')

    # Si producción necesitara CB, el validador completo o el scheduler, esta
    # sonda no podría enlazarse. También exige una extracción física conocida.
    def test_00_enlace_minimo(self):
        self.ejecutar([str(self.minimo)])

    # Los oráculos manuales cubren las34 operaciones y sus mínimos; CB y S2
    # aportan un contraste independiente del extractor bajo prueba.
    def test_01_catalogo(self):
        self.grupo('catalogo')

    # Distingue ausencia de x0, store de load y pares Feistel, incluidos los
    # GPR del límite y fuentes/destinos que coinciden.
    def test_02_registros(self):
        self.grupo('registros')

    # Enlace x1 y la alias RET no autorizan a inventar VliwControl o destinos.
    # Las transferencias mantienen explícita la clasificación ABI pendiente.
    def test_03_control(self):
        self.grupo('control')

    # Cada rechazo debe conservar íntegra la salida: NOP, reservado, unidad
    # incorrecta, argumentos y restricciones estáticas de campos reales.
    def test_04_rechazos(self):
        self.grupo('rechazos')

    # Reutilizar una salida no debe dejar datos parciales tras un rechazo ni
    # arrastrar las máscaras anteriores al extraer después otra operación.
    def test_05_publicacion(self):
        self.grupo('publicacion')

    # El barrido de tipos/IDs/slots contrasta estados con S2 y detecta aceptar
    # identificadores reservados o tratar silenciosamente errores como NOP.
    def test_06_barrido(self):
        self.grupo('barrido')

    # La evidencia opcional solo se escribe fuera del árbol del proyecto.
    # Los temporales pertenecen a la clase y se liberan incluso ante errores.
    @classmethod
    def tearDownClass(cls):
        print(f'I1-A2a: {cls.escenarios} escenarios C; {cls.comprobaciones} comprobaciones C.')
        ruta = os.environ.get('I1A2A_EVIDENCIA')
        if ruta:
            destino = Path(ruta).resolve()
            if destino.is_relative_to(ROOT):
                raise AssertionError('la evidencia debe permanecer fuera del repositorio')
            destino.write_text(json.dumps(dict(escenarios=cls.escenarios,
                comprobaciones=cls.comprobaciones, comandos=cls.registros),
                ensure_ascii=False, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
