"""Integración de los adaptadores: CLI/script reales y archivos temporales.
No sustituye las comprobaciones gráficas de menús, diálogos o navegador.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE = Path(os.environ.get("IDE_HERRAMIENTAS_PROBE", ROOT / "tests/test_ide_herramientas"))
SOURCE = "create_funk #declare_infinite_void# main() { valor * declare_int : 0007; }"

class HerramientasTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="IDE con espacios ")
        self.addCleanup(self.tmp.cleanup)
        self.dir = Path(self.tmp.name)
        self.src = self.dir / "sin guardar.bal"
        self.src.write_text(SOURCE)

    def run_probe(self, mode, source=None, output=None, root=ROOT):
        return subprocess.run([str(PROBE), mode, str(root), str(source or self.src),
                               str(output or self.dir / "resultado.json")], capture_output=True, text=True)

    def test_ast_buffer_y_actualizacion(self):
        for literal in ("0007", "0042"):
            self.src.write_text(SOURCE.replace("0007", literal))
            r = self.run_probe("ast")
            self.assertEqual(r.returncode, 0, r.stderr)
            tree = json.loads((self.dir / "resultado.json").read_text())
            self.assertEqual(tree["tipo"], "Programa")
            self.assertIn(literal, json.dumps(tree))
            if literal == "0042": self.assertNotIn("0007", json.dumps(tree))
            self.assertEqual(list(self.dir.glob("*.bin")), [])

    def test_ast_errores_sin_publicacion(self):
        for text in ("create_funk {", SOURCE.replace("0007", "@")):
            self.src.write_text(text)
            r = self.run_probe("ast")
            self.assertNotEqual(r.returncode, 0)
            self.assertTrue(r.stderr.strip())
            self.assertFalse((self.dir / "resultado.json").exists())

    def test_asm_igual_cli_y_contenedor(self):
        asm = self.dir / "entrada con espacios.asm"
        asm.write_text("nop\nnop\n")
        r = self.run_probe("asm", asm, self.dir / "adaptador.bin")
        self.assertEqual(r.returncode, 0, r.stderr)
        r = subprocess.run([str(ROOT / "balc"), "-x", str(asm)], capture_output=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        binary = (self.dir / "adaptador.bin").read_bytes()
        self.assertEqual(binary, asm.with_suffix(".bin").read_bytes())
        self.assertEqual(binary[:4], b"BALB")
        self.assertEqual(len(binary), 48 + 32)
        self.assertEqual((self.dir / "adaptador.bin.mem").read_bytes(), asm.with_suffix(".mem").read_bytes())

    def test_asm_invalido_no_sobrescribe(self):
        asm = self.dir / "malo.asm"
        asm.write_text("no_existe x1\n")
        sentinel = asm.with_suffix(".bin")
        sentinel.write_bytes(b"conservar")
        r = self.run_probe("asm", asm, self.dir / "nuevo.bin")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("linea", r.stderr)
        self.assertEqual(sentinel.read_bytes(), b"conservar")
        self.assertFalse((self.dir / "nuevo.bin").exists())

    def test_spilling_o2_real_y_fuente_vigente(self):
        for literal in ("0007", "0042"):
            text = SOURCE.replace("0007", literal)
            self.src.write_text(text)
            r = self.run_probe("spill")
            self.assertEqual(r.returncode, 0, r.stderr)
            d = json.loads((self.dir / "resultado.json").read_text())
            self.assertEqual(d["formato"], "bal-spilling")
            self.assertTrue(d["validado_contra_balc"])
            self.assertEqual(d["fuente"], text.splitlines())
            self.assertEqual(d["k"], 3)
            for key in ("bloques", "pasos", "temporales", "interferencias", "iteraciones_vida"):
                self.assertIn(key, d)
            self.assertIn("[O2]", d["resumen_balc"])

    def test_spilling_error_real(self):
        self.src.write_text('bring modulo_ausente./ aka modulo;\n' + SOURCE)
        r = self.run_probe("spill")
        self.assertNotEqual(r.returncode, 0)
        self.assertTrue(r.stderr.strip())
        self.assertFalse((self.dir / "resultado.json").exists())

    def test_proceso_ausente(self):
        r = self.run_probe("ast", root=self.dir)
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse((self.dir / "resultado.json").exists())

if __name__ == "__main__":
    unittest.main(verbosity=2)
