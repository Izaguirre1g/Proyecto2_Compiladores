"""Contrasta S3 con top/ALU reales y recorre MEM/BALB mediante S1 y S2."""
from pathlib import Path
import importlib.util
import json
import os
import random
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
# Arquitectura conserva el núcleo pero puede ubicar su top en rtl-p2.
sys.path.insert(0, str(ROOT / 'tools/vliw'))
from rutas_vliw import top_rtl, argumentos_rtl
OPERACIONES = ['suma', 'resta', 'cader', 'cizq', 'and', 'or', 'xor', 'cder',
               'mrq', 'myq', 'sumai', 'restai', 'cizqi', 'cderi', 'caderi',
               'xori', 'andi', 'ori']
CODIGOS_RTL = [0, 1, 4, 2, 6, 7, 5, 3, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7]


class PipelineTests(unittest.TestCase):
    # Compila las bibliotecas y sondas reales una vez para la suite y prepara los recursos
    # compartidos en directorios de prueba.
    @classmethod
    def setUpClass(cls):
        cls.temporal = tempfile.TemporaryDirectory(prefix='pruebas-pipeline-')
        cls.addClassCleanup(cls.temporal.cleanup)
        cls.carpeta = Path(cls.temporal.name)
        cls.alu = cls.carpeta / 'alu'
        cls.pipeline = cls.carpeta / 'pipeline'
        # Los ejecutables de prueba se reconstruyen en el temporal con el compilador y flags
        # seleccionados. La preparación no reutiliza binarios antiguos del repositorio.
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        subprocess.run(cc + flags + ['tests/vliw/test_sim_alu.c', 'src/Simulador_VLIW/sim_alu.c',
                       '-o', str(cls.alu)], cwd=ROOT, check=True)
        subprocess.run(cc + flags + ['tests/vliw/test_sim_pipeline.c', 'src/Simulador_VLIW/sim_pipeline.c', 'src/Simulador_VLIW/sim_crypto.c', 'src/Simulador_VLIW/sim_bru.c',
                       'src/Simulador_VLIW/sim_alu.c', 'src/Simulador_VLIW/sim_lsu.c', 'src/Simulador_VLIW/sim_decode.c', 'src/Simulador_VLIW/sim_cargador.c',
                       '-o', str(cls.pipeline)], cwd=ROOT, check=True)
        subprocess.run(['make', 'balc'], cwd=ROOT, check=True, capture_output=True)
        cls.arquitectura = Path(os.environ.get('ARQUITECTURA_ROOT',
                                  ROOT.parent / 'Proyecto2_Arquitectura1')).resolve()
        cls.rtl_disponible = top_rtl(cls.arquitectura).is_file() and all(
            shutil.which(h) for h in ['iverilog', 'vvp'])
        if cls.rtl_disponible:
            spec = importlib.util.spec_from_file_location('ensamblador_s3',
                       cls.arquitectura / 'tools/assembler.py')
            cls.asm = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = cls.asm
            spec.loader.exec_module(cls.asm)
            cls.referencia = cls.carpeta / 'referencia.vvp'
            proceso = subprocess.run(['iverilog', '-g2012', '-I' + str(cls.arquitectura), '-I' + str(cls.arquitectura / 'rtl'),
                '-s', 'tb_sim_pipeline_ref', '-o', str(cls.referencia),
                str(ROOT / 'tests/vliw/rtl/tb_sim_pipeline_ref.sv')]
                + argumentos_rtl(cls.arquitectura),
                capture_output=True, text=True)
            if proceso.returncode:
                raise RuntimeError(proceso.stdout + proceso.stderr)

    # Invoca el ejecutable del caso con sus argumentos y comprueba su retorno; devuelve la
    # salida para contrastar datos.
    def ejecutar(self, argumentos, esperado=0):
        p = subprocess.run(list(map(str, argumentos)), cwd=ROOT,
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, esperado, p.stdout + p.stderr)
        return p

    # Marca SKIP cuando falta la referencia o las herramientas RTL; evita presentar una omisión
    # como contraste exitoso.
    def exigir_rtl(self):
        if not self.rtl_disponible:
            self.skipTest('se requieren ARQUITECTURA_ROOT, iverilog y vvp para contraste real')

    # Ensambla los bundles de la prueba con la herramienta real y guarda una imagen MEM temporal
    # sin reparar hazards deliberados.
    def crear_mem(self, instrucciones, nombre='programa'):
        # Fijamos la semilla y construimos programas de este escenario para repetir el mismo
        # estímulo. El ayudante contrastar ejecuta después C y RTL y comprueba sus
        # observaciones; aquí se preparan los datos, sin reemplazar ese oráculo.
        self.exigir_rtl()
        # Reutiliza la codificación y serialización del ensamblador existente.
        # Se evita únicamente su verificador global de hazards: N+1/N+2 son
        # programas mal calendarizados deliberados, que S3 no debe corregir.
        bundles = []
        for n, texto in enumerate(instrucciones):
            slots = (self.asm.encode_instruction(texto, pc=16*n),) + tuple(
                self.asm.encode_instruction('nop') for _ in range(3))
            bundles.append(self.asm.Bundle(n, n + 1, slots))
        ruta = self.carpeta / (nombre + '.mem')
        ruta.write_text(self.asm.Program(tuple(bundles)).memory_text())
        return ruta

    # Ejecuta la imagen en el pipeline C de prueba y devuelve su traza para contrastar
    # temporización y estado.
    def simular(self, ruta, cantidad, formato='mem', reset_despues=0):
        return [json.loads(l) for l in self.ejecutar(
            [self.pipeline, formato, ruta, cantidad + 3 + reset_despues, reset_despues]).stdout.splitlines()]

    def contrastar(self, ruta, cantidad, formato='mem', mem_rtl=None, reset_despues=0):
        """Compara ocupación/operandos previos y banco posterior en cada flanco."""
        self.exigir_rtl()
        # Ejecutamos primero la sonda C y luego el top real con la misma IMEM, horizonte e
        # instante de reset. Solo se comparan observaciones de la misma fase: ID previo y banco
        # posterior al flanco.
        simulador = self.simular(ruta, cantidad, formato, reset_despues)
        salida = self.ejecutar(['vvp', self.referencia, '+IMEM=' + str(mem_rtl or ruta),
                               '+CICLOS=' + str(cantidad + 3 + reset_despues),
                               '+RESET_DESPUES=' + str(reset_despues)]).stdout
        lineas = [l.split()[1:] for l in salida.splitlines() if l.startswith('PIPE ')]
        self.assertEqual(len(lineas), cantidad + 3 + reset_despues)
        self.assertEqual(len(simulador), cantidad + 3 + reset_despues)
        # Índices hexadecimales: PC/datos. Banderas y direcciones de registros
        # se imprimen decimales. Ningún dato X/Z puede convertirse sin fallar.
        # El protocolo RTL mezcla campos decimales y hexadecimales. Esta tabla indica cuáles
        # convertir en base 16 antes de comparar; no modifica los valores observados ni
        # normaliza diferencias de ejecución.
        hexadecimales = {1, 4, 6, 8, 12, 13, 14, 15, 16, 17, 20, 21}
        for sim, fila in zip(simulador, lineas):
            rtl = [int(v, 16 if i in hexadecimales or i >= 22 else 10)
                   for i, v in enumerate(fila)]
            with self.subTest(ciclo=sim['ciclo'], programa=Path(ruta).name):
                self.assertEqual(sim['ciclo'], rtl[0])
                self.assertEqual(sim['if_pc'], rtl[1])
                self.assertEqual(sim['pc'], rtl[21])
                self.assertEqual(sim['registros'], rtl[22:])
                for etapa, v, pc in [('if', 2, 1), ('id', 3, 4), ('ex', 5, 6), ('wb', 7, 8)]:
                    # El hardware continúa buscando NOP fuera de la extensión;
                    # S3 deja de admitirlos y drena. Se exige que toda diferencia
                    # de validez pertenezca EXCLUSIVAMENTE a esa región externa.
                    if sim[etapa]:
                        self.assertEqual(rtl[v], 1)
                        self.assertEqual(sim[etapa + '_pc'], rtl[pc])
                        self.assertLess(rtl[pc], cantidad * 16)
                    elif rtl[v]:
                        # El modelo finito puede marcar inválida una etapa fuera del código
                        # mientras RTL propaga NOP. Esa diferencia no se acepta para un PC
                        # perteneciente al programa.
                        self.assertGreaterEqual(rtl[pc], cantidad * 16)
                if sim['id']:
                    self.assertEqual(0 if sim['id_rs1'] == 32 else sim['id_rs1'], rtl[9])
                    self.assertEqual(0 if sim['id_rs2'] == 32 else sim['id_rs2'], rtl[10])
                    inmediato = rtl[12] - 2048 if rtl[12] & 1024 else rtl[12]
                    self.assertEqual(sim['id_a'], rtl[13])
                    self.assertEqual(sim['id_b'], (inmediato & 0xffffffff) if rtl[11] else rtl[14])
                if sim['ex']:
                    self.assertEqual([sim['ex_a'], sim['ex_b'], sim['ex_resultado']], rtl[15:18])
                if sim['wb']:
                    # S2 representa un destino ausente con un índice fuera del banco; el bus
                    # físico usa cero. La normalización permite comparar el puerto sin confundir
                    # ausencia de instrucción con una escritura útil hacia x0.
                    self.assertEqual(sim['wb_escribe'], rtl[18])
                    self.assertEqual(0 if sim['wb_rd'] == 32 else sim['wb_rd'], rtl[19])
                    self.assertEqual(sim['wb_valor'], rtl[20])
        return simulador

    # Ejecuta vectores ALU con oráculos conocidos para comprobar operaciones y extremos
    # aritméticos.
    def test_01_alu_casos_conocidos(self):
        self.assertIn('29 casos conocidos', self.ejecutar([self.alu]).stdout)

    # Ejecuta el banco C y exige sus comprobaciones de pipeline, reset y publicación de
    # resultados.
    def test_02_pipeline_245_comprobaciones(self):
        self.assertIn('245 comprobaciones', self.ejecutar([self.pipeline]).stdout)

    # Contrasta operandos extremos y aleatorios con la ALU RTL real.
    def test_03_alu_extremos_y_aleatorios_con_rtl(self):
        self.exigir_rtl()
        rng = random.Random(43011108)
        extremos = [0, 1, 31, 32, 33, 0x7fffffff, 0x80000000, 0xffffffff]
        vectores = []
        for indice, codigo in enumerate(CODIGOS_RTL):
            pares = [(a, b) for a in extremos for b in extremos]
            pares += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(128)]
            vectores += [f'{indice} {codigo} {a:08x} {b:08x}\n' for a, b in pares]
        ruta = self.carpeta / 'alu.txt'
        ruta.write_text(''.join(vectores))
        c = self.ejecutar([self.alu, ruta]).stdout.splitlines()
        rtl = self.ejecutar(['vvp', self.referencia, '+ALU', '+VECTORES=' + str(ruta)]).stdout
        rtl = [l for l in rtl.splitlines() if l.startswith('ALU ')]
        self.assertEqual(len(c), 3456)
        self.assertEqual(c, rtl)

    # Reproduce la tabla N+3 y comprueba en qué flanco se vuelve visible cada resultado.
    def test_04_tabla_n_mas_3(self):
        programa = ['sumai x2,x0,7', 'suma x5,x2,x0', 'suma x6,x2,x0', 'suma x7,x2,x0']
        traza = self.contrastar(self.crear_mem(programa, 'tabla'), 4)
        self.assertEqual([(traza[c-1]['ciclo'], traza[c-1]['id_a']) for c in (3,4,5)],
                         [(3,0), (4,0), (5,7)])
        self.assertEqual((traza[3]['wb_escribe'], traza[3]['wb_valor']), (1,7))
        # La evidencia completa queda visible al ejecutar este test con -v y
        # puede conservarse activando S3_TRAZA sin crear archivos en el proyecto.
        if os.environ.get('S3_TRAZA'):
            Path(os.environ['S3_TRAZA']).write_text(json.dumps(traza, indent=2) + '\n')

    # Prepara productor y consumidor con una separación controlada para probar exactamente la
    # disponibilidad N+1, N+2 o N+3.
    def dependencia(self, distancia):
        programa = ['sumai x2,x0,7'] + ['nop'] * (distancia - 1) + ['suma x5,x2,x0']
        traza = self.contrastar(self.crear_mem(programa, 'n_mas_' + str(distancia)), len(programa))
        lectura = traza[distancia + 1]
        # El consumidor N+2 captura el banco antes del WB del productor y todavía lee el valor
        # antiguo. Solo N+3 debe recibir el resultado, porque el pipeline no tiene forwarding.
        self.assertEqual(lectura['ciclo'], distancia + 2)
        self.assertEqual(lectura['id_pc'], distancia * 16)
        self.assertEqual(lectura['id_a'], 7 if distancia == 3 else 0)
        self.assertEqual(traza[3]['wb_pc'], 0)
        self.assertEqual(traza[3]['wb_valor'], 7)
        self.assertEqual(traza[-1]['registros'][5], 7 if distancia == 3 else 0)

    # Consume en N+1 y comprueba que se capture el valor anterior, sin forwarding.
    def test_05_dependencia_n_mas_1(self):
        self.dependencia(1)

    # Consume en N+2 y comprueba que todavía no se observe el nuevo WB.
    def test_06_dependencia_n_mas_2(self):
        self.dependencia(2)

    # Consume en N+3 y comprueba que el resultado ya publicado sea visible en ID.
    def test_07_dependencia_n_mas_3(self):
        self.dependencia(3)

    # Reproduce el programa regfile/pipeline original contra el procesador de referencia.
    def test_08_programa_original_regfile_pipeline(self):
        # Reproduce el estímulo del TB original, incluidas dos dependencias
        # peligrosas y una escritura descartada a x0; todas las trazas coinciden.
        p = ['sumai x2,x0,7','suma x5,x2,x0','suma x6,x2,x0','suma x7,x2,x0',
             'sumai x8,x0,-5','nop','nop','suma x9,x8,x7','sumai x0,x0,99',
             'sumai x2,x2,1','suma x10,x2,x0','nop','nop','suma x11,x2,x0']
        t = self.contrastar(self.crear_mem(p, 'original'), len(p))
        self.assertEqual([t[-1]['registros'][i] for i in [0,2,5,6,7,9,10,11]], [0,8,0,0,7,2,7,8])

    # Emite escrituras consecutivas y comprueba su orden de publicación.
    def test_09_escrituras_consecutivas(self):
        p = ['sumai x4,x0,1','sumai x4,x0,2','sumai x4,x0,3',
             'suma x5,x4,x0','suma x6,x4,x0','suma x7,x4,x0']
        t = self.contrastar(self.crear_mem(p, 'escrituras'), len(p))
        self.assertEqual([t[c-1]['id_a'] for c in (5,6,7)], [1,2,3])
        self.assertEqual(t[-1]['registros'][4:8], [3,1,2,3])

    # Encadena dependencias y contrasta los valores capturados por cada consumidor.
    def test_10_dependencias_encadenadas(self):
        p = ['sumai x1,x0,9','nop','nop','sumai x2,x1,4','suma x3,x2,x1',
             'nop','suma x4,x2,x3','suma x5,x2,x3']
        t = self.contrastar(self.crear_mem(p, 'cadena'), len(p))
        self.assertEqual(t[-1]['registros'][2:6], [13,9,13,22])

    # Comprueba NOP, x0, x31 y drenado sin confundir huecos con instrucciones útiles.
    def test_11_nop_x0_x31_y_vaciado(self):
        for nombre, p in [('nop', ['nop'] * 3), ('x0', ['sumai x0,x0,99','nop','sumai x31,x0,13'])]:
            with self.subTest(nombre=nombre):
                t = self.contrastar(self.crear_mem(p, nombre), len(p))
                self.assertEqual(t[-1]['registros'][0], 0)
                self.assertEqual(sum(c['wb_escribe'] for c in t), int(nombre == 'x0'))
                self.assertEqual(t[-1]['registros'][31], 13 if nombre == 'x0' else 0)

    # Carga MEM y BALB y ejecuta las operaciones ALU conservando resultados equivalentes.
    def test_12_integracion_mem_balb_18_operaciones(self):
        self.exigir_rtl()
        # Fuentes sin hazards: x1/x2 se inicializan con distancia suficiente y
        # cada resultado usa un destino diferente. BALC existente crea BALB y
        # MEM, por lo que este caso recorre el flujo real S1 -> S2 -> S3.
        p = ['sumai x1,x0,-8', 'sumai x2,x0,33', 'nop', 'nop']
        inmediatos = [-1024, 1023, 2047, 32, 31, -1, -1024, 1023]
        for n, op in enumerate(OPERACIONES):
            b = 'x2' if n < 10 else str(inmediatos[n-10])
            p.append(f'{op} x{n+3},x1,{b}')
        fuente = self.carpeta / 'integracion.asm'
        fuente.write_text(''.join(f'{op} | nop | nop | nop\n' for op in p))
        binario = self.carpeta / 'integracion.bin'
        self.ejecutar([ROOT / 'balc', '-x', fuente, '-o', binario])
        mem = binario.with_suffix('.mem')
        a = self.contrastar(mem, len(p))
        b = self.contrastar(binario, len(p), 'balb', mem)
        self.assertEqual(a, b)
        self.assertEqual(a[-1]['registros'][1:3], [0xfffffff8, 33])
        self.assertEqual({c['ex_op'] for c in a if c['ex'] and c['ex_op'] != 'nop'}, set(OPERACIONES))

    # Contrasta un programa aleatorio con dependencias y una semilla reproducible.
    def test_13_programa_aleatorio_con_dependencias(self):
        self.exigir_rtl()
        rng = random.Random(1108)
        p = []
        for _ in range(128):
            op = rng.choice(OPERACIONES)
            rd, rs1, rs2 = [rng.randrange(32) for _ in range(3)]
            b = (str(rng.randrange(2048) if op in ('cizqi', 'cderi', 'caderi')
                     else rng.randrange(-1024, 1024)) if op.endswith('i') else f'x{rs2}')
            p.append(f'{op} x{rd},x{rs1},{b}')
        self.contrastar(self.crear_mem(p, 'aleatorio'), len(p))

    # Reinicia con resultados pendientes y publicados y compara el reset con RTL.
    def test_14_reset_con_resultados_pendientes_y_publicados(self):
        # Se reinicia antes del primer WB y también después de escrituras reales.
        # Cada época vuelve a PC=0/ciclo=1 y debe coincidir con reset del top.
        p = ['sumai x2,x0,7','sumai x31,x0,13','nop','suma x5,x2,x0',
             'nop','nop','suma x6,x31,x0']
        for instante in (3, 5):
            with self.subTest(reset_despues=instante):
                t = self.contrastar(self.crear_mem(p, 'reset'), len(p), reset_despues=instante)
                # El reset intermedio descarta resultados pendientes y reinicia la numeración de
                # ciclos. La ejecución posterior debe volver a obtener los valores del programa,
                # sin reutilizar registros de la época anterior.
                self.assertEqual(t[instante]['ciclo'], 1)
                self.assertEqual(t[instante]['registros'], [0]*32)
                self.assertEqual(t[-1]['registros'][5:7], [7,13])


if __name__ == '__main__':
    unittest.main()
