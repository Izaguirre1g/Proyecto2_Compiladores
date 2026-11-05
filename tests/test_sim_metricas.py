"""M1: valores conocidos, observación sin efectos y contraste con sondas S6/RTL.

Se reutilizan CB, S2, S7 y S8-A. El oráculo RTL suma señales observadas y
operaciones S2 por PC retirado, sin ejecutar nuevamente instrucciones.
"""
from pathlib import Path
import collections
import ctypes
import importlib.util
import json
import math
import os
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fixtures_emisor_m1', ROOT/'tests/test_vliw_emisor.py')
s8 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s8)
VACIO = (1 << (ctypes.sizeof(ctypes.c_size_t)*8))-1
NOP = 'nop | nop | nop | nop\n'


def fnv(datos):
    h = 14695981039346656037
    for b in datos:
        h = ((h ^ b)*1099511628211) & ((1 << 64)-1)
    return f'{h:016x}'


class MetricasTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='pruebas-m1-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.comandos, cls.ejemplos = [], []
        cls.ejecuciones = cls.rtl_programas = cls.rtl_ciclos = cls.comparaciones = 0
        cls.cc = shlex.split(os.environ.get('CC', 'cc'))
        cls.flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        sim = [f'src/sim_{s}.c' for s in ('pipeline', 'alu', 'lsu', 'bru', 'crypto', 'decode', 'cargador')]
        cls.bin, cls.fallos, cls.cb, cls.s7, cls.s6, cls.s2 = (cls.dir/n for n in ('metricas', 'fallos', 'cb', 's7', 's6', 's2'))
        fuentes = ['src/sim_metricas.c', 'tests/test_sim_metricas.c', 'src/codificacion_binaria.c']+sim
        cls.compilar(fuentes, cls.bin)
        cls.compilar(fuentes, cls.fallos, ['-DSIM_METRICAS_FALLOS']+
                     ['-Wl,--wrap='+n for n in ('realloc', 'vsnprintf', '__vsnprintf_chk')])
        fuente = cls.dir/'cb.c'; fuente.write_text(s8.CODIFICADOR)
        cls.compilar([fuente, 'src/codificacion_binaria.c'], cls.cb)
        cls.compilar(['tests/test_vliw_emisor.c', 'src/vliw_scheduler.c', 'src/vliw_emisor.c',
                      'src/codificacion_binaria.c', 'src/sim_decode.c'], cls.s7)
        cls.compilar(['tests/test_sim_crypto.c']+sim, cls.s6)
        cls.compilar(['tests/test_sim_decode.c', 'src/sim_decode.c', 'src/sim_cargador.c'], cls.s2)
        cls.arch = Path(os.environ.get('ARQUITECTURA_ROOT', ROOT.parent/'Proyecto2_Arquitectura1')).resolve()
        cls.sv = cls.dir/'referencia.vvp'
        cls.ejecutar(['iverilog', '-g2012', '-I'+str(cls.arch), '-I'+str(cls.arch/'rtl'), '-s',
                      'tb_sim_crypto_ref', '-o', cls.sv, ROOT/'tests/rtl/tb_sim_crypto_ref.sv']+
                     sorted((cls.arch/'rtl').glob('*.sv')))

    @classmethod
    def ejecutar(cls, cmd, esperado=0):
        cmd = list(map(str, cmd))
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=90)
        cls.comandos.append(dict(comando=cmd, codigo=p.returncode))
        if p.returncode != esperado:
            raise AssertionError(f'{cmd}: código {p.returncode}, esperado {esperado}\n{p.stdout!r}\n{p.stderr!r}')
        return p.stdout.decode('utf-8')

    @classmethod
    def compilar(cls, fuentes, salida, extra=()):
        cls.ejecutar(cls.cc+cls.flags+list(extra)+list(map(str, fuentes))+['-o', salida])

    @classmethod
    def tearDownClass(cls):
        e = dict(ejecuciones=cls.ejecuciones, rtl_programas=cls.rtl_programas, rtl_ciclos=cls.rtl_ciclos,
                 comparaciones=cls.comparaciones, unitarias=getattr(cls, 'unitarias', ''),
                 fallos=getattr(cls, 'reservas', ''), ejemplos=cls.ejemplos, comandos=cls.comandos)
        print('M1: '+json.dumps({k: v for k, v in e.items() if k not in ('ejemplos', 'comandos')}))
        if os.environ.get('M1_EVIDENCIA'):
            Path(os.environ['M1_EVIDENCIA']).write_text(json.dumps(e, indent=2)+'\n')

    def codificar(self, asm):
        fuente, mem, bin_ = self.dir/'programa.asm', self.dir/'programa.mem', self.dir/'programa.bin'
        fuente.write_text(asm)
        self.ejecutar([self.cb, fuente, mem, bin_])
        return mem

    def correr(self, nombre, asm=None, mem=None, datos=None, presupuesto=200, fisica=False, balb=False):
        mem = self.codificar(asm) if mem is None else mem
        datos = bytes(65536) if datos is None else bytes(datos)
        crudo, final = self.dir/'datos.bin', self.dir/'final.bin'
        crudo.write_bytes(datos)
        archivo = mem
        if balb:
            # Contenedor BALB v1 de fixture: las instrucciones siguen siendo
            # las palabras del CB real; S1 valida encabezado y secciones.
            codigo = b''.join(bytes.fromhex(l)[::-1] for l in mem.read_text().splitlines())
            archivo = self.dir/'programa.balb'
            archivo.write_bytes(b'BALB'+struct.pack('<11I', 1, 48, 48+len(codigo), 0, 48,
                                                 len(codigo), 0, 48+len(codigo), 0, 0, 0)+codigo)
        modo = 'fisica' if fisica else 'finita'
        args = ['balb' if balb else 'mem', archivo, crudo, presupuesto, modo]
        salida = self.ejecutar([self.bin, *args, 'json', final])
        r = json.loads(salida)
        memoria = final.read_bytes()
        self.assertEqual(len(memoria), 65536)
        self.assertEqual(r['arquitectura']['dmem_fnv1a64'], fnv(memoria))
        self.assertEqual(r['naturaleza'], 'dinamica')
        self.assertEqual(r['version'], 1)
        self.assertEqual(r['instrucciones_utiles'], sum(r['instrucciones_por_slot'].values()))
        self.assertEqual(r['instrucciones_utiles'], sum(r['operaciones_isa'].values()))
        self.assertEqual(r['completados'], r['bundles_nop']+r['bundles_utiles'])
        self.assertEqual(r['admitidos'], r['completados']+r['anulados']+r['pendientes'])
        self.assertEqual(r['pendientes'], sum(r['pendientes_por_etapa']))
        self.assertTrue(math.isclose(r['ipc'], r['instrucciones_utiles']/r['ciclos'] if r['ciclos'] else 0, abs_tol=1.01e-9))
        self.assertTrue(math.isclose(r['bundles_por_ciclo'], r['completados']/r['ciclos'] if r['ciclos'] else 0, abs_tol=1.01e-9))
        self.assertNotIn('password', salida)
        self.assertNotIn('boveda', salida)
        self.assertNotIn('keys', salida)
        # Una ejecución nueva con idénticas entradas debe producir el mismo
        # JSON, incluyendo huella y eventos. El auxiliar contrasta además cada
        # paso con un segundo simulador sin M1 y compara DMEM/bóveda completas.
        self.assertEqual(self.ejecutar([self.bin, *args, 'json', final]), salida)
        texto = self.ejecutar([self.bin, *args, 'texto', final])
        self.assertIn('Métricas dinámicas VLIW', texto)
        type(self).ejecuciones += 3
        type(self).ejemplos.append(dict(nombre=nombre, resultado=r))
        if os.environ.get('M1_ARTEFACTOS'):
            carpeta = Path(os.environ['M1_ARTEFACTOS']); carpeta.mkdir(exist_ok=True)
            (carpeta/(nombre+'.json')).write_text(salida)
            (carpeta/(nombre+'.txt')).write_text(texto)
            (carpeta/(nombre+'.mem')).write_bytes(mem.read_bytes())
            (carpeta/(nombre+'-dmem.bin')).write_bytes(memoria)
            if asm is not None:
                (carpeta/(nombre+'.asm')).write_text(asm)
        return r, memoria

    def contraste_rtl(self, nombre, asm=None, mem=None, datos=None, ciclos=20):
        mem = self.codificar(asm) if mem is None else mem
        datos = bytes(65536) if datos is None else bytes(datos)
        r, memoria = self.correr(nombre, mem=mem, datos=datos, presupuesto=ciclos, fisica=True)
        crudo, hex_ = self.dir/'datos.bin', self.dir/'datos.hex'
        crudo.write_bytes(datos)
        hex_.write_text(''.join(f'{int.from_bytes(datos[i:i+4], "little"):08x}\n' for i in range(0, 65536, 4)))
        c = [json.loads(l) for l in self.ejecutar([self.s6, 'mem', mem, crudo, ciclos, 0, 'sonda']).splitlines()]
        sv = [json.loads(l) for l in self.ejecutar(['vvp', self.sv, '+IMEM='+str(mem), '+DMEM='+str(hex_),
              '+CICLOS='+str(ciclos), '+RESET_DESPUES=0']).splitlines() if l.startswith('{')]
        ct, st = ([x for x in lista if 'c' in x] for lista in (c, sv))
        self.assertEqual(len(ct), ciclos); self.assertEqual(len(st), ciclos)
        for a, b in zip(ct, st):
            for clave in a:
                self.assertEqual(a[clave], b[clave], (a['c'], clave))
        for lista in (c, sv):
            completa = bytearray(65536)
            for x in lista:
                if 'mem' in x:
                    struct.pack_into('<I', completa, x['mem'], x['valor'])
            self.assertEqual(memoria, completa)
        # Oráculo desde la traza RTL, con S2 original solo para identificar
        # instrucciones y destinos x0. No se calcula ninguna semántica ISA.
        dec = [json.loads(l) for l in self.ejecutar([self.s2, 'imagen', 'mem', mem]).splitlines()]
        por_pc = collections.defaultdict(dict)
        for x in dec:
            por_pc[x['bundle']][x['slot']] = x
        retiradas = collections.Counter(); por_slot = collections.Counter(); nop = 0; cero = [0]*4; bru = 0
        for t in st:
            if t['v3']:
                originales = por_pc[(t['pc3'] >> 4) & 1023]
                utiles = [i for i in originales.values() if i['estado'] == 0]
                nop += not utiles
                for i in utiles:
                    retiradas[i['operacion']] += 1; por_slot[i['slot']] += 1
                    if i['slot'] < 3 and i['rd'] == 0:
                        cero[i['slot']] += 1
            if t['v2'] and not t['fault']:
                bru += por_pc[(t['pc2'] >> 4) & 1023].get(2, {}).get('estado') == 0
        for clave in ('admitidos', 'completados', 'anulados'):
            self.assertEqual(r[clave], st[-1][clave])
        self.assertEqual(r['ciclos'], ciclos)
        self.assertEqual(r['bundles_nop'], nop)
        self.assertEqual(r['instrucciones_por_slot'], {n: por_slot[i] for i, n in enumerate(('ALU', 'LSU', 'BRU', 'CRIPTO'))})
        self.assertEqual({k: v for k, v in r['operaciones_isa'].items() if v}, dict(retiradas))
        self.assertEqual(r['escrituras_por_puerto'], [sum(t['w'+str(i)] for t in st) for i in range(5)])
        self.assertEqual(r['descartes_x0_por_slot'], cero)
        self.assertEqual(r['bru_ex'], bru)
        self.assertEqual(r['saltos_tomados'], sum(t['flush'] and not t['fault'] for t in st))
        self.assertEqual(r['saltos_no_tomados'], bru-r['saltos_tomados'])
        self.assertEqual(r['busquedas_descartadas'], sum(t['kill_if'] for t in st))
        self.assertEqual(r['traps'], sum(t['fault'] for t in st))
        self.assertEqual(r['anulados_excepcion'], sum(1+t['kill_id'] for t in st if t['fault']))
        self.assertEqual(r['anulados_salto'], sum(t['kill_id'] for t in st if t['flush'] and not t['fault']))
        for clave, valor in dict(lsu_lecturas_ex=sum(t['a_valid'] and not t['a_we'] for t in st),
            lsu_escrituras_ex=sum(t['a_we'] for t in st), puerto_b_operaciones_ex=sum(t['b_valid'] for t in st),
            puerto_b_lecturas_ex=sum(t['b_valid'] for t in st), puerto_b_escrituras_ex=sum(t['b_we'] for t in st),
            conflictos_registro=sum(t['conflicto'] for t in st),
            conflictos_memoria=sum(t['a_we'] and t['b_we'] and (t['a_addr'] & 0xfffc) == (t['b_addr'] & 0xfffc) for t in st)).items():
            self.assertEqual(r[clave], valor, clave)
        self.assertEqual(r['excepciones'], [dict(ciclo=t['c'], pc=t['pc2'], vector=t['redirect']) for t in st if t['fault']])
        for clave, trace in (('pc', 'pc'), ('estado_crypto', 'estado'), ('trap_count32', 'traps'), ('trap_pc', 'trap_pc'), ('registros', 'regs')):
            self.assertEqual(r['arquitectura'][clave], st[-1][trace])
        type(self).rtl_programas += 1; type(self).rtl_ciclos += ciclos
        if os.environ.get('M1_ARTEFACTOS'):
            (Path(os.environ['M1_ARTEFACTOS'])/(nombre+'-rtl.json')).write_text(json.dumps(sv, indent=2)+'\n')
        return r

    def test_01_c_y_fallos(self):
        type(self).unitarias = self.ejecutar([self.bin]).strip()
        self.assertRegex(self.unitarias, r'M1: \d+ escenarios, \d+ comprobaciones PASS')
        type(self).reservas = self.ejecutar([self.fallos, 'fallos']).strip()
        self.assertRegex(self.reservas, r'\d+ reservas, 4 errores de generación PASS')

    def test_02_cero_y_secuencial(self):
        r, _ = self.correr('sin-ciclos', NOP, presupuesto=0)
        self.assertEqual((r['ciclos'], r['ipc'], r['admitidos'], r['estado']), (0, 0, 0, 'LIMITE'))
        r, _ = self.correr('secuencial', 'sumai x5,x0,7 | nop | nop | nop\n')
        self.assertEqual((r['ciclos'], r['instrucciones_utiles'], r['ipc']), (4, 1, .25))
        r, _ = self.correr('nop', NOP)
        self.assertEqual((r['ciclos'], r['completados'], r['bundles_nop'], r['instrucciones_utiles']), (4, 1, 1, 0))

    def test_03_raw_y_memoria(self):
        datos = bytearray(65536); datos[0] = 11
        r, _ = self.correr('raw', 'sumai x5,x0,7 | cargai x8,x0,0 | nop | nop\n'+NOP*2+'suma x6,x5,x8 | nop | nop | nop\n', datos=datos)
        self.assertEqual((r['ciclos'], r['instrucciones_utiles'], r['arquitectura']['registros'][6]), (7, 3, 18))
        r, mem = self.correr('memoria', 'sumai x5,x0,7 | nop | nop | nop\n'+NOP*2+'nop | guardap x0,x5,4 | nop | nop\nnop | cargai x7,x0,4 | nop | nop\n')
        self.assertEqual((r['lsu_lecturas_ex'], r['lsu_escrituras_ex'], mem[4], r['arquitectura']['registros'][7]), (1, 1, 7, 7))

    def test_04_balb_y_limite(self):
        asm = 'sumai x5,x0,7 | nop | nop | nop\n'
        a, ma = self.correr('balb', asm, balb=True)
        b, mb = self.correr('mem-igual', asm)
        self.assertEqual(a, b); self.assertEqual(ma, mb)
        r, _ = self.correr('limite', asm, presupuesto=3)
        self.assertEqual((r['ciclos'], r['completados'], r['pendientes'], r['estado']), (3, 0, 1, 'LIMITE'))

    def test_05_rtl_raw(self):
        datos = bytearray(65536); datos[0] = 11
        r = self.contraste_rtl('rtl-raw', 'sumai x5,x0,7 | cargai x8,x0,0 | nop | nop\n'+NOP*2+'suma x6,x5,x8 | nop | nop | nop\n', datos=datos, ciclos=12)
        self.assertEqual(r['instrucciones_utiles'], 3)

    def test_06_rtl_saltos_y_bucle(self):
        for nombre, condicion, tomado in (('tomado', 'igualsi', 1), ('no-tomado', 'igualno', 0)):
            r = self.contraste_rtl(nombre, f'nop | nop | {condicion} x0,x0,fin | nop\nsumai x5,x0,1 | nop | nop | nop\nfin: sumai x6,x0,9 | nop | nop | nop\n', ciclos=12)
            self.assertEqual(r['saltos_tomados'], tomado)
        asm = 'sumai x5,x0,3 | nop | nop | nop\n'+NOP*2+'bucle: restai x5,x5,1 | nop | nop | nop\n'+NOP*2+'nop | nop | igualno x5,x0,bucle | nop\nsuma x6,x5,x0 | nop | nop | nop\n'
        r, _ = self.correr('bucle', asm)
        self.assertEqual((r['ciclos'], r['completados'], r['instrucciones_utiles'], r['saltos_tomados']), (23, 16, 8, 2))
        self.contraste_rtl('rtl-bucle', asm, ciclos=28)

    def test_07_rtl_excepcion_vector_y_repeticion(self):
        mem = self.codificar('sumai x10,x0,42 | nop | nop | nop\nsumai x8,x0,7 | guardap x0,x0,4 | sye x0,0 | fsl x4,x2,0,0\n'+NOP)
        palabras = mem.read_text().splitlines()
        handler = self.codificar('sumai x12,x0,77 | nop | nop | nop\n').read_text().splitlines()[0]
        mem.write_text('\n'.join(palabras+['0'*32]*(1023-len(palabras))+[handler])+'\n')
        r = self.contraste_rtl('excepcion-vector', mem=mem, ciclos=8)
        self.assertEqual((r['traps'], r['instrucciones_utiles'], r['anulados_excepcion'], r['lsu_escrituras_ex']), (1, 2, 2, 0))
        self.assertEqual(r['arquitectura']['registros'][10:13:2], [42, 77])
        mem = self.codificar('nop | nop | nop | fsl x4,x2,0,0\n')
        palabra = mem.read_text().strip()
        mem.write_text('\n'.join([palabra]+['0'*32]*1022+[palabra])+'\n')
        r = self.contraste_rtl('excepciones-repetidas', mem=mem, ciclos=20)
        self.assertGreater(r['traps'], 4)
        self.assertEqual(r['excepciones'][0], dict(ciclo=3, pc=0, vector=0x3ff0))
        self.assertTrue(all(x['pc'] == 0x3ff0 for x in r['excepciones'][1:]))

    def test_08_rtl_cripto_y_conflicto_memoria(self):
        operaciones = ['sumai x2,x0,7 | nop | nop | nop', 'nop | guardap x0,x2,0 | nop | nop',
                      'nop | nop | nop | setpwd x2', 'nop | nop | nop | vcr 0',
                      'nop | nop | nop | ell 0,0,x8,x10', 'nop | nop | nop | fsl x4,x2,0,0',
                      'nop | nop | nop | fsli x6,x4,0,0', 'nop | nop | nop | camcon 0,7',
                      'nop | nop | nop | vcr 0', 'suma x12,x6,x7 | nop | nop | nop']
        asm = ''.join(x+'\n'+NOP*2 for x in operaciones)
        r, datos = self.correr('seis-cripto', asm)
        self.assertEqual((r['ciclos'], r['instrucciones_utiles'], r['instrucciones_por_slot']['CRIPTO'], r['arquitectura']['registros'][12]), (33, 10, 7, 7))
        self.assertEqual((r['puerto_b_lecturas_ex'], r['puerto_b_escrituras_ex'], sum(r['escrituras_por_puerto'][3:])), (3, 1, 4))
        self.assertEqual(int.from_bytes(datos[:4], 'little'), 896)
        self.contraste_rtl('rtl-cripto', asm, ciclos=36)
        # A y B publican sobre la misma palabra. La lectura B usa el dato
        # anterior; el reporte cuenta el conflicto y ambas escrituras.
        conflicto = ''.join(x+'\n'+NOP*2 for x in operaciones[:4])+'nop | guardap x0,x0,0 | nop | camcon 0,7\n'+NOP*2
        r = self.contraste_rtl('conflicto-memoria', conflicto, ciclos=18)
        self.assertEqual((r['conflictos_memoria'], r['lsu_escrituras_ex'], r['puerto_b_escrituras_ex']), (1, 2, 1))

    def test_10_rtl_escrituras(self):
        # Conflicto provocado combinando palabras legales de CB; su análisis
        # global lo rechazaría, pero el RTL tiene prioridad WB definida.
        mem = self.codificar('sumai x5,x0,7 | nop | nop | nop\n')
        alu = mem.read_text().strip()
        carga = self.codificar('nop | cargai x5,x0,0 | nop | nop\n').read_text().strip()
        mem.write_text(f'{int(alu,16) | int(carga,16):032x}\n')
        datos = bytearray(65536); datos[0] = 11
        r = self.contraste_rtl('conflicto-registros', mem=mem, datos=datos, ciclos=10)
        self.assertEqual((r['conflictos_registro'], r['instrucciones_utiles'], r['arquitectura']['registros'][5]), (1, 2, 11))
        r = self.contraste_rtl('descartes-x0', 'sumai x0,x0,7 | cargai x0,x0,0 | sye x0,fin | nop\nfin: nop\n', ciclos=10)
        self.assertEqual(r['descartes_x0_por_slot'], [1, 1, 1, 0])
        self.assertEqual(r['instrucciones_utiles'], 3)

    def test_11_error_administrativo_sin_flanco(self):
        # CB produce una BRU legal; recortar el archivo de fixture deja un
        # destino fuera de la extensión finita. Se acepta su EX y el intento
        # siguiente se detiene antes de WB, sin trap ni un ciclo nuevo.
        mem = self.codificar('nop | nop | sye x0,fin | nop\n'+NOP+'fin: nop\n')
        mem.write_text(mem.read_text().splitlines()[0]+'\n')
        r, _ = self.correr('error-pc', mem=mem)
        self.assertEqual((r['estado'], r['ciclos'], r['bru_ex'], r['completados'], r['instrucciones_utiles'], r['traps'], r['pendientes']),
                         ('PC', 3, 1, 0, 0, 0, 1))

    def comparar_plan(self, nombre, ins, deps, bloques=None, destinos=(), datos=None):
        bloques = bloques or [(0, len(ins), [])]
        lineas = [f'{len(ins)} {len(deps)} {len(bloques)} {len(destinos)}']
        lineas += [f'{100+3*i} {a["slot"]} {a["lee"]:x} {a["escribe"]:x} {a["efectos"]} {a["control"]} {a["bloque"]}' for i, a in enumerate(ins)]
        lineas += [' '.join(map(str, a)) for a in deps]
        lineas += [' '.join(map(str, (inicio, fin, len(su), *su))) for inicio, fin, su in bloques]
        lineas += [' '.join(map(str, t)) for t in destinos]+[a['texto'] for a in ins]
        entrada, asm_path = self.dir/'plan.txt', self.dir/'s7.asm'
        entrada.write_text('\n'.join(lineas)+'\n')
        p = json.loads(self.ejecutar([self.s7, 'fixture', entrada, asm_path]))
        self.assertEqual(p['estado'], 0)
        optimizado = asm_path.read_text()
        # Política conservadora aprobada de las pruebas S7: no es un nuevo
        # codificador/backend. CB real resuelve todas las etiquetas.
        conservador = s8.s7.SchedulerTests.renderizar(None, ins, bloques)
        a, ma = self.correr(nombre+'-conservador', conservador, datos=datos)
        b, mb = self.correr(nombre+'-s7', optimizado, datos=datos)
        self.assertEqual((a['estado'], b['estado']), ('TERMINADO', 'TERMINADO'))
        self.assertEqual(a['arquitectura']['registros'], b['arquitectura']['registros'])
        self.assertEqual(ma, mb, 'comparar los 65536 bytes, no solo sus hashes')
        self.assertEqual(a['arquitectura']['estado_crypto'], b['arquitectura']['estado_crypto'])
        self.assertEqual(a['traps'], b['traps']); self.assertEqual(a['traps'], 0)
        self.assertEqual(a['operaciones_isa'], b['operaciones_isa'])
        # PC final es relativo al layout: debe estar tres bundles después de
        # su código, no coincidir numéricamente entre imágenes distintas.
        self.assertEqual(a['arquitectura']['pc'], 16*(3*len(ins)+3))
        self.assertEqual(b['arquitectura']['pc'], 16*(len(p['bundles'])+3))
        self.assertLess(b['ciclos'], a['ciclos'])
        type(self).comparaciones += 1
        return a, b

    def test_09_comparacion_s7_emisor(self):
        op = s8.s7.op
        ins = [op(escribe=[5], texto='sumai x5,x0,7'), op(1, escribe=[8], efectos=1, texto='cargai x8,x0,0'), op(lee=[5, 8], escribe=[6], texto='suma x6,x5,x8')]
        datos = bytearray(65536); datos[0] = 11
        a, b = self.comparar_plan('comparacion-raw', ins, [(0, 2, 0, 3), (1, 2, 0, 3)], datos=datos)
        self.assertEqual((a['ciclos'], b['ciclos'], a['instrucciones_utiles'], b['instrucciones_utiles']), (12, 7, 3, 3))
        ins = [op(escribe=[5], texto='sumai x5,x0,7'), op(escribe=[6], texto='sumai x6,x0,8'),
               op(1, lee=[5], efectos=2, texto='guardap x0,x5,4'), op(1, escribe=[7], efectos=1, texto='cargai x7,x0,4'), op(lee=[6, 7], escribe=[8], texto='suma x8,x6,x7')]
        self.comparar_plan('comparacion-memoria', ins, [(0, 2, 0, 3), (2, 3, 3, 1), (1, 4, 0, 3), (3, 4, 0, 3)])


if __name__ == '__main__':
    unittest.main()
