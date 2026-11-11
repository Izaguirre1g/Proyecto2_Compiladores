"""Contrasta formatos S8-C con planes S7 y ensamblador S8-A reales.

Los metadatos y destinos son fixtures explícitos, nunca se analizan dependencias
ni BAL. JSON y CSV se leen con parsers estándar y se comparan campo a campo.
Los ejecutables y reportes se generan exclusivamente en directorios temporales.
"""
from pathlib import Path
import sys
import csv
import ctypes
import io
import json
import math
import os
import random
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
# La dependencia compartida conserva su dueño y su API; solo resolvemos
# su ubicación plana o por módulos según el árbol que contiene esta prueba.
sys.path.insert(0, str(ROOT / 'tools/vliw'))
from rutas_vliw import fuente_compartida
VACIO = (1 << (ctypes.sizeof(ctypes.c_size_t)*8))-1
SLOTS = ('ALU', 'LSU', 'BRU', 'CRIPTO')
TIPOS = ('RAW', 'WAR', 'WAW', 'MEMORIA', 'CONTROL', 'BARRERA')


# Construye los metadatos de una operación sintética, incluidas máscaras, slot y efectos, para
# probar la calendarización.
def op(slot=0, lee=(), escribe=(), efectos=0, control=0, bloque=0, texto='sumai x0,x0,0'):
    return dict(slot=slot, lee=sum(1 << r for r in set(lee)), escribe=sum(1 << r for r in set(escribe)),
                efectos=efectos, control=control, bloque=bloque, texto=texto)


