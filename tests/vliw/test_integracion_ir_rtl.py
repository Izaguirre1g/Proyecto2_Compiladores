"""Prueba el productor real con oráculos lógicos y contrastes RTL completos.

Compila C11 en temporales externos, conserva las APIs del equipo y no publica
fragmentos rechazados. Los nombres de escenarios distinguen riesgos/semántica;
las dependencias siempre proceden del DAG físico real.
"""
from pathlib import Path
import importlib.util
import json
import os
import sys
import tempfile
import unittest
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/vliw'))
import validar_integracion_ir_rtl as i3


class IntegracionIrRtlTests(unittest.TestCase):
    # La copia de Arquitectura pertenece al fixture. Ni el módulo importado ni
    # los runners pueden crear cachés/binarios en el repositorio de referencia.
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='i1a3-suite-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir=Path(cls.tmp.name)
        cls.arq=i3.copiar_arquitectura(Path(os.environ.get('ARQUITECTURA_ROOT',ROOT.parent/'Proyecto2_Arquitectura1')).resolve(),cls.dir)
        cls.exe=i3.compilar(cls.dir)
        cls.recursos=i3.compilar(cls.dir,True)

    # El conjunto mezcla presión real, control y límites ISA. Se verifica el
    # oráculo lógico antes del contraste completo entre procesadores.
    def test_01_productor_y_simulador(self):
        for caso in i3.CASOS:
            with self.subTest(caso=caso):
                r=i3.validar_caso(self.exe,caso,'base',self.dir/('sim-'+caso),self.arq,())
                self.assertGreater(r['oraculo']['fragmento_bytes'],0)

    # Estos cinco casos ejercitan spills, rama tomada/no tomada y backedges con
    # memoria directa y L2 real; la captura RAM exige flush confirmado del runner.
    def test_02_contraste_rtl(self):
        for caso in ('copia','presion','tomado','no_tomado','backedge'):
            with self.subTest(caso=caso):
                r=i3.validar_caso(self.exe,caso,'base',self.dir/('rtl-'+caso),self.arq)
                self.assertEqual([m['metadata']['effective_mode'] for m in r['rtl']],[0,2])
                self.assertEqual(r['rtl'][1]['metadata']['configuration']['L2_MODEL'],0)

    # Los errores de IR/marco/control y salida ocupada no permiten publicar
    # binarios; el driver conserva la IR por campos/texto antes y después.
    def test_03_rechazos(self):
        self.assertIn('17 escenarios',i3.ejecutar([self.exe,'negativas'],self.dir))

    # La muestra de fallos recorre reservas iniciales, intermedias y finales y
    # exige recuperación sana sin alterar el fixture ni ignorar rechazos.
    def test_04_recursos(self):
        self.assertIn('16 fallos y 16 recuperaciones',i3.ejecutar([self.recursos,'recursos'],self.dir))

    # DCE debe transformar realmente el caso: aceptar sin cambio no acredita
    # optimización. Los temporales internos eliminados no son resultados BAL;
    # este fixture solo acredita transformación, SP/RA y ejecución sin traps.
    def test_05_perfil_dce(self):
        r=i3.validar_caso(self.exe,'dce','dce',self.dir/'perfil-dce',self.arq,())
        self.assertGreater(r['oraculo']['transformaciones'],0)

    # Reordenamiento y unrolling se ejecutan sin atribuir cambios si su política
    # rechaza el patrón. Sus métricas estimadas nunca se llaman IPC dinámico.
    def test_06_perfiles_capacidades(self):
        for perfil in ('reorder','unroll'):
            r=i3.validar_caso(self.exe,'raw',perfil,self.dir/perfil,self.arq,())
            self.assertEqual(r['oraculo']['perfil'],perfil)

    # El backend tradicional cubre las funciones aún rechazadas por IR-VLIW.
    # B5 obtiene cortes nuevos; centinelas no cero detectan destrucción de s0–s15.
    def test_08_saltos_funciones_tradicionales(self):
        import shutil
        copia=self.dir/'compilador'
        shutil.copytree(ROOT,copia,ignore=shutil.ignore_patterns('.git','__pycache__'))
        # El repositorio versiona balc: forzamos recompilación para que las
        # marcas de tiempo de la copia no permitan ejecutar un binario anterior.
        i3.ejecutar(['make','-B','balc'],self.dir,cwd=copia)
        i3.validar_preview_cli(self.exe,copia/'balc',self.arq,self.dir/'preview-cli')
        filas=i3.validar_tradicionales(self.exe,copia/'balc',self.arq,self.dir/'tradicionales')
        self.assertEqual([x['esperado'] for x in filas],[20,21,14])

    # La herramienta debe rechazar modos/identidades antes de crear evidencia
    # que pudiera confundirse con una ejecución válida de Arquitectura.
    def test_07_caso_inexistente(self):
        with self.assertRaises(i3.Rechazo):
            i3.validar_caso(self.exe,'inexistente','base',self.dir/'no-publicado',self.arq)
        self.assertFalse((self.dir/'no-publicado').exists())


    # Un registro repetido no puede esconder otro ausente, aunque todos sus
    # valores fueran cero. El snapshot completo es parte del contrato observable.
    def test_09_dump_incompleto(self):
        d=self.dir/'dump-invalido';d.mkdir()
        (d/'execution.json').write_text('{}')
        (d/'registers.txt').write_text('x0 00000000\n'*32)
        with self.assertRaises(i3.Rechazo):
            i3.contrastar_rtl(d,[0]*32,bytes(65536))

    # S1 debe rechazar la imagen corrupta antes de crear sesión M2 o métricas.
    def test_10_imagen_malformada(self):
        d=self.dir/'mem-invalida';d.mkdir();mem=d/'mal.mem';mem.write_text('0000x\n')
        i3.ejecutar([self.exe,'sim',mem,d,'1000'],d,esperado=1)
        self.assertFalse((d/'sim-metricas.json').exists())

    # Un modo inexistente o bool se rechaza antes de generar el caso, sin
    # reutilizar un binario previamente elaborado con otra configuración.
    def test_11_modo_invalido(self):
        for modo in (3,True):
            with self.assertRaises(i3.Rechazo):
                i3.validar_caso(self.exe,'copia','base',self.dir/str(modo),self.arq,(modo,))


if __name__=='__main__':
    unittest.main()
