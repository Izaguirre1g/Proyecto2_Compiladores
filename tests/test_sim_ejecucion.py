"""M2: ciclo de vida, coordinación diferencial, fragmentación y RTL original.

Reutiliza los fixtures CB/S7/S8-A y los oráculos S6/M1 sin modificarlos.
Los ejecutables, imágenes y evidencias se construyen fuera del repositorio.
"""
from pathlib import Path
import importlib.util
import json
import os
import struct
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('oraculos_m1_m2', ROOT/'tests/test_sim_metricas.py')
m1 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m1)
NOP = m1.NOP


# Se heredan fixtures y pruebas M1 para conservar CB, S7/S8-A y las sondas
# S6/RTL como referencias. Las trazas RTL exponen PC, etapas, flush y efectos;
# las instrucciones útiles se derivan de sus PC de retiro mediante S2, no de
# un contador RTL nuevo. El candidato que ejecuta esos programas será M2.
class EjecucionTests(m1.MetricasTests):
    @classmethod
    def setUpClass(cls):
        # La preparación M1 construye los oráculos originales. Se reemplazan
        # solo los ejecutables candidatos y se añade la variante de reservas
        # interceptadas; así no se fabrica otra referencia para esta capa.
        super().setUpClass()
        sim = [f'src/sim_{s}.c' for s in ('ejecucion', 'metricas', 'pipeline', 'alu', 'lsu',
                                       'bru', 'crypto', 'decode', 'cargador')]
        cls.bin, cls.fallos, cls.cli = (cls.dir/n for n in ('ejecucion', 'fallos-m2', 'sim_vliw_metricas'))
        cls.compilar(['tests/test_sim_ejecucion.c']+sim, cls.bin)
        cls.compilar(['tests/test_sim_ejecucion.c']+sim, cls.fallos,
                     ['-DSIM_EJECUCION_FALLOS', '-Wl,--wrap=calloc', '-Wl,--wrap=realloc'])
        cls.compilar(['tools/sim_vliw_metricas.c']+sim, cls.cli)
        cls.diferenciales = cls.metamorfismos = 0

    @classmethod
    def tearDownClass(cls):
        evidencia = dict(unitarias=getattr(cls, 'unitarias', ''), fallos=getattr(cls, 'reservas', ''),
                         diferenciales=cls.diferenciales, metamorfismos=cls.metamorfismos,
                         rtl_programas=cls.rtl_programas, rtl_ciclos=cls.rtl_ciclos,
                         ejecuciones=cls.ejecuciones, comparaciones_raw_memoria=cls.comparaciones,
                         ejemplos=cls.ejemplos, comandos=cls.comandos)
        print('M2: '+json.dumps({k:v for k,v in evidencia.items() if k not in ('ejemplos','comandos')}))
        if os.environ.get('M2_EVIDENCIA'):
            Path(os.environ['M2_EVIDENCIA']).write_text(json.dumps(evidencia, indent=2)+'\n')

    def balb(self, mem, datos=b''):
        # CB ya produjo el MEM. Se invierte su texto hexadecimal al orden de
        # bytes BALB y se agrega una sección de datos para inicializar DMEM.
        # Recortar ceros finales no cambia la imagen: S1 rellena con cero lo
        # no cargado. Esto prepara un archivo de prueba; S1 sigue validando
        # encabezado, secciones y límites como único cargador de producción.
        codigo = b''.join(bytes.fromhex(l)[::-1] for l in mem.read_text().splitlines())
        datos = bytes(datos).rstrip(b'\0')
        self.assertLessEqual(len(datos), 16384)
        archivo = self.dir/'programa.balb'
        archivo.write_bytes(b'BALB'+struct.pack('<11I', 1,48,48+len(codigo)+len(datos),0,48,
                           len(codigo),0,48+len(codigo),len(datos),0,0)+codigo+datos)
        return archivo

    def correr(self, nombre, asm=None, mem=None, datos=None, presupuesto=200, fisica=False, balb=False):
        # Se admite un MEM preparado por el caso o se obtiene con CB original.
        # BALB permite datos iniciales distintos de cero; MEM basta sin ellos,
        # salvo cuando el caso pide comprobar expresamente el contenedor BALB.
        mem = self.codificar(asm) if mem is None else mem
        datos = bytes(65536) if datos is None else bytes(datos)
        archivo = self.balb(mem,datos) if balb or any(datos) else mem
        tipo = 'balb' if archivo.suffix == '.balb' else 'mem'
        final = self.dir/'final-m2.bin'
        args = [tipo, archivo, 'fisica' if fisica else 'finita', presupuesto, 1]
        # run compara M2 con S6 directo después de cada flanco: PC, reloj,
        # estado, bundles, GPR, DMEM completa y señales públicas de crypto/trap.
        # También coteja su reporte con M1 aplicado a la referencia.
        salida = self.ejecutar([self.bin,'run',*args,'json',final])
        r = json.loads(salida); memoria=final.read_bytes()
        self.assertEqual(len(memoria),65536)
        self.assertEqual(r['version'],1)
        # Esta huella comprueba que el reporte describe la memoria guardada.
        # La equivalencia de los 65536 bytes ya se verificó con memcmp en C;
        # no se sustituye esa comparación por un hash.
        self.assertEqual(r['arquitectura']['dmem_fnv1a64'],m1.fnv(memoria))
        self.assertEqual(r['admitidos'],r['completados']+r['anulados']+r['pendientes'])
        self.assertEqual(r['instrucciones_utiles'],sum(r['instrucciones_por_slot'].values()))
        for secreto in ('password','keys','boveda'):
            self.assertNotIn(secreto,salida)
        # Repetir desde una sesión nueva exige JSON determinista. La pasada
        # TEXTO ejercita el otro exportador M1 con el mismo programa y estado.
        self.assertEqual(self.ejecutar([self.bin,'run',*args,'json',final]),salida)
        texto=self.ejecutar([self.bin,'run',*args,'texto',final])
        self.assertIn('Métricas dinámicas',texto)
        type(self).ejecuciones+=3; type(self).diferenciales+=3
        type(self).ejemplos.append(dict(nombre=nombre,resultado=r))
        if os.environ.get('M2_ARTEFACTOS'):
            d=Path(os.environ['M2_ARTEFACTOS']);d.mkdir(exist_ok=True)
            (d/(nombre+'.json')).write_text(salida)
            (d/(nombre+'.txt')).write_text(texto)
            (d/(nombre+'-dmem.bin')).write_bytes(memoria)
            (d/(nombre+'.mem')).write_bytes(mem.read_bytes())
            if asm is not None:(d/(nombre+'.asm')).write_text(asm)
        return r,memoria

    def test_01_c_y_fallos(self):
        nop=self.dir/'nop.mem';nop.write_text('0'*32+'\n')
        mem=self.codificar('nop | guardap x0,x0,4 | nop | nop\n')
        datos=bytearray(8);datos[4]=9;store=self.balb(mem,datos)
        # La próxima codificación reutiliza nombres temporales; conservar el
        # BALB evita reemplazar el fixture del store con el de traps.
        permanente=self.dir/'store.balb';permanente.write_bytes(store.read_bytes())
        mem=self.codificar('nop | nop | nop | fsl x4,x2,0,0\n')
        # FSL sin autorización provoca un trap que redirige al vector 0x3ff0,
        # donde se coloca la misma operación. Así se fuerza el crecimiento
        # real del historial, no un estado interno manipulado de M1.
        palabra=mem.read_text().strip();trap=self.dir/'traps.mem'
        trap.write_text('\n'.join([palabra]+['0'*32]*1022+[palabra])+'\n')
        args=['unit',nop,permanente,trap]
        type(self).unitarias=self.ejecutar([self.bin,*args]).strip()
        type(self).reservas=self.ejecutar([self.fallos,*args]).strip()
        self.assertRegex(self.unitarias,r'M2: \d+ comprobaciones PASS')
        self.assertRegex(self.reservas,r'M2: \d+ comprobaciones PASS')

    @classmethod
    def ejecutar(cls, cmd, esperado=0):
        # stderr conserva diagnósticos y la cantidad de flancos cotejados;
        # stdout queda separado para analizar exclusivamente el reporte. Un
        # retorno distinto del esperado hace fallar la prueba con ambas salidas.
        cmd=list(map(str,cmd))
        p=subprocess.run(cmd,cwd=ROOT,capture_output=True,timeout=90)
        cls.comandos.append(dict(comando=cmd,codigo=p.returncode,stderr=p.stderr.decode('utf-8',errors='replace')))
        if p.returncode!=esperado:
            raise AssertionError(f'{cmd}: código {p.returncode}, esperado {esperado}\n{p.stdout!r}\n{p.stderr!r}')
        return p.stdout.decode('utf-8')

    def test_12_fragmentacion(self):
        # Cambiar solo los cortes del presupuesto no debe perder ni repetir
        # observaciones: 100, 10+20+30+40 y 100×1 deben dar el mismo JSON,
        # historial y DMEM completa. En FINITA se compara la terminación real,
        # sin exigir flancos ficticios para consumir los cien intentos.
        casos=[('nop',NOP),('alu','sumai x5,x0,7 | nop | nop | nop\n'),
               ('traps','nop | nop | nop | fsl x4,x2,0,0\n'),
               ('memoria','sumai x5,x0,7 | nop | nop | nop\n'+NOP*2+
                'nop | guardap x0,x5,4 | nop | nop\nnop | cargai x7,x0,4 | nop | nop\n'),
               ('raw','sumai x5,x0,7 | cargai x8,x0,0 | nop | nop\n'+NOP*2+
                'suma x6,x5,x8 | nop | nop | nop\n'),
               ('salto','nop | nop | igualsi x0,x0,fin | nop\n'+
                'sumai x5,x0,1 | nop | nop | nop\nfin: sumai x6,x0,9 | nop | nop | nop\n')]
        for nombre,asm in casos:
            mem=self.codificar(asm)
            datos=bytearray(16);datos[0]=11
            archivo=self.balb(mem,datos) if nombre=='raw' else mem
            tipo='balb' if nombre=='raw' else 'mem'
            for modo in ('finita','fisica'):
                if nombre=='traps' and modo=='finita':continue
                out=self.dir/'fragmentada.bin';resultados=[]
                for fragmento in ('100','10,20,30,40','1'):
                    salida=self.ejecutar([self.bin,'chunk',tipo,archivo,modo,100,fragmento,'json',out])
                    resultados.append((salida,out.read_bytes()))
                self.assertEqual(resultados[0],resultados[1]);self.assertEqual(resultados[1],resultados[2])
                referencia=self.ejecutar([self.bin,'run',tipo,archivo,modo,100,1,'json',out])
                self.assertEqual(resultados[0],(referencia,out.read_bytes()))
                type(self).metamorfismos+=1

    def test_13_cli_adversarial(self):
        mem=self.codificar(NOP)
        for presupuesto,codigo,estado in [('0',2,'LIMITE'),('1',2,'LIMITE'),
                                          ('100',0,'TERMINADO'),('18446744073709551615',0,'TERMINADO')]:
            r=json.loads(self.ejecutar([self.cli,'mem',mem,'finita',presupuesto,'json'],codigo))
            self.assertEqual(r['estado'],estado)
        # La CLI debe rechazar sintaxis o rangos inválidos antes de ejecutar;
        # incluso una conversión numérica que admitiría + o espacios se rechaza.
        for presupuesto in ('-1','+1',' 1','1x','','18446744073709551616'):
            self.assertEqual(self.ejecutar([self.cli,'mem',mem,'fisica',presupuesto,'json'],64),'')
        for tipo,modo,formato in [('crudo','finita','json'),('mem','otro','json'),('mem','finita','xml')]:
            self.assertEqual(self.ejecutar([self.cli,tipo,mem,modo,10,formato],64),'')
        self.assertEqual(self.ejecutar([self.cli],64),'')
        self.assertEqual(self.ejecutar([self.cli,'mem',self.dir/'ausente','finita',1,'json'],1),'')
        r=json.loads(self.ejecutar([self.cli,'mem',mem,'fisica',100,'json'],2))
        self.assertEqual(r['ciclos'],100)
        self.assertIn('Métricas dinámicas',self.ejecutar([self.cli,'mem',mem,'finita',10,'texto']))

    def test_14_entradas_corruptas(self):
        # Son fallos de carga, no excepciones de la ISA: archivos vacíos,
        # hex inválido o bundles incompletos deben impedir cualquier reporte.
        for nombre,datos,tipo in [('truncado',b'BALB','balb'),('vacio',b'','mem'),
                                  ('hex',b'G'*32+b'\n','mem'),('incompleto',b'00\n','mem')]:
            archivo=self.dir/nombre;archivo.write_bytes(datos)
            self.assertEqual(self.ejecutar([self.cli,tipo,archivo,'finita',10,'json'],1),'')
        # El encabezado declara un código completo, pero se elimina su último
        # byte. S1 debe detectar el truncamiento y no publicar una sesión.
        mem=self.codificar(NOP);archivo=self.balb(mem);archivo.write_bytes(archivo.read_bytes()[:-1])
        self.assertEqual(self.ejecutar([self.cli,'balb',archivo,'finita',10,'json'],1),'')

    def test_15_error_cli_y_salida(self):
        # Recortar el código deja un salto legal hacia fuera de la extensión
        # FINITA. La herramienta debe comunicar el rechazo de PC como error,
        # sin presentarlo como terminación o presupuesto agotado.
        mem=self.codificar('nop | nop | sye x0,fin | nop\n'+NOP+'fin: nop\n')
        mem.write_text(mem.read_text().splitlines()[0]+'\n')
        self.assertEqual(self.ejecutar([self.cli,'mem',mem,'finita',100,'json'],1),'')
        # /dev/full rechaza la escritura con el dispositivo lleno. El reporte
        # puede haberse generado bien, pero fputs/fflush deben impedir que la
        # herramienta devuelva éxito si no pudo entregarlo al destino.
        mem=self.codificar(NOP)
        with open('/dev/full','wb') as f:
            p=subprocess.run(list(map(str,[self.cli,'mem',mem,'finita',10,'json'])),stdout=f,stderr=subprocess.PIPE)
        self.assertEqual(p.returncode,1);self.assertIn(b'error al escribir',p.stderr)


if __name__ == '__main__':
    unittest.main()
