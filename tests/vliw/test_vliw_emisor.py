"""Ejercita S8-A con CB/S1–S6 y la sonda RTL aprobada, sin cambiar S7.

Los grafos y destinos son fixtures explícitos. El comparador conservador es
solo una política de pruebas (una operación y dos NOP), no un nuevo backend.
Todos los ejecutables, imágenes y trazas se crean fuera del repositorio.
"""
from pathlib import Path
import sys
import collections
import ctypes
import importlib.util
import json
import os
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
# La dependencia compartida conserva su dueño y su API; solo resolvemos
# su ubicación plana o por módulos según el árbol que contiene esta prueba.
sys.path.insert(0, str(ROOT / 'tools/vliw'))
from rutas_vliw import fuente_compartida, top_rtl, argumentos_rtl
spec = importlib.util.spec_from_file_location('fixtures_s7', ROOT/'tests/vliw/test_vliw_scheduler.py')
s7 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s7)
VACIO = (1 << (8*ctypes.sizeof(ctypes.c_size_t))) - 1
CODIFICADOR = r'''
#include "codificacion_binaria.h"
#include <stdlib.h>
int main(int argc,char **argv){
    if(argc!=4)return 2;
    FILE *f=fopen(argv[1],"rb");if(!f)return 2;
    if(fseek(f,0,SEEK_END)){fclose(f);return 2;}long n=ftell(f);
    if(n<0 || n>1048576 || fseek(f,0,SEEK_SET)){fclose(f);return 2;}
    char *s=calloc((size_t)n+1,1);if(!s){fclose(f);return 2;}
    int e=fread(s,1,(size_t)n,f)!=(size_t)n;if(fclose(f))e=1;
    CbPrograma p={0};if(!e)e=cb_ensamblar(s,&p,stderr);free(s);
    if(e){cb_liberar(&p);return 1;}
    f=fopen(argv[2],"wb");if(!f){cb_liberar(&p);return 2;}
    e=cb_escribir_mem(&p,f);if(fclose(f))e=1;
    f=fopen(argv[3],"wb");if(!f){cb_liberar(&p);return 2;}
    e|=cb_escribir_bin(&p,f);if(fclose(f))e=1;cb_liberar(&p);return e;
}
'''


