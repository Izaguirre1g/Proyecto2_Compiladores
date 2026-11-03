"""Verifica S4, las transacciones y las trazas ALU+LSU contra Arquitectura real."""
from pathlib import Path
import importlib.util
import itertools
import json
import os
import random
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class LsuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporal = tempfile.TemporaryDirectory(prefix='pruebas-lsu-')
        cls.addClassCleanup(cls.temporal.cleanup)
        cls.carpeta = Path(cls.temporal.name)
        cls.sonda = cls.carpeta / 'lsu'
        cls.vectores = cls.ciclos = cls.programas = 0
        cc = shlex.split(os.environ.get('CC', 'cc'))
        flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        # S4 puede probarse sin parser; el caso BALB utiliza después BALC real.
        cls.compilar_c = cc + flags + ['tests/test_sim_lsu.c', 'src/sim_lsu.c',
            'src/sim_pipeline.c', 'src/sim_crypto.c', 'src/sim_bru.c', 'src/sim_alu.c', 'src/sim_decode.c', 'src/sim_cargador.c',
            '-o', str(cls.sonda)]
        subprocess.run(cls.compilar_c, cwd=ROOT, check=True)
        subprocess.run(['make', 'balc'], cwd=ROOT, check=True, capture_output=True)
        cls.arquitectura = Path(os.environ.get('ARQUITECTURA_ROOT',
                                  ROOT.parent / 'Proyecto2_Arquitectura1')).resolve()
        cls.rtl_disponible = (cls.arquitectura / 'rtl/top.sv').is_file() and all(
            shutil.which(h) for h in ['iverilog', 'vvp'])
        if cls.rtl_disponible:
            spec = importlib.util.spec_from_file_location('ensamblador_s4', cls.arquitectura / 'tools/assembler.py')
            cls.asm = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = cls.asm
            spec.loader.exec_module(cls.asm)
            cls.referencia = cls.carpeta / 'lsu.vvp'
            cls.compilar_sv = ['iverilog', '-g2012', '-I' + str(cls.arquitectura),
                '-I' + str(cls.arquitectura / 'rtl'), '-s', 'tb_sim_lsu_ref',
                '-o', str(cls.referencia), str(ROOT / 'tests/rtl/tb_sim_lsu_ref.sv')]
            cls.compilar_sv += list(map(str, sorted((cls.arquitectura / 'rtl').glob('*.sv'))))
            p = subprocess.run(cls.compilar_sv, capture_output=True, text=True)
            if p.returncode:
                raise RuntimeError(p.stdout + p.stderr)
        # Registro opcional fuera del repositorio: conserva comandos exactos,
        # resultados y conteos sin agregar artefactos al árbol de trabajo.
        cls.comandos = [dict(comando=cls.compilar_c, codigo=0)]
        if cls.rtl_disponible:
            cls.comandos.append(dict(comando=cls.compilar_sv, codigo=0))

    @classmethod
    def tearDownClass(cls):
        print(f'Contraste S4: {cls.vectores} vectores LSU, {cls.ciclos} ciclos, {cls.programas} ejecuciones top.')
        if os.environ.get('S4_EVIDENCIA'):
            Path(os.environ['S4_EVIDENCIA']).write_text(json.dumps(dict(
                vectores=cls.vectores, ciclos=cls.ciclos, programas=cls.programas,
                comandos=cls.comandos), indent=2) + '\n')

    def ejecutar(self, comando, esperado=0):
        p = subprocess.run(list(map(str, comando)), cwd=ROOT, capture_output=True, text=True)
        self.comandos.append(dict(comando=list(map(str, comando)), codigo=p.returncode))
        self.assertEqual(p.returncode, esperado, p.stdout + p.stderr)
        return p.stdout

    def exigir_rtl(self):
        if not self.rtl_disponible:
            self.skipTest('se requieren Arquitectura, iverilog y vvp para contraste RTL real')

    def crear_mem(self, programa):
        self.exigir_rtl()
        bundles = []
        for n, fila in enumerate(programa):
            # El ensamblador existente conserva la codificación ISA. Se evita
            # solo el análisis global de hazards para probar dependencias malas.
            slots = tuple(self.asm.encode_instruction(texto, pc=16*n) for texto in (*fila, 'nop', 'nop'))
            bundles.append(self.asm.Bundle(n, n+1, slots))
        ruta = self.carpeta / 'programa.mem'
        ruta.write_text(self.asm.Program(tuple(bundles)).memory_text())
        return ruta

    def contrastar(self, programa, datos=None, reset_despues=0, restaurar=False, balb=None):
        self.exigir_rtl()
        ruta = self.crear_mem(programa)
        inicial = bytearray(65536) if datos is None else bytearray(datos)
        self.assertEqual(len(inicial), 65536)
        crudo, palabras = self.carpeta / 'datos.bin', self.carpeta / 'datos.hex'
        crudo.write_bytes(inicial)
        palabras.write_text(''.join(f'{int.from_bytes(inicial[p:p+4], "little"):08x}\n' for p in range(0,65536,4)))
        cantidad = len(programa)
        limite = cantidad + 3 + reset_despues
        modo = 'restaurar' if restaurar else 'conservar'
        args = [self.sonda, 'balb' if balb else 'mem', balb or ruta, limite, reset_despues, modo]
        if not balb:
            args += [crudo]
        c = [json.loads(l) for l in self.ejecutar(args).splitlines()]
        args = ['vvp', self.referencia, '+IMEM=' + str(ruta), '+DMEM=' + str(palabras),
                '+CICLOS=' + str(limite), '+RESET_DESPUES=' + str(reset_despues)]
        if restaurar:
            args += ['+RESTAURAR']
        rtl = [json.loads(l) for l in self.ejecutar(args).splitlines() if l.startswith('{')]
        cm, rm = [{v['mem']:v['valor'] for v in lista if 'mem' in v} for lista in (c,rtl)]
        self.assertEqual(cm, rm, 'DMEM completa final')
        c, rtl = [[v for v in lista if 'mem' not in v] for lista in (c,rtl)]
        self.assertEqual(len(c), limite + int(bool(reset_despues)))
        self.assertEqual(len(c), len(rtl))
        for sim, hw in zip(c, rtl):
            if 'reset' in sim:
                self.assertEqual(sim, hw, 'flanco de reset y conservación/restauración DMEM')
                continue
            with self.subTest(ciclo=sim['c']):
                self.assertEqual(sim['c'], hw['c'])
                self.assertEqual(sim['pc'], hw['pc'])
                self.assertEqual(sim['pcs'][0], hw['pc_pre'])
                self.assertEqual(sim['regs'], hw['regs'])
                self.assertEqual(sim['conflicto'], hw['conflicto'])
                for n, (v, real) in enumerate(zip(sim['stages'], hw['stages'])):
                    if v:
                        self.assertEqual(real, 1)
                        self.assertEqual(sim['pcs'][n], hw['pcs'][n])
                        self.assertLess(hw['pcs'][n], cantidad*16)
                    elif real:
                        self.assertGreaterEqual(hw['pcs'][n], cantidad*16)
                if sim['stages'][1]:
                    self.assertEqual(sim['idalu'], hw['idalu'])
                    self.assertEqual(sim['idlsu'][0], hw['idlsu'][0])
                    if sim['idlsu'][0]:
                        self.assertEqual(sim['idlsu'], hw['idlsu'])
                if sim['stages'][2]:
                    self.assertEqual(sim['exalu'], hw['exalu'])
                    self.assertEqual(sim['x'][0], hw['x'][0])
                    if sim['x'][0]:
                        self.assertEqual(sim['x'], hw['x'], 'señales LSU antes del flanco')
                        self.assertEqual(sim['postmem'], hw['postmem'], 'palabra posterior al acceso')
                if sim['stages'][3]:
                    w0 = list(sim['w0'])
                    if w0[1] == 32:
                        w0[1] = 0  # NOP S2 sin destino frente al x0 físico del decoder.
                    self.assertEqual(w0, hw['w0'])
                self.assertEqual(sim['w1_valida'], hw['w1_valida'])
                self.assertEqual(sim['w1'][0], hw['w1'][0])
                if sim['w1_valida']:
                    self.assertEqual(sim['w1'], hw['w1'])
                self.assertEqual(sim['pending'][0], hw['pending'][0])
                if sim['pending'][0]:
                    self.assertEqual(sim['pending'], hw['pending'])
        type(self).ciclos += limite
        type(self).programas += 1
        return c, cm

    def test_01_unitarias_memoria_pipeline_y_recursos(self):
        self.assertIn('233 comprobaciones', self.ejecutar([self.sonda]))

    def test_02_vectores_lsu_memory_reales(self):
        self.exigir_rtl()
        rng = random.Random(43011108)
        filas = []
        bases = [0,1,2,3,65532,65535,65536,0xffffffff]
        offsets = [-1024,-1,0,1,1023]
        valores = [0,0x7f80ff00,0x12345678,0xffffffff]
        for indice,base,off,inicial in itertools.product(range(4),bases,offsets,valores):
            filas.append((indice,[0,1,8,9][indice],base,off&2047,rng.getrandbits(32),31,inicial,len(filas)%2,(len(filas)//2)%2))
        for indice,pos,octeto in itertools.product((1,3),range(4),(0,0x7f,0x80,0xff)):
            filas.append((indice,[0,1,8,9][indice],pos,0,0xdead0000|octeto,2,octeto<<(8*pos),1,0))
        for _ in range(512):
            indice=rng.randrange(4)
            filas.append((indice,[0,1,8,9][indice],rng.getrandbits(32),rng.randrange(2048),rng.getrandbits(32),rng.randrange(32),rng.getrandbits(32),rng.randrange(2),rng.randrange(2)))
        ruta=self.carpeta/'vectores.txt'
        ruta.write_text(''.join(f'{i} {op} {base:08x} {imm:03x} {dato:08x} {rd} {inicial:08x} {v} {r}\n'
                              for i,op,base,imm,dato,rd,inicial,v,r in filas))
        c=self.ejecutar([self.sonda,'vectores',ruta]).splitlines()
        rtl=[l for l in self.ejecutar(['vvp',self.referencia,'+VECTORES='+str(ruta)]).splitlines() if l.startswith('V ')]
        self.assertEqual(len(c),len(filas))
        for n,(a,b) in enumerate(zip(c,rtl)):
            self.assertEqual(a,b,f'vector {n}: {filas[n]}')
        self.assertEqual(len(c),len(rtl))
        type(self).vectores=len(filas)

    def test_03_tabla_store_load(self):
        # B0 almacena 0 sobre 12345678 en EX/ciclo 3. B1 lee el cero en EX/4
        # y lo publica en WB/5. El NOP ALU no añade una quinta etapa.
        datos=bytearray(65536); datos[:4]=(0x12345678).to_bytes(4,'little')
        p=[('nop','guardap x0,x0,0'),('nop','cargai x2,x0,0')]
        t,m=self.contrastar(p,datos)
        self.assertEqual((t[2]['x'][5],t[2]['postmem']),(0x12345678,0))
        self.assertEqual((t[3]['pending'],t[4]['w1']),([1,2,0],[1,2,0]))
        self.assertEqual(m,{})
        if os.environ.get('S4_TABLA'):
            Path(os.environ['S4_TABLA']).write_text(json.dumps(t,indent=2)+'\n')

    def test_04_palabras_bytes_y_direcciones(self):
        # Fuente -1 queda disponible para LSU tras N+3. Palabra no alineada,
        # byte alto, alias 64 KiB y wrap de dirección usan memoria física real.
        p=[('sumai x1,x0,-1','nop'),('sumai x2,x0,1','nop'),('nop','nop'),
           ('nop','guardap x0,x1,3'),('nop','cargai x3,x0,1'),
           ('nop','guardab x0,x2,2'),('nop','cargai x4,x0,0')]
        p += [('nop',f'cargabai x{5+n},x0,{n}') for n in range(4)]
        p += [('nop','guardab x1,x2,0'),('nop','cargai x10,x1,0'),('nop','cargai x11,x1,1')]
        t,m=self.contrastar(p)
        self.assertEqual(t[-1]['regs'][3:9],[0xffffffff,0xff01ffff,0xffffffff,0xffffffff,1,0xffffffff])
        self.assertEqual(m[65532],0x01000000)
        self.assertEqual(t[-1]['regs'][11],0xff01ffff)

    def test_05_alu_y_lsu_simultaneos_y_conflictos(self):
        datos=bytearray(65536); datos[:4]=(99).to_bytes(4,'little')
        for destino in (0,5,6):
            with self.subTest(destino=destino):
                p=[('sumai x5,x0,7',f'cargai x{destino},x0,0')]
                t,_=self.contrastar(p,datos)
                self.assertEqual(t[-1]['conflicto'],int(destino==5))
                self.assertEqual(t[-1]['regs'][5],99 if destino==5 else 7)
                self.assertEqual(t[-1]['regs'][0],0)
        # El store del mismo bundle no utiliza el 7 producido por ALU.
        t,m=self.contrastar([('sumai x5,x0,7','guardap x0,x5,0')],datos)
        self.assertEqual(m,{})
        self.assertEqual(t[1]['idlsu'][2],0)

    def test_06_load_a_alu_n_mas_1_2_3(self):
        datos=bytearray(65536); datos[0]=7
        for d in (1,2,3):
            p=[('nop','cargai x2,x0,0')]+[('nop','nop')]*(d-1)+[('suma x5,x2,x0','nop')]
            with self.subTest(distancia=d):
                t,_=self.contrastar(p,datos)
                self.assertEqual(t[d+1]['idalu'][0],7 if d==3 else 0)
                self.assertEqual(t[3]['w1'],[1,2,7])
                self.assertEqual(t[-1]['regs'][5],7 if d==3 else 0)

    def test_07_alu_a_base_y_dato_store(self):
        for d,fuente in itertools.product((1,2,3),('base','dato')):
            store='guardap x2,x0,0' if fuente=='base' else 'guardap x0,x2,0'
            p=[('sumai x2,x0,4','nop')]+[('nop','nop')]*(d-1)+[('nop',store)]
            with self.subTest(distancia=d,fuente=fuente):
                t,m=self.contrastar(p)
                self.assertEqual(t[d+1]['idlsu'][1 if fuente=='base' else 2],4 if d==3 else 0)
                if fuente=='dato': self.assertEqual(m,{0:4} if d==3 else {})

    def test_08_load_a_store_sin_forwarding(self):
        datos=bytearray(65536); datos[0]=23
        for d in (1,2,3):
            p=[('nop','cargai x2,x0,0')]+[('nop','nop')]*(d-1)+[('nop','guardap x0,x2,4')]
            t,m=self.contrastar(p,datos)
            self.assertEqual(t[d+1]['idlsu'][2],23 if d==3 else 0)
            self.assertEqual(m.get(4,0),23 if d==3 else 0)

    def test_09_accesos_consecutivos(self):
        datos=bytearray(65536); datos[:8]=bytes([0x80,0xff,0x7f,0,9,0,0,0])
        p=[('nop','cargai x1,x0,0'),('nop','cargabai x2,x0,1'),
           ('nop','guardab x0,x0,1'),('nop','guardap x0,x0,4'),
           ('nop','cargai x3,x0,0'),('nop','cargai x4,x0,4')]
        t,m=self.contrastar(p,datos)
        self.assertEqual([t[-1]['regs'][n] for n in (1,2,3,4)],[0x007fff80,0xffffffff,0x007f0080,0])
        self.assertEqual(m,{0:0x007f0080})

    def test_10_reset_store_y_load(self):
        datos=bytearray(65536); datos[0]=0x80
        for operacion in ('guardap x0,x0,0','cargabai x31,x0,0'):
            for instante in (1,2,3,4):
                with self.subTest(op=operacion,reset=instante):
                    p=[('sumai x5,x0,7',operacion)]+[('nop','nop')]*3
                    t,_=self.contrastar(p,datos,reset_despues=instante)
                    reset=next(c for c in t if 'reset' in c)
                    self.assertEqual(reset['regs'],[0]*32)
                    esperado=0 if operacion.startswith('guardap') and instante>=2 else 0x80
                    self.assertEqual(reset['despues'],esperado)
        # Recargar la imagen es una acción distinta, posterior al reset que
        # pudo publicar un store. Ambos modelos restauran explícitamente DMEM.
        t,_=self.contrastar([('nop','guardap x0,x0,0')]+[('nop','nop')]*3,
                            datos,reset_despues=2,restaurar=True)
        self.assertEqual(next(c for c in t if 'reset' in c)['despues'],0x80)

    def test_11_balb_datos_iniciales(self):
        self.exigir_rtl()
        p=[('nop','cargai x1,x0,16'),('nop','cargabai x2,x0,19'),
           ('nop','guardap x0,x0,16'),('nop','cargai x3,x0,16')]
        fuente=self.carpeta/'datos.asm'
        fuente.write_text(''.join(f'{a} | {l} | nop | nop\n' for a,l in p))
        binario=self.carpeta/'datos.balb'
        self.ejecutar([ROOT/'balc','-x',fuente,'-o',binario])
        # El escritor BALB existente no tiene sintaxis de datos en -x. Se usa
        # su contenedor real y se agrega una sección v1 válida de fixture; S1
        # sigue siendo el único lector. El RTL se precarga explícitamente igual.
        import struct
        contenido=bytearray(binario.read_bytes())
        struct.pack_into('<I',contenido,12,len(contenido)+4)
        struct.pack_into('<I',contenido,36,4)
        struct.pack_into('<I',contenido,40,16)
        contenido += bytes([0x78,0x56,0x34,0x80])
        binario.write_bytes(contenido)
        datos=bytearray(65536); datos[16:20]=bytes([0x78,0x56,0x34,0x80])
        t,m=self.contrastar(p,datos,balb=binario)
        self.assertEqual(t[-1]['regs'][1:4],[0x80345678,0xffffff80,0])
        self.assertEqual(m,{})

    def test_12_aleatorio_alu_lsu_con_aliasing(self):
        self.exigir_rtl()
        rng=random.Random(11084301)
        datos=bytearray(rng.getrandbits(8) for _ in range(65536))
        p=[]
        for _ in range(128):
            alu=f'sumai x{rng.randrange(32)},x{rng.randrange(32)},{rng.randrange(-1024,1024)}'
            op=rng.choice(['guardap','guardab','cargai','cargabai'])
            l=f'{op} x{rng.randrange(32)},x{rng.randrange(32)},{rng.randrange(-1024,1024)}'
            p.append((alu,l))
        self.contrastar(p,datos)

    def test_13_constante_generada_por_compilador(self):
        self.exigir_rtl()
        import struct
        # El compilador produce realmente la sección global. El arranque BAL
        # completo usa saltos (S5); por eso se conserva su payload de datos y se
        # reemplaza SOLO código del fixture por un programa ALU/LSU acotado.
        fuente=self.carpeta/'global.bal'
        fuente.write_text("declare_const A * declare_int : neumann 128;\n"
            "create_funk #declare_infinite_void# main() { valor * declare_int : A; }\n")
        globales=self.carpeta/'global.bin'
        self.ejecutar([ROOT/'balc','-o',globales,fuente])
        raw=globales.read_bytes()
        offset,cantidad,base,flags=struct.unpack_from('<4I',raw,32)
        self.assertEqual((cantidad,base,flags),(4,0x4000,1))
        payload=raw[offset:offset+cantidad]
        self.assertEqual(payload,struct.pack('<i',-128))
        p=[('sumai x1,x0,1','nop'),('nop','nop'),('nop','nop'),
           ('cizqi x2,x1,14','nop'),('nop','nop'),('nop','nop'),
           ('nop','cargai x3,x2,0'),('nop','cargabai x4,x2,0'),
           ('nop','guardap x2,x0,0'),('nop','cargai x5,x2,0')]
        # Código codificado por la herramienta existente; el contenedor v1 de
        # prueba conserva los datos generados y elimina el arranque con BRU.
        mem=self.crear_mem(p)
        codigo=b''.join(int(l,16).to_bytes(16,'little') for l in mem.read_text().splitlines())
        header=struct.pack('<4s11I',b'BALB',1,48,48+len(codigo)+cantidad,0,48,len(codigo),0,
                           48+len(codigo),cantidad,base,0)
        fixture=self.carpeta/'global-lsu.balb'
        fixture.write_bytes(header+codigo+payload)
        datos=bytearray(65536); datos[base:base+cantidad]=payload
        t,m=self.contrastar(p,datos,balb=fixture)
        self.assertEqual(t[-1]['regs'][2:6],[0x4000,0xffffff80,0xffffff80,0])
        self.assertEqual(m,{})

    def test_14_store_palabra_load_byte_inmediato(self):
        # Un store palabra y un load byte consecutivos se comunican por DMEM
        # sin esperar N+3. Esa distancia solo corresponde al banco de registros.
        p=[('sumai x1,x0,-128','nop'),('nop','nop'),('nop','nop'),
           ('nop','guardap x0,x1,3'),('nop','cargabai x2,x0,0')]
        t,m=self.contrastar(p)
        self.assertEqual(t[5]['x'][7],1)  # EX store en ciclo 6.
        self.assertEqual(t[6]['x'][6],0xffffff80)  # EX load en ciclo 7.
        self.assertEqual(t[-1]['regs'][2],0xffffff80)
        self.assertEqual(m,{0:0xffffff80})


if __name__=='__main__':
    unittest.main()
