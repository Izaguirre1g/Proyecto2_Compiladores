"""Verifica los campos ISA de S2 con fixtures, S1 y los decodificadores RTL reales."""
from pathlib import Path
import json
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

# Cada fila indica bundle del fixture original, slot, mnemónico y operandos
# esperados. Los valores se leen del ensamblador del Proyecto I; no se deducen
# llamando al decodificador que está bajo prueba. Se comprueba la salida completa.
CASOS = [
    (0, 0, "suma", dict(rd=5, rs1=6, rs2=7)),
    (1, 0, "resta", dict(rd=8, rs1=9, rs2=10)),
    (2, 0, "cader", dict(rd=11, rs1=12, rs2=13)),
    (3, 0, "cizq", dict(rd=14, rs1=15, rs2=16)),
    (4, 0, "and", dict(rd=17, rs1=18, rs2=19)),
    (5, 0, "or", dict(rd=20, rs1=21, rs2=22)),
    (6, 0, "xor", dict(rd=23, rs1=24, rs2=25)),
    (7, 0, "cder", dict(rd=26, rs1=27, rs2=28)),
    (8, 0, "mrq", dict(rd=29, rs1=30, rs2=31)),
    (9, 0, "myq", dict(rd=4, rs1=6, rs2=7)),
    (10, 0, "sumai", dict(rd=5, rs1=6, inmediato=-1024)),
    (11, 0, "restai", dict(rd=8, rs1=9, inmediato=1023)),
    (12, 0, "cizqi", dict(rd=10, rs1=6, inmediato=-1, corrimiento=31)),
    (13, 0, "cderi", dict(rd=11, rs1=6, inmediato=31, corrimiento=31)),
    (14, 0, "caderi", dict(rd=12, rs1=6, inmediato=3, corrimiento=3)),
    (15, 0, "xori", dict(rd=13, rs1=6, inmediato=-1)),
    (16, 0, "andi", dict(rd=14, rs1=6, inmediato=255)),
    (17, 0, "ori", dict(rd=15, rs1=6, inmediato=127)),
    (20, 1, "guardap", dict(rs1=2, rs2=16, inmediato=-4)),
    (21, 1, "guardab", dict(rs1=3, rs2=5, inmediato=1023)),
    (22, 1, "cargai", dict(rd=17, rs1=2, inmediato=8)),
    (23, 1, "cargabai", dict(rd=18, rs1=3, inmediato=-1024)),
    (24, 2, "igualsi", dict(rs1=16, rs2=19, inmediato=-384, salto=1)),
    (25, 2, "igualno", dict(rs1=16, rs2=19, inmediato=240, salto=1)),
    (26, 2, "menora", dict(rs1=20, rs2=21, inmediato=-32, salto=1)),
    (27, 2, "mayoroigual", dict(rs1=22, rs2=23, inmediato=16, salto=1)),
    (28, 2, "sye", dict(rd=0, inmediato=192, salto=2)),
    (29, 2, "syer", dict(rd=1, rs1=24, inmediato=0, salto=3)),
    (31, 3, "fsl", dict(rd=6, rd2=7, rs1=2, rs2=3, lk=0, rk=1, auth=1)),
    (32, 3, "fsli", dict(rd=8, rd2=9, rs1=4, rs2=5, lk=3, rk=2, auth=1)),
    (33, 3, "ell", dict(rs1=4, rs2=10, lk=2, off_boveda=0, auth=1)),
    (34, 3, "vcr", dict(direccion=8192, tiene_direccion=1)),
    (35, 3, "camcon", dict(direccion=16, tiene_direccion=1, inmediato=7, auth=1)),
    (36, 3, "setpwd", dict(rs1=25, init=1)),
]


class DecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporal = tempfile.TemporaryDirectory(prefix="pruebas-decode-")
        cls.addClassCleanup(cls.temporal.cleanup)
        cls.carpeta = Path(cls.temporal.name)
        cls.sonda = cls.carpeta / "test_sim_decode"
        cc = shlex.split(os.environ.get("CC", "cc"))
        flags = ["-Iinclude", "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O2"]
        # La compilación aislada no enlaza S1, parser ni codificador. Solo la
        # sonda agrega S1 para probar la carga real de sus imágenes, sin cambiarlo.
        subprocess.run(cc + flags + ["-c", "src/sim_decode.c", "-o",
                       str(cls.carpeta / "sim_decode.o")], cwd=ROOT, check=True)
        subprocess.run(cc + flags + ["tests/test_sim_decode.c", "src/sim_decode.c",
                       "src/sim_cargador.c", "-o", str(cls.sonda)], cwd=ROOT, check=True)
        subprocess.run(["make", "balc"], cwd=ROOT, check=True, capture_output=True)
        cls.fixture = ROOT / "tests/fixtures/isa_todas.mem"
        cls.words = [[(int(line, 16) >> (32 * s)) & 0xffffffff for s in range(4)]
                     for line in cls.fixture.read_text().splitlines()]

    def ejecutar(self, args):
        # Cada fallo conserva stdout/stderr para distinguir errores de prueba,
        # construcción o decodificación. No se ignoran códigos de salida.
        p = subprocess.run(list(map(str, args)), cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        return p.stdout

    def palabra(self, word, slot):
        return json.loads(self.ejecutar([self.sonda, "palabra", slot, f"{word:08x}"]))

    def esperar(self, word, slot, nombre, formato, **campos):
        # Las marcas de ausencia forman parte de lo esperado: un load no puede
        # inventar rs2 y un store no puede aparecer como escritura de registro.
        e = dict(word=word, slot=slot, estado=0, tipo=word >> 25,
                 id=(word >> 21) & 15, operacion=nombre, familia=slot + 1,
                 formato=formato, rd=32, rd2=32, rs1=32, rs2=32, bruto=0,
                 inmediato=0, ancho=0, signo=0, corrimiento=255, salto=0,
                 lk=255, rk=255, off_boveda=255, direccion=0, tiene_direccion=0,
                 auth=0, init=0)
        # Los formatos IMM/LSU/COND/SYER son signed de 11 bits; SYE usa 16;
        # camcon es la única rotación codificada con cinco bits sin signo.
        if formato in (2, 3, 4, 6): e.update(ancho=11, signo=1)
        elif formato == 5: e.update(ancho=16, signo=1)
        elif formato == 10: e.update(ancho=5)
        e.update(campos)
        if e["ancho"]:
            e["bruto"] = e["inmediato"] & ((1 << e["ancho"]) - 1)
        return e

    def comparar(self, actual, esperado):
        # Estos dos campos pertenecen al protocolo de la sonda, no a la
        # instrucción. Se comprueban aparte cuando se prueba el bundle completo.
        actual = dict(actual)
        actual.pop("bundle")
        actual.pop("bundle_estado")
        self.assertEqual(actual, esperado)

    @staticmethod
    def formato_del_caso(b, s):
        if b < 10: return 1
        if b < 18: return 2
        if s == 1: return 3
        if b < 28: return 4
        return {28: 5, 29: 6, 31: 7, 32: 7, 33: 8, 34: 9, 35: 10, 36: 11}[b]

    def test_las_34_operaciones_y_todos_sus_campos(self):
        self.assertEqual(len(CASOS), 34)
        self.assertEqual(len({c[2] for c in CASOS}), 34)
        for b, s, nombre, campos in CASOS:
            with self.subTest(operacion=nombre):
                w = self.words[b][s]
                self.comparar(self.palabra(w, s), self.esperar(
                    w, s, nombre, self.formato_del_caso(b, s), **campos))

    def test_barrido_de_opcodes_y_argumentos_c(self):
        # El ejecutable C recorre los 8192 cruces tipo/ID/slot, incluyendo los
        # reservados, y prueba salida NULL, slot fuera de rango y reuso de salida.
        self.ejecutar([self.sonda])

    def test_nop_en_cada_slot(self):
        for s in range(4):
            with self.subTest(slot=s):
                e = self.esperar(0, s, "nop", 0, estado=1, familia=5)
                self.comparar(self.palabra(0, s), e)

    def test_registros_e_inmediatos_alu(self):
        # REG e IMM tienen destinos en posiciones distintas. x0 debe seguir
        # presente como operando y x31 no debe confundirse con la marca 32.
        for rd, rs1, rs2 in [(0, 31, 0), (31, 0, 31)]:
            w = (0x6a << 25) | (8 << 21) | (rd << 16) | (rs1 << 11) | (rs2 << 6)
            self.comparar(self.palabra(w, 0), self.esperar(w, 0, "suma", 1,
                          rd=rd, rs1=rs1, rs2=rs2))
        valores = [(0, 0), (1, 1), (1023, 1023), (1024, -1024), (2047, -1)]
        nombres = ["sumai", "restai", "cizqi", "cderi", "caderi", "xori", "andi", "ori"]
        for op, nombre in enumerate(nombres):
            for bruto, extendido in valores + [(31, 31), (32, 32)]:
                with self.subTest(operacion=nombre, bruto=bruto):
                    w = (0x40 << 25) | (op << 21) | (31 << 16) | bruto
                    campos = dict(rd=0, rs1=31, inmediato=extendido)
                    if op in (2, 3, 4): campos["corrimiento"] = bruto % 32
                    self.comparar(self.palabra(w, 0), self.esperar(w, 0, nombre, 2, **campos))

    def test_lsu_y_offsets_de_saltos(self):
        for op, nombre in [(0, "guardap"), (1, "guardab"), (8, "cargai"), (9, "cargabai")]:
            for off in (-1024, -1, 0, 1023):
                w = (0x49 << 25) | (op << 21) | (31 << 11) | (off & 2047)
                campos = dict(rs1=0, inmediato=off)
                campos["rd" if op >= 8 else "rs2"] = 31
                self.comparar(self.palabra(w, 1), self.esperar(w, 1, nombre, 3, **campos))
        # Se usan máximos alineados para saltos directos: los máximos positivos
        # del campo completo no son múltiplos de bundle y se prueban como rechazo.
        for op, nombre in [(1, "igualsi"), (2, "igualno"), (4, "menora"), (8, "mayoroigual")]:
            for off in (-1024, -16, 0, 16, 1008):
                w = (0x41 << 25) | (op << 21) | (31 << 11) | (off & 2047)
                self.comparar(self.palabra(w, 2), self.esperar(w, 2, nombre, 4,
                              rs1=0, rs2=31, inmediato=off, salto=1))
        for off in (-32768, -16, 0, 16, 32752):
            w = (0x4b << 25) | (31 << 16) | (off & 65535)
            self.comparar(self.palabra(w, 2), self.esperar(w, 2, "sye", 5,
                          rd=31, inmediato=off, salto=2))
        # syer puede tener offset no alineado: el destino depende del registro
        # y el enmascaramiento final pertenece a ejecución, no a S2.
        for off in (-1024, -1, 0, 1, 1023):
            w = (0x4b << 25) | (1 << 21) | (31 << 11) | (off & 2047)
            self.comparar(self.palabra(w, 2), self.esperar(w, 2, "syer", 6,
                          rd=0, rs1=31, inmediato=off, salto=3))

    def test_extremos_y_formatos_cripto(self):
        for op, nombre in [(0, "fsl"), (1, "fsli")]:
            for rd, rs, lk, rk in [(2, 30, 0, 3), (30, 2, 3, 0), (30, 30, 3, 3)]:
                w = (2 << 25) | (op << 21) | (lk << 19) | (rk << 17) | (rd << 12) | (rs << 7)
                self.comparar(self.palabra(w, 3), self.esperar(w, 3, nombre, 7,
                    rd=rd, rd2=rd+1, rs1=rs, rs2=rs+1, lk=lk, rk=rk, auth=1))
        for off in (0, 2):
            w = (2 << 25) | (2 << 21) | (3 << 19) | (off << 17) | (30 << 7)
            self.comparar(self.palabra(w, 3), self.esperar(w, 3, "ell", 8,
                          rs1=0, rs2=30, lk=3, off_boveda=off, auth=1))
        for addr in (0, 65535):
            w = (2 << 25) | (3 << 21) | (addr << 5)
            self.comparar(self.palabra(w, 3), self.esperar(w, 3, "vcr", 9,
                          direccion=addr, tiene_direccion=1))
            for rot in (0, 31):
                w = (2 << 25) | (4 << 21) | (addr << 5) | rot
                self.comparar(self.palabra(w, 3), self.esperar(w, 3, "camcon", 10,
                              direccion=addr, tiene_direccion=1, inmediato=rot, auth=1))
        for rs in (0, 31):
            w = (2 << 25) | (5 << 21) | rs
            self.comparar(self.palabra(w, 3), self.esperar(w, 3, "setpwd", 11, rs1=rs, init=1))

    def test_restricciones_no_equivalen_a_opcodes_reservados(self):
        invalidas = [(2, (0x41 << 25) | (1 << 21) | 1023),
                     (2, (0x4b << 25) | 32767)]
        # Pares Feistel inválidos, offset ell impar o una fuente ell impar.
        for rd, rs in [(0, 2), (1, 2), (31, 2), (2, 0), (2, 31)]:
            invalidas.append((3, (2 << 25) | (rd << 12) | (rs << 7)))
        for off, rs in [(1, 2), (3, 2), (0, 1)]:
            invalidas.append((3, (2 << 25) | (2 << 21) | (off << 17) | (rs << 12)))
        for s, w in invalidas:
            with self.subTest(word=f"{w:08x}"):
                self.comparar(self.palabra(w, s), self.esperar(w, s, "invalida", 0,
                              estado=4, familia=0))
        # Tipo NOP con payload no cero no es el NOP canónico de palabra cero.
        self.comparar(self.palabra(1, 0), self.esperar(1, 0, "invalida", 0, estado=2, familia=0))

    def test_slot_incorrecto_para_las_34_operaciones(self):
        for b, real, nombre, _ in CASOS:
            for otro in range(4):
                if otro == real: continue
                with self.subTest(operacion=nombre, slot=otro):
                    w = self.words[b][real]
                    self.comparar(self.palabra(w, otro), self.esperar(w, otro, "invalida", 0,
                                  estado=3, familia=0))

    def test_bits_libres_ignorados_por_rtl(self):
        # Estos bits no definen operandos y RTL los ignora. No deben confundirse
        # con IDs reservados: cambiarlos conserva la operación y sus campos.
        for b, s, mascara in [(0, 0, 63), (31, 3, 127), (33, 3, 127),
                              (34, 3, 31), (36, 3, 0x1fffe0)]:
            w = self.words[b][s]
            a = self.palabra(w, s)
            nuevo = w | mascara
            a["word"] = nuevo
            self.assertEqual(self.palabra(nuevo, s), a)

    def test_bundles_completos_mixtos_y_con_error(self):
        palabras = [self.words[1][0], self.words[22][1], self.words[24][2], self.words[31][3]]
        for vector, esperado in [(palabras, 0), ([palabras[0], 0, palabras[2], 0], 0),
                                 ([0, 0, 0, 0], 1), ([1, palabras[1], 0, palabras[0]], 2)]:
            registros = [json.loads(line) for line in self.ejecutar(
                [self.sonda, "bundle", *[f"{w:08x}" for w in vector]]).splitlines()]
            self.assertEqual([r["slot"] for r in registros], list(range(4)))
            self.assertTrue(all(r["bundle_estado"] == esperado for r in registros))
            for s, w in enumerate(vector):
                self.comparar(registros[s], {k: v for k, v in self.palabra(w, s).items()
                                            if k not in ("bundle", "bundle_estado")})

    def test_s1_carga_mem_y_balb_y_s2_decodifica(self):
        destino = self.carpeta / "fixture.balb"
        self.ejecutar([ROOT / "balc", "-x", ROOT / "tests/fixtures/isa_todas.asm", "-o", destino])
        mem = [json.loads(l) for l in self.ejecutar([self.sonda, "imagen", "mem", self.fixture]).splitlines()]
        balb = [json.loads(l) for l in self.ejecutar([self.sonda, "imagen", "balb", destino]).splitlines()]
        self.assertEqual(mem, balb)
        self.assertEqual(len(mem), 43 * 4)
        for b, s, nombre, campos in CASOS:
            self.comparar(mem[b * 4 + s], self.esperar(self.words[b][s], s, nombre,
                          self.formato_del_caso(b, s), **campos))

    def test_fixture_con_ensamblador_actual_de_arquitectura(self):
        arch = Path(os.environ.get("ARQUITECTURA_ROOT", ROOT.parent / "Proyecto2_Arquitectura1"))
        if not (arch / "tools/assembler.py").is_file():
            self.skipTest("Falta assembler.py; indicar ARQUITECTURA_ROOT")
        mem, binario = self.carpeta / "oraculo.mem", self.carpeta / "oraculo.bin"
        # Regenerar las referencias con el ensamblador actual evita depender
        # solo de la procedencia histórica del fixture, sin sobrescribirlo.
        self.ejecutar(["python3", arch / "tools/assembler.py", "--input",
                       ROOT / "tests/fixtures/isa_todas.asm", "--output", mem,
                       "--binary", binario])
        self.assertEqual(mem.read_bytes(), self.fixture.read_bytes())
        self.assertEqual(binario.read_bytes(), (ROOT / "tests/fixtures/isa_todas.bin").read_bytes())

    def test_contraste_con_decodificadores_rtl(self):
        arch = Path(os.environ.get("ARQUITECTURA_ROOT", ROOT.parent / "Proyecto2_Arquitectura1"))
        if not (arch / "rtl/isa_defs.sv").is_file():
            self.skipTest("Falta Arquitectura; indicar ARQUITECTURA_ROOT para contrastar RTL")
        if not shutil.which("iverilog") or not shutil.which("vvp"):
            self.skipTest("El contraste RTL requiere iverilog y vvp")
        # Cada palabra del fixture se prueba con los cuatro estados de permisos.
        # Los casos extra proceden literalmente de tb_decoder_crypto.sv y cubren
        # extremos de pares, dirección y rotación; no se ejecuta la unidad cripto.
        pares = [(s, self.words[b][s]) for b, s, _, _ in CASOS]
        pares += [(3, w) for w in [0x04004200, 0x040e6100, 0x043dee00, 0x04408500,
                                   0x045d4100, 0x047fffe0, 0x049fffff, 0x04a0001f]]
        reservados = [(0, (0x6a << 25) | (7 << 21) | 0x42200),
                      (0, (0x40 << 25) | (8 << 21) | 0x42200),
                      (2, (0x41 << 25) | 0x42200), (1, (0x49 << 25) | (2 << 21)),
                      (2, (0x4b << 25) | (7 << 21)), (3, (2 << 25) | (6 << 21))]
        # Los TB BRU originales incluyen -4 y +5 para probar extracción pura.
        # Se muestran como CAMPOS fuera del contrato alineado del ensamblador,
        # aunque RTL extrae esos bits. Lo mismo ocurre con pares/offsets cripto.
        restringidos = [(2, (0x41 << 25) | (4 << 21) | (7 << 16) | (8 << 11) | 2044),
                        (2, (0x41 << 25) | (8 << 21) | (9 << 16) | (10 << 11) | 5),
                        (3, 0x04001000),
                        (3, (2 << 25) | (2 << 21) | (1 << 17) | (1 << 12) | (2 << 7))]
        nops = [(s, 0) for s in range(4)]
        vectores = self.carpeta / "rtl-vectores.txt"
        filas = [(s, w, a, i) for s, w in pares + reservados + restringidos + nops
                 for a in (0, 1) for i in (0, 1)]
        vectores.write_text("".join(f"{s} {w:08x} {a} {i}\n" for s, w, a, i in filas))
        exe = self.carpeta / "rtl-ref.vvp"
        self.ejecutar(["iverilog", "-g2012", "-I", arch, "-I", arch / "rtl",
                       "-s", "tb_sim_decode_ref", "-o", exe, ROOT / "tests/rtl/tb_sim_decode_ref.sv",
                       *[arch / "rtl" / f"decoder_{unidad}.sv" for unidad in ("alu", "lsu", "bru", "crypto")]])
        salida = self.ejecutar(["vvp", exe, f"+VECTORES={vectores}"])
        nombres = ("aop ard ars1 ars2 aimm ause lop lrd lrs1 lrs2 limm lwe lvalid "
                   "bop brd brs1 brs2 bimm jimm isbranch isjump bwe "
                   "cop lk rk crd crsa crsb addr cimm cwe cvalid fault").split()
        lineas = [l.split() for l in salida.splitlines() if l.startswith("REF ")]
        self.assertEqual(len(lineas), len(filas))
        alu = dict(suma=0, resta=1, cader=4, cizq=2, xor=5, cder=3,
                   mrq=8, myq=9, sumai=0, restai=1, cizqi=2, cderi=3, caderi=4, xori=5, andi=6, ori=7)
        alu.update({"and": 6, "or": 7})
        for fila, esperado in zip(lineas, filas):
            s, w, a, i = esperado
            self.assertEqual((int(fila[1]), int(fila[2], 16), int(fila[3]), int(fila[4])), esperado)
            self.assertEqual(len(fila[5:]), len(nombres))
            r = dict(zip(nombres, map(int, fila[5:])))
            d = self.palabra(w, s)
            with self.subTest(slot=s, word=f"{w:08x}", auth=a, init=i):
                if (s, w) in nops:
                    self.assertEqual(d["estado"], 1)
                    self.assertEqual((r["ard"], r["ars1"], r["ars2"], r["ause"],
                                      r["lvalid"], r["isbranch"], r["isjump"], r["cvalid"]), (0,) * 8)
                    continue
                if (s, w) in restringidos:
                    self.assertEqual(d["estado"], 4)
                    if s == 2:
                        self.assertEqual((r["isbranch"], r["bimm"]), (1, w & 2047))
                    else: self.assertEqual(r["cvalid"], 1)
                    continue
                if (s, w) in reservados:
                    self.assertEqual(d["estado"], 2)
                    # Se evidencia la diferencia entre reconocimiento ISA y
                    # comportamiento físico; no se implementa fallback en S2.
                    if s == 0: self.assertEqual(r["aop"], 0)
                    elif s == 1: self.assertEqual(r["lvalid"], 0)
                    elif s == 2: self.assertEqual(r["isbranch"], int(d["tipo"] == 0x41))
                    else: self.assertEqual(r["cvalid"], 0)
                    continue
                self.assertEqual(d["estado"], 0)
                if s == 0:
                    self.assertEqual(r["aop"], alu[d["operacion"]])
                    self.assertEqual((r["ard"], r["ars1"], r["aimm"], r["ause"]),
                                     (d["rd"], d["rs1"], d["bruto"], int(d["ancho"] != 0)))
                    if d["rs2"] != 32: self.assertEqual(r["ars2"], d["rs2"])
                elif s == 1:
                    self.assertEqual((r["lop"], r["lrs1"], r["limm"], r["lwe"], r["lvalid"]),
                                     (d["id"], d["rs1"], d["bruto"], int(d["rd"] != 32), 1))
                    # RTL cablea rd y rs2 al mismo campo; S2 conserva solo el
                    # que utiliza load/store. Los índices se comparan por ese uso.
                    self.assertEqual(r["lrd"] if d["rd"] != 32 else r["lrs2"],
                                     d["rd"] if d["rd"] != 32 else d["rs2"])
                elif s == 2:
                    self.assertEqual((r["bop"], r["isbranch"], r["isjump"], r["bwe"]),
                                     (d["id"], int(d["salto"] == 1), int(d["salto"] != 1), int(d["salto"] != 1)))
                    self.assertEqual(r["jimm"] if d["ancho"] == 16 else r["bimm"], d["bruto"])
                    for campo, rtl in [("rd", "brd"), ("rs1", "brs1"), ("rs2", "brs2")]:
                        if d[campo] != 32: self.assertEqual(d[campo], r[rtl])
                else:
                    self.assertEqual((r["cop"], r["cvalid"], r["cwe"]), (d["id"], 1, int(d["rd"] != 32)))
                    for campo, rtl in [("rd", "crd"), ("rs1", "crsa"), ("rs2", "crsb")]:
                        if d[campo] != 32: self.assertEqual(d[campo], r[rtl])
                    for campo, rtl in [("lk", "lk"), ("rk", "rk"), ("off_boveda", "rk")]:
                        if d[campo] != 255: self.assertEqual(d[campo], r[rtl])
                    self.assertEqual((r["addr"], r["cimm"]), (d["direccion"], d["bruto"]))
                    self.assertEqual(r["fault"], int((d["auth"] and not a) or (d["init"] and not i)))
        print(f"Contraste directo: {len(filas)} vectores RTL; 34 operaciones ISA, "
              "extremos, reservados, restricciones y NOP con los cuatro estados AUTH/INIT.")


if __name__ == "__main__":
    unittest.main()
