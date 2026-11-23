"""Comprueba niveles productivos, compatibilidad y rechazo sin fallback.

Compila el CLI y un observador C en copias externas. Los fixtures distinguen
eliminación real de una pasada sin cambios; los tests no implementan DCE.
El driver C aísla fallos de reservas propias sin alterar el código del equipo.
"""
from pathlib import Path
import os
import hashlib
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
        cls.balc=cls.root/'balc';cls.colisiones=[]
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

    # La evidencia opcional conserva hashes de cada archivo del fixture, incluso
    # si un alias lo expone con otro nombre. No utiliza archivos del usuario.
    @classmethod
    def tearDownClass(cls):
        if os.environ.get('F01_EVIDENCIA'):
            Path(os.environ['F01_EVIDENCIA']).write_text(json.dumps(cls.colisiones,indent=2))

    # Cada rechazo parte de una unidad BAL válida en un directorio privado;
    # la limpieza pertenece al TemporaryDirectory de la clase.
    def preparar_colision(self):
        d=Path(tempfile.mkdtemp(dir=self.dir));(d/'caso.bal').write_text(FUENTE)
        return d

    # Registramos enlaces como enlaces, sin recorrer sus destinos externos;
    # los archivos regulares del fixture se comparan por contenido SHA-256.
    def hashes_fixture(self,d):
        return {str(p.relative_to(d)):('symlink:'+os.readlink(p) if p.is_symlink()
                else hashlib.sha256(p.read_bytes()).hexdigest())
                for p in d.rglob('*') if p.is_symlink() or p.is_file()}

    # Una colisión debe detectarse antes de abrir la primera salida: comparar el
    # conjunto completo también detecta artefactos nuevos, no solo corrupción.
    def rechazar_colision(self,d,salida,diagnostico='colisión'):
        antes=self.hashes_fixture(d)
        orden=[str(self.balc),'-O2','-o',str(salida),'caso.bal']
        p=subprocess.run(orden,cwd=d,capture_output=True,text=True)
        despues=self.hashes_fixture(d)
        self.colisiones.append(dict(comando=orden,codigo=p.returncode,stderr=p.stderr,
                                    antes=antes,despues=despues))
        self.assertNotEqual(p.returncode,0,p.stderr);self.assertIn(diagnostico,p.stderr)
        self.assertEqual(antes,despues,'El rechazo debe conservar todos los archivos')

    # Estos cinco nombres reproducen BIN sobrescrito por ASM, JSON o cualquiera
    # de los tres volcados IR; los centinelas preexistentes deben sobrevivir.
    def test_14_colisiones_literales(self):
        for ext in ('asm','o2.json','o2-antes.ir','o2-despues.ir','o2-spilling.ir'):
            with self.subTest(ext=ext):
                d=self.preparar_colision();salida='caso.'+ext
                (d/salida).write_bytes(b'ARTEFACTO PROTEGIDO')
                self.rechazar_colision(d,salida)

    # Aunque BIN y MEM tienen nombres distintos, un enlace al mismo inodo
    # haría que la segunda escritura destruyera el ejecutable recién emitido.
    def test_15_colision_mem(self):
        d=self.preparar_colision();(d/'resultado.mem').write_bytes(b'MEM PROTEGIDA')
        (d/'resultado.bin').symlink_to('resultado.mem')
        self.rechazar_colision(d,'resultado.bin')

    # El frontend ya leyó BAL: permitir reutilizar su nombre como BIN destruiría
    # una entrada válida después de compilarla, aun sin error de análisis.
    def test_16_colision_fuente(self):
        d=self.preparar_colision();self.rechazar_colision(d,'caso.bal')

    # El nombre literal difiere; normalizar el padre debe reconocer el mismo
    # sidecar aun cuando este todavía no exista.
    def test_17_alias_relativo(self):
        d=self.preparar_colision();self.rechazar_colision(d,'./caso.o2.json')

    # Un padre real seguido de .. vuelve al directorio del JSON/IR; comparar
    # únicamente la cadena de -o dejaría pasar la sobrescritura.
    def test_18_alias_padre(self):
        d=self.preparar_colision();(d/'sub').mkdir()
        self.rechazar_colision(d,'sub/../caso.o2-antes.ir')

    # Se ejercitan tanto enlaces de directorio con salida nueva como enlaces
    # finales existentes; realpath del archivo inexistente no resolvería el primero.
    def test_19_alias_simbolicos(self):
        d=self.preparar_colision();(d/'alias').symlink_to('.',target_is_directory=True)
        self.rechazar_colision(d,'alias/caso.o2-despues.ir')
        d=self.preparar_colision();(d/'resultado.bin').symlink_to('caso.bal')
        self.rechazar_colision(d,'resultado.bin')

    # Un padre ausente, un archivo usado como padre, una salida directorio y un
    # enlace colgante no son destinos publicables. No deben dejar ni el ASM.
    def test_20_destinos_invalidos(self):
        for tipo in ('ausente','archivo','ejecutable','directorio','colgante'):
            with self.subTest(tipo=tipo):
                d=self.preparar_colision();salida='sub/resultado.bin'
                if tipo in ('archivo','ejecutable'):
                    (d/'sub').write_bytes(b'PADRE PROTEGIDO')
                    if tipo=='ejecutable':(d/'sub').chmod(0o700)
                if tipo=='directorio':(d/'sub').mkdir();(d/salida).mkdir()
                if tipo=='colgante':
                    salida='resultado.bin';(d/salida).symlink_to('sin-destino')
                self.rechazar_colision(d,salida,'destino inválido')

    # Cubrimos los 21 pares de salidas y las siete contra la fuente usando
    # enlaces duros. Igualdad textual/canónica sola no detectaría estos alias.
    def test_21_identidad_archivos_protegidos(self):
        nombres=['caso.asm','caso.bin','caso.mem','caso.o2.json',
                 'caso.o2-antes.ir','caso.o2-despues.ir','caso.o2-spilling.ir']
        for i,a in enumerate(nombres):
            for b in nombres[i+1:]+['caso.bal']:
                with self.subTest(a=a,b=b):
                    d=self.preparar_colision()
                    if b!='caso.bal':(d/b).write_bytes(b'INODO PROTEGIDO')
                    os.link(d/b,d/a);self.rechazar_colision(d,'caso.bin')

    # El preflight conserva las rutas admitidas y todos sus formatos. Repetir
    # sobre las mismas salidas también contrasta la rama de archivos existentes.
    def test_22_salidas_validas_deterministas(self):
        sys.path.insert(0,str(ROOT/'tools/vliw'))
        from evaluar_entrega_final import validar_imagen
        from validar_integracion_ir_rtl import ensamblador
        arq=Path(os.environ.get('ARQUITECTURA_ROOT',str(ROOT.parent/'Proyecto2_Arquitectura1')))
        for modo in ('predeterminado','relativo','absoluto'):
            with self.subTest(modo=modo):
                d=self.preparar_colision();externo=d/'externo';externo.mkdir()
                binario=d/'caso.bin' if modo=='predeterminado' else (d/'resultado.bin' if modo=='relativo' else externo/'resultado.bin')
                opts=[] if modo=='predeterminado' else ['-o',str(binario) if modo=='absoluto' else binario.name]
                orden=[str(self.balc),'-O2',*opts,'caso.bal']
                p=subprocess.run(orden,cwd=d,capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stderr)
                rutas=[d/'caso.asm',binario,binario.with_suffix('.mem'),d/'caso.o2.json',
                       d/'caso.o2-antes.ir',d/'caso.o2-despues.ir',d/'caso.o2-spilling.ir']
                self.assertTrue(all(p.is_file() and p.stat().st_size for p in rutas))
                self.assertEqual(json.loads((d/'caso.o2.json').read_text())['nivel'],'O2')
                validar_imagen(binario,binario.with_suffix('.mem'))
                self.assertEqual(ensamblador(arq).assemble_source((d/'caso.asm').read_text()).memory_text(),binario.with_suffix('.mem').read_text())
                antes=self.hashes_fixture(d)
                q=subprocess.run(orden,cwd=d,capture_output=True,text=True)
                self.assertEqual(q.returncode,0,q.stderr);self.assertEqual(antes,self.hashes_fixture(d))

    # La imagen con BIN externo debe recorrer el mismo M2 físico: f(3) escribe
    # 4 en resultados[0]. La sonda exige retorno real y preservación SP/GP/RA,
    # sin convertir ese corte en terminación B1.
    def test_23_salida_externa_m2(self):
        d=self.preparar_colision();(d/'externo').mkdir();binario=d/'externo/resultado.bin'
        p=subprocess.run([str(self.balc),'-O2','-o',str(binario),'caso.bal'],cwd=d,capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)
        sonda=d/'sonda';cc=shlex.split(os.environ.get('CC','cc'))
        sim=sorted(str(p) for p in (ROOT/'src/Simulador_VLIW').glob('*.c'))
        subprocess.run(cc+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT/'include'),str(ROOT/'tools/vliw/sonda_o2.c'),*sim,'-o',str(sonda)],check=True,capture_output=True)
        meta=json.loads((d/'caso.o2.json').read_text());fn=next(f for f in meta['funciones'] if f['nombre']=='main')
        p=subprocess.run([str(sonda),str(binario.with_suffix('.mem')),str(meta['pc_fin']),str(fn['inicio_pc']),str(fn['fin_pc']),str(d),'100000'],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)
        obj=next(o for o in meta['objetos'] if o['funcion']=='main' and o['nombre']=='resultados')
        direccion=65536-fn['marco']+obj['offset'];mem=(d/'sim-memoria.bin').read_bytes()
        self.assertEqual(int.from_bytes(mem[direccion:direccion+4],'little'),4)

if __name__=='__main__':unittest.main()
