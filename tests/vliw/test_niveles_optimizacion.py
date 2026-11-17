"""Comprueba niveles productivos, compatibilidad y rechazo sin fallback.

Compila el CLI y un observador C en copias externas. Los fixtures distinguen
eliminación real de una pasada sin cambios; los tests no implementan DCE.
El driver C aísla fallos de reservas propias sin alterar el código del equipo.
"""
from pathlib import Path
import os
import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
# El arreglo escribe un observable; la variable muerta es una sentencia completa
# de su propia línea, condición explícita del reescritor BAL del IDE.
FUENTE='''create_funk #declare_int# f(a * declare_int) {
 muerto * declare_int : 99;
 give a gauss 1;
}
create_funk #declare_infinite_void# main() {
 resultados * declare_int[1] : [0];
 muerto * declare_int : 77;
 valor * declare_int : f(3);
 resultados[0] : valor;
}
'''

class NivelesTests(unittest.TestCase):
    # Las copias evitan reconstruir el ejecutable versionado del repositorio.
    # La lista de enlace se obtiene de la receta real y no de un segundo backend.
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='niveles-final-');cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir=Path(cls.tmp.name);cls.root=cls.dir/'codigo'
        shutil.copytree(ROOT,cls.root,ignore=shutil.ignore_patterns('.git','__pycache__','build'))
        subprocess.run(['make','-B','balc'],cwd=cls.root,check=True,capture_output=True)
        cls.balc=cls.root/'balc'
        recipe=subprocess.check_output(['make','-Bn','balc'],cwd=cls.root,text=True)
        line=next(l for l in recipe.splitlines() if 'src/Linea_Comandos/main.c' in l and ' -o ' in l)
        tokens=shlex.split(line);sources=[s for s in tokens if s.endswith('.c') and s not in ('src/Linea_Comandos/main.c','tools/vliw/optimizacion_cli.c')]
        cc=shlex.split(os.environ.get('CC','cc'));flags=['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-Iinclude','-Iloop_unrolling','-Ireordenamiento','-Isrc/Analisis_Lexico','-Isrc/Analisis_Sintactico','-Isrc/Generacion_Codigo']
        obj=cls.dir/'opt.o';cls.driver=cls.dir/'driver'
        subprocess.run(cc+flags+['-Dmalloc=jf_malloc','-Dstrdup=jf_strdup','-c','tools/vliw/optimizacion_cli.c','-o',str(obj)],cwd=cls.root,check=True,capture_output=True)
        subprocess.run(cc+flags+sources+['tests/vliw/test_niveles_optimizacion.c',str(obj),'-o',str(cls.driver)],cwd=cls.root,check=True,capture_output=True)

    # Cada ejecución recibe su propia fuente, salidas y centinela del IDE. Así
    # se detecta cualquier sobrescritura del .ide_ homónimo junto al original.
    def ejecutar(self,opciones,fuente=FUENTE):
        d=Path(tempfile.mkdtemp(dir=self.dir));f=d/'caso.bal';f.write_text(fuente)
        cent=d/'.ide_caso.bal';cent.write_text('NO SOBRESCRIBIR')
        p=subprocess.run([str(self.balc),*opciones,str(f)],cwd=d,capture_output=True,text=True)
        self.assertEqual(f.read_text(),fuente);self.assertEqual(cent.read_text(),'NO SOBRESCRIBIR')
        return p,d

    # O0 debe conservar exactamente las imágenes del camino histórico.
    def test_01_o0_equivale_predeterminado(self):
        a,da=self.ejecutar(['-s']);b,db=self.ejecutar(['-O0','-s'])
        self.assertEqual((a.returncode,b.returncode),(0,0))
        for ext in ('bin','mem','asm'):self.assertEqual((da/('caso.'+ext)).read_bytes(),(db/('caso.'+ext)).read_bytes())

    # Una eliminación en IR sin reescritura BAL no bastaría: se exigen cambios
    # reales en el programa emitido por el backend tradicional.
    def test_02_o1_transformacion_real(self):
        a,da=self.ejecutar(['-O0','-s']);b,db=self.ejecutar(['-O1','-s','-v','-m','-p','-t'])
        self.assertEqual((a.returncode,b.returncode),(0,0));self.assertIn('cambio=1',b.stderr)
        self.assertLess((db/'caso.mem').stat().st_size,(da/'caso.mem').stat().st_size)
        self.assertTrue((db/'caso.simbolos.md').is_file())

    # O2 debe producir una imagen completa desde IR transformada; el reporte
    # identifica spilling y enlace, y una segunda compilación debe ser estable.
    def test_03_o2_productivo(self):
        p,d=self.ejecutar(['-O2','-s']);q,e=self.ejecutar(['-O2','-s'])
        self.assertEqual((p.returncode,q.returncode),(0,0),p.stderr)
        meta=json.loads((d/'caso.o2.json').read_text())
        self.assertEqual(meta['nivel'],'O2');self.assertGreater(meta['spills'],0)
        self.assertGreaterEqual(meta['generaciones'],2)
        for ext in ('bin','mem','asm'):
            self.assertEqual((d/('caso.'+ext)).read_bytes(),(e/('caso.'+ext)).read_bytes())

    def test_04_opciones_invalidas(self):
        for opts in (['-O'],['-O3'],['-O01'],['-O1','-O0'],['-O1','-O1']):
            with self.subTest(opts=opts):
                p,d=self.ejecutar(opts);self.assertEqual(p.returncode,2);self.assertFalse((d/'caso.bin').exists())

    # Las vistas IR/JSON y -x conservan su contrato independiente. No se rotulan
    # como ejecutables optimizados ni se mezclan dos pipelines distintos.
    def test_05_modos_incompatibles(self):
        for option in ('--ir','--ir-spilling','--vliw-ir','--dce','--unroll','--reorder','--ast-json','--diagnosticos','-x'):
            with self.subTest(option=option):
                p,d=self.ejecutar(['-O1',option]);self.assertEqual(p.returncode,2);self.assertFalse((d/'caso.bin').exists())

    # Un bring real es rechazado; su aparición en comentario no es un módulo.
    def test_06_modulos_y_lexer(self):
        p,d=self.ejecutar(['-O1'],'bring biblioteca;\n'+FUENTE);self.assertNotEqual(p.returncode,0);self.assertIn('módulos bring',p.stderr);self.assertFalse((d/'caso.bin').exists())
        p,_=self.ejecutar(['-O1'],'%% bring no es una importación\n'+FUENTE);self.assertEqual(p.returncode,0)

    # El límite se comprueba antes de parser/DCE y no publica salida parcial.
    def test_07_capacidad_y_fuente_corrupto(self):
        for text in ('\0'+FUENTE,' '*((2<<20)+1),FUENTE+'@'):
            p,d=self.ejecutar(['-O1'],text);self.assertNotEqual(p.returncode,0);self.assertFalse((d/'caso.bin').exists())

    # Los tres grupos ejercitan el API real, liberación idempotente y entradas
    # inmutables. La inyección solo alcanza reservas del adaptador, no del IDE.
    def probar_driver(self,grupo):
        f=self.dir/(grupo+'.bal');f.write_text(FUENTE)
        p=subprocess.run([str(self.driver),grupo,str(f)],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertIn('PASS propiedad',p.stdout)

    def test_08_propiedad(self):self.probar_driver('positivas')
    def test_09_argumentos(self):self.probar_driver('negativas')
    def test_10_reservas_y_recuperacion(self):self.probar_driver('recursos')

    # Si la pasada no elimina nada, se informa cambio=0; no se inventa ahorro.
    def test_11_sin_cambios(self):
        text='''create_funk #declare_infinite_void# main() {
 resultados * declare_int[1] : [0];
 resultados[0] : 4;
}
'''
        p,d=self.ejecutar(['-O1'],text);self.assertEqual(p.returncode,0,p.stderr);self.assertIn('cambio=0',p.stderr);self.assertTrue((d/'caso.bin').is_file())

    # Un literal que rebasa la capacidad física no se arregla añadiendo DCE:
    # se exige el rechazo de ambas variantes, sin fallback al productor parcial.
    def test_12_backend_no_acreditado(self):
        p,d=self.ejecutar(['-O1'],FUENTE.replace('99','999999999999999999999999'))
        self.assertNotEqual(p.returncode,0);self.assertFalse((d/'caso.bin').exists())

    # O1 debe validar con el backend ampliado tanto el programa original como
    # el reescrito; las llamadas con efectos permanecen en el ejecutable.
    def test_13_referencias_o0_o1(self):
        fuente=(ROOT/'examples/dce/dce_llamadas.bal').read_text()
        a,da=self.ejecutar(['-O0','-s'],fuente);b,db=self.ejecutar(['-O1','-s'],fuente)
        self.assertEqual((a.returncode,b.returncode),(0,0),a.stderr+b.stderr)
        self.assertIn('[O1] DCE:',b.stderr)
        self.assertNotEqual((da/'caso.mem').read_bytes(),(db/'caso.mem').read_bytes())

if __name__=='__main__':unittest.main()
