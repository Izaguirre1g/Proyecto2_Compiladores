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


class EjecucionTests(m1.MetricasTests):
    @classmethod
    def setUpClass(cls):
        # Conserva compiladores y sondas originales; solo el candidato usa M2.
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
        # Es un contenedor de fixture, no otro cargador: S1 interpreta y valida
        # todas las secciones. Las palabras las produce siempre CB original.
        codigo = b''.join(bytes.fromhex(l)[::-1] for l in mem.read_text().splitlines())
        datos = bytes(datos).rstrip(b'\0')
        self.assertLessEqual(len(datos), 16384)
        archivo = self.dir/'programa.balb'
        archivo.write_bytes(b'BALB'+struct.pack('<11I', 1,48,48+len(codigo)+len(datos),0,48,
                           len(codigo),0,48+len(codigo),len(datos),0,0)+codigo+datos)
        return archivo

    def correr(self, nombre, asm=None, mem=None, datos=None, presupuesto=200, fisica=False, balb=False):
        mem = self.codificar(asm) if mem is None else mem
        datos = bytes(65536) if datos is None else bytes(datos)
        archivo = self.balb(mem,datos) if balb or any(datos) else mem
        tipo = 'balb' if archivo.suffix == '.balb' else 'mem'
        final = self.dir/'final-m2.bin'
        args = [tipo, archivo, 'fisica' if fisica else 'finita', presupuesto, 1]
        salida = self.ejecutar([self.bin,'run',*args,'json',final])
        r = json.loads(salida); memoria=final.read_bytes()
        self.assertEqual(len(memoria),65536)
        self.assertEqual(r['version'],1)
        self.assertEqual(r['arquitectura']['dmem_fnv1a64'],m1.fnv(memoria))
        self.assertEqual(r['admitidos'],r['completados']+r['anulados']+r['pendientes'])
        self.assertEqual(r['instrucciones_utiles'],sum(r['instrucciones_por_slot'].values()))
        for secreto in ('password','keys','boveda'):
            self.assertNotIn(secreto,salida)
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
        permanente=self.dir/'store.balb';permanente.write_bytes(store.read_bytes())
        mem=self.codificar('nop | nop | nop | fsl x4,x2,0,0\n')
        palabra=mem.read_text().strip();trap=self.dir/'traps.mem'
        trap.write_text('\n'.join([palabra]+['0'*32]*1022+[palabra])+'\n')
        args=['unit',nop,permanente,trap]
        type(self).unitarias=self.ejecutar([self.bin,*args]).strip()
        type(self).reservas=self.ejecutar([self.fallos,*args]).strip()
        self.assertRegex(self.unitarias,r'M2: \d+ comprobaciones PASS')
        self.assertRegex(self.reservas,r'M2: \d+ comprobaciones PASS')

    @classmethod
    def ejecutar(cls, cmd, esperado=0):
        # Conservar stderr además del código para contabilizar evidencia de
        # comparación por flanco; nunca incorporarlo al reporte JSON público.
        cmd=list(map(str,cmd))
        p=subprocess.run(cmd,cwd=ROOT,capture_output=True,timeout=90)
        cls.comandos.append(dict(comando=cmd,codigo=p.returncode,stderr=p.stderr.decode('utf-8',errors='replace')))
        if p.returncode!=esperado:
            raise AssertionError(f'{cmd}: código {p.returncode}, esperado {esperado}\n{p.stdout!r}\n{p.stderr!r}')
        return p.stdout.decode('utf-8')

    def test_12_fragmentacion(self):
        # Comparar el JSON completo incluye métricas e historial. Se comprueba
        # también la DMEM byte a byte y el estado finito de terminación temprana.
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
        for nombre,datos,tipo in [('truncado',b'BALB','balb'),('vacio',b'','mem'),
                                  ('hex',b'G'*32+b'\n','mem'),('incompleto',b'00\n','mem')]:
            archivo=self.dir/nombre;archivo.write_bytes(datos)
            self.assertEqual(self.ejecutar([self.cli,tipo,archivo,'finita',10,'json'],1),'')
        # Se valida también un BALB correcto con sección declarada truncada.
        mem=self.codificar(NOP);archivo=self.balb(mem);archivo.write_bytes(archivo.read_bytes()[:-1])
        self.assertEqual(self.ejecutar([self.cli,'balb',archivo,'finita',10,'json'],1),'')

    def test_15_error_cli_y_salida(self):
        mem=self.codificar('nop | nop | sye x0,fin | nop\n'+NOP+'fin: nop\n')
        mem.write_text(mem.read_text().splitlines()[0]+'\n')
        self.assertEqual(self.ejecutar([self.cli,'mem',mem,'finita',100,'json'],1),'')
        # /dev/full informa errores reales de salida sin tocar el repositorio.
        mem=self.codificar(NOP)
        with open('/dev/full','wb') as f:
            p=subprocess.run(list(map(str,[self.cli,'mem',mem,'finita',10,'json'])),stdout=f,stderr=subprocess.PIPE)
        self.assertEqual(p.returncode,1);self.assertIn(b'error al escribir',p.stderr)


if __name__ == '__main__':
    unittest.main()
