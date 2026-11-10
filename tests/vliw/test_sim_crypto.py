"""S6: compara unidad criptográfica, cuatro slots y flancos completos con RTL.

Los programas usan el ensamblador del Proyecto I. La sonda interna de pruebas
observa la bóveda; ninguna traza pública del simulador publica esos secretos.
"""
from pathlib import Path
import importlib.util
import itertools
import json
import os
import random
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
# Arquitectura conserva el núcleo pero puede ubicar su top en rtl-p2.
sys.path.insert(0, str(ROOT / 'tools/vliw'))
from rutas_vliw import top_rtl, argumentos_rtl
NOP = ('nop',)*4
KEY = (0x0f1e2d3c, 0x4b5a6978, 0x8796a5b4, 0xc3d2e1f0)


def bundle(crypto='nop', alu='nop', lsu='nop', bru='nop'):
    return alu, lsu, bru, crypto


def rol(x, n):
    # Modelo independiente mediante una cadena circular de bits: evita
    # compartir la implementación de shifts de C o la función del RTL.
    bits = f'{x:032b}'
    n %= 32
    return int(bits[n:]+bits[:n], 2)


def encrypt(l, r, key):
    for k in key:
        l, r = r, l ^ (((rol(r, 5)+k) & 0xffffffff) ^ rol(r, 13))
    return l, r


class CryptoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='pruebas-crypto-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.cmds = []
        cls.programas = cls.ciclos = cls.vectores = 0
        cls.arch = Path(os.environ.get('ARQUITECTURA_ROOT', ROOT.parent/'Proyecto2_Arquitectura1')).resolve()
        cls.rtl = top_rtl(cls.arch).is_file() and all(shutil.which(t) for t in ('iverilog', 'vvp'))
        cls.c = cls.dir/'sonda'
        cls.run_cmd(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude',
            'tests/vliw/test_sim_crypto.c'] + [f'src/Simulador_VLIW/sim_{n}.c' for n in ('crypto', 'pipeline', 'alu', 'lsu', 'bru', 'decode', 'cargador')] + ['-o', str(cls.c)])
        cls.run_cmd(['make', 'balc'])
        if cls.rtl:
            spec = importlib.util.spec_from_file_location('ensamblador_s6', cls.arch/'tools/assembler.py')
            cls.asm = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = cls.asm
            spec.loader.exec_module(cls.asm)
            cls.sv = cls.dir/'referencia.vvp'
            cls.run_cmd(['iverilog', '-g2012', '-I'+str(cls.arch), '-I'+str(cls.arch/'rtl'), '-s', 'tb_sim_crypto_ref', '-o', str(cls.sv),
                str(ROOT/'tests/vliw/rtl/tb_sim_crypto_ref.sv')] + argumentos_rtl(cls.arch))

    @classmethod
    def run_cmd(cls, cmd, code=0):
        cmd = list(map(str, cmd))
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        cls.cmds.append(dict(comando=cmd, codigo=p.returncode))
        if p.returncode != code:
            raise AssertionError(f'{cmd}: salida {p.returncode}, esperada {code}\n{p.stdout}\n{p.stderr}')
        return p.stdout

    @classmethod
    def tearDownClass(cls):
        print(f'Contraste S6: {cls.vectores} vectores Feistel/ROL, {cls.ciclos} ciclos, {cls.programas} programas top.')
        if os.environ.get('S6_EVIDENCIA'):
            Path(os.environ['S6_EVIDENCIA']).write_text(json.dumps(dict(vectores=cls.vectores, ciclos=cls.ciclos,
                programas=cls.programas, comandos=cls.cmds), indent=2)+'\n')

    def exigir_rtl(self):
        if not self.rtl:
            self.skipTest('se requieren Arquitectura, iverilog y vvp; contraste no ejecutado')

    def contrastar(self, programa, datos=None, ciclos=None, reset=0, balb=False, tabla=None):
        self.exigir_rtl()
        datos = bytearray(65536) if datos is None else bytearray(datos)
        mem = self.dir/'programa.mem'
        b = [self.asm.Bundle(n, n+1, tuple(self.asm.encode_instruction(t, pc=n*16) for t in fila)) for n, fila in enumerate(programa)]
        mem.write_text(self.asm.Program(tuple(b)).memory_text())
        crudo = self.dir/'datos.bin'
        crudo.write_bytes(datos)
        dmem = self.dir/'datos.hex'
        dmem.write_text(''.join(f'{int.from_bytes(datos[i:i+4],"little"):08x}\n' for i in range(0, 65536, 4)))
        archivo = mem
        if balb:
            fuente = self.dir/'programa.asm'
            fuente.write_text(''.join(' | '.join(f)+'\n' for f in programa))
            archivo = self.dir/'programa.balb'
            self.run_cmd([ROOT/'balc', '-x', fuente, '-o', archivo])
            # -x no tiene sintaxis de datos. Se amplía únicamente el fixture
            # BALB real; S1 valida el contenedor y el RTL recibe los mismos bytes.
            raw = bytearray(archivo.read_bytes())
            n = max((i+1 for i, v in enumerate(datos) if v), default=0)
            self.assertLessEqual(n, 16384)
            struct.pack_into('<I', raw, 12, len(raw)+n)
            struct.pack_into('<I', raw, 36, n)
            struct.pack_into('<I', raw, 40, 0)
            raw.extend(datos[:n])
            archivo.write_bytes(raw)
        ciclos = ciclos or len(programa)+3
        c = [json.loads(l) for l in self.run_cmd([self.c, 'balb' if balb else 'mem', archivo, '-' if balb else crudo, ciclos, reset, 'sonda']).splitlines()]
        sv = [json.loads(l) for l in self.run_cmd(['vvp', self.sv, '+IMEM='+str(mem), '+DMEM='+str(dmem), '+CICLOS='+str(ciclos), '+RESET_DESPUES='+str(reset)]).splitlines() if l.startswith('{')]
        ct = [x for x in c if 'c' in x]
        st = [x for x in sv if 'c' in x]
        self.assertEqual(len(ct), ciclos)
        self.assertEqual(len(st), ciclos)
        for paso, (a, b) in enumerate(zip(ct, st)):
            with self.subTest(paso=paso+1, pc=a['pc_pre']):
                for k in a:
                    self.assertEqual(a[k], b[k], f'campo {k}, paso {paso+1}: C={a[k]}, RTL={b[k]}')
        cm = {x['mem']: x['valor'] for x in c if 'mem' in x}
        sm = {x['mem']: x['valor'] for x in sv if 'mem' in x}
        self.assertEqual(cm, sm, 'DMEM final completa')
        type(self).programas += 1
        type(self).ciclos += ciclos
        if tabla and os.environ.get('S6_TABLAS'):
            out = Path(os.environ['S6_TABLAS']); out.mkdir(exist_ok=True)
            (out/(tabla+'.json')).write_text(json.dumps(ct, indent=2)+'\n')
        return ct, cm

    def provision(self, pwd=0, direccion=0):
        # x2 se produce por ALU y se consume N+3. setpwd cambia la bóveda
        # en EX; vcr del siguiente bundle ya lee esa contraseña actualizada.
        # Dos NOP después de vcr permiten AUTH visible en ID del protegido.
        return [bundle(alu=f'sumai x2,x0,{pwd}'), NOP, NOP,
                bundle('setpwd x2'), bundle(f'vcr {direccion}'), NOP, NOP]

    def cargar(self, key=KEY, l=0x414c4f48, r=0x36323032, lk=0):
        datos = bytearray(65536)
        for i, x in enumerate((*key, l, r)):
            struct.pack_into('<I', datos, 16+4*i, x)
        p = self.provision()
        for reg, addr in zip((8, 10, 12, 14, 4, 5), range(16, 40, 4)):
            p.append(bundle(lsu=f'cargai x{reg},x0,{addr}'))
        p += [NOP, NOP, bundle(f'ell {lk},0,x8,x10'), bundle(f'ell {lk},2,x12,x14')]
        return p, datos

    def test_01_unitarias(self):
        self.assertIn('comprobaciones unitarias PASS', self.run_cmd([self.c]))

    def test_02_vectores_independientes_y_rtl(self):
        self.exigir_rtl()
        rng = random.Random(110843016)
        valores = (0, 1, 0x7fffffff, 0x80000000, 0xffffffff, 0xaaaaaaaa)
        filas = list(itertools.product(valores, valores, valores, (0, 1, 5, 13, 31)))
        filas += [(rng.getrandbits(32), rng.getrandbits(32), rng.getrandbits(32), rng.randrange(32)) for _ in range(1024)]
        f = self.dir/'vectores.txt'
        f.write_text(''.join(f'{l:08x} {r:08x} {k:08x} {n}\n' for l, r, k, n in filas))
        c = self.run_cmd([self.c, 'vectores', f]).splitlines()
        rtl = [l for l in self.run_cmd(['vvp', self.sv, '+VECTORES='+str(f)]).splitlines() if l.startswith('F ')]
        self.assertEqual(c, rtl)
        self.assertEqual(len(c), len(filas))
        for line, (l, r, k, n) in zip(c, filas):
            f = lambda x: ((rol(x, 5)+k) & 0xffffffff) ^ rol(x, 13)
            self.assertEqual([int(x, 16) for x in line.split()[1:]], [rol(l, n), f(r), r, l ^ f(r), r ^ f(l), l])
        type(self).vectores = len(filas)

    def test_03_boveda_estado_y_tabla_a(self):
        p = self.provision()+[bundle('fsl x4,x2,0,0')]
        t, _ = self.contrastar(p, tabla='A-provisionamiento')
        self.assertEqual(t[5]['estado'], 2)  # EX setpwd aún no publica INIT.
        self.assertEqual(t[6]['estado'], 0)
        self.assertEqual(t[7]['estado'], 1)
        self.assertEqual(t[8]['id_crypto_fault'], 0)
        self.assertFalse(any(c['fault'] for c in t))

    def test_04_cuatro_rondas_y_referencia_conocida(self):
        for lk, (l, r) in itertools.product(range(4), [(0, 0), (0xffffffff, 0x80000000), (0x414c4f48, 0x36323032)]):
            with self.subTest(lk=lk, l=l, r=r):
                p, datos = self.cargar(l=l, r=r, lk=lk)
                inicio = len(p)
                for k in range(4):
                    p += [bundle(f'fsl x4,x4,{lk},{k}'), NOP, NOP]
                for k in reversed(range(4)):
                    p += [bundle(f'fsli x4,x4,{lk},{k}'), NOP, NOP]
                t, _ = self.contrastar(p, datos)
                cifrado = t[inicio+12]['regs'][4:6]
                self.assertEqual(cifrado, list(encrypt(l, r, KEY)))
                if (l, r) == (0x414c4f48, 0x36323032):
                    self.assertEqual(cifrado, [0x03d44466, 0xd685afaf])
                self.assertEqual(t[-1]['regs'][4:6], [l, r])
                self.assertFalse(any(c['fault'] for c in t))

    def test_05_ell_inmediata_y_sobrescritura(self):
        for lk, op in itertools.product(range(4), ('fsl', 'fsli')):
            p, datos = self.cargar(lk=lk)
            p += [bundle(f'{op} x30,x4,{lk},3'), bundle(f'ell {lk},2,x0,x0'), bundle(f'{op} x28,x4,{lk},3')]
            t, _ = self.contrastar(p, datos)
            self.assertNotEqual(t[-1]['regs'][28:30], t[-1]['regs'][30:32])
            self.assertEqual(t[-1]['sonda_boveda'][2+lk*4+2:2+lk*4+4], [0, 0])

    def test_06_auth_init_distancias_y_consecutivas(self):
        # La autorización queda capturada ID antes de publicar WB. N+1/N+2
        # ven AUTH viejo; N+3 ve el nuevo. setpwd consecutivos son legales si
        # sus ID todavía vieron INIT=1, aunque pwd_set ya esté activo en EX.
        for op, d in itertools.product(('fsl x4,x2,0,0', 'fsli x4,x2,0,0', 'ell 0,0,x0,x0', 'camcon 0,1'), (1, 2, 3)):
            p = self.provision()[:5]+[NOP]*(d-1)+[bundle(op)]
            t, _ = self.contrastar(p, ciclos=len(p)+6)
            self.assertEqual(sum(c['fault'] for c in t), int(d < 3))
        for d in (1, 2, 3):
            p = [bundle('setpwd x0')]+[NOP]*(d-1)+[bundle('setpwd x0')]
            t, _ = self.contrastar(p)
            self.assertEqual(sum(c['fault'] for c in t), int(d == 3))
        t, _ = self.contrastar([bundle('setpwd x0')]*3)
        self.assertFalse(any(c['fault'] for c in t))

    def test_07_vcr_direcciones_pwd_set_y_revocacion(self):
        for pwd, addr in itertools.product((0, 7, -1, -1024), (0, 1, 65532, 65535)):
            datos = bytearray(65536)
            struct.pack_into('<I', datos, addr & 0xfffc, pwd & 0xffffffff)
            p = self.provision(pwd, addr)+[bundle(f'vcr {addr}'), NOP, NOP]
            t, _ = self.contrastar(p, datos)
            self.assertEqual(t[-1]['estado'], 1)
        t, _ = self.contrastar([bundle('vcr 0'), NOP, NOP])
        self.assertEqual(t[-1]['estado'], 2)
        datos = bytearray(65536); datos[4] = 1
        p = self.provision()+[bundle('vcr 4'), NOP, NOP, bundle('vcr 0'), NOP, NOP]
        t, _ = self.contrastar(p, datos)
        self.assertIn(0, [c['estado'] for c in t[10:]])
        self.assertEqual(t[-1]['estado'], 1)

    def test_08_camcon_y_prioridad_memoria_tabla_c(self):
        # Ambos puertos deben leer preflanco. B reemplaza la palabra completa
        # incluso si A escribió un byte. Se prueba alias LSU alto y extremos.
        for shift, value in itertools.product((0, 1, 7, 31), (0, 0xffffffff, 0xaaaaaaaa, 0x80000001)):
            for lsu, addr in [('guardap x0,x0,17', 19), ('guardab x0,x0,18', 17),
                              ('cargai x6,x0,16', 16), ('guardap x0,x0,20', 16),
                              ('guardab x0,x0,-1', 65535)]:
                datos = bytearray(65536); struct.pack_into('<I', datos, addr & 0xfffc, value)
                p = self.provision()+[bundle(f'camcon {addr},{shift}', lsu=lsu), bundle('vcr 0')]
                t, m = self.contrastar(p, datos, tabla='C-puertos' if shift == 7 and value == 0x80000001 and addr == 17 else None)
                self.assertEqual(m.get(addr & 0xfffc, 0), rol(value, shift))
                ex = next(c for c in t if c['b_we'])
                self.assertEqual(ex['b_rd'], value)
                if (ex['a_addr'] & 0xfffc) == (addr & 0xfffc):
                    self.assertEqual(ex['a_rd'], value)
                if lsu.startswith('cargai'):
                    self.assertEqual(t[-1]['regs'][6], value)
        # Alias físico por bits altos de LSU: -1 selecciona FFFC como B=FFFF.
        datos = bytearray(65536); datos[65532:]=bytes([1, 2, 3, 4])
        self.contrastar(self.provision()+[bundle('camcon 65535,7', lsu='guardap x0,x0,-1')], datos)

    def test_09_camcon_vcr_pwd_actualizada(self):
        for shift in (0, 1, 7, 31):
            datos = bytearray(65536); datos[0] = 7
            p = self.provision(7)+[bundle(f'camcon 0,{shift}'), bundle('vcr 0'), NOP, NOP]
            t, m = self.contrastar(p, datos)
            self.assertEqual((t[-1]['estado'], m[0]), (1, rol(7, shift)))

    def test_10_excepcion_bundle_completo_y_manejador_tabla_b(self):
        for op in ('fsl x4,x2,0,0', 'fsli x4,x2,0,0', 'ell 0,0,x0,x0', 'camcon 0,7'):
            p = [bundle(alu='sumai x10,x0,42'), bundle(op, alu='sumai x5,x0,99', lsu='guardap x0,x0,0', bru='sye x31,48'),
                 bundle('fsl x6,x2,0,0'), bundle('setpwd x0')]
            p += [NOP]*(1023-len(p))+[bundle(alu='sumai x9,x0,9')]
            datos = bytearray(65536); datos[0] = 7
            t, m = self.contrastar(p, datos, ciclos=8, tabla='B-excepcion' if op.startswith('fsl ') else None)
            self.assertEqual((t[3]['pc'], t[3]['trap_pc'], t[3]['traps']), (0x3ff0, 16, 1))
            self.assertEqual(t[-1]['regs'][4:7], [0, 0, 0])
            self.assertEqual([t[-1]['regs'][i] for i in (9, 10, 31)], [9, 42, 0])
            self.assertEqual((m[0], t[-1]['traps']), (7, 1))
        # Manejador que también falla: el contador y el PC se actualizan otra
        # vez, sin crear retorno implícito ni detener el simulador.
        p[1023] = bundle('fsl x4,x2,0,0')
        t, _ = self.contrastar(p, datos, ciclos=12)
        self.assertGreater(t[-1]['traps'], 1)
        self.assertEqual(t[-1]['trap_pc'], 0x3ff0)

    def test_11_flush_descarta_crypto_y_privilegios(self):
        for joven in ('ell 0,0,x0,x0', 'camcon 0,1', 'fsl x4,x2,0,0', 'setpwd x0'):
            p = [bundle(bru='sye x31,48'), bundle(joven), bundle(joven), NOP]
            t, _ = self.contrastar(p)
            self.assertFalse(any(c['fault'] for c in t))
            self.assertEqual(t[-1]['sonda_boveda'], [0]*18)

    def test_12_cinco_puertos_y_prioridad(self):
        for a, l, b, rd in ((4, 4, 4, 4), (5, 5, 5, 4), (0, 0, 0, 30), (6, 8, 10, 28)):
            p = self.provision()
            n = len(p)
            p += [bundle(f'fsl x{rd},x2,0,0', alu=f'sumai x{a},x0,7', lsu=f'cargai x{l},x0,4', bru=f'sye x{b},48'), NOP, NOP, NOP]
            datos = bytearray(65536); datos[4] = 99
            t, _ = self.contrastar(p, datos)
            wb = t[n+3]
            self.assertEqual((wb['w3'], wb['w4']), (1, 1))
            esperado = [0]*32
            for dest, v in ((a, 7), (l, 99), (b, 16*n+16), (rd, 0), (rd+1, 0)):
                if dest:
                    esperado[dest] = v
            self.assertEqual(t[-1]['regs'], esperado)
            self.assertEqual(wb['conflicto'], int(a in (4, 5)))

    def test_13_dependencias_registros_n_1_2_3(self):
        # Todos los consumidores leen ID anterior a WB, sin forwarding. El
        # productor cripto da (7,5 XOR F(7,0)); se verifica cada unidad destino.
        for consumidor, d in itertools.product(('alu', 'lsu', 'bru', 'crypto'), (1, 2, 3)):
            p, datos = self.cargar(key=(0,)*4, l=5, r=7)
            n = len(p)
            p += [bundle('fsl x6,x4,0,0')]+[NOP]*(d-1)
            cons = {'alu': bundle(alu='suma x20,x6,x0'), 'lsu': bundle(lsu='guardap x0,x6,64'),
                    'bru': bundle(bru='igualno x6,x0,48'), 'crypto': bundle('fsl x8,x6,0,0')}[consumidor]
            p += [cons, NOP, NOP, NOP]
            t, _ = self.contrastar(p, datos)
            self.assertEqual(t[n+d+1]['id_'+consumidor+'_a' if consumidor != 'lsu' else 'id_lsu_b'], 7 if d == 3 else 0)
        for productor, d in itertools.product(('alu', 'lsu'), (1, 2, 3)):
            p = self.provision()
            n = len(p)
            p += [bundle(alu='sumai x4,x0,7') if productor == 'alu' else bundle(lsu='cargai x4,x0,4')]
            p += [NOP]*(d-1)+[bundle('fsl x6,x4,0,0')]
            datos = bytearray(65536); datos[4] = 7
            t, _ = self.contrastar(p, datos)
            self.assertEqual(t[n+d+1]['id_crypto_a'], 7 if d == 3 else 0)
        # Dentro del mismo bundle no se reenvía el resultado ALU a CRIPTO.
        t, _ = self.contrastar(self.provision()+[bundle('fsl x6,x4,0,0', alu='sumai x4,x0,7')])
        self.assertEqual(t[-1]['regs'][6:8], [0, 0])

    def test_14_reset_fases_efectos_y_memoria(self):
        p, datos = self.cargar()
        p += [bundle('camcon 16,7', lsu='guardab x0,x0,17', bru='sye x31,0'), bundle('vcr 0'), bundle('fsl x4,x4,0,0')]
        # Recorre todos los flancos: antes/después de provisionar, autenticar,
        # cargar llaves y con camcon/store/BRU en EX. La traza C/RTL comprueba
        # también el primer ciclo posterior al reset, sin observar solo finales.
        for instante in range(1, len(p)+3):
            t, _ = self.contrastar(p, datos, ciclos=instante+4, reset=instante)
            self.assertEqual(t[instante]['estado_pre'], 2)
            self.assertEqual(t[instante]['sonda_boveda'], [0]*18)
            self.assertEqual(t[instante]['traps'], 0)
        # Casos separados para resultado Feistel, AUTH y excepción pendientes.
        for q in ([bundle('fsl x4,x2,0,0')], [bundle('setpwd x0'), bundle('vcr 0')], self.provision()+[bundle('fsl x4,x2,0,0')]):
            for instante in (2, 3, len(q)+2):
                self.contrastar(q, ciclos=instante+3, reset=instante)

    def test_15_mem_balb_y_programa_archivo_real(self):
        # Se usa el programa de cifrado/descifrado real del Proyecto I, con sus
        # bucles y LSU. El mismo MEM se ejecuta con la memoria de archivo y la
        # llave/contraseña preparadas como tb_archivo.sv.
        self.exigir_rtl()
        datos = bytearray(65536)
        pwd = 0x5ec2e7a1
        for addr, v in [(0x100, pwd), (0x104, pwd), (0x108, 0x210)]+[(0x110+4*i, k) for i, k in enumerate(KEY)]:
            struct.pack_into('<I', datos, addr, v)
        original = b'HOLA2026abcdefgh'
        datos[0x200:0x210] = original
        for nombre in ('cifrar_archivo', 'descifrar_archivo'):
            fuente = (self.arch/f'examples/{nombre}.asm').read_text()
            ensamblado = self.asm.assemble_source(fuente)
            # Se conservan palabras ensambladas por la herramienta existente;
            # las filas originales se recuperan de su campo text, sin recodificar.
            p = [tuple(i.text for i in b.instructions) for b in ensamblado.bundles]
            # target es metadato del ensamblador existente. Se conserva el
            # programa y se expresa cada etiqueta ya resuelta como offset;
            # no se extraen campos binarios ni se escribe otro ensamblador.
            for n, b in enumerate(ensamblado.bundles):
                ins = b.instructions[2]
                if ins.target is not None:
                    br = ins.text.rsplit(',', 1)[0]+','+str(ins.target-b.pc)
                    p[n] = (*p[n][:2], br, p[n][3])
            t, m = self.contrastar(p, datos, ciclos=300, balb=True)
            salida = b''.join(m.get(i, 0).to_bytes(4, 'little') for i in range(0x200, 0x210, 4))
            if nombre == 'cifrar_archivo':
                esperado = b''.join(struct.pack('<2I', *encrypt(*struct.unpack('<2I', original[i:i+8]), KEY)) for i in (0, 8))
                self.assertEqual(salida, esperado)
                datos[0x200:0x210] = salida
            else:
                self.assertEqual(salida, original)
            self.assertFalse(any(c['fault'] for c in t))
        # Imagen con handler real también se carga por el escritor BALB.
        p = [bundle('fsl x4,x2,0,0')]+[NOP]*1022+[bundle(alu='sumai x9,x0,9')]
        self.contrastar(p, ciclos=8, balb=True)

    def test_16_aleatorio_cuatro_slots(self):
        rng = random.Random(430111086)
        p, datos = self.cargar()
        for _ in range(120):
            op = rng.choice(['fsl', 'fsli', 'ell', 'vcr', 'camcon'])
            c = {'fsl': f'fsl x{rng.randrange(1,16)*2},x{rng.randrange(1,16)*2},{rng.randrange(4)},{rng.randrange(4)}',
                 'fsli': f'fsli x{rng.randrange(1,16)*2},x{rng.randrange(1,16)*2},{rng.randrange(4)},{rng.randrange(4)}',
                 'ell': f'ell {rng.randrange(4)},{rng.choice([0,2])},x{rng.randrange(16)*2},x{rng.randrange(16)*2}',
                 'vcr': f'vcr {rng.randrange(65536)}', 'camcon': f'camcon {rng.randrange(65536)},{rng.randrange(32)}'}[op]
            p.append(bundle(c, alu=f'sumai x{rng.randrange(32)},x{rng.randrange(32)},{rng.randrange(-1024,1024)}',
                lsu=f'{rng.choice(["guardap","guardab","cargai","cargabai"])} x{rng.randrange(32)},x{rng.randrange(32)},{rng.randrange(-1024,1024)}',
                bru=f'igualno x{rng.randrange(32)},x{rng.randrange(32)},48'))
        self.contrastar(p, datos, ciclos=250)

    def test_17_lecturas_preflanco_lsu_vcr(self):
        # Store A y lectura B de la misma palabra: vcr autentica con el valor
        # anterior, aunque A publique otro valor en ese flanco. La siguiente
        # vcr observa la escritura y revoca AUTH, sin forwarding entre puertos.
        datos = bytearray(65536)
        datos[0] = 7
        p = self.provision(7)+[bundle('vcr 1', lsu='guardap x0,x0,0'),
                              bundle('vcr 0'), NOP, NOP]
        t, m = self.contrastar(p, datos)
        ex = next(c for c in t if c['a_we'] and c['b_valid'])
        self.assertEqual((ex['a_rd'], ex['b_rd'], ex['b_post']), (7, 7, 0))
        self.assertEqual((t[-1]['estado'], m.get(0, 0)), (0, 0))
        # Ambas lecturas simultáneas, sobre la misma palabra y sobre distintas.
        for addr in (0, 4):
            p = self.provision(7)+[bundle('vcr 0', lsu=f'cargai x6,x0,{addr}')]
            t, _ = self.contrastar(p, datos)
            self.assertEqual(t[-1]['regs'][6], 7 if addr == 0 else 0)


if __name__ == '__main__':
    unittest.main()
