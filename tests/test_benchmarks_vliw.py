"""Verifica B2 con B1 real y respuestas adversariales derivadas de su JSON."""
import base64
import copy
import csv
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

# Las importaciones de fixtures tampoco deben dejar __pycache__ en el repo.
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('b2_probado', ROOT/'tools/benchmarks_vliw.py')
b2 = importlib.util.module_from_spec(spec); spec.loader.exec_module(b2)


class BenchmarksTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='pruebas-b2-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.comandos = []
        cls.integraciones = []
        cls.lotes_probados = []
        # Se reutilizan puentes CB/S7 aprobados. B1 se construye una sola vez
        # para todos los casos del proceso de prueba; B2 jamás lo compila.
        spec = importlib.util.spec_from_file_location('fixtures_b1_b2', ROOT/'tests/test_sim_comparador.py')
        cls.fixtures = importlib.util.module_from_spec(spec); spec.loader.exec_module(cls.fixtures)
        cls.cc = shlex.split(os.environ.get('CC', 'cc'))
        cls.flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        cls.b1, cls.cb, cls.s7 = (cls.dir/n for n in ('comparar_vliw', 'cb', 's7'))
        fuentes = ['tools/comparar_vliw.c'] + ['src/sim_'+s+'.c' for s in
                   ('comparador', 'ejecucion', 'metricas', 'pipeline', 'alu', 'lsu', 'bru', 'crypto', 'decode', 'cargador')]
        cls.compilar(fuentes, cls.b1)
        puente = cls.dir/'puente.c'; puente.write_text(cls.fixtures.m2.m1.s8.CODIFICADOR)
        cls.compilar([puente, 'src/codificacion_binaria.c'], cls.cb)
        cls.compilar(['tests/test_vliw_emisor.c', 'src/vliw_scheduler.c', 'src/vliw_emisor.c',
                      'src/codificacion_binaria.c', 'src/sim_decode.c'], cls.s7)
        cls.nop = cls.imagen('nop\n', 'nop')
        cls.alu = cls.imagen('sumai x6,x0,19 | nop | nop | nop\n', 'alu')
        p = cls.ejecutar([cls.b1, 'mem', cls.nop, 'mem', cls.nop, 'finita', 100, 'json'])
        # Todas las respuestas simuladas parten de un documento B1 real. Las
        # mutaciones prueban validación/transporte, no optimizaciones reales.
        cls.base = json.loads(p.stdout)

    @classmethod
    def ejecutar(cls, args, esperado=0, cwd=None):
        args = list(map(str, args))
        p = subprocess.run(args, cwd=cwd or ROOT, capture_output=True, timeout=120,
                           env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        cls.comandos.append(dict(comando=args, cwd=str(cwd or ROOT), codigo=p.returncode,
                                 stdout=p.stdout.decode('utf-8',errors='backslashreplace'),
                                 stderr=p.stderr.decode('utf-8',errors='backslashreplace')))
        if p.returncode != esperado:
            raise AssertionError(f'{args}: {p.returncode}, esperado {esperado}\n{p.stdout!r}\n{p.stderr!r}')
        return p

    @classmethod
    def compilar(cls, fuentes, salida):
        cls.ejecutar(cls.cc+cls.flags+list(map(str, fuentes))+['-o', salida])

    @classmethod
    def imagen(cls, asm, nombre):
        fuente, mem, bin_ = (cls.dir/(nombre+s) for s in ('.asm', '.mem', '.bin'))
        fuente.write_text(asm)
        cls.ejecutar([cls.cb, fuente, mem, bin_])
        return mem

    @classmethod
    def tearDownClass(cls):
        e = dict(comandos=cls.comandos, integraciones=cls.integraciones, lotes=cls.lotes_probados)
        print('B2: '+json.dumps({'integraciones_reales': len(cls.integraciones), 'comandos': len(cls.comandos)}))
        if os.environ.get('B2_EVIDENCIA'):
            Path(os.environ['B2_EVIDENCIA']).write_text(json.dumps(e, indent=2)+'\n')

    def setUp(self):
        self.t = tempfile.TemporaryDirectory(prefix='caso-b2-', dir=self.dir)
        self.addCleanup(self.t.cleanup)
        self.d = Path(self.t.name)

    def caso(self, id_='caso', categoria='demostracion', **extra):
        return dict(id=id_, categoria=categoria, referencia={'formato': 'mem', 'ruta': str(self.nop)},
                    candidato={'formato': 'mem', 'ruta': str(self.nop)}, modo='finita', presupuesto=100, **extra)

    def manifiesto(self, casos=None, **cambios):
        m = dict(version=1, casos=casos if casos is not None else [self.caso()]); m.update(cambios)
        p = self.d/'manifiesto.json'; p.write_text(json.dumps(m), encoding='utf-8')
        return p

    def lote(self, casos=None, exe=None, timeout=3, exigir=False):
        m = b2.leer_manifiesto(self.manifiesto(casos))
        r = b2.ejecutar_lote(m, exe or self.b1, timeout, exigir)
        type(self).lotes_probados.append(dict(prueba=self._testMethodName,reporte=copy.deepcopy(r)))
        return r

    def falso(self, cuerpo):
        p = self.d/'comparador falso'
        p.write_text('#!'+sys.executable+'\n'+cuerpo+'\n')
        p.chmod(0o755)
        return p

    def respuesta(self, documento=None, codigo=0):
        # Las rutas se copian de argv igual que en B1; así la validación puede
        # detectar específicamente una respuesta de otros archivos.
        d = self.base if documento is None else documento
        return self.falso('import json,sys\nr=json.loads('+repr(json.dumps(d))+')\nr["referencia"]["ruta"]=sys.argv[2]\nr["candidato"]["ruta"]=sys.argv[4]\nprint(json.dumps(r))\nsys.exit('+str(codigo)+')')

    def cli(self, manifiesto, exe=None, esperado=0, extra=(), cwd=None):
        args = [sys.executable, ROOT/'tools/benchmarks_vliw.py', '--manifest', manifiesto,
                '--comparador', exe or self.b1, '--json', self.d/'salida.json', '--csv', self.d/'salida.csv',
                '--timeout', '3', *extra]
        return self.ejecutar(args, esperado, cwd)

    def test_01_manifiesto_valido(self):
        p = self.manifiesto(); m = b2.leer_manifiesto(p)
        self.assertEqual(m['casos'][0]['referencia']['sha256'], hashlib.sha256(self.nop.read_bytes()).hexdigest())
        self.assertEqual(m['sha256'], hashlib.sha256(p.read_bytes()).hexdigest())

    def test_02_vacio_y_cobertura(self):
        r = self.lote([])
        self.assertEqual(r['resumen']['registrados'], 0)
        self.assertEqual(r['cobertura']['faltantes'], 15)
        self.assertFalse(r['cobertura']['completa_registrada'])
        self.assertEqual(b2.codigo_lote(r), 0)
        self.assertIsNone(r['resumen']['rendimiento_validado_por_modo']['finita']['ciclos_referencia'])
        self.assertEqual(b2.codigo_lote(self.lote([], exigir=True)), 5)

    def test_03_json_utf8_y_duplicados(self):
        p = self.d/'m.json'
        for contenido in (b'{', b'\xff', b'{"version":1,"version":1,"casos":[]}',
                          b'{"version":NaN,"casos":[]}', b'{"version":1e309,"casos":[]}', b'['*2000+b']'*2000):
            with self.subTest(contenido=contenido[:30]):
                p.write_bytes(contenido)
                with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(p)

    def test_04_version_y_estructura(self):
        for m in (dict(version=2, casos=[]), dict(version=True, casos=[]), dict(version=1, casos={}),
                  dict(version=1, casos=[], comando='ignorado')):
            p = self.d/'m.json'; p.write_text(json.dumps(m))
            with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(p)

    def test_05_id_duplicado_y_vacio(self):
        for casos in ([self.caso(), self.caso()], [self.caso('')], [self.caso(' '*3)]):
            with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(self.manifiesto(casos))

    def test_06_categoria_campos_formato_modo(self):
        mutaciones = [dict(categoria='otro'), dict(modo='otro'), dict(comando='echo')]
        for cambios in mutaciones:
            c = self.caso(); c.update(cambios)
            with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(self.manifiesto([c]))
        c = self.caso(); c['referencia']['formato'] = 'crudo'
        with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(self.manifiesto([c]))

    def test_07_presupuestos(self):
        for v in (-1, 'muchos', True, 1.0, 1 << 64, None):
            c = self.caso(); c['presupuesto'] = v
            with self.subTest(valor=v), self.assertRaises(b2.ManifiestoInvalido):
                b2.leer_manifiesto(self.manifiesto([c]))
        for v in (0, (1 << 64)-1):
            c = self.caso(); c['presupuesto'] = v
            self.assertEqual(b2.leer_manifiesto(self.manifiesto([c]))['casos'][0]['presupuesto'], v)

    def test_08_faltante_valida_antes_de_ejecutar(self):
        a, b = self.caso('primero'), self.caso('faltante'); b['candidato']['ruta'] = 'no-existe'
        exe = self.falso('raise RuntimeError("no debe invocarse")')
        self.cli(self.manifiesto([a,b]), exe, 4)
        self.assertFalse((self.d/'salida.json').exists())

    def test_09_rutas_relativas(self):
        (self.d/'relativo.mem').write_bytes(self.nop.read_bytes())
        c = self.caso(); c['referencia']['ruta'] = c['candidato']['ruta'] = 'relativo.mem'
        self.cli(self.manifiesto([c]), cwd='/tmp')
        r = json.loads((self.d/'salida.json').read_text())
        self.assertEqual(r['casos'][0]['resultado'], 'EQUIVALENTE')

    def test_10_real_equivalente(self):
        r = self.lote(); self.assertEqual(r['casos'][0]['resultado'], 'EQUIVALENTE')
        self.assertTrue(r['casos'][0]['rendimiento_valido'])
        type(self).integraciones.append(dict(nombre='identico', reporte=r))

    def test_11_real_diferente(self):
        c = self.caso(); c['candidato']['ruta'] = str(self.alu)
        r = self.lote([c]); self.assertEqual(r['casos'][0]['resultado'], 'DIFERENTE')
        self.assertEqual(b2.codigo_lote(r), 2); self.assertFalse(r['casos'][0]['rendimiento_valido'])

    def test_12_real_inconcluso_metricas_null(self):
        c = self.caso(); c['presupuesto'] = 1
        r = self.lote([c]); self.assertEqual(b2.codigo_lote(r), 3)
        # Un BALB legal con datos distintos impide comparar iniciales. No se
        # convierten sus diferencias de métricas null en un delta de cero.
        mem = bytes.fromhex(self.nop.read_text().strip())[::-1]
        import struct
        balb = self.d/'datos.balb'; balb.write_bytes(b'BALB'+struct.pack('<11I',1,48,65,0,48,16,0,64,1,0,0)+mem+b'\x01')
        c = self.caso(); c['candidato'] = dict(formato='balb',ruta=str(balb))
        r = self.lote([c]); self.assertEqual(r['casos'][0]['resultado'], 'INCONCLUSO')
        self.assertIsNone(r['casos'][0]['b1']['diferencias_metricas'])
        fila = list(csv.DictReader(io.StringIO(b2.generar_csv(r))))[0]
        self.assertEqual(fila['diferencia_ciclos'], '')

    def test_13_error_b1_y_continuacion(self):
        mal = self.d/'truncado.balb'; mal.write_bytes(b'BALB')
        c = self.caso('error'); c['candidato'] = dict(formato='balb',ruta=str(mal))
        r = self.lote([c,self.caso('siguiente')])
        self.assertEqual([x['resultado'] for x in r['casos']], ['ERROR','EQUIVALENTE'])
        self.assertEqual(b2.codigo_lote(r), 6)
        self.assertIn('cargador',r['casos'][0]['diagnostico']['stderr'])

    def test_14_json_b1_corrupto(self):
        for cuerpo in ('print("{")', 'import os;os.write(1,b"\\xff")', 'print("NaN")', 'pass'):
            r = self.lote(exe=self.falso(cuerpo))
            self.assertEqual(r['casos'][0]['resultado'], b2.INFRA)
            self.assertIsNotNone(r['casos'][0]['diagnostico']['error'])
            self.assertIn('stdout_no_validado_base64', r['casos'][0]['diagnostico'])

    def test_15_codigo_inconsistente(self):
        for codigo in (2,3,1,64,9):
            r = self.lote(exe=self.respuesta(codigo=codigo))
            self.assertEqual(r['casos'][0]['resultado'], b2.INFRA)
            self.assertIn('inconsistentes',r['casos'][0]['diagnostico']['error'])

    def test_16_timeout_y_recoleccion(self):
        pid = self.d/'pid'
        exe = self.falso('import os,time\nopen('+repr(str(pid))+',"w").write(str(os.getpid()))\ntime.sleep(10)')
        r = self.lote(exe=exe, timeout=0.2)
        self.assertEqual(r['casos'][0]['resultado'], b2.INFRA)
        self.assertIn('timeout',r['casos'][0]['diagnostico']['error'])
        with self.assertRaises(ProcessLookupError): os.kill(int(pid.read_text()),0)

    def test_17_multiple_orden_y_agregacion(self):
        a, b, c = self.caso('eq'), self.caso('diff'), self.caso('inc')
        b['candidato']['ruta'] = str(self.alu); c['presupuesto'] = 0
        r = self.lote([a,b,c])
        self.assertEqual([x['id'] for x in r['casos']], ['eq','diff','inc'])
        self.assertEqual([x['resultado'] for x in r['casos']], ['EQUIVALENTE','DIFERENTE','INCONCLUSO'])
        v = r['resumen']['rendimiento_validado_por_modo']['finita']
        self.assertEqual((v['casos'],v['ciclos_referencia'],v['ciclos_candidato']), (1,4,4))
        type(self).integraciones.append(dict(nombre='mixto',reporte=r))

    def test_18_demostracion_no_oficial(self):
        r = self.lote([self.caso(str(i)) for i in range(3)])
        self.assertEqual(r['cobertura']['registrados'],0)
        self.assertEqual(r['categorias']['demostracion']['registrados'],3)
        self.assertEqual(r['cobertura']['faltantes'],15)

    def test_19_cobertura_parcial(self):
        r = self.lote([self.caso('a','dce')], exigir=True)
        self.assertEqual(r['categorias']['dce']['faltantes'],2)
        self.assertEqual(r['cobertura']['faltantes'],14)
        self.assertEqual(b2.codigo_lote(r),5)

    def test_20_cobertura_controlada(self):
        # Solo verifica conteo con la misma imagen NOP y una respuesta B1
        # controlada. No son quince optimizaciones ni programas oficiales.
        casos = [self.caso(f'conteo-{cat}-{i}',cat,descripcion='Fixture de conteo, sin optimización') for cat in b2.CATEGORIAS for i in range(3)]
        r = self.lote(casos, self.respuesta(), exigir=True)
        self.assertTrue(r['cobertura']['completa_registrada'])
        self.assertTrue(r['cobertura']['completa_estricta'])
        self.assertEqual(b2.codigo_lote(r),0)
        casos.append(self.caso('excedente','dce'))
        self.assertEqual(b2.codigo_lote(self.lote(casos,self.respuesta(),exigir=True)),5)

    def test_21_porcentajes_y_cero(self):
        # Son agregados de resultados controlados, no diferencias recalculadas
        # por caso. Una referencia cero mantiene el porcentaje no definido.
        d = copy.deepcopy(self.base)
        for lado in ('referencia','candidato'):
            d[lado]['metricas']['ciclos'] = 0
        d['diferencias_metricas']['porcentaje_definido'] = False
        d['diferencias_metricas']['porcentaje_ciclos'] = None
        r = self.lote(exe=self.respuesta(d))
        self.assertIsNone(r['resumen']['rendimiento_validado_por_modo']['finita']['reduccion_porcentual_referencia'])

    def test_22_csv_y_formulas(self):
        r = self.lote([self.caso('=SUM(1)', descripcion='\t=HYPERLINK("x")',procedencia={'nivel_candidato':'-O2'})])
        filas = list(csv.DictReader(io.StringIO(b2.generar_csv(r))))
        self.assertEqual(tuple(filas[0]), b2.COLUMNAS)
        self.assertTrue(filas[0]['id'].startswith("'="))
        self.assertTrue(filas[0]['descripcion'].startswith("'\t="))
        self.assertEqual(filas[0]['procedencia_verificada'],'False')
        self.assertEqual(r['casos'][0]['id'],'=SUM(1)')

    def test_23_determinismo(self):
        m = b2.leer_manifiesto(self.manifiesto())
        a = b2.ejecutar_lote(m,self.b1); b = b2.ejecutar_lote(m,self.b1)
        self.assertEqual(json.dumps(a,sort_keys=True),json.dumps(b,sort_keys=True))
        self.assertEqual(b2.generar_csv(a),b2.generar_csv(b))

    def test_24_publicacion_y_fallos(self):
        r = self.lote(); a,b = self.d/'out.json',self.d/'out.csv'
        b2.publicar(r,a,b,[self.nop])
        self.assertEqual(json.loads(a.read_text()),r)
        previo_a,previo_b = a.read_bytes(),b.read_bytes()
        real = os.replace; veces = 0
        def fallar_primero(x,y):
            raise OSError('fallo de reemplazo')
        with mock.patch.object(b2.os,'replace',side_effect=fallar_primero), self.assertRaises(b2.PublicacionError) as e:
            b2.publicar(r,a,b,[self.nop])
        self.assertEqual(e.exception.publicados,[])
        self.assertEqual((a.read_bytes(),b.read_bytes()),(previo_a,previo_b))
        def fallar_segundo(x,y):
            nonlocal veces
            veces += 1
            if veces == 2: raise OSError('fallo CSV')
            return real(x,y)
        with mock.patch.object(b2.os,'replace',side_effect=fallar_segundo), self.assertRaises(b2.PublicacionError) as e:
            b2.publicar(r,a,b,[self.nop])
        self.assertEqual(e.exception.publicados,[str(a)])
        self.assertEqual(list(self.d.glob('.b2-*')),[])

    def test_25_destinos_y_entradas(self):
        p = self.manifiesto(); original = p.read_bytes()
        self.cli(p, esperado=1, extra=['--json',str(p)])
        self.assertEqual(p.read_bytes(),original)
        enlace = self.d/'enlace'; os.link(self.nop,enlace)
        with self.assertRaises(ValueError): b2.destinos_validos((enlace,self.d/'otra'),[self.nop])
        with self.assertRaises(ValueError): b2.destinos_validos((self.d/'igual',self.d/'igual'),[])

    def test_26_rutas_especiales_sin_shell(self):
        especial = self.d/'nombre ; $(touch NO_CREAR) "á".mem'; especial.write_bytes(self.nop.read_bytes())
        c = self.caso(); c['candidato']['ruta'] = str(especial)
        r = self.lote([c]); self.assertEqual(r['casos'][0]['resultado'],'EQUIVALENTE')
        self.assertFalse((ROOT/'NO_CREAR').exists())

    def test_27_limites_y_tipos(self):
        for casos in ([self.caso('x'*97)], [self.caso(descripcion='x'*4097)], [self.caso(str(i)) for i in range(b2.MAX_CASOS+1)]):
            with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(self.manifiesto(casos))
        p = self.d/'grande.json';p.write_bytes(b' '*(b2.MAX_MANIFIESTO+1))
        with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(p)
        c = self.caso();c['id']='\ud800'
        with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(self.manifiesto([c]))

    def test_28_respuestas_incompletas(self):
        for modificar in (lambda x:x.pop('version'), lambda x:x.update(version=True),
                          lambda x:x['referencia'].update(estado=2),
                          lambda x:x['referencia']['metricas'].update(ciclos=True),
                          lambda x:x.update(contrato='otro'), lambda x:x.update(diferencias_metricas=None)):
            d = copy.deepcopy(self.base);modificar(d)
            self.assertEqual(self.lote(exe=self.respuesta(d))['casos'][0]['resultado'],b2.INFRA)
        d=copy.deepcopy(self.base);d['diferencias_metricas']['ciclos']['magnitud']=(1<<64)-1
        # La representación unsigned válida se conserva; B2 no intenta
        # recomputar ni "corregir" las diferencias ya declaradas por B1.
        r=self.lote(exe=self.respuesta(d))
        self.assertEqual(r['casos'][0]['b1']['diferencias_metricas']['ciclos']['magnitud'],(1<<64)-1)

    def test_29_salida_excesiva_y_limite_lote(self):
        exe = self.falso('import os\nwhile True: os.write(1,b"x"*65536)')
        r = self.lote(exe=exe,timeout=2)
        self.assertIn('excede',r['casos'][0]['diagnostico']['error'])
        with mock.patch.object(b2,'MAX_LOTE',0):
            r = self.lote()
        self.assertFalse(r['casos'][0]['intentado'])
        self.assertEqual(r['casos'][0]['resultado'],b2.INFRA)

    def test_30_argumentos_cli(self):
        for args in ([],['--desconocido']):
            self.ejecutar([sys.executable,ROOT/'tools/benchmarks_vliw.py',*args],64)
        for timeout in ('nan','inf','0','-1','3601'):
            self.cli(self.manifiesto(),esperado=64,extra=['--timeout',timeout])
        with self.assertRaises(b2.InfraestructuraError): self.lote(exe=self.d/'no-existe')

    def test_31_procedencia(self):
        # Declarar una fuente real tampoco demuestra que haya generado el NOP.
        fuente = self.d/'original.bal';fuente.write_bytes((ROOT/'examples/factorial.bal').read_bytes())
        c = self.caso(procedencia={'fuente_bal':'original.bal','nivel_referencia':'-O0','nivel_candidato':'-O2'})
        r = self.lote([c]);proc=r['casos'][0]['procedencia']
        self.assertFalse(proc['verificada']);self.assertEqual(proc['fuente_bal']['sha256'],hashlib.sha256(fuente.read_bytes()).hexdigest())

    def test_32_raw_real_y_estadisticas(self):
        # Se invocan los mismos fixtures S7/S8-A y CB usados por B1. Este caso
        # solo valida transporte/agregación y queda en categoria demostracion.
        s8 = self.fixtures.m2.m1.s8; op = s8.s7.op
        ins=[op(escribe=[5],texto='sumai x5,x0,7'),op(1,escribe=[8],efectos=1,texto='cargai x8,x0,0'),op(lee=[5,8],escribe=[6],texto='suma x6,x5,x8')]
        lineas=['3 2 1 0']+[f'{100+3*i} {a["slot"]} {a["lee"]:x} {a["escribe"]:x} {a["efectos"]} {a["control"]} {a["bloque"]}' for i,a in enumerate(ins)]
        lineas+=['0 2 0 3','1 2 0 3','0 3 0']+[a['texto'] for a in ins]
        entrada,asm=self.d/'plan.txt',self.d/'s7.asm';entrada.write_text('\n'.join(lineas)+'\n')
        self.ejecutar([self.s7,'fixture',entrada,asm])
        conservador=s8.s7.SchedulerTests.renderizar(None,ins,[(0,3,[])])
        # BALB contiene el dato 11 en DMEM[0], idéntico para las dos imágenes.
        import struct
        rutas=[]
        for nombre,fuente in (('conservador',conservador),('calendarizado',asm.read_text())):
            mem=self.imagen(fuente,nombre);codigo=b''.join(bytes.fromhex(x)[::-1] for x in mem.read_text().splitlines())
            balb=self.d/(nombre+'.balb');balb.write_bytes(b'BALB'+struct.pack('<11I',1,48,49+len(codigo),0,48,len(codigo),0,48+len(codigo),1,0,0)+codigo+b'\x0b');rutas.append(balb)
        c=self.caso('raw-demo');c['referencia']=dict(formato='balb',ruta=str(rutas[0]));c['candidato']=dict(formato='balb',ruta=str(rutas[1]))
        r=self.lote([c]);b=r['casos'][0]['b1']
        self.assertEqual([b[x]['metricas']['ciclos'] for x in ('referencia','candidato')],[12,7])
        self.assertEqual(b['diferencias_metricas']['ciclos'],dict(negativa=True,magnitud=5))
        a=r['resumen']['rendimiento_validado_por_modo']['finita']
        self.assertEqual((a['ciclos_referencia'],a['ciclos_candidato'],a['diferencia_total_ciclos']),(12,7,-5))
        self.assertAlmostEqual(a['reduccion_porcentual_referencia'],100*5/12)
        self.assertEqual(r['cobertura']['registrados'],0)
        tipo=self.caso('diferente');tipo['candidato']['ruta']=str(self.alu)
        inc=self.caso('inconcluso');inc['presupuesto']=0
        mezcla=self.lote([c,tipo,inc]);v=mezcla['resumen']['rendimiento_validado_por_modo']['finita']
        self.assertEqual((v['casos'],v['ciclos_referencia'],v['ciclos_candidato']),(1,12,7))
        type(self).integraciones.append(dict(nombre='raw-y-mixto',reporte=mezcla))
        if os.environ.get('B2_ARTEFACTOS'):
            destino=Path(os.environ['B2_ARTEFACTOS']);destino.mkdir(exist_ok=True)
            # Conservar imágenes y manifiesto permite repetir el lote tras
            # destruir los temporales de unittest, sin artefactos en el repo.
            (destino/'comparar_vliw').write_bytes(self.b1.read_bytes())
            (destino/'comparar_vliw').chmod(0o755)
            casos=copy.deepcopy([c,tipo,inc])
            for caso in casos:
                for lado in ('referencia','candidato'):
                    origen=Path(caso[lado]['ruta']);nombre=caso['id']+'-'+lado+origen.suffix
                    (destino/nombre).write_bytes(origen.read_bytes());caso[lado]['ruta']=nombre
            (destino/'manifiesto.json').write_text(json.dumps(dict(version=1,casos=casos),indent=2))
            b2.publicar(mezcla,destino/'lote.json',destino/'lote.csv',[])

    def test_33_salida_duplicada_no_finita_y_version(self):
        for cuerpo in ('print("{\\\"version\\\":1,\\\"version\\\":1}")',
                       'print("{\\\"ipc\\\":Infinity}")', 'print("{\\\"ipc\\\":1e309}")'):
            self.assertEqual(self.lote(exe=self.falso(cuerpo))['casos'][0]['resultado'],b2.INFRA)
        d=copy.deepcopy(self.base);d['version']=2
        self.assertEqual(self.lote(exe=self.respuesta(d))['casos'][0]['resultado'],b2.INFRA)

    def test_34_archivos_modificados_y_respuesta_ajena(self):
        entrada=self.d/'entrada.mem';entrada.write_bytes(self.nop.read_bytes())
        c=self.caso();c['referencia']['ruta']=str(entrada)
        cuerpo='import json,sys\nr=json.loads('+repr(json.dumps(self.base))+')\nr["referencia"]["ruta"]=sys.argv[2]\nr["candidato"]["ruta"]=sys.argv[4]\nopen(sys.argv[2],"ab").write(b"\\n")\nprint(json.dumps(r))'
        r=self.lote([c],self.falso(cuerpo))
        self.assertIn('cambió durante',r['casos'][0]['diagnostico']['error'])
        r=self.lote(exe=self.falso('import json\nprint(json.dumps('+repr(self.base)+'))'))
        # El NOP base coincide con ambos archivos en este caso. Cambiar solo
        # la ruta solicitada exige que B1 identifique ese nuevo nombre.
        c=self.caso();c['candidato']['ruta']=str(entrada)
        r=self.lote([c],self.falso('import json\nprint(json.dumps('+repr(self.base)+'))'))
        self.assertIn('archivos distintos',r['casos'][0]['diagnostico']['error'])

    def test_35_fallo_preparacion_no_publica(self):
        r=self.lote();a,b=self.d/'a',self.d/'b'
        a.write_text('anterior JSON');b.write_text('anterior CSV')
        with mock.patch.object(b2.os,'fsync',side_effect=OSError('fallo disco')),self.assertRaises(b2.PublicacionError):
            b2.publicar(r,a,b,[])
        self.assertEqual((a.read_text(),b.read_text()),('anterior JSON','anterior CSV'))
        self.assertEqual(list(self.d.glob('.b2-*')),[])
        with self.assertRaises(b2.PublicacionError): b2.publicar(r,self.d/'no-existe'/'a',b,[])

    def test_36_timeout_si_cierra_pipes(self):
        # Cerrar stdout/stderr no debe eludir el límite: todavía hay que
        # esperar al proceso B1 y recogerlo si se queda ejecutando.
        exe=self.falso('import os,time\nos.close(1);os.close(2)\ntime.sleep(10)')
        r=self.lote(exe=exe,timeout=0.15)
        self.assertEqual(r['casos'][0]['resultado'],b2.INFRA)
        self.assertIn('timeout',r['casos'][0]['diagnostico']['error'])

    def test_37_grupo_hijos_y_limpieza(self):
        pid=self.d/'hijo.pid'
        cuerpo='import subprocess,sys,time\np=subprocess.Popen([sys.executable,"-c","import time;time.sleep(10)"])\nopen('+repr(str(pid))+',"w").write(str(p.pid))\ntime.sleep(10)'
        r=self.lote(exe=self.falso(cuerpo),timeout=0.25)
        self.assertEqual(r['casos'][0]['resultado'],b2.INFRA)
        estado=Path('/proc')/pid.read_text().strip()/'stat'
        # Un descendiente huérfano puede esperar al reaper del sistema como Z;
        # no debe quedar un proceso ejecutándose tras cerrar el grupo.
        if estado.exists(): self.assertEqual(estado.read_text().split()[2],'Z')

    def test_38_csv_negativos_y_estadistica_separada(self):
        d=copy.deepcopy(self.base);d['referencia']['metricas']['ciclos']=12;d['candidato']['metricas']['ciclos']=7
        d['diferencias_metricas']['ciclos']=dict(negativa=True,magnitud=5)
        d['diferencias_metricas']['porcentaje_ciclos']=-100*5/12
        r=self.lote(exe=self.respuesta(d))
        fila=list(csv.DictReader(io.StringIO(b2.generar_csv(r))))[0]
        self.assertEqual(fila['diferencia_ciclos'],'-5')
        self.assertFalse(fila['diferencia_ciclos'].startswith("'"))
        self.assertEqual(r['resumen']['rendimiento_validado_por_modo']['fisica']['casos'],0)

    def test_39_prioridad_y_exigir_cli(self):
        self.cli(self.manifiesto([]),esperado=5,extra=['--exigir-cobertura'])
        r=self.lote([self.caso('diff'),self.caso('inc')])
        # La política se prueba sobre un resumen controlado, sin convertir
        # ese fixture de prioridad en evidencia de comparaciones reales.
        r['resumen']['conteos']['DIFERENTE']=1;r['resumen']['conteos']['INCONCLUSO']=1
        self.assertEqual(b2.codigo_lote(r),2)
        r['resumen']['conteos']['ERROR']=1;self.assertEqual(b2.codigo_lote(r),6)
        r['resumen']['conteos'][b2.INFRA]=1;self.assertEqual(b2.codigo_lote(r),1)

    def test_40_no_regular_permisos_y_procedencia(self):
        c=self.caso();c['referencia']['ruta']=str(self.d)
        with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(self.manifiesto([c]))
        exe=self.falso('pass');exe.chmod(0o644)
        with self.assertRaises(b2.InfraestructuraError): self.lote(exe=exe)
        c=self.caso(procedencia={'comando':'no ejecutar'})
        with self.assertRaises(b2.ManifiestoInvalido): b2.leer_manifiesto(self.manifiesto([c]))

    def test_41_fallo_captura_recoge_proceso(self):
        original=b2.os.read
        def fallar_captura(fd,n):
            # Popen también usa os.read al iniciar: solo se inyecta en la
            # lectura de 64 KiB que pertenece a la captura B2 ya iniciada.
            if n==65536: raise OSError('captura fallida')
            return original(fd,n)
        with mock.patch.object(b2.os,'read',side_effect=fallar_captura):
            r=self.lote(exe=self.respuesta())
        c=r['casos'][0]
        self.assertEqual(c['resultado'],b2.INFRA)
        self.assertTrue(c['proceso_iniciado'])
        self.assertIn('error al capturar',c['diagnostico']['error'])

    def test_42_fallo_selector_no_deja_proceso(self):
        # Incluso si falta un recurso antes de registrar los pipes, el
        # proceso iniciado debe terminar en la misma limpieza del timeout.
        with mock.patch.object(b2.selectors,'DefaultSelector',side_effect=OSError('sin selector')):
            r=self.lote(exe=self.falso('import time;time.sleep(10)'))
        c=r['casos'][0]
        self.assertTrue(c['proceso_iniciado'])
        self.assertEqual(c['resultado'],b2.INFRA)
        self.assertIn('sin selector',c['diagnostico']['error'])


if __name__=='__main__':
    unittest.main()
