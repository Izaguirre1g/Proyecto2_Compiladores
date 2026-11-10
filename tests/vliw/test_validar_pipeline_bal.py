"""Comprueba B3 con el compilador real y fallos de transporte controlados."""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
# La dependencia compartida conserva su dueño y su API; solo resolvemos
# su ubicación plana o por módulos según el árbol que contiene esta prueba.
sys.path.insert(0, str(ROOT / 'tools/vliw'))
from rutas_vliw import fuente_compartida
spec = importlib.util.spec_from_file_location('b3', ROOT/'tools/vliw/validar_pipeline_bal.py')
b3 = importlib.util.module_from_spec(spec); spec.loader.exec_module(b3)


class PipelineBalTests(unittest.TestCase):
    # Compila las bibliotecas y sondas reales una vez para la suite y prepara los recursos
    # compartidos en directorios de prueba.
    @classmethod
    def setUpClass(cls):
        # El backend y M2 se construyen una vez. La copia permite ejecutar la
        # suite desde el repo original sin regenerar lexer, parser o binarios allí.
        cls.tmp = tempfile.TemporaryDirectory(prefix='pruebas-b3-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.d = Path(os.environ.get('B3_ARTEFACTOS', cls.tmp.name))
        cls.d.mkdir(parents=True, exist_ok=True)
        cls.comandos = []
        if os.environ.get('B3_BALC'):
            cls.balc = Path(os.environ['B3_BALC']).resolve()
        else:
            copia = cls.d/'compilador'
            shutil.copytree(ROOT, copia, ignore=shutil.ignore_patterns('.git', '__pycache__'))
            cls.run_cmd(['make', '-B', 'balc'], cwd=copia)
            cls.balc = copia/'balc'
        cls.sim, cls.adapter, cls.comparador = (cls.d/n for n in ('sim', 'memoria', 'comparador'))
        # Los ejecutables de prueba se reconstruyen en el temporal con el compilador y flags
        # seleccionados. La preparación no reutiliza binarios antiguos del repositorio.
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        comunes = ['src/Simulador_VLIW/sim_'+s+'.c' for s in ('ejecucion','metricas','pipeline','alu','lsu','bru','crypto','decode','cargador')]
        for fuente, salida, extra in (('tools/vliw/sim_vliw_metricas.c', cls.sim, []),
                                      ('tests/vliw/test_validar_pipeline_bal.c', cls.adapter, []),
                                      ('tools/vliw/comparar_vliw.c', cls.comparador, ['src/Simulador_VLIW/sim_comparador.c'])):
            cls.run_cmd(cc+flags+[fuente]+extra+comunes+['-o',salida])
        # NOP sirve únicamente como fixture de protocolo, no como programa
        # oficial ni evidencia de transformación del compilador.
        cls.nop = cls.d/'nop.mem'; cls.nop.write_text('0'*32+'\n')
        cls.reporte_nop = json.loads(cls.run_cmd([cls.sim,'mem',cls.nop,'finita','100','json']).stdout)
        cls.raw = struct.pack('<4s11I', b'BALB',1,48,64,0,48,16,0,64,0,16384,1)+bytes(16)
        cls.real = cls.d/'reales'
        cls.run_cmd([sys.executable,ROOT/'tools/vliw/validar_pipeline_bal.py','--balc',cls.balc,
                     '--simulador',cls.sim,'--presupuesto','10000','--salida',cls.real,
                     *[x for n in ('factorial','whale','ejercicio3_puntajes')
                       for x in ('--fuente',ROOT/'examples'/(n+'.bal'))]], esperado=2)
        cls.reales = json.loads((cls.real/'reporte.json').read_text())

    # Ejecuta los argumentos sin shell, conserva la salida y comprueba el código esperado; un
    # fallo inesperado invalida la prueba.
    @classmethod
    def run_cmd(cls, args, esperado=0, cwd=ROOT):
        p = subprocess.run(list(map(str,args)), cwd=cwd, capture_output=True, timeout=120,
                           env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        cls.comandos.append(dict(comando=list(map(str,args)), cwd=str(cwd), codigo=p.returncode,
                                 stdout=p.stdout.decode(errors='backslashreplace'),
                                 stderr=p.stderr.decode(errors='backslashreplace')))
        if p.returncode != esperado:
            raise AssertionError(f'{args}: {p.returncode} != {esperado}: {p.stderr!r}')
        return p

    # Conserva la evidencia solicitada y libera los recursos temporales comunes de la suite.
    @classmethod
    def tearDownClass(cls):
        if os.environ.get('B3_EVIDENCIA'):
            Path(os.environ['B3_EVIDENCIA']).write_text(json.dumps(dict(comandos=cls.comandos,
                                                reales=cls.reales), indent=2)+'\n')

    # Prepara archivos y directorios exclusivos del caso para que sus fallos o resultados no
    # contaminen las otras pruebas.
    def setUp(self):
        self.tmpcaso = tempfile.TemporaryDirectory(dir=self.d, prefix='caso-')
        self.addCleanup(self.tmpcaso.cleanup)
        self.c = Path(self.tmpcaso.name)
        (self.c/'fuentes').mkdir()
        self.fuente = self.c/'fuentes/programa.bal'
        self.fuente.write_text('create_funk #declare_infinite_void# main() {}\n')

    # Escribe un ejecutable temporal con comportamiento controlado para inyectar fallos de
    # compilación o simulación.
    def executable(self, nombre, cuerpo):
        p = self.c/nombre
        p.write_text('#!'+sys.executable+'\n'+cuerpo)
        p.chmod(0o700)
        return p

    # Prepara el compilador falso del caso para controlar retornos, artefactos y errores sin
    # modificar balc.
    def compiler(self, modo='ok'):
        return self.executable('compilador controlado',
            'import pathlib,sys,time\n'
            f'modo={modo!r}\n'
            'if modo=="timeout": time.sleep(10)\n'
            'if modo=="rechazo": sys.exit(1)\n'
            'p=pathlib.Path(sys.argv[sys.argv.index("-o")+1])\n'
            f'raw={self.raw!r}\n'
            'if modo!="sin_bin": p.write_bytes(raw)\n'
            'if modo!="sin_mem": p.with_suffix(".mem").write_text("0"*32+"\\n")\n')

    # Prepara la sonda falsa del caso para probar estados, métricas y errores del recorrido B3.
    def simulator(self, modo='terminado', reporte=None):
        # Partimos de una copia del reporte NOP para no alterar el esperado compartido. El modo
        # controlado cambia la respuesta del ejecutable falso; el recorrido B3 debe detectarla
        # como límite, error o JSON inválido según el escenario.
        m = copy.deepcopy(self.reporte_nop if reporte is None else reporte)
        if modo=='limite':
            m['estado']='LIMITE'
        return self.executable('simulador controlado',
            'import sys,time\n'+f'modo={modo!r}\n'+
            'if modo=="timeout": time.sleep(10)\n'
            'if modo=="carga": sys.stderr.write("cargador: error controlado\\n"); sys.exit(1)\n'
            'if modo=="ejecucion": sys.stderr.write("ejecucion: código 4\\n"); sys.exit(1)\n'
            'if modo=="json": print("{roto"); sys.exit(0)\n'+
            f'print({json.dumps(m)!r})\n'+
            'sys.exit(2 if modo=="limite" else 0)\n')

    # Construye los argumentos del análisis para este caso y permite sustituir componentes por
    # fallos controlados.
    def config(self, **kwargs):
        a = argparse.Namespace(balc=kwargs.pop('balc',None) or self.compiler(),
            simulador=kwargs.pop('simulador',None) or self.simulator(),
            fuente=[self.fuente],salida=self.c/'salida',presupuesto=100,timeout=3,oraculos=None)
        for k,v in kwargs.items(): setattr(a,k,v)
        return a

    # Ejecuta el lote con la configuración de prueba y devuelve su resultado para verificar
    # clasificación y publicación.
    def lote(self, **kwargs):
        return b3.publicar(self.config(**kwargs))['casos'][0]

    # Escribe el par BIN/MEM temporal del caso, permitiendo sustituir sus bytes o texto para
    # probar formatos incompatibles.
    def archivos(self, raw=None, mem=None):
        a,b = self.c/'archivo.bin',self.c/'archivo.mem'
        a.write_bytes(self.raw if raw is None else raw)
        b.write_text('0'*32+'\n' if mem is None else mem)
        return a,b

    # Prepara un contrato de resultados externos ligado al SHA del fuente para comprobar
    # satisfacción o incumplimiento.
    def oraculo(self, valor=0):
        p = self.c/'oraculos.json'
        p.write_text(json.dumps(dict(version=1,fuentes={hashlib.sha256(self.fuente.read_bytes()).hexdigest():
            dict(procedencia='ISA: x0 permanece cero; fixture de protocolo, no semántica BAL general',
                 alcance='invariante x0',registros={'0':valor})})))
        return p

    # Rechaza argumentos incompletos de B3 antes de ejecutar el compilador o M2.
    def test_01_argumentos(self):
        self.assertEqual(b3.main([]),64)

    # Comprueba que un compilador inexistente produzca error explícito.
    def test_02_compilador_inexistente(self):
        with self.assertRaises(OSError): b3.publicar(self.config(balc=self.c/'ausente'))

    # Comprueba que una sonda M2 inexistente impida validar el pipeline.
    def test_03_simulador_inexistente(self):
        with self.assertRaises(OSError): b3.publicar(self.config(simulador=self.c/'ausente'))

    # Rechaza una fuente ausente sin crear artefactos aparentes.
    def test_04_fuente_inexistente(self):
        with self.assertRaises(OSError): b3.publicar(self.config(fuente=[self.c/'ausente.bal']))

    # Recorre fuentes BAL reales y verifica sus artefactos y ejecución.
    def test_05_fuentes_reales_validas(self):
        self.assertEqual(len(self.reales['casos']),3)
        self.assertTrue(all(c['compilacion']['codigo']==0 for c in self.reales['casos']))

    # Compila BAL inválido y distingue el rechazo del frontend de un fallo del simulador.
    def test_06_bal_invalido_real(self):
        # Este rechazo proviene del frontend real, no del proceso controlado.
        self.fuente.write_text('create_funk { texto inválido\n')
        c=self.lote(balc=self.balc,simulador=self.sim)
        self.assertEqual(c['resultado'],'COMPILACION_FALLIDA')
        self.assertNotEqual(c['compilacion']['codigo'],0)

    # Comprueba que una compilación exitosa entregue los artefactos requeridos.
    def test_07_compilacion_exitosa(self):
        self.assertEqual(self.lote()['compilacion']['estado'],'COMPILACION_EXITOSA')

    # Conserva el diagnóstico cuando el compilador rechaza la entrada.
    def test_08_compilacion_rechazada(self):
        self.assertEqual(self.lote(balc=self.compiler('rechazo'))['resultado'],'COMPILACION_FALLIDA')

    # Simula compilación sin BIN y exige marcar la ausencia como fallo de infraestructura.
    def test_09_sin_bin(self):
        self.assertEqual(self.lote(balc=self.compiler('sin_bin'))['compilacion']['estado'],'SALIDA_INCOMPLETA')

    # Simula compilación sin MEM y exige rechazar la validación incompleta.
    def test_10_sin_mem(self):
        self.assertEqual(self.lote(balc=self.compiler('sin_mem'))['resultado'],'FORMATO_INVALIDO')

    # Valida un BALB correcto y su correspondencia binaria con MEM.
    def test_11_balb_valido(self):
        self.assertEqual(b3.validar_formato(*self.archivos())['codigo_bytes'],16)

    # Trunca el contenedor y comprueba que la validación estructural lo rechace.
    def test_12_truncamiento(self):
        with self.assertRaises(ValueError): b3.validar_formato(*self.archivos(self.raw[:40]))

    # Altera los tamaños declarados y exige coherencia con los bytes reales.
    def test_13_tamano_inconsistente(self):
        r=bytearray(self.raw);struct.pack_into('<I',r,12,65)
        with self.assertRaises(ValueError): b3.validar_formato(*self.archivos(r))

    # Entrega MEM diferente de BALB y exige detectar la discrepancia de código.
    def test_14_mem_diferente(self):
        with self.assertRaises(ValueError): b3.validar_formato(*self.archivos(mem='0'*31+'1\n'))

    # Cambia firma o versión y comprueba que no se acepte un contrato incompatible.
    def test_15_firma_version(self):
        for off,v in ((0,0),(4,2),(8,32)):
            r=bytearray(self.raw);struct.pack_into('<I',r,off,v)
            with self.assertRaises(ValueError): b3.validar_formato(*self.archivos(r))

    # Comprueba que código M2 de éxito y estado TERMINADO sean coherentes.
    def test_16_terminado(self):
        self.assertEqual(self.lote()['ejecucion']['balb']['estado'],'TERMINADO')

    # Comprueba que presupuesto agotado conserve el estado LIMITE y su código previsto.
    def test_17_limite(self):
        self.assertEqual(self.lote(simulador=self.simulator('limite'))['resultado'],'NO_TERMINADO')

    # Fuerza un fallo de carga y exige distinguirlo de una ejecución válida.
    def test_18_carga_fallida(self):
        c=self.lote(simulador=self.simulator('carga'))
        self.assertEqual(c['resultado'],'EJECUCION_FALLIDA')
        self.assertEqual(c['ejecucion']['balb']['carga'],'FAIL_O_NO_OBSERVABLE')

    # Provoca un fallo de ejecución y verifica la clasificación de error.
    def test_19_ejecucion_fallida(self):
        c=self.lote(simulador=self.simulator('ejecucion'))
        self.assertEqual(c['ejecucion']['balb']['carga'],'PASS')
        self.assertIsNone(c['metricas']['balb'])

    # Acepta métricas M1 completas y coherentes con el presupuesto y el estado.
    def test_20_metricas_validas(self):
        self.assertEqual(self.lote()['metricas']['mem'],self.reporte_nop)

    # Contrasta registros con un oráculo externo satisfecho y conserva su alcance.
    def test_21_oraculo_satisfecho(self):
        self.assertEqual(self.lote(oraculos=self.oraculo())['resultado'],'VALIDADO')

    # Introduce una diferencia frente al oráculo y exige informar los campos incumplidos.
    def test_22_oraculo_incumplido(self):
        self.assertEqual(self.lote(oraculos=self.oraculo(1))['resultado'],'ORACULO_FALLIDO')

    # Ejecuta sin oráculo y comprueba que la ausencia no se presente como corrección semántica.
    def test_23_sin_oraculo(self):
        self.assertEqual(self.lote()['resultado'],'VALIDADO_SIN_ORACULO')

    # Compara BALB con datos iniciales y MEM con DMEM cero y detecta condiciones iniciales
    # distintas.
    def test_24_datos_iniciales_distintos(self):
        r=bytearray(self.raw);struct.pack_into('<I',r,12,68);struct.pack_into('<I',r,36,4);r.extend(b'\x01\0\0\0')
        self.assertFalse(b3.validar_formato(*self.archivos(r))['iniciales_compatibles'])

    # Comprueba que ejecutar el pipeline no modifique el fuente original.
    def test_25_fuente_protegido(self):
        antes=self.fuente.read_bytes();self.lote(balc=self.balc,simulador=self.sim)
        self.assertEqual(self.fuente.read_bytes(),antes)
        self.assertEqual(list(self.fuente.parent.iterdir()),[self.fuente])

    # Ejecuta desde otro directorio y conserva la resolución de rutas.
    def test_26_otro_cwd(self):
        a=self.config()
        self.run_cmd([sys.executable,ROOT/'tools/vliw/validar_pipeline_bal.py','--balc',a.balc,
                     '--simulador',a.simulador,'--fuente',self.fuente,'--presupuesto','100',
                     '--salida',a.salida],cwd='/tmp')

    # Usa rutas con espacios sin dividirlas como argumentos del shell.
    def test_27_espacios(self):
        p=self.fuente.with_name('fuente con espacios.bal');self.fuente.rename(p)
        self.assertEqual(self.lote(fuente=[p])['resultado'],'VALIDADO_SIN_ORACULO')

    # Repite programas reales y comprueba el determinismo de sus resultados.
    def test_28_determinismo_real(self):
        c=self.lote(balc=self.balc,simulador=self.sim,presupuesto=10000)
        r=b3.publicar(self.config(balc=self.balc,simulador=self.sim,presupuesto=10000,salida=self.c/'segunda'))
        self.assertEqual(c,r['casos'][0])

    # Fuerza un timeout del compilador y comprueba su diagnóstico y limpieza.
    def test_29_timeout_compilador(self):
        self.assertEqual(self.lote(balc=self.compiler('timeout'),timeout=.1)['resultado'],'ERROR_INFRAESTRUCTURA')

    # Fuerza un timeout de M2 y exige clasificar la ejecución como infraestructura fallida.
    def test_30_timeout_simulador(self):
        self.assertEqual(self.lote(simulador=self.simulator('timeout'),timeout=.1)['resultado'],'ERROR_INFRAESTRUCTURA')

    # Inyecta un fallo de publicación y comprueba que no se presente una salida parcial como
    # completa.
    def test_31_publicacion_fallida(self):
        with mock.patch.object(b3.os,'rename',side_effect=OSError('fallo controlado')):
            with self.assertRaises(OSError): b3.publicar(self.config())
        self.assertFalse((self.c/'salida').exists())
        self.assertFalse(list(self.c.glob('.b3-*')))

    # Verifica que una ejecución normal libere sus temporales.
    def test_32_limpieza_normal(self):
        self.lote();self.assertFalse(list(self.c.glob('.b3-*')))

    # Procesa varias fuentes sin mezclar artefactos ni métricas.
    def test_33_varias_fuentes(self):
        otra=self.fuente.with_name('otra.bal');otra.write_bytes(self.fuente.read_bytes())
        r=b3.publicar(self.config(fuente=[self.fuente,otra]))
        self.assertEqual([c['id'] for c in r['casos']],['caso-001','caso-002'])

    # Comprueba que el repositorio conserve sus archivos tras ejecutar el lote.
    def test_34_repositorio_intacto(self):
        # La compilación real publica exclusivamente en la evidencia externa.
        protegidos=[fuente_compartida(ROOT, 'src/cli.c'),ROOT/'Makefile',ROOT/'examples/factorial.bal']
        antes=[b3.identidad(p) for p in protegidos]
        self.lote(balc=self.balc,simulador=self.sim)
        self.assertEqual(antes,[b3.identidad(p) for p in protegidos])

    # Integra el recorrido con B2 real sin cambiar el contrato del comparador.
    def test_35_b2_real(self):
        # Son imágenes distintas de dos formatos reales, con DMEM inicial
        # compatible. B1 debe informar INCONCLUSO por el bucle, no equivalencia.
        j,csv=self.c/'b2.json',self.c/'b2.csv'
        self.run_cmd([sys.executable,ROOT/'tools/vliw/benchmarks_vliw.py','--manifest',
                     self.real/'manifiesto-b2.json','--comparador',self.comparador,
                     '--json',j,'--csv',csv],esperado=3)
        datos=json.loads(j.read_text())
        self.assertEqual(len(datos['casos']),3)
        self.assertTrue(all(c['b1']['resultado_comparacion']=='INCONCLUSO' for c in datos['casos']))

    # Contrasta los resultados DMEM de programas reales mediante sus oráculos declarados.
    def test_36_oraculos_dmem_reales(self):
        # Esperados y posiciones vienen de test_resolucion_saltos.c, no de
        # ejecutar M2 para construirlos. La comparación usa los 65536 bytes.
        esperados=({65528:120,65532:6},None,{65520:24,65524:0,65528:13,65532:37})
        evidencia=[]
        memoria_dir=self.d/'dmem';memoria_dir.mkdir()
        for c,esperado in zip(self.reales['casos'],esperados):
            memorias=[]
            for formato in ('balb','mem'):
                destino=memoria_dir/(c['id']+formato+'.dmem')
                self.run_cmd([self.adapter,formato,self.real/c['artefactos'][formato]['ruta'],
                             '10000',destino],esperado=2)
                datos=destino.read_bytes();self.assertEqual(len(datos),65536);memorias.append(datos)
                if esperado:
                    for direccion,valor in esperado.items():
                        # Los valores esperados proceden de oráculos anteriores e independientes
                        # de esta ejecución. Se leen como palabras little-endian en sus
                        # direcciones de DMEM, evitando derivar el esperado del propio
                        # simulador.
                        self.assertEqual(struct.unpack_from('<I',datos,direccion)[0],valor)
            self.assertEqual(memorias[0],memorias[1])
            evidencia.append(dict(id=c['id'],bytes_comparados=65536,iguales=True,
                oraculo='SATISFECHO_PARCIAL' if esperado else 'ORACULO_NO_DISPONIBLE',
                esperado=esperado,terminacion='LIMITE'))
        (self.d/'oraculos-dmem.json').write_text(json.dumps(evidencia,indent=2)+'\n')

    # Rechaza JSON corrupto de las herramientas externas.
    def test_37_json_corrupto(self):
        self.assertEqual(self.lote(simulador=self.simulator('json'))['resultado'],'ERROR_INFRAESTRUCTURA')

    # Altera métricas o su coherencia con M2 y exige rechazo.
    def test_38_metricas_incoherentes(self):
        for campo,valor in (('ciclos',101),('ipc',-1),('version',True),('estado','LIMITE')):
            m=copy.deepcopy(self.reporte_nop);m[campo]=valor
            with self.assertRaises(ValueError): b3.validar_metricas(m,0,100)

    # Comprueba que una salida existente se conserve y no sea reemplazada.
    def test_39_salida_existente_conservada(self):
        p=self.c/'salida';p.mkdir();(p/'ajeno').write_text('conservar')
        with self.assertRaises(ValueError): b3.publicar(self.config())
        self.assertEqual((p/'ajeno').read_text(),'conservar')

    # Copia una dependencia BAL local y verifica que el compilador pueda resolverla.
    def test_40_dependencia_local(self):
        # La dependencia se copia sin interpretar bring ni enlazar el frontend.
        p=self.fuente.with_name('matematicas.bal');p.write_text('módulo de demostración')
        c=self.lote();self.assertEqual(len(c['fuente']['dependencias']),2)
        self.assertEqual((self.c/'salida/caso-001/fuentes/matematicas.bal').read_bytes(),p.read_bytes())

    # Rechaza límites y tipos de argumentos fuera del contrato.
    def test_41_limites_argumentos(self):
        base=['--balc',str(self.balc),'--simulador',str(self.sim),'--fuente',str(self.fuente),'--salida',str(self.c/'salida')]
        for v in ('-1',str(1<<64),'1.5'):
            self.assertEqual(b3.main(base+['--presupuesto',v]),64)
        self.assertEqual(b3.main(base+['--presupuesto','0','--timeout','nan']),64)

    # Introduce una dependencia simbólica y exige no seguirla como fuente propia.
    def test_42_symlink_dependencia(self):
        (self.fuente.parent/'alias.bal').symlink_to(self.fuente)
        self.assertEqual(self.lote()['resultado'],'ERROR_INFRAESTRUCTURA')

    # Verifica que una salida inválida real del backend se informe sin corregirla desde B3.
    def test_43_salida_invalida_real_backend(self):
        # Un rechazo de generación no deja un resultado viejo reutilizable.
        self.fuente.write_text('declare_const A * declare_int : 1 euclides 0;\ncreate_funk #declare_infinite_void# main() {}\n')
        c=self.lote(balc=self.balc,simulador=self.sim)
        self.assertEqual(c['resultado'],'COMPILACION_FALLIDA');self.assertEqual(c['artefactos'],{})

    # Asocia un oráculo a otro SHA y comprueba que no se aplique a una fuente diferente.
    def test_44_oraculo_sha_ajeno(self):
        o=self.oraculo();self.fuente.write_text(self.fuente.read_text()+'\n')
        self.assertEqual(self.lote(oraculos=o)['resultado'],'VALIDADO_SIN_ORACULO')

    # Omite datos de un protocolo y verifica que no se sustituyan por valores cero.
    def test_45_ausencias_no_son_cero(self):
        c=self.lote(simulador=self.simulator('carga'))
        self.assertIsNone(c['metricas']['mem'])
        self.assertFalse(c['oraculo']['mem']['disponible'])

    # Comprueba que globales BAL reales no se usen para fabricar un par de optimización.
    def test_46_globales_reales_no_preparan_par(self):
        # BALB precarga 5; MEM empieza en cero, aunque el arranque luego lo
        # escriba. B3 debe evitar el par y B1 rechazar esas condiciones iniciales.
        self.fuente.write_text('declare_const A * declare_int : 5;\ncreate_funk #declare_infinite_void# main() { n * declare_int : A; }\n')
        a=self.config(balc=self.balc,simulador=self.sim,presupuesto=1000)
        r=b3.publicar(a)
        self.assertEqual(r['b1_b2']['pares_demostracion'],0)
        c=r['casos'][0]
        p=self.run_cmd([self.comparador,'balb',a.salida/c['artefactos']['balb']['ruta'],
                       'mem',a.salida/c['artefactos']['mem']['ruta'],'finita','1000','json'],esperado=3)
        b=json.loads(p.stdout)
        # BALB y MEM pueden compartir código y aun así comenzar con distinta DMEM. Esa
        # diferencia inicial impide preparar un par comparable; no debe ocultarse porque el
        # arranque luego escriba datos.
        self.assertFalse(b['iniciales_compatibles'])
        self.assertEqual(b['bytes_iniciales_distintos'],1)

    # Inyecta un formato inválido en el recorrido y exige detener su validación.
    def test_47_formato_invalido_en_recorrido(self):
        exe=self.compiler()
        with exe.open('a') as f:
            f.write('p.write_bytes(b"truncado")\n')
        c=self.lote(balc=exe)
        self.assertEqual(c['resultado'],'FORMATO_INVALIDO')
        self.assertEqual(c['compilacion']['estado'],'SALIDA_INVALIDA')
        self.assertEqual(c['ejecucion'],{})

    # Entrega fuentes duplicadas y comprueba su rechazo.
    def test_48_fuentes_duplicadas(self):
        with self.assertRaises(ValueError): b3.publicar(self.config(fuente=[self.fuente,self.fuente]))

    # Intenta publicar dentro de la carpeta del fuente y exige preservar sus entradas.
    def test_49_salida_dentro_del_fuente(self):
        with self.assertRaises(ValueError): b3.publicar(self.config(salida=self.fuente.parent/'salida'))

    # Entrega un oráculo vacío o corrupto y comprueba que no acredite corrección.
    def test_50_oraculo_vacio_o_corrupto(self):
        p=self.oraculo();o=json.loads(p.read_text())
        next(iter(o['fuentes'].values()))['registros']={}
        p.write_text(json.dumps(o))
        with self.assertRaises(ValueError): b3.cargar_oraculos(p)

    # Distingue el texto hexadecimal MEM de los bytes little-endian de código BALB.
    def test_51_mem_textual_no_es_codigo(self):
        a,b=self.archivos()
        f=b3.validar_formato(a,b)
        self.assertEqual(f['codigo_bytes'],16)
        self.assertEqual(b.stat().st_size,33)

    # Trunca la información de un trap y exige un historial M1 completo.
    def test_52_trap_incompleto(self):
        m=copy.deepcopy(self.reporte_nop);m['traps']=1
        with self.assertRaises(ValueError): b3.validar_metricas(m,0,100)

    # Comprueba que cada proceso externo tenga cwd propio y no genere auxiliares en el
    # repositorio original.
    def test_53_directorio_del_proceso_aislado(self):
        # Una salida auxiliar relativa del compilador debe quedar en el caso,
        # incluso al lanzar la herramienta desde otro directorio.
        exe=self.compiler()
        with exe.open('a') as f:
            f.write('pathlib.Path("marca-cwd").write_text("aislado")\n')
        antes=Path.cwd()
        self.lote(balc=exe)
        # Los procesos externos deben ejecutar sus salidas auxiliares dentro del directorio
        # aislado. Al regresar, el coordinador restaura el cwd del llamador para no contaminar
        # validaciones posteriores.
        self.assertEqual(Path.cwd(),antes)
        self.assertEqual((self.c/'salida/caso-001/marca-cwd').read_text(),'aislado')


if __name__=='__main__':
    unittest.main(verbosity=2)
