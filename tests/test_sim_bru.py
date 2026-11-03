"""Contrasta BRU, PC, flush y ejecución ALU/LSU/BRU con el RTL original."""
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

ROOT=Path(__file__).resolve().parents[1]
CODIGOS=[1,2,4,8,0,1]
NOP=('nop','nop','nop')


class BruTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporal=tempfile.TemporaryDirectory(prefix='pruebas-bru-')
        cls.addClassCleanup(cls.temporal.cleanup)
        cls.carpeta=Path(cls.temporal.name)
        cls.sonda=cls.carpeta/'bru'
        cls.vectores=cls.ciclos=cls.programas=0
        cls.comandos=[]
        cc=shlex.split(os.environ.get('CC','cc'))
        cmd=cc+['-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-O2','-Iinclude',
            'tests/test_sim_bru.c','src/sim_bru.c','src/sim_pipeline.c','src/sim_lsu.c',
            'src/sim_alu.c','src/sim_decode.c','src/sim_cargador.c','-o',str(cls.sonda)]
        subprocess.run(cmd,cwd=ROOT,check=True)
        cls.comandos.append(dict(comando=cmd,codigo=0))
        subprocess.run(['make','balc'],cwd=ROOT,check=True,capture_output=True)
        cls.arquitectura=Path(os.environ.get('ARQUITECTURA_ROOT',ROOT.parent/'Proyecto2_Arquitectura1')).resolve()
        cls.rtl_disponible=(cls.arquitectura/'rtl/top.sv').is_file() and all(shutil.which(t) for t in ('iverilog','vvp'))
        if cls.rtl_disponible:
            spec=importlib.util.spec_from_file_location('ensamblador_s5',cls.arquitectura/'tools/assembler.py')
            cls.asm=importlib.util.module_from_spec(spec)
            sys.modules[spec.name]=cls.asm
            spec.loader.exec_module(cls.asm)
            cls.referencia=cls.carpeta/'bru.vvp'
            cmd=['iverilog','-g2012','-I'+str(cls.arquitectura),'-I'+str(cls.arquitectura/'rtl'),
                 '-s','tb_sim_bru_ref','-o',str(cls.referencia),str(ROOT/'tests/rtl/tb_sim_bru_ref.sv')]
            cmd+=list(map(str,sorted((cls.arquitectura/'rtl').glob('*.sv'))))
            p=subprocess.run(cmd,capture_output=True,text=True)
            if p.returncode: raise RuntimeError(p.stdout+p.stderr)
            cls.comandos.append(dict(comando=cmd,codigo=p.returncode))

    @classmethod
    def tearDownClass(cls):
        print(f'Contraste S5: {cls.vectores} vectores BRU/PC, {cls.ciclos} ciclos, {cls.programas} ejecuciones top.')
        if os.environ.get('S5_EVIDENCIA'):
            Path(os.environ['S5_EVIDENCIA']).write_text(json.dumps(dict(vectores=cls.vectores,
                ciclos=cls.ciclos,programas=cls.programas,comandos=cls.comandos),indent=2)+'\n')

    def ejecutar(self,cmd,esperado=0):
        p=subprocess.run(list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True)
        self.comandos.append(dict(comando=list(map(str,cmd)),codigo=p.returncode))
        self.assertEqual(p.returncode,esperado,p.stdout+p.stderr)
        self.ultimo_stderr=p.stderr
        return p.stdout

    def exigir_rtl(self):
        if not self.rtl_disponible: self.skipTest('se requieren Arquitectura, iverilog y vvp para contraste real')

    def crear_mem(self,programa,raw=None):
        self.exigir_rtl()
        bundles=[]
        for n,fila in enumerate(programa):
            slots=tuple(self.asm.encode_instruction(t,pc=16*n) for t in (*fila,'nop'))
            bundles.append(self.asm.Bundle(n,n+1,slots))
        # El ensamblador existente codifica y serializa. Los overrides raw son
        # únicamente fixtures de palabras reservadas/no soportadas del camino
        # descartado; no agregan una sintaxis ni decodificador alternativo.
        texto=self.asm.Program(tuple(bundles)).memory_text().splitlines()
        for (n,slot),palabra in (raw or {}).items():
            v=int(texto[n],16)
            v=(v&~(0xffffffff<<(32*slot)))|(palabra<<(32*slot))
            texto[n]=f'{v:032x}'
        ruta=self.carpeta/'programa.mem'
        ruta.write_text('\n'.join(texto)+'\n')
        return ruta

    def contrastar(self,programa,datos=None,presupuesto=200,fisica=False,reset=0,raw=None,balb=None):
        self.exigir_rtl()
        ruta=self.crear_mem(programa,raw)
        inicial=bytearray(65536) if datos is None else bytearray(datos)
        self.assertEqual(len(inicial),65536)
        crudo,palabras=self.carpeta/'datos.bin',self.carpeta/'datos.hex'
        crudo.write_bytes(inicial)
        palabras.write_text(''.join(f'{int.from_bytes(inicial[p:p+4],"little"):08x}\n' for p in range(0,65536,4)))
        args=[self.sonda,'balb' if balb else 'mem',balb or ruta,presupuesto,reset,'fisica' if fisica else 'finita']
        if not balb: args+=[crudo]
        salida=[json.loads(l) for l in self.ejecutar(args).splitlines()]
        resumen=salida[-1]
        self.assertIn(resumen['estado'],(1,2))
        c=[v for v in salida if 'c' in v or 'reset' in v]
        cantidad=sum('c' in v for v in c)
        args=['vvp',self.referencia,'+IMEM='+str(ruta),'+DMEM='+str(palabras),
              '+CICLOS='+str(cantidad),'+RESET_DESPUES='+str(reset)]
        rtl=[json.loads(l) for l in self.ejecutar(args).splitlines() if l.startswith('{')]
        cm={v['mem']:v['valor'] for v in salida if 'mem' in v}
        rm={v['mem']:v['valor'] for v in rtl if 'mem' in v}
        self.assertEqual(cm,rm,'DMEM final completa')
        rtl=[v for v in rtl if 'mem' not in v]
        self.assertEqual(len(c),len(rtl))
        fin=len(programa)*16
        for sim,hw in zip(c,rtl):
            if 'reset' in sim:
                self.assertEqual(sim,hw,'reset: banco, store de EX y DMEM')
                continue
            with self.subTest(ciclo=sim['c'],pc=sim['pcs'][0]):
                for clave in ('c','pc','regs','flush','conflicto'):
                    self.assertEqual(sim[clave],hw[clave],clave)
                self.assertEqual(sim['pcs'][0],hw['pc_pre'])
                for n,(v,r) in enumerate(zip(sim['stages'],hw['stages'])):
                    if v:
                        self.assertEqual(r,1)
                        self.assertEqual(sim['pcs'][n],hw['pcs'][n])
                    elif r:
                        self.assertFalse(fisica,'diferencia de validez en modo físico')
                        self.assertGreaterEqual(hw['pcs'][n],fin,'diferencia dentro del código')
                # En flush no se acepta ni decodifica ID. Su contenido raw no
                # puede detener C ni convertirse en operandos de la siguiente EX.
                if sim['stages'][1] and not sim['flush']:
                    self.assertEqual(sim['idalu'],hw['idalu'])
                    for clave in ('idlsu','idbru'):
                        self.assertEqual(sim[clave][0],hw[clave][0])
                        if sim[clave][0]: self.assertEqual(sim[clave],hw[clave])
                if sim['stages'][2]:
                    self.assertEqual(sim['exalu'],hw['exalu'])
                    self.assertEqual(sim['target'],hw['target'])
                    self.assertEqual(sim['x'][0],hw['x'][0])
                    if sim['x'][0]:
                        self.assertEqual(sim['x'],hw['x'])
                        self.assertEqual(sim['postmem'],hw['postmem'])
                for n in range(2):
                    self.assertEqual(sim['kill'][n],int(bool(sim['flush'] and sim['stages'][n])))
                    if fisica or sim['stages'][n]: self.assertEqual(sim['kill'][n],hw['kill'][n])
                if sim['stages'][3]:
                    w0=list(sim['w0'])
                    if w0[1]==32: w0[1]=0
                    self.assertEqual(w0,hw['w0'])
                for puerto in (1,2):
                    self.assertEqual(sim[f'w{puerto}_valida'],hw[f'w{puerto}_valida'])
                    self.assertEqual(sim[f'w{puerto}'][0],hw[f'w{puerto}'][0])
                    if sim[f'w{puerto}_valida']: self.assertEqual(sim[f'w{puerto}'],hw[f'w{puerto}'])
                for clave in ('pending','link'):
                    self.assertEqual(sim[clave][0],hw[clave][0])
                    if sim[clave][0]: self.assertEqual(sim[clave],hw[clave])
                for n,(v,r) in enumerate(zip(sim['next'],hw['next'])):
                    if v:
                        self.assertEqual(r,1)
                        self.assertEqual(sim['next_pc'][n],hw['next_pc'][n])
                    elif r:
                        self.assertFalse(fisica)
                        self.assertGreaterEqual(hw['next_pc'][n],fin)
                if sim['flush']:
                    self.assertEqual(sim['next'][:2],[0,0])
                    self.assertEqual(sim['pc'],sim['target'])
                # Los contadores incluyen solo IF efectivamente capturado.
                # Todo admitido está completado, anulado o todavía en vuelo.
                a,completados,anulados=sim['cuentas']
                self.assertEqual(a,completados+anulados+sum(sim['next']))
                if fisica: self.assertEqual(sim['cuentas'],hw['cuentas'])
        type(self).ciclos+=cantidad
        type(self).programas+=1
        return c,cm,resumen

    def test_01_unitarias_bru_y_control(self):
        self.assertIn('181 comprobaciones',self.ejecutar([self.sonda]))

    def test_02_vectores_bru_pc_reales(self):
        self.exigir_rtl()
        rng=random.Random(430111085)
        valores=[0,1,0x7fffffff,0x80000000,0xffffffff]
        filas=[]
        for i,a,b in itertools.product(range(6),valores,valores):
            for off in ([-32768,-16,0,16,32752] if i==4 else [-1024,-16,0,16,1008]):
                filas.append((i,CODIGOS[i],rng.choice([0,16,0xfffffff0]),a,b,off,rng.choice([0,1,31]),len(filas)%2))
        for _ in range(512):
            i=rng.randrange(6)
            off=rng.randrange(-32768,32768) if i==4 else rng.randrange(-1024,1024)
            filas.append((i,CODIGOS[i],rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32),off,rng.randrange(32),rng.randrange(2)))
        ruta=self.carpeta/'vectores.txt'
        ruta.write_text(''.join(f'{i} {op} {pc:08x} {a:08x} {b:08x} {off} {rd} {reset}\n' for i,op,pc,a,b,off,rd,reset in filas))
        c=self.ejecutar([self.sonda,'vectores',ruta]).splitlines()
        rtl=[l for l in self.ejecutar(['vvp',self.referencia,'+VECTORES='+str(ruta)]).splitlines() if l.startswith('B ')]
        self.assertEqual(len(c),len(filas)); self.assertEqual(len(c),len(rtl))
        for n,(a,b) in enumerate(zip(c,rtl)): self.assertEqual(a,b,f'vector {n}: {filas[n]}')
        type(self).vectores=len(filas)

    def test_03_tabla_salto_tomado(self):
        # B0 calcula ALU, almacena y salta. B1/B2 producirían cambios visibles,
        # pero el flush de EX/ciclo3 debe impedir todos sus efectos posteriores.
        p=[('sumai x5,x0,7','guardap x0,x0,0','sye x31,48'),
           ('sumai x6,x0,99','guardap x0,x0,4','sye x30,0'),
           ('sumai x7,x0,88','cargai x8,x0,0','sye x29,0'),
           ('sumai x9,x0,9','nop','nop')]
        datos=bytearray(65536); datos[0]=0x80;datos[4]=23
        t,m,r=self.contrastar(p,datos)
        self.assertEqual(len(t),7)
        self.assertEqual((t[2]['flush'],t[2]['kill'],t[2]['pc']),(1,[1,1],48))
        self.assertEqual(t[3]['w2'],[1,31,16])
        self.assertEqual([t[-1]['regs'][n] for n in (5,6,7,8,9,29,30,31)],[7,0,0,0,9,0,0,16])
        self.assertEqual(m,{4:23})
        self.assertEqual(t[-1]['cuentas'],[3,2,1])
        if os.environ.get('S5_TABLA_TOMADO'):
            Path(os.environ['S5_TABLA_TOMADO']).write_text(json.dumps(t,indent=2)+'\n')

    def test_04_tabla_salto_no_tomado(self):
        p=[('sumai x5,x0,7','nop','igualno x0,x0,48'),
           ('sumai x6,x0,8','nop','nop'),('sumai x7,x0,9','nop','nop'),NOP]
        t,m,r=self.contrastar(p)
        self.assertEqual(len(t),7)
        self.assertFalse(any(c['flush'] for c in t))
        self.assertEqual(t[-1]['regs'][5:8],[7,8,9])
        self.assertEqual(t[-1]['cuentas'],[4,4,0])
        if os.environ.get('S5_TABLA_NO_TOMADO'):
            Path(os.environ['S5_TABLA_NO_TOMADO']).write_text(json.dumps(t,indent=2)+'\n')

    def test_05_cuatro_condiciones_tomadas_y_no_tomadas(self):
        # x1=-1 y x2=1 disponibles tras N+3. Invertir fuentes y comparar un
        # registro consigo mismo cubre ambos resultados de cada condición.
        casos=[('igualsi','x1','x1',True),('igualsi','x1','x2',False),
               ('igualno','x1','x1',False),('igualno','x1','x2',True),
               ('menora','x1','x2',True),('menora','x2','x1',False),
               ('mayoroigual','x1','x2',False),('mayoroigual','x2','x1',True)]
        for op,a,b,tomado in casos:
            with self.subTest(op=op,tomado=tomado):
                p=[('sumai x1,x0,-1','nop','nop'),('sumai x2,x0,1','nop','nop'),NOP,NOP,
                   ('nop','nop',f'{op} {a},{b},48'),('sumai x5,x0,99','nop','nop'),NOP,('sumai x6,x0,7','nop','nop')]
                t,_,_=self.contrastar(p)
                ex=next(c for c in t if c['stages'][2] and c['pcs'][2]==64)
                self.assertEqual(bool(ex['flush']),tomado)
                self.assertEqual(t[-1]['regs'][5],0 if tomado else 99)

    def test_06_camino_incorrecto_invalido_o_no_soportado(self):
        # Las mismas palabras en ID causarían rechazo sin redirect. Con salto
        # tomado se anulan antes de aceptar S2, incluso con store válido al lado.
        for posicion,palabra,slot in [(1,1,0),(2,1,0),(1,0x04600000,3),(2,0x04600000,3),
                                       (1,0x80042807,1)]:
            with self.subTest(posicion=posicion,palabra=palabra,slot=slot):
                p=[('nop','nop','sye x31,48'),('sumai x5,x0,99','guardap x0,x0,0','nop'),
                   ('sumai x6,x0,88','guardap x0,x0,4','nop'),NOP]
                datos=bytearray(65536);datos[0]=17;datos[4]=19
                t,m,_=self.contrastar(p,datos,raw={(posicion,slot):palabra})
                self.assertEqual(m,{0:17,4:19})
                self.assertEqual(t[-1]['regs'][5:7],[0,0])

    def test_07_conflictos_tres_puertos(self):
        datos=bytearray(65536);datos[0]=99
        # Se ejecutan ambos saltos de enlace. Los casos indirectos inicializan
        # su base con distancia suficiente; también prueban alineación 0x43->40.
        for indirecto in (False,True):
            for destinos in ((5,6,7),(5,6,5),(5,6,6),(5,5,5),(0,0,0)):
                with self.subTest(indirecto=indirecto,destinos=destinos):
                    a,l,b=destinos
                    pref=[('sumai x2,x0,0x83','nop','nop'),NOP,NOP] if indirecto else []
                    pc=len(pref)*16
                    salto=f'syer x{b},x2,-3' if indirecto else f'sye x{b},{128-pc}'
                    p=pref+[(f'sumai x{a},x0,7',f'cargai x{l},x0,0',salto)]
                    p += [NOP]*(9-len(p))
                    t,_,_=self.contrastar(p,datos)
                    wb=next(c for c in t if c['stages'][3] and c['pcs'][3]==pc)
                    esperado=[0]*32
                    if indirecto: esperado[2]=0x83
                    for rd,v in ((a,7),(l,99),(b,pc+16)):
                        if rd: esperado[rd]=v
                    self.assertEqual(t[-1]['regs'],esperado)
                    self.assertEqual(bool(wb['conflicto']),len({d for d in destinos if d})<sum(bool(d) for d in destinos))
                    self.assertEqual(wb['w2'][0],int(b!=0))

    def test_08_alu_y_load_a_bru_n_mas_1_2_3(self):
        datos=bytearray(65536);datos[0]=1
        for productor,d in itertools.product(('alu','lsu'),(1,2,3)):
            with self.subTest(productor=productor,distancia=d):
                inicial=('sumai x2,x0,1','nop','nop') if productor=='alu' else ('nop','cargai x2,x0,0','nop')
                p=[inicial]+[NOP]*(d-1)+[('nop','nop',f'igualsi x2,x0,{(7-d)*16}')]
                p += [NOP]*(8-len(p))
                t,_,_=self.contrastar(p,datos)
                lectura=t[d+1]
                self.assertEqual((lectura['c'],lectura['idbru'][1]),(d+2,1 if d==3 else 0))
                ex=t[d+2]
                self.assertEqual(bool(ex['flush']),d<3)
                # EX usa el valor capturado: N+2 decide con 0 aunque WB acaba
                # de escribir x2 en el flanco de su lectura ID.
                self.assertEqual(t[3]['regs'][2],1)

    def test_09_enlace_no_forwarding_mismo_bundle(self):
        p=[('suma x5,x31,x0','nop','sye x31,48'),NOP,NOP,('suma x6,x31,x0','nop','nop')]
        t,_,_=self.contrastar(p)
        self.assertEqual(t[1]['idalu'][0],0)
        self.assertEqual(t[4]['idalu'][0],16)
        self.assertEqual([t[-1]['regs'][n] for n in (5,6,31)],[0,16,16])

    def test_10_llamada_y_ret_con_enlace_ya_disponible(self):
        # El destino comienza a leerse DESPUÉS de WB gracias al flush de dos
        # ciclos. ret inmediato en la subrutina es válido en el RTL real.
        p=[('nop','nop','sye x1,64'),('sumai x5,x0,7','nop','sye x0,96'),NOP,NOP,
           ('nop','nop','ret'),('sumai x6,x0,99','guardap x0,x0,0','nop'),NOP,
           ('sumai x7,x0,9','nop','nop')]
        t,_,_=self.contrastar(p)
        ret=next(c for c in t if c['stages'][1] and c['pcs'][1]==64 and not c['flush'])
        self.assertEqual((ret['c'],ret['idbru'][1]),(5,16))
        self.assertEqual(t[3]['w2'],[1,1,16])
        self.assertEqual([t[-1]['regs'][n] for n in (1,5,6,7)],[16,7,0,9])
        # El validador existente exige N+3 también por distancia en el CFG y
        # rechaza ret inmediato, aunque el flush ya hace seguro ese caso RTL.
        # Se verifica su diagnóstico sin modificar el compilador; para BALB se
        # calendariza explícitamente una variante con dos NOP en la subrutina.
        fuente=self.carpeta/'llamada.asm'
        fuente.write_text(''.join(' | '.join((*fila,'nop'))+'\n' for fila in p))
        binario=self.carpeta/'llamada.bin'
        salida=self.ejecutar([ROOT/'balc','-x',fuente,'-o',binario],esperado=1)
        self.assertIn('dependencia RAW',salida+self.ultimo_stderr)
        q=[('nop','nop','sye x1,64'),('sumai x5,x0,7','nop','sye x0,128'),NOP,NOP,
           NOP,NOP,('nop','nop','ret'),('sumai x6,x0,99','guardap x0,x0,0','nop'),NOP,
           ('sumai x7,x0,9','nop','nop')]
        fuente.write_text(''.join(' | '.join((*fila,'nop'))+'\n' for fila in q))
        self.ejecutar([ROOT/'balc','-x',fuente,'-o',binario])
        a,_,_=self.contrastar(q)
        u,_,_=self.contrastar(q,balb=binario)
        self.assertEqual(a,u)

    def test_11_bucle_finito(self):
        # B3 decrementa el 3 inicial y B6 lee x2 con distancia N+3. Se esperan
        # dos retornos a PC48 y luego continuación por B7 al alcanzar cero.
        p=[('sumai x2,x0,3','nop','nop'),NOP,NOP,('restai x2,x2,1','nop','nop'),NOP,NOP,
           ('nop','nop','igualno x2,x0,-48'),('sumai x5,x0,9','nop','nop'),NOP]
        t,_,r=self.contrastar(p)
        decisiones=[c for c in t if c['stages'][2] and c['pcs'][2]==96]
        self.assertEqual([c['flush'] for c in decisiones],[1,1,0])
        self.assertEqual((r['estado'],t[-1]['regs'][2],t[-1]['regs'][5]),(1,0,9))
        self.assertGreater(t[-1]['cuentas'][0],len(p))

    def test_12_bucle_infinito_acotado_y_fin_busqueda(self):
        p=[('sumai x2,x2,1','guardap x0,x2,0','sye x31,0')]
        for fisica in (False,True):
            t,m,r=self.contrastar(p,presupuesto=40,fisica=fisica)
            self.assertEqual((r['estado'],r['ciclos']),(2,40))
            self.assertEqual((t[-1]['regs'][2],t[-1]['regs'][31],m[0]),(13,16,12))
            self.assertGreater(sum(c['flush'] for c in t),1)

    def test_13_saltos_consecutivos_y_ultimo_bundle(self):
        p=[('nop','nop','sye x31,48'),NOP,NOP,('nop','nop','sye x30,48'),NOP,NOP,
           ('sumai x5,x0,9','nop','nop')]
        t,_,_=self.contrastar(p)
        self.assertEqual(sum(c['flush'] for c in t),2)
        self.assertEqual([t[-1]['regs'][n] for n in (5,30,31)],[9,64,16])
        # El salto del último bundle se resuelve con fin_busqueda ya activo.
        # Debe volver a B0 y continuar con presupuesto, sin terminar por drenado.
        self.contrastar([NOP,('nop','nop','sye x31,-16')],presupuesto=35)

    def test_14_politica_fisica_fuera_del_archivo_y_wrap(self):
        # IMEM usa PC[13:4]. El salto relativo desde 0 a -16 llega a FFFFFFF0,
        # observa NOP físico en índice1023 y la suma secuencial vuelve a PC=0.
        for destino in (16,64,16384,-16):
            with self.subTest(destino=destino):
                p=[('nop','nop',f'sye x31,{destino}')]
                t,_,r=self.contrastar(p,presupuesto=20,fisica=True)
                self.assertEqual(r['estado'],2)
                self.assertEqual(t[2]['pc'],destino&0xffffffff)
        t,_,r=self.contrastar([('nop','nop','sye x31,16')])
        self.assertEqual((r['estado'],len(t),t[-1]['regs'][31]),(1,4,16))

    def test_15_reset_redirect_enlace_bucle_y_store(self):
        # Reset tras 1 paso precede EX; tras 2 coincide con store/redirect;
        # tras 3 descarta enlace pendiente y tras 4 limpia uno ya retirado.
        # Todos limpian el banco; solo el primer caso conserva el byte 80.
        datos=bytearray(65536);datos[0]=0x80
        p=[('sumai x5,x0,7','guardap x0,x0,0','sye x31,48'),NOP,NOP,NOP]
        for instante in (1,2,3,4):
            with self.subTest(reset=instante):
                t,m,r=self.contrastar(p,datos,presupuesto=30,reset=instante)
                evento=next(c for c in t if 'reset' in c)
                self.assertEqual(evento['regs'],[0]*32)
                self.assertEqual(evento['despues'],0 if instante>=2 else 0x80)
        self.contrastar([('sumai x2,x2,1','guardap x0,x2,0','sye x31,0')],presupuesto=40,fisica=True,reset=10)

    def test_16_syer_base_capturada_y_alineacion(self):
        # ALU del mismo bundle produce 0x43, pero syer captura la base anterior
        # (0) y vuelve a B0. En la siguiente iteración ya usa 0x43 -> destino40.
        p=[('sumai x2,x0,67','nop','syer x31,x2,0')]+[NOP]*4
        t,_,_=self.contrastar(p,presupuesto=30)
        targets=[c['target'] for c in t if c['flush']]
        self.assertEqual(targets,[0,64])
        self.assertEqual(t[-1]['regs'][31],16)
        for off,base,target in ((3,64,64),(-4,67,48),(-1,0,0xfffffff0)):
            pref=[(f'sumai x2,x0,{base}','nop','nop'),NOP,NOP]
            p=pref+[('nop','nop',f'syer x31,x2,{off}')]+[NOP]*5
            t,_,_=self.contrastar(p,presupuesto=25,fisica=True)
            primera=next(c for c in t if c['flush'])
            self.assertEqual(primera['target'],target)

    def test_17_programa_aleatorio_con_ramas(self):
        self.exigir_rtl()
        rng=random.Random(110843015)
        datos=bytearray(rng.getrandbits(8) for _ in range(65536))
        p=[]
        for n in range(24):
            a=f'sumai x{rng.randrange(32)},x{rng.randrange(32)},{rng.randrange(-1024,1024)}'
            l=f'{rng.choice(["guardap","guardab","cargai","cargabai"])} x{rng.randrange(32)},x{rng.randrange(32)},{rng.randrange(-1024,1024)}'
            target=rng.randrange(24)
            b=f'{rng.choice(["igualsi","igualno","menora","mayoroigual"])} x{rng.randrange(32)},x{rng.randrange(32)},{(target-n)*16}'
            p.append((a,l,b))
        self.contrastar(p,datos,presupuesto=160,fisica=True)


if __name__=='__main__':
    unittest.main()
