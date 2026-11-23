"""Verifica S7 con invariantes externas, fallos de reserva y fixtures ejecutables.

La conversión de índices a texto/etiquetas existe solo en esta prueba. Se usa
el codificador real y el simulador S6 intacto; no implementa el empaquetador S8.
"""
from pathlib import Path
import sys
import json
import os
import random
import re
import shlex
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
# La dependencia compartida conserva su dueño y su API; solo resolvemos
# su ubicación plana o por módulos según el árbol que contiene esta prueba.
sys.path.insert(0, str(ROOT / 'tools/vliw'))
from rutas_vliw import fuente_compartida
MEM_R, MEM_W, BOVEDA, ESTADO, EXCEPCION, DESCONOCIDOS, BARRERA = (1 << i for i in range(7))


# Construye los metadatos de una operación sintética, incluidas máscaras, slot y efectos, para
# probar la calendarización.
def op(slot=0, lee=(), escribe=(), efectos=0, control=0, bloque=0, texto=None):
    return dict(slot=slot, lee=sum(1 << r for r in set(lee)), escribe=sum(1 << r for r in set(escribe)),
                efectos=efectos, control=control, bloque=bloque, texto=texto)


# Este puente temporal solo llama cb_ensamblar/cb_escribir_mem. La biblioteca
# S7 no enlaza el codificador ni adquiere capacidad para interpretar ensamblador.
CODIFICADOR = r'''
#include "codificacion_binaria.h"
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 2; }
    long n = ftell(f);
    if (n < 0 || n > 1048576 || fseek(f, 0, SEEK_SET)) { fclose(f); return 2; }
    char *texto = calloc((size_t)n + 1, 1);
    if (!texto) { fclose(f); return 2; }
    if (fread(texto, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(texto); return 2; }
    if (fclose(f)) { free(texto); return 2; }
    CbPrograma p = {0};
    int error = cb_ensamblar(texto, &p, stderr);
    free(texto);
    if (error) return 1;
    f = fopen(argv[2], "wb");
    if (!f) { cb_liberar(&p); return 2; }
    error = cb_escribir_mem(&p, f);
    if (fclose(f)) error = 1;
    cb_liberar(&p);
    return error;
}
'''