class EmisorTests(unittest.TestCase):
    ejecutar = s7.SchedulerTests.__dict__['ejecutar']
    compilar = s7.SchedulerTests.__dict__['compilar']
    verificar_independiente = s7.SchedulerTests.verificar_independiente

    # Compila las bibliotecas y sondas reales una vez para la suite y prepara los recursos
    # compartidos en directorios de prueba.
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='pruebas-s8a-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.comandos, cls.ejemplos = [], []
        cls.planes = cls.ejecuciones = cls.ciclos = cls.rtl_programas = cls.rtl_ciclos = 0
        # Los ejecutables de prueba se reconstruyen en el temporal con el compilador y flags
        # seleccionados. Las variantes de inyección se enlazan aparte: permiten provocar fallos
        # sin cambiar las bibliotecas bajo prueba.
        cls.cc = shlex.split(os.environ.get('CC', 'cc'))
        cls.flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        fuentes = ['src/Calendarizacion_VLIW/vliw_emisor.c', 'src/Calendarizacion_VLIW/vliw_scheduler.c', fuente_compartida(ROOT, 'src/codificacion_binaria.c'),
                   'src/Simulador_VLIW/sim_decode.c', 'tests/vliw/test_vliw_emisor.c']
        cls.bin = cls.dir/'emisor'
        cls.compilar(fuentes, cls.bin)
        cls.fallos = cls.dir/'fallos'
        cls.compilar(fuentes, cls.fallos, ['-DVLIW_EMISOR_FALLOS'] +
                     ['-Wl,--wrap='+n for n in ('malloc', 'calloc', 'realloc', 'tmpfile', 'fflush', 'fseek', 'fclose')])
        fuente = cls.dir/'codificar.c'
        fuente.write_text(CODIFICADOR)
        cls.codificador = cls.dir/'codificar'
        cls.compilar([fuente, fuente_compartida(ROOT, 'src/codificacion_binaria.c')], cls.codificador)
        cls.sim = cls.dir/'simulador'
        cls.compilar(['tests/vliw/test_sim_crypto.c']+[f'src/Simulador_VLIW/sim_{n}.c' for n in
                     ('crypto', 'pipeline', 'alu', 'lsu', 'bru', 'decode', 'cargador')], cls.sim)
        cls.arch = Path(os.environ.get('ARQUITECTURA_ROOT', ROOT.parent/'Proyecto2_Arquitectura1')).resolve()
        cls.sv = cls.dir/'referencia.vvp'
        # No se sustituye la sonda de S6 ni se altera el RTL para acomodar S8-A.
        cls.ejecutar(['iverilog', '-g2012', '-I'+str(cls.arch), '-I'+str(cls.arch/'rtl'),
                      '-s', 'tb_sim_crypto_ref', '-o', cls.sv, ROOT/'tests/vliw/rtl/tb_sim_crypto_ref.sv']+
                     argumentos_rtl(cls.arch))

    # Conserva la evidencia solicitada y libera los recursos temporales comunes de la suite.
    @classmethod
    def tearDownClass(cls):
        evidencia = dict(planes=cls.planes, ejecuciones=cls.ejecuciones, ciclos=cls.ciclos,
                         rtl_programas=cls.rtl_programas, rtl_ciclos=cls.rtl_ciclos,
                         ejemplos=cls.ejemplos, comandos=cls.comandos,
                         unitarias=getattr(cls, 'unitarias', ''), fallos=getattr(cls, 'fallos_resultado', ''))
        print('S8-A evidencia: '+json.dumps({k: v for k, v in evidencia.items() if k not in ('comandos', 'ejemplos')}))
        if os.environ.get('S8_EVIDENCIA'):
            Path(os.environ['S8_EVIDENCIA']).write_text(json.dumps(evidencia, indent=2)+'\n')

    # Ejecuta las comprobaciones C de texto, CFG, codificador y fallos del emisor.
    def test_01_unitarias(self):
        salida = self.ejecutar([self.bin])
        self.assertRegex(salida, r'S8-A: \d+ escenarios, \d+ comprobaciones PASS')
        type(self).unitarias = salida.strip()
        type(self).fallos_resultado = self.ejecutar([self.fallos, 'fallos']).strip()
        self.assertIn('4 fallos IO PASS', self.fallos_resultado)

    # Entrega los metadatos al scheduler real y recoge el plan o rechazo del caso; no deduce
    # dependencias ausentes.
    def planificar(self, instrucciones, aristas=(), bloques=None):
        bloques = bloques or [(0, len(instrucciones), [])]
        self.ins, self.aristas, self.bloques = instrucciones, aristas, bloques
        filas = [f'{len(instrucciones)} {len(aristas)} {len(bloques)} {len(self.destinos)}']
        filas += [f'{100+7*i} {a["slot"]} {a["lee"]:x} {a["escribe"]:x} {a["efectos"]} {a["control"]} {a["bloque"]}'
                  for i, a in enumerate(instrucciones)]
        filas += [' '.join(map(str, a)) for a in aristas]
        filas += [' '.join(map(str, (a, z, len(su), *su))) for a, z, su in bloques]
        filas += [' '.join(map(str, a)) for a in self.destinos]
        filas += [a['texto'] for a in instrucciones]
        ruta, salida = self.dir/'fixture.txt', self.dir/'emitido.asm'
        ruta.write_text('\n'.join(filas)+'\n')
        p = json.loads(self.ejecutar([self.bin, 'fixture', ruta, salida]))
        self.assertEqual(p['estado'], 0)
        self.verificar_independiente(instrucciones, aristas, bloques, p)
        self.emitido = salida.read_text()
        # Oráculo estructural distinto del emisor: cada etiqueta ocupa cero
        # bytes y cada línea de cuatro slots conserva el índice del plan.
        # Leemos el ASM real emitido y verificamos etiquetas y líneas de bundle contra el mapa.
        # Las etiquetas no consumen IMEM y deben preceder las guardas NOP del bloque de destino.
        filas = self.emitido.splitlines()
        q = 0
        for fila in filas:
            if fila.endswith(':'):
                b = int(fila.removeprefix('__vliw_b').removesuffix(':'))
                self.assertEqual(q, p['bloques'][b])
            else:
                slots = fila.split(' | ')
                self.assertEqual(len(slots), 4)
                for slot, i in enumerate(p['bundles'][q]):
                    if i is None:
                        self.assertEqual(slots[slot], 'nop')
                    elif not instrucciones[i]['control'] or any(t[0] == i and t[1] in (2, 3) for t in self.destinos):
                        self.assertEqual(slots[slot], instrucciones[i]['texto'])
                q += 1
        self.assertEqual(q, len(p['bundles']))
        self.plan = p
        if os.environ.get('S8_ARTEFACTOS'):
            carpeta = Path(os.environ['S8_ARTEFACTOS']); carpeta.mkdir(exist_ok=True)
            (carpeta/(getattr(self, 'nombre', 'fixture')+'-entrada.txt')).write_text(ruta.read_text())
            (carpeta/(getattr(self, 'nombre', 'fixture')+'-plan.json')).write_text(json.dumps(p, indent=2)+'\n')
        type(self).planes += 1
        return p

    # Convierte el plan de prueba a su representación ejecutable manteniendo slots, destinos y
    # NOP.
    def renderizar(self, ins, bloques, p=None):
        return self.emitido if p else s7.SchedulerTests.renderizar(self, ins, bloques)

    # Compila y ejecuta la representación de referencia del caso para contrastarla con la salida
    # calendarizada.
    def ejecutar_fuente(self, texto, datos, ciclos):
        fuente, mem, binario = self.dir/'programa.asm', self.dir/'programa.mem', self.dir/'programa.bin'
        fuente.write_text(texto)
        self.ejecutar([self.codificador, fuente, mem, binario])
        # CB escribe dos formatos de la misma imagen; S1 recibe MEM y el
        # contraste confirma el orden ALU primero y little-endian de BIN.
        esperado = b''.join(bytes.fromhex(l)[::-1] for l in mem.read_text().splitlines())
        self.assertEqual(binario.read_bytes(), esperado)
        crudo, dmem = self.dir/'datos.bin', self.dir/'datos.hex'
        crudo.write_bytes(datos)
        dmem.write_text(''.join(f'{int.from_bytes(datos[i:i+4], "little"):08x}\n' for i in range(0, 65536, 4)))
        salida = [json.loads(l) for l in self.ejecutar([self.sim, 'mem', mem, crudo, ciclos, 0, 'sonda']).splitlines()]
        trazas = [x for x in salida if 'c' in x]
        memoria = {x['mem']: x['valor'] for x in salida if 'mem' in x}
        self.assertEqual(len(trazas), ciclos)
        if not getattr(self, 'permitir_trap', False):
            self.assertTrue(all(c['traps'] == 0 for c in trazas))
        if texto == self.emitido:
            sv = [json.loads(l) for l in self.ejecutar(['vvp', self.sv, '+IMEM='+str(mem), '+DMEM='+str(dmem),
                  '+CICLOS='+str(ciclos), '+RESET_DESPUES=0']).splitlines() if l.startswith('{')]
            st = [x for x in sv if 'c' in x]
            self.assertEqual(len(st), ciclos)
            for a, b in zip(trazas, st):
                for campo, valor in a.items():
                    # El contraste con RTL recorre cada campo de la traza en su mismo flanco.
                    # Igualar únicamente el estado final no detectaría una lectura de ID
                    # adelantada o un resultado publicado antes de tiempo.
                    self.assertEqual(valor, b[campo], (a['c'], campo))
            self.assertEqual(memoria, {x['mem']: x['valor'] for x in sv if 'mem' in x})
            type(self).rtl_programas += 1
            type(self).rtl_ciclos += ciclos
            self.traza_opt = trazas
        else:
            self.traza_base = trazas
        if os.environ.get('S8_ARTEFACTOS'):
            carpeta = Path(os.environ['S8_ARTEFACTOS']); carpeta.mkdir(exist_ok=True)
            prefijo = getattr(self, 'nombre', 'fixture')+('-s8a' if texto == self.emitido else '-conservador')
            for sufijo, contenido in (('.asm', fuente.read_bytes()), ('.mem', mem.read_bytes()), ('.bin', binario.read_bytes())):
                (carpeta/(prefijo+sufijo)).write_bytes(contenido)
            (carpeta/(prefijo+'-traza.json')).write_text(json.dumps(salida, indent=2)+'\n')
        type(self).ejecuciones += 1
        type(self).ciclos += ciclos
        return trazas[-1], memoria

    # Ejecuta la misma imagen en simulador C y referencia y compara las observaciones públicas
    # del caso.
    def contrastar(self, ins, aristas, bloques=None, datos=None, ciclos=180, nombre='ejemplo'):
        # Destinos explícitos por fixture. Nunca se deducen del offset viejo
        # ni del orden de los sucesores; los textos anteriores solo son entrada.
        self.nombre = nombre
        self.destinos = {'diamante-False': [(1, 1, 2, 1), (3, 0, 3, VACIO)],
                         'diamante-True': [(1, 1, 2, 1), (3, 0, 3, VACIO)],
                         'bucle': [(2, 1, 1, 2)], 'bucle-inicial': [(2, 1, 0, 1)]}.get(nombre, getattr(self, 'destinos_fixture', []))
        a, m = s7.SchedulerTests.contrastar(self, ins, aristas, bloques, datos, ciclos, nombre)
        # Comprueba las operaciones que llegaron realmente a WB, incluso si
        # el resultado final coincide por casualidad o el consumidor escribe0.
        # Contamos las instrucciones realmente observadas en las rutas de ambos layouts. Igualar
        # solo el estado final podría esconder operaciones omitidas; por eso también comprobamos
        # rutas y multiplicidades.
        observados = []
        for traza, mapa in ((self.traza_opt, self.plan['mapa']), (self.traza_base, [3*i for i in range(len(ins))])):
            por_pc = collections.defaultdict(list)
            for i, q in enumerate(mapa):
                por_pc[16*q].append(i)
            cuenta = collections.Counter(i for c in traza if c['v3'] for i in por_pc[c['pc3']])
            observados.append(cuenta)
        self.assertEqual(*observados)
        esperado = {i: 1 for i in range(len(ins))}
        if nombre.startswith('diamante'):
            for i in ([2, 3] if nombre.endswith('True') else [4]):
                esperado.pop(i)
        elif nombre == 'bucle':
            esperado.update({1: 3, 2: 3})
        elif nombre == 'bucle-inicial':
            esperado.update({0: 3, 1: 3, 2: 3})
        # Contar las operaciones realmente observadas impide aceptar dos layouts que terminan
        # igual por casualidad pero omiten rutas o repiten consumidores. El esperado depende del
        # camino tomado y de las iteraciones del fixture.
        self.assertEqual(dict(observados[0]), esperado, 'deben ejecutarse las rutas y consumidores previstos')
        ejemplo = type(self).ejemplos[-1]
        n = len(self.plan['bundles'])
        ejemplo.update(vacios=self.plan['vacios'], ocupacion_media=len(ins)/n, bytes_s8=16*n,
                       bytes_conservador=48*len(ins), reduccion_porcentaje=100*(3*len(ins)-n)/(3*len(ins)),
                       operaciones_ejecutadas=dict(observados[0]), ensamblador=self.emitido)
        return a, m

    test_02_raw = s7.SchedulerTests.test_06_ejecucion_raw_alu_lsu
    test_03_memoria = s7.SchedulerTests.test_07_ejecucion_memoria_ordenada
    test_04_diamante = s7.SchedulerTests.test_08_ejecucion_condicional_y_varios_predecesores
    test_05_bucle = s7.SchedulerTests.test_09_ejecucion_bucle
    test_06_bucle_inicial = s7.SchedulerTests.test_10_ejecucion_bucle_hacia_bloque_inicial
    test_07_cripto = s7.SchedulerTests.test_11_ejecucion_cripto_estado_y_par

    # Emite y ejecuta las operaciones criptográficas conservando slots y metadatos físicos.
    def test_08_seis_operaciones_cripto(self):
        # Contraseña7 y llaves cero: Feistel+inversa recuperan (7,0).
        # camcon rota tanto contraseña como candidata a896; vcr mantiene AUTH.
        op = s7.op
        ins = [op(escribe=[2], texto='sumai x2,x0,7'),
               op(1, lee=[2], efectos=2, texto='guardap x0,x2,0'),
               op(3, lee=[2], efectos=28, texto='setpwd x2'),
               op(3, efectos=9, texto='vcr 0'),
               op(3, lee=[8, 10], efectos=20, texto='ell 0,0,x8,x10'),
               op(3, lee=[2, 3], escribe=[4, 5], efectos=16, texto='fsl x4,x2,0,0'),
               op(3, lee=[4, 5], escribe=[6, 7], efectos=16, texto='fsli x6,x4,0,0'),
               op(lee=[6, 7], escribe=[12], texto='suma x12,x6,x7'),
               op(3, efectos=23, texto='camcon 0,7'), op(3, efectos=9, texto='vcr 0')]
        aristas = [(0, 1, 0, 3), (0, 2, 0, 3), (0, 5, 0, 3),
                   (1, 3, 3, 1), (5, 6, 0, 3), (6, 7, 0, 3),
                   (1, 8, 3, 1), (3, 8, 3, 1), (8, 9, 3, 1), (1, 9, 3, 1)]
        a, m = self.contrastar(ins, aristas, nombre='seis-cripto')
        self.assertEqual((a['regs'][12], a['estado'], m[0]), (7, 1, 896))

    # Empaqueta ALU independientes y contrasta la ejecución emitida con la referencia.
    def test_09_alu_independientes(self):
        ins = [s7.op(escribe=[r], texto=f'sumai x{r},x0,{r}') for r in (5, 6, 7)]
        a, _ = self.contrastar(ins, [], nombre='alu-independiente')
        self.assertEqual(a['regs'][5:8], [5, 6, 7])

    # Emite llamada y retorno y comprueba etiquetas, enlaces y continuación.
    def test_10_llamada_y_retorno(self):
        # El enlace vuelve al primer guard NOP de B1. El sye de B1 evita
        # caer de nuevo en la función; se exige una visita a cada operación.
        op = s7.op
        self.destinos_fixture = [(0, 0, 2, 1), (2, 0, 3, VACIO), (4, 3, VACIO, VACIO)]
        ins = [op(2, escribe=[1], efectos=32, control=2, texto='sye x1,block2'),
               op(escribe=[6], bloque=1, texto='sumai x6,x0,8'),
               op(2, control=1, bloque=1, texto='sye x0,block3'),
               op(escribe=[7], bloque=2, texto='sumai x7,x0,9'),
               op(2, lee=[1], control=3, bloque=2, texto='ret'),
               op(escribe=[8], bloque=3, texto='sumai x8,x0,10'),
               op(escribe=[1], bloque=3, texto='sumai x1,x0,0')]
        a, _ = self.contrastar(ins, [(0, 4, 0, 3), (0, 6, 2, 1), (4, 6, 1, 1)],
                              [(0, 1, [2, 1]), (1, 3, [3]), (3, 5, [1]), (5, 7, [])], nombre='llamada')
        # ra contiene un PC dependiente del layout, por lo que comparar GPR
        # finales entre políticas exige borrar explícitamente el enlace al salir.
        self.assertEqual(a['regs'][6:9], [8, 9, 10])

    # Emite un programa con excepción CRIPTO y verifica que conserve su comportamiento.
    def test_11_excepcion_cripto(self):
        # FSL sin AUTH debe anular ambos destinos y redirigir al vector;
        # el RTL y S6 reciben exactamente la imagen emitida por S8-A.
        self.destinos, self.permitir_trap = [], True
        self.nombre = 'excepcion'
        ins = [s7.op(3, lee=[2, 3], escribe=[4, 5], efectos=16, texto='fsl x4,x2,0,0')]
        self.planificar(ins)
        a, m = self.ejecutar_fuente(self.emitido, bytes(65536), 5)
        self.assertEqual(a['traps'], 1)
        self.assertEqual(a['trap_pc'], 0)
        self.assertEqual(a['regs'][4:6], [0, 0])
        self.assertFalse(m)
        self.assertTrue(any(c['pc'] == 0x3ff0 for c in self.traza_opt))


if __name__ == '__main__':
    unittest.main()