class VisualizadorTests(unittest.TestCase):
    # Compila las bibliotecas y sondas reales una vez para la suite y prepara los recursos
    # compartidos en directorios de prueba.
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='pruebas-s8c-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.dir = Path(cls.tmp.name)
        cls.comandos, cls.ejemplos = [], []
        cls.planes = cls.contrastes_emisor = 0
        # Los ejecutables de prueba se reconstruyen en el temporal con el compilador y flags
        # seleccionados. Las variantes de inyección se enlazan aparte: permiten provocar fallos
        # sin cambiar las bibliotecas bajo prueba.
        cls.c = shlex.split(os.environ.get('CC', 'cc'))
        cls.flags = ['-std=c11', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-O2', '-Iinclude']
        fuentes = ['src/Calendarizacion_VLIW/vliw_visualizador.c', 'src/Calendarizacion_VLIW/vliw_scheduler.c', 'tests/vliw/test_vliw_visualizador.c']
        cls.bin, cls.fallos, cls.integracion, cls.s7 = (cls.dir/n for n in ('visual', 'fallos', 'integracion', 's7'))
        cls.compilar(fuentes, cls.bin)
        cls.compilar(fuentes, cls.fallos, ['-DVLIW_VIS_FALLOS']+
                     ['-Wl,--wrap='+n for n in ('malloc', 'calloc', 'realloc', 'vsnprintf', '__vsnprintf_chk')])
        cls.compilar(fuentes+['src/Calendarizacion_VLIW/vliw_emisor.c', fuente_compartida(ROOT, 'src/codificacion_binaria.c'), 'src/Simulador_VLIW/sim_decode.c'],
                     cls.integracion, ['-DVLIW_VIS_EMISOR'])
        cls.compilar(['src/Calendarizacion_VLIW/vliw_scheduler.c', 'tests/vliw/test_vliw_scheduler.c'], cls.s7)

    # Invoca el ejecutable del caso con sus argumentos y comprueba su retorno; devuelve la
    # salida para contrastar datos.
    @classmethod
    def ejecutar(cls, cmd, codigo=0):
        cmd = list(map(str, cmd))
        # Capturar bytes evita que universal_newlines cambie CR dentro de un
        # campo CSV entrecomillado, que debe conservarse exactamente.
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=60)
        cls.comandos.append(dict(comando=cmd, codigo=p.returncode))
        if p.returncode != codigo:
            raise AssertionError(f'{cmd}: código {p.returncode}, esperado {codigo}\n{p.stdout!r}\n{p.stderr!r}')
        return p.stdout.decode('utf-8')

    # Reconstruye el componente necesario desde las fuentes de la copia; un error de compilación
    # no se considera una prueba aprobada.
    @classmethod
    def compilar(cls, fuentes, salida, extra=()):
        cls.ejecutar(cls.c+cls.flags+list(extra)+fuentes+['-o', salida])

    # Conserva la evidencia solicitada y libera los recursos temporales comunes de la suite.
    @classmethod
    def tearDownClass(cls):
        e = dict(planes=cls.planes, contrastes_emisor=cls.contrastes_emisor,
                 unitarias=getattr(cls, 'unitarias', ''), fallos=getattr(cls, 'reservas', ''),
                 ejemplos=cls.ejemplos, comandos=cls.comandos)
        print('S8-C: '+json.dumps({k: v for k, v in e.items() if k not in ('ejemplos', 'comandos')}))
        if os.environ.get('S8C_EVIDENCIA'):
            Path(os.environ['S8C_EVIDENCIA']).write_text(json.dumps(e, indent=2)+'\n')

    # Contrasta las vistas y estadísticas del plan con oráculos de la prueba, separando layout
    # estático de ciclos dinámicos.
    def comprobar(self, nombre, ins, deps=(), bloques=None, destinos=(), emisor=True, ancho=None):
        if bloques is None:
            bloques = [(0, len(ins), [])] if ins else []
        base = [f'{100+11*i} {a["slot"]} {a["lee"]:x} {a["escribe"]:x} {a["efectos"]} {a["control"]} {a["bloque"]}' for i, a in enumerate(ins)]
        base += [' '.join(map(str, a)) for a in deps]
        base += [' '.join(map(str, (inicio, fin, len(su), *su))) for inicio, fin, su in bloques]
        archivo, entrada_s7 = self.dir/'fixture.txt', self.dir/'s7.txt'
        entrada_s7.write_text(f'{len(ins)} {len(deps)} {len(bloques)}\n'+'\n'.join(base)+'\n')
        # El oráculo es el ejecutable original S7, sin modificar sus fuentes.
        p = json.loads(self.ejecutar([self.s7, 'fixture', entrada_s7]))
        self.assertEqual(p['estado'], 0)
        lineas = [f'{len(ins)} {len(deps)} {len(bloques)} {len(destinos)}']+base
        lineas += [' '.join(map(str, t)) for t in destinos]
        lineas += ['-' if a['texto'] is None else a['texto'].encode('utf-8').hex() for a in ins]
        archivo.write_text('\n'.join(lineas)+'\n')
        args = [ancho] if ancho is not None else []
        # Solicitamos tabla, CSV y JSON al mismo binario y fixture. Sus lectores son
        # independientes: el test contrasta filas, índices, textos y estadísticas con el plan
        # S7, no con una copia de otro formato.
        tabla = self.ejecutar([self.bin, '--tabla', archivo]+args)
        csv_texto = self.ejecutar([self.bin, '--csv', archivo]+args)
        json_texto = self.ejecutar([self.bin, '--json', archivo]+args)
        j = json.loads(json_texto)
        filas = list(csv.DictReader(io.StringIO(csv_texto, newline='')))
        a = j['estadisticas']
        n = len(p['bundles'])
        # La vista describe posiciones estáticas de IMEM, no ciclos ejecutados. Los conteos de
        # bundles, slots y bytes deben derivarse del mismo plan, sin presentar ocupación
        # estática como IPC dinámico.
        self.assertEqual(j['resumen'], dict(naturaleza='estatica', unidad='bundle_IMEM', origen_pc=0, plan_verificado=True))
        self.assertEqual(len(j['bundles']), n)
        self.assertEqual(len(filas), n)
        self.assertEqual(a['instrucciones_utiles'], len(ins))
        self.assertEqual(a['bundles_totales'], n)
        self.assertEqual(a['bundles_vacios'], p['vacios'])
        self.assertEqual(a['bundles_con_operaciones'], n-p['vacios'])
        self.assertEqual(a['slots_disponibles'], 4*n)
        self.assertEqual(a['slots_utilizados'], len(ins))
        self.assertEqual(a['slots_vacios'], 4*n-len(ins))
        self.assertEqual(a['bytes_imem'], 16*n)
        self.assertEqual(a['bloques'], len(bloques))
        self.assertEqual(a['textos_ausentes'], sum(not x['texto'] for x in ins))
        for clave, valor in (('utilizacion_porcentual', 100*len(ins)/(4*n) if n else 0),
                              ('operaciones_por_bundle', len(ins)/n if n else 0)):
            # Los reportes redondean sus cocientes a seis decimales. La tolerancia cubre ese
            # redondeo de representación, sin exigir igualdad binaria de los double.
            self.assertTrue(math.isclose(a[clave], valor, abs_tol=0.00000051), (clave, a[clave], valor))
        self.assertEqual(a['operaciones_por_slot'], {s: sum(x['slot'] == i for x in ins) for i, s in enumerate(SLOTS)})
        self.assertEqual(a['dependencias_por_tipo'], {s: sum(x[2] == i for x in deps) for i, s in enumerate(TIPOS)})
        stats = json.loads(self.ejecutar([self.bin, '--estadisticas', archivo]))
        for clave, valor in stats.items():
            self.assertTrue(math.isclose(valor, a[clave], abs_tol=0.00000051), clave)
        protegido = any(0 in su for _, _, su in bloques)
        guardas = 0
        filas_tabla = tabla.splitlines()[2:2+n]
        self.assertEqual(len(filas_tabla), n)
        # Recorremos juntos el plan, el JSON y las filas CSV. En cada slot distinguimos NOP,
        # índice de instrucción y texto ausente; también verificamos PC, bloque y guardas sin
        # confundirlos con ciclos dinámicos.
        for q, (indices, bu, fila) in enumerate(zip(p['bundles'], j['bundles'], filas)):
            b = next(b for b, inicio in reversed(list(enumerate(p['bloques']))) if inicio <= q)
            guard = bool(b or protegido) and q < p['bloques'][b]+2
            es_control = any(ins[i]['control'] for i in indices if i is not None)
            clase = 'PROTECCION' if guard else 'VACIO' if all(i is None for i in indices) else 'CONTROL' if es_control else 'OPERACIONES'
            guardas += guard
            self.assertEqual((bu['indice'], bu['pc'], bu['bloque'], bu['operaciones']), (q, 16*q, b, sum(i is not None for i in indices)))
            self.assertEqual((bu['inicio_bloque'], bu['proteccion'], bu['control'], bu['clase']), (q == p['bloques'][b], guard, bool(es_control), clase))
            self.assertEqual([x['instruccion'] for x in bu['slots']], indices)
            self.assertEqual((int(fila['bundle']), int(fila['pc'], 0), int(fila['bloque']), int(fila['operaciones'])), (q, 16*q, b, bu['operaciones']))
            for clave in ('inicio_bloque', 'proteccion', 'control'):
                self.assertEqual(bool(int(fila[clave])), bu[clave])
            self.assertEqual(fila['clase'], clase)
            columnas = [x.strip() for x in filas_tabla[q].split(' | ')]
            self.assertEqual(len(columnas), 10)
            self.assertEqual((int(columnas[0]), int(columnas[1], 0), columnas[2], columnas[3], columnas[4], columnas[5]),
                             (q, 16*q, f'B{b}', 'SI' if bu['inicio_bloque'] else 'NO', clase, 'SI' if es_control else 'NO'))
            for slot, i in enumerate(indices):
                s = bu['slots'][slot]
                texto = None if i is None else ins[i]['texto']
                # Un slot NOP y una instrucción sin texto son casos distintos: el primero no
                # tiene índice; la segunda conserva su identidad y una marca de texto ausente.
                # JSON debe preservar esa distinción.
                self.assertEqual(s['slot'], SLOTS[slot])
                self.assertEqual(s['texto'], texto)
                self.assertEqual(s['texto_ausente'], i is not None and not texto)
                clave = SLOTS[slot].lower()
                self.assertEqual(fila[clave], 'NOP' if i is None else texto or '')
                self.assertEqual(fila[clave+'_indice'], '' if i is None else str(i))
                self.assertEqual(fila[clave+'_texto_ausente'], str(int(i is not None and not texto)))
                if emisor:
                    # Los fixtures ISA caben completos: se comprueba cada
                    # celda humana, no solo que el JSON y CSV sean correctos.
                    self.assertEqual(columnas[6+slot], 'NOP' if i is None else f'I{i}: {texto}')
        self.assertEqual(a['bundles_proteccion'], guardas)
        for b, bloque in enumerate(j['bloques']):
            self.assertEqual(bloque, dict(indice=b, etiqueta=f'B{b}', inicio_bundle=p['bloques'][b],
                fin_bundle=p['bloques'][b+1] if b+1 < len(bloques) else n,
                pc=16*p['bloques'][b], proteccion_bundles=2 if b or protegido else 0, sucesores=bloques[b][2]))
        self.assertEqual(len(j['instrucciones']), len(ins))
        for i, info in enumerate(j['instrucciones']):
            for campo in ('texto', 'lee', 'escribe', 'efectos', 'control', 'bloque'):
                self.assertEqual(info[campo], ins[i][campo])
            self.assertEqual((info['indice'], info['id'], info['slot'], info['bundle'], info['pc']),
                             (i, 100+11*i, SLOTS[ins[i]['slot']], p['mapa'][i], 16*p['mapa'][i]))
        self.assertEqual(len(j['dependencias']), len(deps))
        for dep, (origen, destino, tipo, distancia) in zip(j['dependencias'], deps):
            self.assertEqual(dep, dict(origen=origen, destino=destino, tipo=TIPOS[tipo], requerida=distancia,
                efectiva=p['mapa'][destino]-p['mapa'][origen], bundle_origen=p['mapa'][origen],
                bundle_destino=p['mapa'][destino], estado='SATISFECHA'))
        # Mismos datos: el formato elegido y su ancho no cambian el plan.
        self.assertEqual(self.ejecutar([self.bin, '--json', archivo]), json_texto)
        self.assertIn('estática', tabla)
        asm = None
        if emisor:
            ruta = self.dir/'emitido.asm'
            contraste = json.loads(self.ejecutar([self.integracion, '--contraste', archivo, ruta]))
            self.assertEqual(contraste, j)
            asm = ruta.read_text()
            filas_asm = [x for x in asm.splitlines() if not x.endswith(':')]
            self.assertEqual(len(filas_asm), n)
            for q, indices in enumerate(p['bundles']):
                celdas = filas_asm[q].split(' | ')
                for s, i in enumerate(indices):
                    esperado = 'nop' if i is None else ins[i]['texto']
                    meta = next((t for t in destinos if t[0] == i), None)
                    if i is not None and meta and meta[1] in (0, 1):
                        esperado = esperado.rsplit(',', 1)[0]+f',__vliw_b{meta[2]}'
                    # Solo los saltos directos sustituyen su destino por la etiqueta del bloque.
                    # Los otros operandos y los slots NOP se conservan, de modo que visualizar
                    # el plan no cambie lo que el emisor codifica.
                    self.assertEqual(celdas[s], esperado)
            etiquetas, q = {}, 0
            for linea in asm.splitlines():
                if linea.endswith(':'):
                    etiquetas[linea[:-1]] = q
                else:
                    q += 1
            # Las etiquetas no ocupan bytes IMEM y deben preceder las guardas del bloque. El
            # mapa se contrasta contando bundles, sin confundir líneas de etiquetas con
            # instrucciones.
            self.assertEqual(etiquetas, {f'__vliw_b{b}': q for b, q in enumerate(p['bloques'])})
            type(self).contrastes_emisor += 1
        type(self).planes += 1
        type(self).ejemplos.append(dict(nombre=nombre, operaciones=len(ins), bundles=n, vacios=p['vacios'], guardas=guardas,
                                       utilizacion=a['utilizacion_porcentual'], bytes_imem=a['bytes_imem'], contraste_emisor=emisor))
        if os.environ.get('S8C_ARTEFACTOS'):
            carpeta = Path(os.environ['S8C_ARTEFACTOS']); carpeta.mkdir(exist_ok=True)
            for sufijo, valor in (('.tabla.txt', tabla), ('.csv', csv_texto), ('.json', json_texto), ('.fixture', archivo.read_text()), ('.plan.json', json.dumps(p, indent=2))):
                (carpeta/(nombre+sufijo)).write_bytes(valor.encode('utf-8'))
            if asm is not None:
                (carpeta/(nombre+'.asm')).write_text(asm)
        return j, tabla

    # Ejecuta las pruebas C de formatos, propiedad de buffers y errores inyectados.
    def test_01_unitarias_y_fallos(self):
        type(self).unitarias = self.ejecutar([self.bin]).strip()
        self.assertRegex(self.unitarias, r'S8-C: \d+ escenarios, \d+ comprobaciones PASS')
        type(self).reservas = self.ejecutar([self.fallos, '--fallos']).strip()
        self.assertRegex(self.reservas, r'\d+ reservas inyectadas, 6 errores de generación PASS')

    # Visualiza entrada vacía y una operación sin inventar utilización ni ciclos dinámicos.
    def test_02_vacio_y_una_operacion(self):
        j, _ = self.comprobar('vacio', [])
        self.assertEqual(j['estadisticas']['bytes_imem'], 0)
        self.comprobar('una', [op(escribe=[5], texto='sumai x5,x0,7')])

    # Muestra empaquetado y RAW y comprueba los NOP necesarios entre productor y consumidor.
    def test_03_empaquetado_y_raw(self):
        ins = [op(escribe=[5], texto='sumai x5,x0,7'), op(1, escribe=[8], efectos=1, texto='cargai x8,x0,0')]
        self.comprobar('empaquetado', ins)
        ins += [op(lee=[5, 8], escribe=[6], texto='suma x6,x5,x8')]
        j, _ = self.comprobar('raw', ins, [(0, 2, 0, 3), (1, 2, 0, 3)])
        self.assertEqual(j['estadisticas']['utilizacion_porcentual'], 18.75)
        self.assertEqual(j['estadisticas']['operaciones_por_bundle'], 0.75)
        self.assertEqual([x['clase'] for x in j['bundles']], ['OPERACIONES', 'VACIO', 'VACIO', 'OPERACIONES'])

    # Comprueba las categorías de dependencia y los efectos de memoria en las vistas exportadas.
    def test_04_seis_tipos_y_memoria(self):
        ins = [op(lee=[6], escribe=[5], texto='suma x5,x6,x0'),
               op(lee=[5], escribe=[6], texto='suma x6,x5,x0'), op(escribe=[6], texto='sumai x6,x0,9')]
        deps = [(0, 1, 0, 3), (0, 1, 1, 1), (1, 2, 2, 1)]+[(0, 2, t, 1) for t in (3, 4, 5)]
        self.comprobar('seis-dependencias', ins, deps)
        ins = [op(1, efectos=2, texto='guardap x0,x0,4'), op(1, escribe=[5], efectos=1, texto='cargai x5,x0,4')]
        self.comprobar('memoria', ins, [(0, 1, 3, 1)])

    # Visualiza un CFG en diamante y contrasta bloques, destinos y protecciones.
    def test_05_diamante(self):
        ins = [op(escribe=[5], texto='sumai x5,x0,7'), op(2, control=1, texto='igualsi x0,x0,999'),
               op(escribe=[6], bloque=1, texto='sumai x6,x0,1'), op(2, control=1, bloque=1, texto='sye x0,-16'),
               op(escribe=[6], bloque=2, texto='sumai x6,x0,2'), op(lee=[5, 6], escribe=[7], bloque=3, texto='suma x7,x5,x6')]
        # El tomado aparece al final del arreglo, sin inferirlo por posición.
        j, _ = self.comprobar('diamante', ins, [(0, 5, 0, 3), (2, 4, 2, 1), (2, 5, 0, 3), (4, 5, 0, 3)],
                             [(0, 2, [1, 2]), (2, 4, [3]), (4, 5, [3]), (5, 6, [])], [(1, 1, 2, 1), (3, 0, 3, VACIO)])
        self.assertEqual(j['estadisticas']['bundles_proteccion'], 6)
        self.assertEqual(j['instrucciones'][1]['destino'], dict(tipo='CONDICIONAL', tomado=2, caida=1))

    # Comprueba bucles y backedges a B0 con sus protecciones de entrada.
    def test_06_bucles_y_bloque_inicial(self):
        ins = [op(escribe=[5], texto='sumai x5,x0,3'), op(lee=[5], escribe=[5], bloque=1, texto='restai x5,x5,1'),
               op(2, lee=[5], control=1, bloque=1, texto='igualno x5,x0,999'), op(lee=[5], escribe=[6], bloque=2, texto='suma x6,x5,x0')]
        self.comprobar('bucle', ins, [(0, 1, 0, 3), (0, 1, 2, 1), (0, 2, 0, 3), (1, 2, 0, 3), (0, 3, 0, 3), (1, 3, 0, 3)],
                       [(0, 1, [1]), (1, 3, [1, 2]), (3, 4, [])], [(2, 1, 1, 2)])
        ins = [op(escribe=[2], texto='sumai x2,x0,3'), op(lee=[5], escribe=[5], texto='sumai x5,x5,1'),
               op(2, lee=[2, 5], control=1, texto='menora x5,x2,999'), op(lee=[5], escribe=[6], bloque=1, texto='suma x6,x5,x0')]
        j, _ = self.comprobar('backedge-b0', ins, [(0, 2, 0, 3), (1, 2, 0, 3), (1, 3, 0, 3)],
                             [(0, 3, [0, 1]), (3, 4, [])], [(2, 1, 0, 1)])
        self.assertEqual([b['clase'] for b in j['bundles'][:2]], ['PROTECCION']*2)
        self.assertEqual(j['estadisticas']['bundles_proteccion'], 4)

    # Verifica que unidades físicas y CRIPTO conserven sus índices y estadísticas.
    def test_07_unidades_y_cripto(self):
        ins = [op(), op(1, efectos=1, texto='cargai x0,x0,0'), op(3, efectos=28, texto='setpwd x0'),
               op(2, control=1, texto='sye x0,999'), op(bloque=1)]
        self.comprobar('cuatro-unidades', ins, [], [(0, 4, [1]), (4, 5, [])], [(3, 0, 1, VACIO)])
        ins = [op(3, efectos=28, texto='setpwd x0'), op(3, efectos=9, texto='vcr 0'),
               op(3, lee=[2, 4], efectos=20, texto='ell 0,0,x2,x4'),
               op(3, lee=[2, 3], escribe=[6, 7], efectos=16, texto='fsl x6,x2,0,0'),
               op(3, lee=[6, 7], escribe=[8, 9], efectos=16, texto='fsli x8,x6,0,0'),
               op(3, efectos=23, texto='camcon 0,7')]
        self.comprobar('seis-cripto', ins, [(3, 4, 0, 3)])

    # Exporta textos especiales y comprueba escape, UTF-8 y distinción entre ausencia de texto y
    # NOP.
    def test_08_textos_y_escapado(self):
        # Estos textos sirven para probar presentación, no son instrucciones
        # ensamblables. Nunca se envían al emisor ni se les inventa semántica.
        especial = 'comas,"comillas"\\barra\n\r\t\x1b | ñ 漢字 😀 '+''.join(chr(i) for i in range(1, 32))
        self.comprobar('ausentes', [op(texto=None), op(texto='')], emisor=False)
        j, t = self.comprobar('escapado', [op(texto=especial)], emisor=False, ancho=16)
        # La tabla debe impedir que controles de terminal o saltos de línea alteren su
        # presentación. JSON conserva el texto original escapado, para que esa protección visual
        # no se convierta en pérdida de datos.
        self.assertEqual(j['instrucciones'][0]['texto'], especial)
        self.assertNotIn('\x1b', t)
        largo = 'sumai x5,x0,7 '+('muy largo, "ñ" | '*300)
        j, t = self.comprobar('largo', [op(texto=largo)], emisor=False, ancho=12)
        # El ancho limita únicamente la celda de la tabla. El JSON conserva el texto completo:
        # truncar también el dato exportado impediría reconstruir la descripción original.
        self.assertEqual(j['instrucciones'][0]['texto'], largo)
        self.assertIn('...', t)
        self.comprobar('auto-ancho', [op(texto=largo)], emisor=False, ancho=0)

    # Repite planes sintéticos y exige vistas deterministas.
    def test_09_determinismo_sintetico(self):
        rng = random.Random(110828)
        # Registros distintos por construcción: no se deducen aristas de texto.
        for k in range(20):
            ins = []
            for r in range(5, 13):
                slot = rng.randrange(2)
                ins.append(op(slot, escribe=[r], efectos=1 if slot else 0,
                              texto=f'cargai x{r},x0,0' if slot else f'sumai x{r},x0,{r}'))
            self.comprobar(f'independientes-{k}', ins)

    # Visualiza el límite de capacidad sin desbordar índices, buffers ni estadísticas.
    def test_10_capacidad_maxima(self):
        j, _ = self.comprobar('limite-1023', [op() for _ in range(1023)])
        self.assertEqual(j['bundles'][-1]['pc'], 0x3fe0)
        self.assertEqual(j['estadisticas']['bytes_imem'], 16368)


if __name__ == '__main__':
    unittest.main()