class SchedulerTests(unittest.TestCase):
    # Compila las bibliotecas y sondas reales una vez para la suite y prepara los recursos
    # compartidos en directorios de prueba.
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='pruebas-s7-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.comandos = []
        cls.planes = cls.ejecuciones = cls.ciclos = 0
        cls.unidades = cls.checks = 0
        cls.ejemplos = []
        # Los ejecutables de prueba se reconstruyen en el temporal con el compilador y flags
        # seleccionados. Las variantes de inyección se enlazan aparte: permiten provocar fallos
        # sin cambiar las bibliotecas bajo prueba.
        cls.cc = shlex.split(os.environ.get('CC', 'cc'))
        cls.flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        cls.bin = cls.dir/'scheduler'
        cls.compilar(['src/Calendarizacion_VLIW/vliw_scheduler.c', 'tests/vliw/test_vliw_scheduler.c'], cls.bin)
        cls.reservas = cls.dir/'reservas'
        cls.compilar(['src/Calendarizacion_VLIW/vliw_scheduler.c', 'tests/vliw/test_vliw_scheduler.c'], cls.reservas,
                     ['-DVLIW_PRUEBA_RESERVAS', '-Wl,--wrap=malloc', '-Wl,--wrap=calloc'])
        fuente = cls.dir/'codificar.c'
        fuente.write_text(CODIFICADOR)
        cls.codificador = cls.dir/'codificar'
        cls.compilar([fuente, fuente_compartida(ROOT, 'src/codificacion_binaria.c')], cls.codificador)
        cls.sim = cls.dir/'simulador'
        cls.compilar(['tests/vliw/test_sim_crypto.c']+[f'src/Simulador_VLIW/sim_{n}.c' for n in
                     ('crypto', 'pipeline', 'alu', 'lsu', 'bru', 'decode', 'cargador')], cls.sim)

    # Invoca el ejecutable del caso con sus argumentos y comprueba su retorno; devuelve la
    # salida para contrastar datos.
    @classmethod
    def ejecutar(cls, args, esperado=0):
        args = list(map(str, args))
        p = subprocess.run(args, cwd=ROOT, capture_output=True, text=True, timeout=60)
        cls.comandos.append(dict(comando=args, codigo=p.returncode))
        if p.returncode != esperado:
            raise AssertionError(f'{args}: código {p.returncode}, esperado {esperado}\n{p.stdout}\n{p.stderr}')
        return p.stdout

    # Reconstruye el componente necesario desde las fuentes de la copia; un error de compilación
    # no se considera una prueba aprobada.
    @classmethod
    def compilar(cls, fuentes, salida, extra=()):
        return cls.ejecutar(cls.cc+cls.flags+list(extra)+list(map(str, fuentes))+['-o', salida])

    # Conserva la evidencia solicitada y libera los recursos temporales comunes de la suite.
    @classmethod
    def tearDownClass(cls):
        print(f'S7 Python: {cls.planes} planes verificados; {cls.ejecuciones} ejecuciones S6, {cls.ciclos} ciclos.')
        if os.environ.get('S7_EVIDENCIA'):
            Path(os.environ['S7_EVIDENCIA']).write_text(json.dumps(dict(
                escenarios_c=cls.unidades, comprobaciones_c=cls.checks, planes=cls.planes,
                ejecuciones_simulador=cls.ejecuciones, ciclos=cls.ciclos,
                ejemplos=cls.ejemplos, comandos=cls.comandos), indent=2)+'\n')

    # Entrega los metadatos al scheduler real y recoge el plan o rechazo del caso; no deduce
    # dependencias ausentes.
    def planificar(self, instrucciones, aristas=(), bloques=None, esperado=0):
        bloques = bloques if bloques is not None else [(0, len(instrucciones), [])] if instrucciones else []
        f = self.dir/'entrada.txt'
        texto = [f'{len(instrucciones)} {len(aristas)} {len(bloques)}']
        for i, ins in enumerate(instrucciones):
            texto.append(f'{1000+i} {ins["slot"]} {ins["lee"]:x} {ins["escribe"]:x} {ins["efectos"]} {ins["control"]} {ins["bloque"]}')
        texto.extend(' '.join(map(str, a)) for a in aristas)
        texto.extend(' '.join(map(str, (inicio, fin, len(sucesores), *sucesores))) for inicio, fin, sucesores in bloques)
        f.write_text('\n'.join(texto)+'\n')
        p = json.loads(self.ejecutar([self.bin, 'fixture', f]))
        self.assertEqual(p['estado'], esperado, p)
        if not esperado:
            self.verificar_independiente(instrucciones, aristas, bloques, p)
            type(self).planes += 1
        else:
            self.assertNotIn('bundles', p, 'no debe publicarse una salida parcial')
        return p

    # Comprueba el plan sin reutilizar la selección de S7: índices, slots, distancias y orden
    # deben satisfacer el contrato.
    def verificar_independiente(self, ins, aristas, bloques, p):
        # Este oráculo inspecciona índices reales y restricciones, sin llamar
        # al selector C ni calcular una planificación alternativa con su lógica.
        paquetes = p['bundles']
        self.assertLessEqual(len(paquetes), 1023)
        self.assertEqual(len(p['mapa']), len(ins))
        self.assertEqual(len(p['bloques']), len(bloques))
        # El verificador del test recorre el plan sin reutilizar la selección C: registra cada
        # instrucción, contrasta mapa y slot y exige presencia única. Después comprueba
        # distancias, conflictos y guardas de entrada por separado.
        vistos = []
        for ciclo, bu in enumerate(paquetes):
            self.assertEqual(len(bu), 4)
            for slot, i in enumerate(bu):
                if i is None:
                    continue
                self.assertTrue(0 <= i < len(ins))
                self.assertEqual(ins[i]['slot'], slot)
                self.assertEqual(p['mapa'][i], ciclo)
                b = ins[i]['bloque']
                fin = p['bloques'][b+1] if b+1 < len(bloques) else len(paquetes)
                self.assertTrue(p['bloques'][b] <= ciclo < fin)
                if ins[i]['control']:
                    # Una transferencia cierra su bloque y se emite sola. Permitir operaciones
                    # detrás de ella en el mismo bloque cambiaría qué instrucciones pertenecen a
                    # cada ruta del CFG.
                    self.assertEqual(ciclo, fin-1)
                    self.assertEqual(sum(x is not None for x in bu), 1)
                vistos.append(i)
        self.assertEqual(sorted(vistos), list(range(len(ins))))
        self.assertEqual(p['vacios'], sum(all(i is None for i in bu) for bu in paquetes))
        # Para cada arista restamos las posiciones estáticas de consumidor y productor. Deben
        # respetar la distancia declarada; una operación ubicada en un slot libre todavía puede
        # violar RAW N+3.
        for antes, despues, tipo, distancia in aristas:
            self.assertGreaterEqual(p['mapa'][despues]-p['mapa'][antes], distancia)
        for i, a in enumerate(ins):
            for j in range(i+1, len(ins)):
                b = ins[j]
                ca, cb = p['mapa'][i], p['mapa'][j]
                ra, wa, rb, wb = (v & ~1 for v in (a['lee'], a['escribe'], b['lee'], b['escribe']))
                if wa & rb:
                    # RAW exige esperar a que el resultado ya sea visible para ID; un slot libre
                    # no elimina la latencia N+3 porque no existe forwarding.
                    self.assertGreaterEqual(cb-ca, 3)
                if (wa & wb) or (ra & wb):
                    # WAR y WAW deben conservar el orden original de lectura y escritura.
                    # Adelantar la segunda operación cambiaría el valor leído o el último valor
                    # del registro, aunque cada una use un slot válido.
                    self.assertGreater(cb, ca)
                if a['efectos'] & 3 and b['efectos'] & 3:
                    # Sin un análisis externo de aliasing no puede suponerse que dos accesos de
                    # memoria sean independientes. El plan debe conservar su orden original.
                    self.assertGreater(cb, ca)
                if any(x['slot'] == 3 or x['efectos'] & 124 or x['control'] in (2, 3) for x in (a, b)):
                    # Las operaciones con efectos globales y las llamadas o retornos actúan como
                    # barreras conservadoras. Su separación evita adelantar lecturas de estado,
                    # permisos o resultados todavía pendientes.
                    self.assertGreaterEqual(cb-ca, 3)
        protege_cero = any(0 in b[2] for b in bloques)
        for b, inicio in enumerate(p['bloques']):
            if b or protege_cero:
                # El destino de cada entrada protegida incluye dos bundles NOP. También se
                # protege B0 si el CFG vuelve a él, para que un backedge no omita el margen de
                # latencia.
                self.assertEqual(paquetes[inicio:inicio+2], [[None]*4]*2)

    # Ejecuta los casos C y el verificador independiente de calendarizaciones.
    def test_01_casos_c_y_verificador(self):
        salida = self.ejecutar([self.bin])
        m = re.search(r'S7: (\d+) escenarios, (\d+) comprobaciones PASS', salida)
        self.assertIsNotNone(m, salida)
        type(self).unidades, type(self).checks = map(int, m.groups())
        self.assertGreaterEqual(self.unidades, 22)
        print(salida, end='')

    # Fuerza fallos de reserva y exige que no se publique un plan parcial.
    def test_02_reservas_y_salida_atomica(self):
        self.assertIn('32', self.ejecutar([self.reservas, 'reservas']))

    # Comprueba selección entre instrucciones listas según dependencias, latencia y
    # disponibilidad de slot.
    def test_03_escoger_listas_por_disponibilidad(self):
        # I1 espera a I0; I2 es LSU independiente y puede acompañar a I0.
        # I3 ALU usa el hueco ciclo1, antes del consumidor RAW del ciclo3.
        ins = [op(escribe=[5]), op(lee=[5], escribe=[6]), op(1, efectos=MEM_R), op(escribe=[7])]
        p = self.planificar(ins, [(0, 1, 0, 3)])
        self.assertEqual(p['bundles'], [[0, 2, None, None], [3, None, None, None], [None]*4, [1, None, None, None]])
        self.assertLess(len(p['bundles']), len(ins)*3)

    # Genera DAG reproducibles y verifica seguridad y determinismo de sus planes.
    def test_04_dags_sinteticos_y_determinismo(self):
        # Cuatro cadenas independientes creadas por el fixture. Cada hilo
        # actualiza su registro propio, de modo que conocemos sus aristas
        # RAW/WAR/WAW sin incorporar un analizador de programas al producto.
        rng = random.Random(110843017)
        for semilla in range(60):
            ins, aristas = [], []
            ultimo = {}
            for paso in range(48):
                hilo = paso % 4
                slot = rng.randrange(2)
                ins.append(op(slot, lee=[hilo+1] if hilo in ultimo else [], escribe=[hilo+1], efectos=MEM_R if slot else 0))
                if hilo in ultimo:
                    aristas += [(ultimo[hilo], paso, 0, 3), (ultimo[hilo], paso, 2, 1)]
                    if ultimo[hilo] >= 4:
                        aristas.append((ultimo[hilo], paso, 1, 1))
                ultimo[hilo] = paso
            with self.subTest(semilla=semilla):
                a = self.planificar(ins, aristas)
                b = self.planificar(ins, list(reversed(aristas)))
                self.assertEqual(a, b, 'el orden del arreglo de aristas no cambia el desempate')

    # Omite metadatos y exige rechazo defensivo, sin reconstruir dependencias desde el
    # scheduler.
    def test_05_defensa_sin_reconstruir_dependencias(self):
        # Máscaras declaradas y completas, pero RAW omitida: se devuelve
        # METADATOS, no un plan ni una arista inventada por S7.
        self.planificar([op(escribe=[5]), op(lee=[5])], esperado=2)
        self.planificar([op(lee=[32])], esperado=2)
        # La prioridad física de WB nunca legitima coemitir WAW.
        self.planificar([op(escribe=[5]), op(1, escribe=[5], efectos=MEM_R)], [(0, 1, 2, 1)])

    # Convierte el plan de prueba a su representación ejecutable manteniendo slots, destinos y
    # NOP.
    def renderizar(self, ins, bloques, p=None):
        # Adaptador exclusivamente de pruebas. S8 deberá hacer la integración
        # real: aquí las etiquetas se entregan al codificador ya existente.
        paquetes = p['bundles'] if p else [fila for i, in_ in enumerate(ins) for fila in
                    [[i if s == in_['slot'] else None for s in range(4)], [None]*4, [None]*4]]
        inicio = p['bloques'] if p else [b[0]*3 for b in bloques]
        etiquetas = {c: f'block{b}:' for b, c in enumerate(inicio)}
        return ''.join((etiquetas.get(c, '')+'\n' if c in etiquetas else '')+
                       ' | '.join('nop' if i is None else ins[i]['texto'] for i in fila)+'\n'
                       for c, fila in enumerate(paquetes))

    # Compila y ejecuta la representación de referencia del caso para contrastarla con la salida
    # calendarizada.
    def ejecutar_fuente(self, texto, datos, ciclos):
        fuente, mem, dmem = self.dir/'programa.asm', self.dir/'programa.mem', self.dir/'dmem.bin'
        fuente.write_text(texto)
        self.ejecutar([self.codificador, fuente, mem])
        dmem.write_bytes(datos)
        salida = [json.loads(l) for l in self.ejecutar([self.sim, 'mem', mem, dmem, ciclos, 0, 'sonda']).splitlines()]
        trazas = [l for l in salida if 'c' in l]
        self.assertEqual(len(trazas), ciclos)
        self.assertTrue(all(c['traps'] == 0 for c in trazas), 'fixture con excepción inesperada')
        type(self).ejecuciones += 1
        type(self).ciclos += ciclos
        return trazas[-1], {x['mem']: x['valor'] for x in salida if 'mem' in x}

    # Ejecuta la misma imagen en simulador C y referencia y compara las observaciones públicas
    # del caso.
    def contrastar(self, ins, aristas, bloques=None, datos=None, ciclos=180, nombre='ejemplo'):
        bloques = bloques or [(0, len(ins), [])]
        p = self.planificar(ins, aristas, bloques)
        optimizado, conservador = self.renderizar(ins, bloques, p), self.renderizar(ins, bloques)
        datos = bytes(65536) if datos is None else datos
        a, ma = self.ejecutar_fuente(optimizado, datos, ciclos)
        b, mb = self.ejecutar_fuente(conservador, datos, ciclos)
        self.assertEqual(a['regs'], b['regs'])
        self.assertEqual(a['estado'], b['estado'])
        self.assertEqual(a['sonda_boveda'], b['sonda_boveda'])
        self.assertEqual(ma, mb)
        type(self).ejemplos.append(dict(nombre=nombre, operaciones=len(ins), bundles_s7=len(p['bundles']), bundles_conservador=3*len(ins), ciclos_por_ejecucion=ciclos))
        if os.environ.get('S7_EJEMPLOS'):
            carpeta = Path(os.environ['S7_EJEMPLOS']); carpeta.mkdir(exist_ok=True)
            (carpeta/(nombre+'.json')).write_text(json.dumps(p, indent=2)+'\n')
            (carpeta/(nombre+'-s7.asm')).write_text(optimizado)
            (carpeta/(nombre+'-conservador.asm')).write_text(conservador)
        return a, ma

    # Ejecuta planes ALU/LSU con RAW y comprueba la separación N+3.
    def test_06_ejecucion_raw_alu_lsu(self):
        ins = [op(escribe=[5], texto='sumai x5,x0,7'), op(1, escribe=[8], efectos=MEM_R, texto='cargai x8,x0,0'),
               op(lee=[5, 8], escribe=[6], texto='suma x6,x5,x8')]
        datos = bytearray(65536); datos[0] = 11
        a, _ = self.contrastar(ins, [(0, 2, 0, 3), (1, 2, 0, 3)], datos=datos, nombre='raw')
        self.assertEqual(a['regs'][6], 18)

    # Comprueba que la calendarización conserve el orden requerido de accesos a memoria.
    def test_07_ejecucion_memoria_ordenada(self):
        ins = [op(escribe=[5], texto='sumai x5,x0,7'), op(escribe=[6], texto='sumai x6,x0,8'),
               op(1, lee=[5], efectos=MEM_W, texto='guardap x0,x5,4'),
               op(1, escribe=[7], efectos=MEM_R, texto='cargai x7,x0,4'),
               op(lee=[6, 7], escribe=[8], texto='suma x8,x6,x7')]
        a, m = self.contrastar(ins, [(0, 2, 0, 3), (2, 3, 3, 1), (1, 4, 0, 3), (3, 4, 0, 3)], nombre='memoria')
        self.assertEqual((a['regs'][8], m[4]), (15, 7))

    # Ejecuta control condicional con varios predecesores y verifica protecciones de entrada.
    def test_08_ejecucion_condicional_y_varios_predecesores(self):
        for tomado in (False, True):
            ins = [op(escribe=[5], texto='sumai x5,x0,7'),
                   op(2, control=1, texto=('igualsi' if tomado else 'igualno')+' x0,x0,block2'),
                   op(escribe=[6], bloque=1, texto='sumai x6,x0,1'), op(2, control=1, bloque=1, texto='sye x0,block3'),
                   op(escribe=[6], bloque=2, texto='sumai x6,x0,2'),
                   op(lee=[5, 6], escribe=[7], bloque=3, texto='suma x7,x5,x6')]
            bloques = [(0, 2, [1, 2]), (2, 4, [3]), (4, 5, [3]), (5, 6, [])]
            aristas = [(0, 5, 0, 3), (2, 4, 2, 1), (2, 5, 0, 3), (4, 5, 0, 3)]
            a, _ = self.contrastar(ins, aristas, bloques, nombre='diamante-'+str(tomado))
            self.assertEqual(a['regs'][7], 9 if tomado else 8)

    # Ejecuta un bucle calendarizado y compara su resultado con la referencia.
    def test_09_ejecucion_bucle(self):
        ins = [op(escribe=[5], texto='sumai x5,x0,3'),
               op(lee=[5], escribe=[5], bloque=1, texto='restai x5,x5,1'),
               op(2, lee=[5], control=1, bloque=1, texto='igualno x5,x0,block1'),
               op(lee=[5], escribe=[6], bloque=2, texto='suma x6,x5,x0')]
        bloques = [(0, 1, [1]), (1, 3, [1, 2]), (3, 4, [])]
        aristas = [(0, 1, 0, 3), (0, 1, 2, 1), (0, 2, 0, 3), (1, 2, 0, 3), (0, 3, 0, 3), (1, 3, 0, 3)]
        a, _ = self.contrastar(ins, aristas, bloques, nombre='bucle')
        self.assertEqual(a['regs'][5:7], [0, 0])

    # Comprueba un backedge hacia B0 para que la protección inicial no se omita.
    def test_10_ejecucion_bucle_hacia_bloque_inicial(self):
        ins = [op(escribe=[2], texto='sumai x2,x0,3'), op(lee=[5], escribe=[5], texto='sumai x5,x5,1'),
               op(2, lee=[2, 5], control=1, texto='menora x5,x2,block0'),
               op(lee=[5], escribe=[6], bloque=1, texto='suma x6,x5,x0')]
        a, _ = self.contrastar(ins, [(0, 2, 0, 3), (1, 2, 0, 3), (1, 3, 0, 3)], [(0, 3, [0, 1]), (3, 4, [])], nombre='bucle-inicial')
        self.assertEqual(a['regs'][5:7], [3, 3])

    # Ejecuta CRIPTO con estado y destinos pares y verifica las barreras conservadoras.
    def test_11_ejecucion_cripto_estado_y_par(self):
        ins = [op(escribe=[2], texto='sumai x2,x0,7'), op(1, lee=[2], efectos=MEM_W, texto='guardap x0,x2,0'),
               op(3, lee=[2], efectos=BOVEDA | ESTADO | EXCEPCION, texto='setpwd x2'),
               op(3, efectos=ESTADO | MEM_R, texto='vcr 0'),
               op(3, lee=[2, 3], escribe=[4, 5], efectos=EXCEPCION, texto='fsl x4,x2,0,0'),
               op(lee=[4, 5], escribe=[6], texto='suma x6,x4,x5')]
        aristas = [(0, 1, 0, 3), (0, 2, 0, 3), (0, 4, 0, 3), (1, 3, 3, 1), (4, 5, 0, 3)]
        a, m = self.contrastar(ins, aristas, nombre='cripto')
        self.assertEqual((a['regs'][6], a['estado'], m[0]), (7, 1, 7))


if __name__ == '__main__':
    unittest.main()
