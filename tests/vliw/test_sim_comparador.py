"""B1: estados estrictos, métricas M1 y regresión de los oráculos M2/RTL."""
from pathlib import Path
import importlib.util
import json
import math
import os
import subprocess
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('oraculos_m2_b1',ROOT/'tests/vliw/test_sim_ejecucion.py')
m2=importlib.util.module_from_spec(spec);spec.loader.exec_module(m2)
NOP=m2.NOP


# Heredar los casos existentes mantiene referencias independientes CB/S2/S6/RTL;
# B1 agrega comparaciones sin reemplazar el simulador ni los resultados esperados.
class ComparadorTests(m2.EjecucionTests):
    # Compila las bibliotecas y sondas reales una vez para la suite y prepara los recursos
    # compartidos en directorios de prueba.
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        src=['src/Simulador_VLIW/sim_'+s+'.c' for s in ('comparador','ejecucion','metricas','pipeline','alu','lsu','bru','crypto','decode','cargador')]
        cls.b1,cls.unit,cls.fallos_b1=(cls.dir/n for n in ('comparar_vliw','unit-b1','fallos-b1'))
        cls.compilar(['tools/vliw/comparar_vliw.c']+src,cls.b1)
        cls.compilar(['tests/vliw/test_sim_comparador.c']+src,cls.unit)
        cls.compilar(['tests/vliw/test_sim_comparador.c']+src,cls.fallos_b1,
                     ['-DSIM_COMPARADOR_FALLOS']+['-Wl,--wrap='+s for s in
                     ('malloc','calloc','realloc','vsnprintf','__vsnprintf_chk','newlocale')])
        cls.b1_casos=[];cls.numero=0

    # Conserva la evidencia solicitada y libera los recursos temporales comunes de la suite.
    @classmethod
    def tearDownClass(cls):
        super().tearDownClass()
        e=dict(unitarias=getattr(cls,'c_unit',''),fallos=getattr(cls,'c_fallos',''),
               comparaciones=cls.b1_casos,comandos=cls.comandos,
               rtl_programas=cls.rtl_programas,rtl_ciclos=cls.rtl_ciclos)
        print('B1: '+json.dumps(dict(unitarias=e['unitarias'],fallos=e['fallos'],pares=len(cls.b1_casos))))
        if os.environ.get('B1_EVIDENCIA'):Path(os.environ['B1_EVIDENCIA']).write_text(json.dumps(e,indent=2)+'\n')

    # Construye la imagen de prueba a partir de sus palabras y datos iniciales; conserva el
    # orden físico de los slots.
    def imagen(self,asm,datos=None):
        # Las instrucciones proceden de CB aprobado. BALB se usa si hay datos
        # explícitos; el helper M2 arma su contenedor y S1 lo valida al cargar.
        mem=self.codificar(asm);archivo=self.balb(mem,datos) if datos is not None else mem
        type(self).numero+=1;n=self.dir/(str(self.numero)+archivo.suffix);n.write_bytes(archivo.read_bytes())
        return n

    # Ejecuta la comparación de los programas del caso y contrasta su conclusión y estado
    # público con los valores esperados.
    def comparar(self,a,b,esperado='EQUIVALENTE',presupuesto=200,modo='finita',oraculo=True):
        ta='balb' if a.suffix=='.balb' else 'mem';tb='balb' if b.suffix=='.balb' else 'mem'
        args=[ta,a,tb,b,modo,presupuesto]
        codigo={'EQUIVALENTE':0,'DIFERENTE':2,'INCONCLUSO':3,'ERROR':1}[esperado]
        salida=self.ejecutar([self.b1,*args,'json'],codigo);r=json.loads(salida)
        self.assertEqual(r['version'],1);self.assertEqual(r['resultado_comparacion'],esperado)
        self.assertEqual(self.ejecutar([self.b1,*args,'json'],codigo),salida)
        texto=self.ejecutar([self.b1,*args,'texto'],codigo)
        # El reporte textual debe conservar la conclusión del JSON sin exponer secretos
        # criptográficos. Cambiar de formato no amplía el estado público que B1 puede entregar.
        self.assertIn(esperado,texto)
        self.assertNotIn('password',salida);self.assertNotIn('"keys"',salida)
        # Cuando se pide el oráculo adicional, ejecutamos ambas imágenes por separado mediante
        # los ayudantes de M2 y contrastamos el resultado B1. No se usa el propio veredicto B1
        # como único esperado.
        if oraculo:
            estados=[];memorias=[]
            # M2 run conserva la comparación por flanco con S6. A partir de
            # esos resultados independientes se calcula solo el oráculo de
            # igualdad: GPR, bytes reales y estado público, nunca otra ejecución.
            for tipo,archivo in ((ta,a),(tb,b)):
                final=self.dir/'oraculo.bin'
                j=json.loads(self.ejecutar([self.bin,'run',tipo,archivo,modo,presupuesto,1,'json',final]))
                estados.append(j);memorias.append(final.read_bytes())
            for lado,j in zip(('referencia','candidato'),estados):
                v=r[lado]['metricas']
                self.assertEqual(v['ciclos'],j['ciclos']);self.assertEqual(v['bundles'],j['completados'])
                self.assertEqual(v['instrucciones'],j['instrucciones_utiles'])
                self.assertTrue(math.isclose(v['ipc'],j['ipc'],abs_tol=1.01e-9))
                self.assertEqual(v['estado_crypto'],j['arquitectura']['estado_crypto'])
                self.assertEqual(r[lado]['excepciones'],j['excepciones'])
            if esperado in ('EQUIVALENTE','DIFERENTE'):
                da=r['diferencias_arquitectonicas'];regs=[i for i,(x,y) in enumerate(zip(estados[0]['arquitectura']['registros'],estados[1]['arquitectura']['registros'])) if x!=y]
                diferentes=[i for i,(x,y) in enumerate(zip(*memorias)) if x!=y]
                self.assertTrue(da['comparadas']);self.assertEqual(da['registros'],len(regs));self.assertEqual(da['bytes'],len(diferentes))
                if regs:
                    self.assertEqual(da['primer_registro'],regs[0]);self.assertEqual(da['valores_registro'],[j['arquitectura']['registros'][regs[0]] for j in estados])
                if diferentes:
                    # El diagnóstico debe señalar la primera dirección de byte distinta y sus
                    # valores en ambos programas. Confundir dirección de byte con índice de
                    # palabra desplazaría la diferencia informada.
                    self.assertEqual(da['primer_byte'],diferentes[0]);self.assertEqual(da['valores_byte'],[m[diferentes[0]] for m in memorias])
                crypto=estados[0]['arquitectura']['estado_crypto']!=estados[1]['arquitectura']['estado_crypto']
                vectores=[[e['vector'] for e in j['excepciones']] for j in estados]
                self.assertEqual(da['crypto'],crypto);self.assertEqual(da['cantidad_traps'],len(vectores[0])!=len(vectores[1]))
                self.assertEqual(esperado=='EQUIVALENTE',not regs and not diferentes and not crypto and vectores[0]==vectores[1])
        type(self).b1_casos.append(r)
        if os.environ.get('B1_ARTEFACTOS'):
            d=Path(os.environ['B1_ARTEFACTOS']);d.mkdir(exist_ok=True);numero=len(self.b1_casos)
            (d/f'{numero}.json').write_text(salida);(d/f'{numero}.txt').write_text(texto)
        return r

    # Ejecuta las pruebas C de comparación y fallos de reserva o generación.
    def test_20_c_y_reservas(self):
        nop=self.imagen(NOP);trap=self.imagen('nop | nop | nop | fsl x4,x2,0,0\n')
        palabra=trap.read_text().strip();trap.write_text('\n'.join([palabra]+['0'*32]*1022+[palabra])+'\n')
        type(self).c_unit=self.ejecutar([self.unit,nop,trap]).strip()
        type(self).c_fallos=self.ejecutar([self.fallos_b1,nop,trap]).strip()
        self.assertIn('PASS',self.c_unit);self.assertIn('PASS',self.c_fallos)

    # Compara programas idénticos y con NOP y comprueba los límites del contrato público
    # estricto.
    def test_21_identico_y_nops(self):
        a=self.imagen('sumai x6,x0,18 | nop | nop | nop\n');b=self.imagen('sumai x6,x0,18 | nop | nop | nop\n'+NOP*3)
        r=self.comparar(a,a);self.assertEqual(r['diferencias_metricas']['ciclos']['magnitud'],0)
        r=self.comparar(a,b);self.assertNotEqual(r['referencia']['metricas']['pc_diagnostico'],r['candidato']['metricas']['pc_diagnostico'])
        self.assertEqual(r['diferencias_metricas']['ciclos'],dict(negativa=False,magnitud=3))
        self.assertEqual(r['diferencias_metricas']['instrucciones']['magnitud'],0)

    # Compara una calendarización RAW producida por S7 con su referencia real.
    def test_22_raw_s7_real(self):
        # Las dos distribuciones se obtienen de los fixtures S7/S8-A originales.
        # No se calendariza en B1: solo se consume la imagen generada por ellos.
        op=m2.m1.s8.s7.op
        ins=[op(escribe=[5],texto='sumai x5,x0,7'),op(1,escribe=[8],efectos=1,texto='cargai x8,x0,0'),op(lee=[5,8],escribe=[6],texto='suma x6,x5,x8')]
        lineas=['3 2 1 0']+[f'{100+3*i} {a["slot"]} {a["lee"]:x} {a["escribe"]:x} {a["efectos"]} {a["control"]} {a["bloque"]}' for i,a in enumerate(ins)]
        lineas+=['0 2 0 3','1 2 0 3','0 3 0']+[a['texto'] for a in ins]
        entrada,asm=self.dir/'plan-b1.txt',self.dir/'plan-b1.asm';entrada.write_text('\n'.join(lineas)+'\n')
        self.assertEqual(json.loads(self.ejecutar([self.s7,'fixture',entrada,asm]))['estado'],0)
        conservador=m2.m1.s8.s7.SchedulerTests.renderizar(None,ins,[(0,3,[])])
        datos=bytearray(16);datos[0]=11
        a=self.imagen(conservador,datos);b=self.imagen(asm.read_text(),datos);r=self.comparar(a,b)
        self.assertEqual([r[x]['metricas']['ciclos'] for x in ('referencia','candidato')],[12,7])
        self.assertEqual(r['diferencias_metricas']['ciclos'],dict(negativa=True,magnitud=5))
        self.assertEqual([r[x]['metricas']['instrucciones'] for x in ('referencia','candidato')],[3,3])
        self.assertTrue(math.isclose(r['candidato']['metricas']['ipc'],3/7))
        self.assertTrue(math.isclose(r['diferencias_metricas']['porcentaje_ciclos'],-100*5/12))

    # Cambia registros intermedios y comprueba que B1 mantenga la comparación estricta de los 32
    # GPR.
    def test_23_registros_estrictos(self):
        # Se incluyen enlace y temporales: no existe una excepción oculta por
        # convención del compilador. x0 se conserva cero mediante S6.
        for i in range(1,32):
            a=self.imagen(f'sumai x{i},x0,18 | nop | nop | nop\n');b=self.imagen(f'sumai x{i},x0,19 | nop | nop | nop\n')
            r=self.comparar(a,b,'DIFERENTE');self.assertEqual(r['diferencias_arquitectonicas']['primer_registro'],i)

    # Introduce diferencias DMEM, incluido el último byte, para exigir comparación completa de
    # memoria.
    def test_24_memoria_y_ultimo_byte(self):
        # Los GPR finales coinciden; solo cambian bytes publicados por stores.
        # Se fuerza también la última palabra para comprobar el límite DMEM.
        datos=bytearray(16384);datos[0]=0x12;datos[1]=0x34
        for destino in (4,-4):
            base='nop | cargai x5,x0,0 | nop | nop\n'+NOP*2
            a=self.imagen(base+f'nop | guardap x0,x5,{destino} | nop | nop\n'+'sumai x5,x0,0 | nop | nop | nop\n',datos)
            b=self.imagen(NOP,datos);r=self.comparar(a,b,'DIFERENTE')
            self.assertEqual(r['diferencias_arquitectonicas']['primer_byte'],4 if destino==4 else 65532)
            self.assertEqual(r['diferencias_arquitectonicas']['bytes'],2)

        # Si solo el byte alto de la última palabra cambia, la primera
        # dirección distinta debe ser 65535, nunca 65532 ni un índice de palabra.
        datos=bytearray(8);datos[3]=0x42
        base='nop | cargai x5,x0,0 | nop | nop\n'+NOP*2
        a=self.imagen(base+'nop | guardap x0,x5,-4 | nop | nop\n'+'sumai x5,x0,0 | nop | nop | nop\n',datos)
        r=self.comparar(a,self.imagen(NOP,datos),'DIFERENTE')
        # La diferencia se coloca en el último byte de DMEM para detectar comparaciones
        # limitadas al código cargado, a una sección de datos o a un prefijo de memoria.
        self.assertEqual(r['diferencias_arquitectonicas']['primer_byte'],65535)
        self.assertEqual(r['diferencias_arquitectonicas']['bytes'],1)

    # Cambia el layout de saltos y comprueba su efecto en el estado público comparado.
    def test_25_saltos_layout(self):
        a=self.imagen('nop | nop | igualsi x0,x0,fin | nop\nsumai x5,x0,1 | nop | nop | nop\nfin: sumai x6,x0,9 | nop | nop | nop\n')
        b=self.imagen(NOP+'nop | nop | igualsi x0,x0,fin | nop\nsumai x5,x0,1 | nop | nop | nop\n'+NOP+'fin: sumai x6,x0,9 | nop | nop | nop\n')
        self.comparar(a,b)

    # Compara formatos y condiciones iniciales y rechaza una base de ejecución incompatible.
    def test_26_iniciales_y_formatos(self):
        datos=bytearray(8);datos[4]=1;a=self.imagen(NOP);b=self.imagen(NOP,datos)
        r=self.comparar(a,b,'INCONCLUSO',oraculo=False)
        self.assertFalse(r['iniciales_compatibles']);self.assertFalse(r['referencia']['ejecutada'])
        self.assertEqual((r['bytes_iniciales_distintos'],r['primer_byte_inicial']),(1,4))
        self.assertIsNone(r['diferencias_metricas'])
        b=self.imagen(NOP,bytes(8));self.comparar(a,b)

    # Agota los presupuestos y exige INCONCLUSO en lugar de equivalencia no demostrada.
    def test_27_limites(self):
        a=self.imagen(NOP);b=self.imagen(NOP*5)
        r=self.comparar(a,a,'INCONCLUSO',presupuesto=0)
        # Una comparación con presupuesto agotado no demuestra igualdad final. Puede conservar
        # métricas diagnósticas, pero no debe presentarse como equivalente ni calcular un
        # porcentaje con base cero.
        self.assertFalse(r['diferencias_metricas']['porcentaje_definido'])
        self.assertIsNone(r['diferencias_metricas']['porcentaje_ciclos'])
        self.comparar(a,a,'INCONCLUSO',presupuesto=2)
        r=self.comparar(a,b,'INCONCLUSO',presupuesto=4)
        self.assertEqual((r['referencia']['estado'],r['candidato']['estado']),(1,2))
        self.comparar(a,b,'INCONCLUSO',presupuesto=30,modo='fisica')
        self.comparar(a,a,presupuesto=18446744073709551615)
        bucle=self.imagen('bucle: nop | nop | sye x0,bucle | nop\n')
        r=self.comparar(a,bucle,'INCONCLUSO',presupuesto=20)
        self.assertFalse(r['diferencias_arquitectonicas']['comparadas'])

    # Compara historiales de excepciones y verifica que formen parte del contrato.
    def test_28_excepciones(self):
        trampa=self.imagen('nop | nop | nop | fsl x4,x2,0,0\n');palabra=trampa.read_text().strip()
        a=self.imagen(NOP);a.write_text('\n'.join([palabra]+['0'*32]*1023)+'\n')
        b=self.imagen(NOP);b.write_text('\n'.join(['0'*32,palabra]+['0'*32]*1022)+'\n')
        r=self.comparar(a,b)
        self.assertNotEqual(r['referencia']['excepciones'][0]['ciclo'],r['candidato']['excepciones'][0]['ciclo'])
        self.assertEqual([x['vector'] for x in r['referencia']['excepciones']],[0x3ff0])
        self.comparar(a,self.imagen(NOP),'DIFERENTE')
        # Solo hay un vector arquitectónico en S6; un par legal no puede
        # producir vectores distintos. Esa rama se documenta como revisada.

    # Contrasta el estado criptográfico público sin acceder a la bóveda privada.
    def test_29_crypto_publico(self):
        self.comparar(self.imagen(NOP),self.imagen('nop | nop | nop | setpwd x0\n'),'DIFERENTE')

    # Entrega argumentos y archivos inválidos y distingue errores de diferencias
    # arquitectónicas.
    def test_30_errores_y_argumentos(self):
        a=self.imagen(NOP);invalido=self.dir/'corrupto.balb';invalido.write_bytes(b'BALB')
        self.comparar(a,invalido,'ERROR',oraculo=False)
        self.comparar(self.dir/'no-existe.mem',a,'ERROR',oraculo=False)
        for v in ('-1','+1','',' 1','1x','18446744073709551616'):
            self.assertEqual(self.ejecutar([self.b1,'mem',a,'mem',a,'finita',v,'json'],64),'')
        for args in ([],['otro',a,'mem',a,'finita',10,'json'],['mem',a,'mem',a,'otro',10,'json'],['mem',a,'mem',a,'finita',10,'xml']):
            self.assertEqual(self.ejecutar([self.b1,*args],64),'')
        # La BRU es legal, pero truncar la extensión deja un PC fuera del
        # archivo finito. B1 debe devolver ERROR y conservar la causa M2.
        b=self.imagen('nop | nop | sye x0,fin | nop\n'+NOP+'fin: nop\n');b.write_text(b.read_text().splitlines()[0]+'\n')
        r=self.comparar(a,b,'ERROR',oraculo=False);self.assertEqual(r['error']['lado'],1)

    # Comprueba el JSON exportado y el manejo de fallos al entregar la salida.
    def test_31_json_y_salida(self):
        # Una ALU dirigida a x0 cuenta como instrucción útil, pero no cambia
        # el resultado público: rendimiento y equivalencia siguen separados.
        self.comparar(self.imagen(NOP),self.imagen('sumai x0,x0,1 | nop | nop | nop\n'))
        a=self.imagen(NOP);especial=self.dir/'nombre"\\\n.mem';especial.write_bytes(a.read_bytes());self.comparar(especial,a)
        unicode_=self.dir/'candidato-á-😀.mem';unicode_.write_bytes(a.read_bytes())
        r=self.comparar(unicode_,a);self.assertEqual(r['referencia']['ruta'],str(unicode_))
        with open('/dev/full','wb') as f:
            p=subprocess.run(list(map(str,[self.b1,'mem',a,'mem',a,'finita',10,'json'])),stdout=f,stderr=subprocess.PIPE)
        # La comparación puede haber terminado correctamente y aun así fallar la entrega del
        # reporte. /dev/full permite verificar que ese error de salida no se anuncie como éxito.
        self.assertEqual(p.returncode,1);self.assertIn(b'error al escribir',p.stderr)


if __name__=='__main__':
    unittest.main()
