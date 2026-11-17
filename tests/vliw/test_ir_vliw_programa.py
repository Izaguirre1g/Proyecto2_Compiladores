"""Ejercita O2 desde BAL y la pasada de nombres con oráculos independientes.

La compilación y las imágenes viven en temporales. CC instrumenta tanto el
backend productivo como las sondas; cada ejecución exige retorno real de main,
SP/GP/RA y registros preservados, además del valor BAL esperado.
"""
from pathlib import Path
import json,os,shlex,shutil,subprocess,sys,tempfile,unittest
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
class ProgramaTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='o2-productivo-');cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir=Path(cls.tmp.name);cls.root=cls.dir/'codigo'
        shutil.copytree(ROOT,cls.root,ignore=shutil.ignore_patterns('.git','__pycache__','build'))
        subprocess.run(['make','-B','balc'],cwd=cls.root,check=True,capture_output=True)
        recipe=subprocess.check_output(['make','-Bn','balc'],cwd=cls.root,text=True)
        line=next(l for l in recipe.splitlines() if 'src/Linea_Comandos/main.c' in l and ' -o ' in l)
        tokens=shlex.split(line);sources=[s for s in tokens if s.endswith('.c') and s!='src/Linea_Comandos/main.c']
        flags=['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-Iinclude','-Iloop_unrolling','-Ireordenamiento','-Isrc/Analisis_Lexico','-Isrc/Analisis_Sintactico','-Isrc/Generacion_Codigo']
        cc=shlex.split(os.environ.get('CC','cc'));cls.driver=cls.dir/'nombres';cls.sonda=cls.dir/'sonda'
        subprocess.run(cc+flags+sources+['tests/vliw/test_ir_renombramiento.c','-Wl,--wrap=malloc','-Wl,--wrap=calloc','-o',str(cls.driver)],cwd=cls.root,check=True,capture_output=True)
        sim=sorted(str(p.relative_to(cls.root)) for p in (cls.root/'src/Simulador_VLIW').glob('*.c'))
        subprocess.run(cc+flags+['tools/vliw/sonda_o2.c',*sim,'-o',str(cls.sonda)],cwd=cls.root,check=True,capture_output=True)
    def grupo(self,nombre):
        p=subprocess.run([str(self.driver),nombre],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)
        if p.stdout:print(p.stdout.strip(),flush=True)
    def test_01_versiones_war_waw_raw(self):self.grupo('versiones')
    def test_02_publicacion_raices_dce(self):self.grupo('raices')
    def test_03_rechazos_sin_mutacion(self):self.grupo('rechazos')
    def test_04_fallos_reserva(self):self.grupo('recursos')
    def caso(self,fuente,nombre,valor):
        d=Path(tempfile.mkdtemp(dir=self.dir));f=d/'caso.bal';f.write_text(fuente)
        p=subprocess.run([str(self.root/'balc'),'-O2','-s',str(f)],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)
        meta=json.loads(f.with_suffix('.o2.json').read_text());fn=next(x for x in meta['funciones'] if x['nombre']=='main')
        p=subprocess.run([str(self.sonda),str(f.with_suffix('.mem')),str(meta['pc_fin']),str(fn['inicio_pc']),str(fn['fin_pc']),str(d),'100000'],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)
        obj=next(x for x in meta['objetos'] if x['funcion']=='main' and x['nombre']==nombre)
        direccion=65536-fn['marco']+obj['offset'];mem=(d/'sim-memoria.bin').read_bytes()
        self.assertEqual(int.from_bytes(mem[direccion:direccion+min(4,obj['bytes'])],'little'),valor)
        return meta
    # El callee recibe la dirección de datos: debe publicar 7 en su primer
    # elemento y devolver 4. Copiar el arreglo o perder la llamada rompería esto.
    def test_05_arreglo_alias_llamadas(self):
        s=(ROOT/'examples/dce/dce_llamadas.bal').read_text();self.caso(s,'datos',7);self.caso(s,'total',4)
    # La suma 1+2+3+4+5 exige leer el quinto argumento de la pila del caller,
    # usando el marco final del callee, además de los cuatro registros ABI.
    def test_06_cinco_argumentos(self):
        self.caso("create_funk #declare_int# f(a * declare_int,b * declare_int,c * declare_int,d * declare_int,e * declare_int) { give a gauss b gauss c gauss d gauss e; } create_funk #declare_infinite_void# main() { resultado * declare_int : f(1,2,3,4,5); }",'resultado',15)
    # El 37 solo se observa desde fuera de BAL. Si DCE confunde falta de
    # lectura interna con falta de observable, la memoria final queda sin valor.
    def test_07_valor_final_sin_lectura(self):
        self.caso("create_funk #declare_infinite_void# main() { resultado * declare_int : 37; }",'resultado',37)
    # Dos símbolos homónimos ocupan ámbitos distintos: escribir el interno
    # no debe convertir el 8 externo en 5 por una búsqueda textual del nombre.
    def test_08_ambitos(self):
        self.caso("create_funk #declare_infinite_void# main() { resultado * declare_int : 8; whether (declare_true) { resultado * declare_int : 2; resultado : resultado gauss 3; } }",'resultado',8)
    # Cada llamada conserva su RA, SP y temporales propios; sumar de 5 a 0
    # produce 15 y expone marcos compartidos o retornos que pisan al caller.
    def test_09_recursion(self):
        self.caso("create_funk #declare_int# f(n * declare_int) { whether (n < 1) { give 0; } also { give n gauss f(n neumann 1); } } create_funk #declare_infinite_void# main() { resultado * declare_int : f(5); }",'resultado',15)
    # El límite constante permite cinco copias reales. Además del resultado
    # 10, exigimos que el productor consumido declare una transformación LU.
    def test_10_unrolling_real(self):
        m=self.caso((ROOT/'tests/vliw/programas_finales/lu_constante.bal').read_text(),'resultados',10)
        self.assertGreater(m['unroll'],0);self.assertGreater(m['rename'],0)
    # El inicializador global debe ejecutarse antes de main: 17+2 da 19.
    # Una entrada en la tabla sin stores de arranque dejaría el global en cero.
    def test_11_globales(self):
        self.caso("declare_const limite * declare_int : 17; create_funk #declare_infinite_void# main() { resultado * declare_int : limite gauss 2; }",'resultado',19)
    # Cambiar A por B debe conservar el booleano vecino. Estos objetos de
    # un byte detectan stores de palabra que invadan memoria adyacente.
    def test_12_bytes_adyacentes(self):
        fuente="create_funk #declare_infinite_void# main() { letra * declare_char : $A$; valido * declare_boolean : declare_true; letra : $B$; resultado * declare_boolean : valido; }"
        self.caso(fuente,'valido',1);self.caso(fuente,'letra',66)
    # Los 300 enteros colocan resultado fuera del inmediato LSU. El valor
    # 12345 y los centinelas de la sonda detectan direcciones o auxiliares dañados.
    def test_13_marco_lejano(self):
        self.caso("create_funk #declare_infinite_void# main() { relleno * declare_int[300]; resultado * declare_int : 12345; }",'resultado',12345)
    # Novecientos objetos fuerzan el rechazo de la expansión física; la
    # capacidad estructural no autoriza publicar una imagen que desborde IMEM.
    def test_14_capacidad_sin_imagen(self):
        fuente="create_funk #declare_infinite_void# main() {"+" ".join('v%d * declare_int : %d;'%(i,i) for i in range(900))+" }"
        d=Path(tempfile.mkdtemp(dir=self.dir));f=d/'caso.bal';f.write_text(fuente)
        p=subprocess.run([str(self.root/'balc'),'-O2',str(f)],capture_output=True,text=True)
        self.assertNotEqual(p.returncode,0);self.assertFalse(f.with_suffix('.mem').exists())
    def productor(self,grupo):
        d=Path(tempfile.mkdtemp(dir=self.dir));f=d/'caso.bal'
        f.write_text("create_funk #declare_infinite_void# main() { resultado * declare_int : 37; }")
        p=subprocess.run([str(self.driver),grupo,str(f)],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)
        if p.stdout:print(p.stdout.strip(),flush=True)
    # El driver usa ocho registros y la misma cadena real, mientras el CLI
    # usa tres; así la API no queda acreditada solo para su configuración por defecto.
    def test_15_api_ocho_registros(self):self.productor('programa')
    # Se falla cada reserva anterior al primer éxito de un programa completo.
    # El driver exige salida vacía e identidad de AST/tabla después de cada error.
    def test_16_fallos_productor(self):self.productor('recursos_programa')
    # La función sin callers debe descartarse sin inventarle una continuación
    # de retorno; el único main observable conserva 37.
    def test_17_funcion_no_llamada(self):
        self.caso("create_funk #declare_int# innecesaria() { give 99; } create_funk #declare_infinite_void# main() { resultado * declare_int : 37; }",'resultado',37)
    # El divisor literal cero conserva el rechazo del backend BAL. No debe
    # publicarse una imagen que aparente éxito o simule una terminación inventada.
    def test_18_division_cero(self):
        d=Path(tempfile.mkdtemp(dir=self.dir));f=d/'caso.bal'
        f.write_text("create_funk #declare_infinite_void# main() { resultado * declare_int : 7 euclides 0; }")
        p=subprocess.run([str(self.root/'balc'),'-O2',str(f)],capture_output=True,text=True)
        self.assertNotEqual(p.returncode,0);self.assertFalse(f.with_suffix('.mem').exists())
    # Un contador de versiones sin WAR/WAW eliminadas no acredita la categoría.
    def test_19_categoria_exige_dependencia(self):
        sys.path.insert(0,str(ROOT/'tools/vliw'))
        from evaluar_integracion_productiva import categoria
        caso={'categoria':'renombramiento'};m={'rename':4,'war_eliminadas':0,'waw_eliminadas':0}
        self.assertFalse(categoria(caso,m));m['waw_eliminadas']=1;self.assertTrue(categoria(caso,m))
    # Un alias lógico se ubica con el offset original explícito, trasladado al
    # marco final; un valor distinto debe rechazarse en lugar de cambiarlo.
    def test_20_oraculo_alias(self):
        sys.path.insert(0,str(ROOT/'tools/vliw'))
        from evaluar_integracion_productiva import observables
        caso={'observables':{'cuadrado4':16},'offsets_o0':{'cuadrado4':4}}
        meta={'funciones':[{'nombre':'main','marco':16}],'objetos':[{'id':3,'nombre':'resultados','funcion':'main','offset':4,'bytes':8}]}
        datos=bytearray(65536);datos[65524:65528]=(16).to_bytes(4,'little')
        self.assertEqual(observables(caso,meta,datos)['cuadrado4']['direccion'],65524)
        datos[65524]=17
        with self.assertRaises(ValueError):observables(caso,meta,datos)
if __name__=='__main__':unittest.main()
