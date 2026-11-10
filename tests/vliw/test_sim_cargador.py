"""Prueba S1 con el compilador real y ejecutables aislados en directorios temporales."""
from pathlib import Path
import os
import resource
import shlex
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class CargadorTests(unittest.TestCase):
    # Prepara una sola vez el ejecutable C y el compilador que usarán los casos.
    # Los archivos propios de esta suite quedan fuera de los fixtures existentes.
    @classmethod
    def setUpClass(cls):
        cls.temporal = tempfile.TemporaryDirectory(prefix="pruebas-cargador-")
        # unittest ejecuta esta limpieza incluso si una prueba falla; el nombre
        # temporal permanece vivo durante todos los métodos de la clase.
        cls.addClassCleanup(cls.temporal.cleanup)
        cls.carpeta = Path(cls.temporal.name)
        cls.prueba = cls.carpeta / "test_sim_cargador"
        # Compilar únicamente lector y prueba demuestra que el módulo no
        # necesita enlazar parser, semántica, codificador ni futuro scheduler.
        cmd = shlex.split(os.environ.get("CC", "cc")) + [
            "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O2",
            "-Iinclude", "tests/vliw/test_sim_cargador.c", "src/Simulador_VLIW/sim_cargador.c",
            "-o", str(cls.prueba)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        # Se utiliza el target balc existente. check=True detiene la preparación
        # si la construcción falla, antes de atribuir ese error al cargador.
        subprocess.run(["make", "balc"], cwd=ROOT, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    # Ejecuta una orden desde la raíz y exige código 0. Captura ambos canales
    # para mostrar el motivo de un fallo del compilador o de la prueba C.
    def ejecutar(self, args):
        resultado = subprocess.run(list(map(str, args)), cwd=ROOT,
                                   capture_output=True, text=True)
        self.assertEqual(resultado.returncode, 0,
                         resultado.stdout + resultado.stderr)
        return resultado

    def test_validaciones_unitarias(self):
        # Sin rutas, el ejecutable C construye sus entradas temporales y verifica
        # contenido, errores de formato y recursos. Se espera que termine con 0.
        self.ejecutar([self.prueba])

    def test_recursos_con_limite_de_archivos(self):
        # Si un rechazo pierde descriptores, las 128 repeticiones fallan bajo
        # este límite. Solo se cambia el límite del proceso de prueba hijo.
        def limitar():
            _, maximo = resource.getrlimit(resource.RLIMIT_NOFILE)
            # Se reduce el límite disponible sin superar el máximo del sistema.
            # RLIM_INFINITY es un valor especial, no una cantidad de descriptores.
            limite = 32 if maximo == resource.RLIM_INFINITY else min(32, maximo)
            resource.setrlimit(resource.RLIMIT_NOFILE, (limite, maximo))
        # preexec_fn aplica el límite en el hijo antes de ejecutar la prueba C;
        # no restringe el proceso Python ni los demás métodos de esta suite.
        resultado = subprocess.run([str(self.prueba)], cwd=ROOT,
                                   capture_output=True, text=True, preexec_fn=limitar)
        self.assertEqual(resultado.returncode, 0,
                         resultado.stdout + resultado.stderr)

    def test_seis_programas_del_proyecto_i(self):
        # Cada fuente existente se compila al directorio temporal. subTest
        # identifica cuál programa falla; se esperan imágenes MEM/BALB iguales
        # en instrucciones, sin inferir todavía resultados de ejecución VLIW.
        for nombre in ["factorial", "ejercicio2_busqueda_matriz", "ejercicio3_puntajes",
                       "llamadas", "division_residuo_potencia", "multiplicacion"]:
            with self.subTest(programa=nombre):
                destino = self.carpeta / (nombre + ".bin")
                self.ejecutar([ROOT / "balc", "-o", destino,
                               ROOT / "examples" / (nombre + ".bal")])
                # El oráculo es la salida actual del compilador, no un formato
                # paralelo inventado por las pruebas del cargador.
                self.ejecutar([self.prueba, destino.with_suffix(".mem"), destino])

    def test_fixture_de_las_34_instrucciones(self):
        # -x empaqueta el fixture del ensamblador en BALB. La prueba C compara
        # su código con el MEM de referencia, que contiene las 34 instrucciones.
        # El lector solo conserva palabras: esto no prueba su decodificación.
        destino = self.carpeta / "isa.bin"
        fixture = ROOT / "tests/fixtures/isa_todas"
        self.ejecutar([ROOT / "balc", "-x", fixture.with_suffix(".asm"), "-o", destino])
        self.ejecutar([self.prueba, fixture.with_suffix(".mem"), destino])
        raw = destino.read_bytes()
        # Los primeros 48 bytes son encabezado. El resto debe ser el binario
        # crudo de referencia; flags=0 confirma que -x no agrega inicialización
        # de datos proveniente del análisis semántico.
        self.assertEqual(raw[48:], fixture.with_suffix(".bin").read_bytes())
        self.assertEqual(struct.unpack_from("<I", raw, 44)[0], 0)

    def test_constantes_globales_en_dmem(self):
        # Caso del mismo lenguaje usado por test_formato_binario: confirma
        # signos, datos no vacíos y flag de inicialización del arranque.
        fuente = self.carpeta / "globales.bal"
        fuente.write_text("""declare_const A * declare_int : 5;
declare_const B * declare_int : neumann 7;
create_funk #declare_infinite_void# main() { valor * declare_int : A; }
""", encoding="utf-8")
        destino = self.carpeta / "globales.bin"
        self.ejecutar([ROOT / "balc", "-o", destino, fuente])
        raw = destino.read_bytes()
        # Desde el byte 32 se leen cuatro campos little-endian de 32 bits:
        # offset de datos, longitud, base en DMEM y flags. No son instrucciones.
        offset, cantidad, base, flags = struct.unpack_from("<4I", raw, 32)
        self.assertEqual((cantidad, base, flags), (8, 0x4000, 1))
        # <2i representa dos enteros con signo en little-endian. Se exige el
        # payload exacto de 5 y -7 antes de comprobar que el cargador lo copia
        # en DMEM; flags=1 indica que el arranque también inicializa esos datos.
        self.assertEqual(raw[offset:], struct.pack("<2i", 5, -7))
        self.ejecutar([self.prueba, destino.with_suffix(".mem"), destino])


if __name__ == "__main__":
    unittest.main()
