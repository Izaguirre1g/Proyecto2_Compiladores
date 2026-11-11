"""Verifica cortes B5 mediante programas reales, efectos pendientes y M2 independiente."""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
# La dependencia compartida conserva su dueño y su API; solo resolvemos
# su ubicación plana o por módulos según el árbol que contiene esta prueba.
sys.path.insert(0, str(ROOT / 'tools/vliw'))
from rutas_vliw import fuente_compartida
spec=importlib.util.spec_from_file_location('b5',ROOT/'tools/vliw/analizar_corte_bal.py')
b5=importlib.util.module_from_spec(spec);spec.loader.exec_module(b5)


class CorteBalTests(unittest.TestCase):
    # Compila las bibliotecas y sondas reales una vez para la suite y prepara los recursos
    # compartidos en directorios de prueba.
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='pruebas-b5-');cls.addClassCleanup(cls.tmp.cleanup)
        cls.d=Path(os.environ.get('B5_ARTEFACTOS',cls.tmp.name));cls.d.mkdir(parents=True,exist_ok=True)
        cls.comandos=[];cls.fixtures=[]
        # Solo la copia se compila. El ejecutable se reutiliza para fuentes
        # reales y fixtures ASM; B5 no integra una nueva ruta de compilación.
        if os.environ.get('B5_BALC'):cls.balc=Path(os.environ['B5_BALC']).resolve()
        else:
            copia=cls.d/'compilador';shutil.copytree(ROOT,copia,ignore=shutil.ignore_patterns('.git','__pycache__'))
            cls.run_cmd(['make','-B','balc'],cwd=copia);cls.balc=copia/'balc'
        cls.b4,cls.sonda,cls.fallos=(cls.d/n for n in ('sonda-fin','sonda-corte','sonda-fallos'))
        # Los ejecutables de prueba se reconstruyen en el temporal con el compilador y flags
        # seleccionados. Las variantes de inyección se enlazan aparte: permiten provocar fallos
        # sin cambiar las bibliotecas bajo prueba.
        cc=shlex.split(os.environ.get('CC','cc'))
        flags=['-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-O2','-Iinclude']
        comunes=['src/Simulador_VLIW/sim_'+s+'.c' for s in ('ejecucion','metricas','pipeline','alu','lsu','bru','crypto','decode','cargador')]
        cls.run_cmd(cc+flags+['tools/vliw/sonda_fin_bal.c',fuente_compartida(ROOT, 'src/codificacion_binaria.c')]+comunes+['-o',cls.b4])
        cls.run_cmd(cc+flags+['tools/vliw/sonda_corte_bal.c']+comunes+['-o',cls.sonda])
        # Las envolturas son exclusivamente de prueba: fuerzan fallos en APIs
        # públicas y reservas, sin editar M1/M2 o alterar su estado privado.
        w=cls.d/'fallos.c';w.write_text('''#include "sim_ejecucion.h"
#include <stdlib.h>
#include <string.h>
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n){static unsigned c;const char *f=getenv("B5_MALLOC");if(f && ++c==strtoul(f,NULL,10))return NULL;return __real_malloc(n);}
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t k){static unsigned c;const char *f=getenv("B5_CALLOC");if(f && ++c==strtoul(f,NULL,10))return NULL;return __real_calloc(n,k);}
void *__real_realloc(void *,size_t);
void *__wrap_realloc(void *p,size_t n){static unsigned c;const char *f=getenv("B5_REALLOC");if(f && ++c==strtoul(f,NULL,10))return NULL;return __real_realloc(p,n);}
SimMetricasEstado __real_sim_metricas_observar(SimMetricas *,const SimPipeline *);
SimMetricasEstado __wrap_sim_metricas_observar(SimMetricas *m,const SimPipeline *s){
 static unsigned c;const char *f=getenv("B5_OBSERVAR");
 if(f && !strcmp(f,"siempre"))return SIM_METRICAS_MEMORIA;
 if(f && !strcmp(f,"fatal"))return SIM_METRICAS_INCONSISTENCIA;
 if(f && !strcmp(f,"una") && !c++)return SIM_METRICAS_MEMORIA;
 return __real_sim_metricas_observar(m,s);
}
SimEjecucionCodigo __real_sim_ejecucion_memoria(const SimEjecucion *,uint8_t *,size_t);
SimEjecucionCodigo __wrap_sim_ejecucion_memoria(const SimEjecucion *s,uint8_t *p,size_t n){
 SimEjecucionCodigo c=__real_sim_ejecucion_memoria(s,p,n);
 if(c==SIM_EJECUCION_OK && getenv("B5_DIFERENCIA"))p[n-1]^=1;
 return c;
}
''')
        cls.run_cmd(cc+flags+['tools/vliw/sonda_corte_bal.c',w]+comunes+
                    ['-Wl,--wrap=malloc','-Wl,--wrap=calloc','-Wl,--wrap=realloc',
                     '-Wl,--wrap=sim_metricas_observar','-Wl,--wrap=sim_ejecucion_memoria','-o',cls.fallos])
        cls.reales_dir=cls.d/'reales'
        args=[sys.executable,ROOT/'tools/vliw/analizar_corte_bal.py','--balc',cls.balc,'--sonda',cls.sonda,
              '--sonda-fin',cls.b4,'--presupuesto','10000','--salida',cls.reales_dir]
        for n in ('factorial','whale','ejercicio3_puntajes'):args+=['--fuente',ROOT/'examples'/(n+'.bal')]
        cls.run_cmd(args);cls.reales=json.loads((cls.reales_dir/'reporte.json').read_text())

    # Ejecuta los argumentos sin shell, conserva la salida y comprueba el código esperado; un
    # fallo inesperado invalida la prueba.
    @classmethod
    def run_cmd(cls,args,codigo=0,cwd=ROOT,env=None):
        p=subprocess.run(list(map(str,args)),cwd=cwd,capture_output=True,timeout=120,
                         env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',**(env or {})))
        cls.comandos.append(dict(comando=list(map(str,args)),cwd=str(cwd),codigo=p.returncode,
              stdout=p.stdout.decode(errors='backslashreplace'),stderr=p.stderr.decode(errors='backslashreplace')))
        if codigo is not None and p.returncode!=codigo:raise AssertionError(f'{args}: {p.returncode} != {codigo}: {p.stderr!r}')
        return p

    # Conserva la evidencia solicitada y libera los recursos temporales comunes de la suite.
    @classmethod
    def tearDownClass(cls):
        if os.environ.get('B5_EVIDENCIA'):
            Path(os.environ['B5_EVIDENCIA']).write_text(json.dumps(dict(comandos=cls.comandos,fixtures=cls.fixtures,reales=cls.reales),indent=2)+'\n')

    # Prepara archivos y directorios exclusivos del caso para que sus fallos o resultados no
    # contaminen las otras pruebas.
    def setUp(self):
        t=tempfile.TemporaryDirectory(prefix='caso-',dir=self.d);self.addCleanup(t.cleanup);self.c=Path(t.name)
        (self.c/'fuentes').mkdir();self.fuente=self.c/'fuentes/demo.bal'
        self.fuente.write_text('create_funk #declare_infinite_void# main() {}\n')
        self.numero=0

    # Construye los argumentos del análisis para este caso y permite sustituir componentes por
    # fallos controlados.
    def config(self,**cambios):
        a=argparse.Namespace(balc=self.balc,sonda=self.sonda,sonda_fin=self.b4,fuente=[self.fuente],
                             salida=self.c/'salida',presupuesto=1000,timeout=10)
        for k,v in cambios.items():setattr(a,k,v)
        return a

    # Ensambla un programa temporal con llamada, retorno y continuación controlados para probar
    # las garantías del corte.
    def construir(self,cuerpo='nop | nop | syer x0,x1,0 | nop\n',gap=2,completo=None):
        self.numero+=1
        d=self.c/str(self.numero);d.mkdir();a=d/'demo.asm';bin_=d/'demo.bin'
        # El fixture se ensambla con balc -x. Estos programas temporales son
        # demostraciones de control/pendientes, no optimizaciones oficiales.
        # Construimos un arranque que carga el destino en x13, espera N+3 y llama con SYER
        # x1,x13,0. gap controla los NOP de continuación: permite probar distintas ocupaciones
        # sin cambiar el contrato del retorno.
        texto=completo if completo is not None else (
            f'sumai x13,x0,{(5+gap)*16} | nop | nop | nop\nnop\nnop\n'
            'nop | nop | syer x1,x13,0 | nop\n'+'nop\n'*gap+
            '# __arranque_fin:\nnop | nop | sye x0,0 | nop\n# __fn_main:\n'+cuerpo+'nop\nnop\n')
        a.write_text(texto);self.run_cmd([self.balc,'-x','-o',bin_,a])
        sent=b5.b4.localizar(texto,'__arranque_fin');main=b5.b4.localizar(texto,'__fn_main')
        # Primero pedimos a B4 su evidencia independiente. Los argumentos B5 provienen de esos
        # eventos y PC verificados, no de contar líneas ni de asumir que toda instrucción SYER
        # es el retorno inicial.
        p=self.run_cmd([self.b4,a,bin_,sent['linea'] if sent else 0,main['linea'] if main else 0,1000])
        base=json.loads(p.stdout);ll=base['llamada_inicial_observada']
        args=[bin_,str(base['direccion_sentinela']) if base['patron_valido'] else '-',
              str(ll['ex_pc']) if ll else '-',str(ll['destino']) if ll else '-']
        self.fixtures.append(dict(texto=texto,b4=base))
        return d,args,base

    # Ejecuta la sonda del caso y lee su protocolo para comprobar hechos dinámicos y condiciones
    # de aceptación.
    def observar(self,cuerpo='nop | nop | syer x0,x1,0 | nop\n',gap=2,completo=None,presupuesto=1000,env=None,sonda=None):
        d,args,base=self.construir(cuerpo,gap,completo)
        memoria=d/'corte.dmem'
        p=self.run_cmd([sonda or self.sonda,*args,presupuesto,memoria],env=env)
        r=json.loads(p.stdout);b5.validar(r,presupuesto,memoria)
        self.fixtures[-1].update(b5=r)
        return r,memoria,base

    # Exige todas las garantías de una captura positiva, incluidos retiro, métricas, estado y
    # comparación M2.
    def comprobar_positivo(self,r,memoria):
        self.assertEqual(r['conclusion'],'CORTE_ARQUITECTONICO_CANDIDATO')
        e=r['evidencia_pipeline'][-1];ret=r['retorno_candidato'];p=r['punto_corte'];m=r['metricas']
        # Resolver el retorno en EX aún deja trabajo en vuelo. El corte exige su retiro en WB y
        # una continuación segura, con M1 al mismo flanco y sin efectos anteriores pendientes.
        self.assertTrue(e['retorno_retirado_wb'] and e['ruta_continuacion'])
        self.assertTrue(p['flanco_aceptado'] and p['observado_m1'])
        self.assertGreater(p['ciclo'],ret['ciclo']);self.assertIsNone(r['primer_bucle_final'])
        self.assertEqual(m['ciclos'],p['ciclo']);self.assertEqual(m['traps'],0)
        self.assertFalse(any(e[k] for k in ('wb_gpr_pendiente','wb_crypto_pendiente','ex_lsu_pendiente','ex_crypto_pendiente')))
        self.assertTrue(all(r['comparacion_m2'].values()));self.assertEqual(len(memoria.read_bytes()),65536)
        self.assertEqual(len(m['arquitectura']['registros']),32);self.assertFalse(r['retorno_main_certificado'])

    # Rechaza argumentos incompletos de B5 sin publicar una captura.
    def test_01_argumentos(self):
        self.assertEqual(b5.main([]),64);self.run_cmd([self.sonda],codigo=64)

    # Comprueba que las rutas inexistentes produzcan error antes de ejecutar herramientas.
    def test_02_inexistentes(self):
        for k in ('balc','sonda','sonda_fin'):
            with self.assertRaises(OSError):b5.analizar(self.config(**{k:self.c/'ausente'}))
        with self.assertRaises(OSError):b5.analizar(self.config(fuente=[self.c/'ausente.bal']))

    # Ejecuta factorial, whale y puntajes y contrasta sus cortes históricos, estado ACTIVO y
    # garantías M2.
    def test_03_programas_reales(self):
        # Los horizontes de referencia de factorial, whale y puntajes son 3403, 247 y 2748
        # flancos. Contrastamos además que precedan al salto final B4 y mantengan ACTIVO, para
        # detectar cortes adelantados o terminaciones artificiales.
        esperados=(3403,247,2748)
        for c,n in zip(self.reales['casos'],esperados):
            self.assertEqual(c['conclusion'],'CORTE_ARQUITECTONICO_CANDIDATO')
            self.assertEqual(c['punto_corte']['ciclo'],n)
            self.assertLess(n,c['primer_bucle_final']['evento']['ciclo'])
            self.assertTrue(all(c['comparacion_m2'].values()))
            self.assertEqual(c['metricas']['estado'],'ACTIVO')
            self.assertEqual(c['evidencia_pipeline'][-1]['if_id']['operaciones'],['nop']*4)

    # Retira la sentinela del programa y exige que no se declare un corte demostrado.
    def test_04_sin_sentinela(self):
        r,_,_=self.observar(completo='nop\n')
        self.assertEqual(r['conclusion'],'CORTE_NO_DEMOSTRADO');self.assertIsNone(r['punto_corte'])

    # Altera el patrón final y comprueba que una sentinela malformada no legitime la captura.
    def test_05_sentinela_malformada(self):
        r,_,_=self.observar(completo='# __arranque_fin:\nnop\n')
        self.assertEqual(r['conclusion'],'CORTE_NO_DEMOSTRADO')

    # Ejecuta whale con un retorno no demostrado y exige conservar esa limitación.
    def test_06_whale_no_retornado(self):
        self.fuente.write_text('create_funk #declare_infinite_void# main() { whale (1 == 1) {} stop; }\n')
        c=b5.analizar(self.config())['casos'][0]
        self.assertEqual(c['conclusion'],'PRESUPUESTO_AGOTADO');self.assertIsNone(c['punto_corte'])

    # Comprueba que un cycle que no retorna no produzca una captura positiva.
    def test_07_cycle_no_retornado(self):
        self.fuente.write_text('create_funk #declare_infinite_void# main() { cycle i let 0 until 10000 {} endgame; }\n')
        c=b5.analizar(self.config())['casos'][0]
        self.assertEqual(c['conclusion'],'PRESUPUESTO_AGOTADO');self.assertIsNone(c['retorno_candidato'])

    # Entra por una ruta inesperada y comprueba que se rechace la continuación supuestamente
    # segura.
    def test_08_entrada_inesperada(self):
        r,_,_=self.observar(completo='nop | nop | sye x0,fin | nop\nnop\nnop\nfin:\n# __arranque_fin:\nnop | nop | sye x0,0 | nop\n')
        self.assertEqual(r['conclusion'],'CORTE_NO_DEMOSTRADO');self.assertIsNone(r['retorno_candidato'])

    # Cambia el destino del retorno y exige que coincida con el enlace inicial para aceptar el
    # corte.
    def test_09_destino_incorrecto(self):
        r,_,_=self.observar('nop | nop | syer x0,x0,64 | nop\n')
        self.assertIsNone(r['retorno_candidato']);self.assertIsNone(r['punto_corte'])

    # Ejecuta llamadas anidadas reales y verifica que no se confunda un retorno interno con el
    # candidato de arranque.
    def test_10_anidamiento_real(self):
        c=self.reales['casos'][2]
        self.assertEqual(c['punto_corte']['ciclo'],2748)
        self.assertEqual(c['retorno_candidato']['ex_pc'],12768)
        self.assertFalse(c['retorno_main_certificado'])

    # Deja una escritura GPR pendiente y comprueba que B5 espere su publicación antes del corte.
    def test_11_registro_pendiente(self):
        # ALU del mismo bundle ret publica en WB posterior, no en el salto.
        r,mem,_=self.observar('sumai x14,x0,77 | nop | syer x0,x1,0 | nop\n')
        # La sonda debe esperar la escritura posterior en WB. El primer registro de evidencia
        # todavía muestra el efecto pendiente; el snapshot final debe incluir x14=77 y mantener
        # el retiro del retorno demostrado.
        self.comprobar_positivo(r,mem)
        self.assertTrue(r['evidencia_pipeline'][0]['wb_gpr_pendiente'])
        self.assertFalse(r['evidencia_pipeline'][0]['retorno_retirado_wb'])
        self.assertEqual(r['metricas']['arquitectura']['registros'][14],77)

    # Verifica que un store aplicado en EX esté incluido en la DMEM capturada, sin esperar un WB
    # ficticio.
    def test_12_store_publicado_en_ex(self):
        r,mem,_=self.observar('sumai x5,x0,77 | nop | nop | nop\nnop\nnop\nnop | guardap x0,x5,4 | syer x0,x1,0 | nop\n')
        self.comprobar_positivo(r,mem)
        self.assertEqual(struct.unpack_from('<I',mem.read_bytes(),4)[0],77)
        self.assertEqual(r['metricas']['lsu_escrituras_ex'],1)

    # Comprueba que una carga pendiente llegue a WB y publique su registro antes de aceptar la
    # captura.
    def test_13_carga_pendiente_wb(self):
        r,mem,_=self.observar('nop | cargai x4,x0,0 | syer x0,x1,0 | nop\n')
        self.comprobar_positivo(r,mem)
        self.assertTrue(r['evidencia_pipeline'][0]['wb_gpr_pendiente'])
        self.assertEqual(r['metricas']['lsu_lecturas_ex'],1)

    # Mantiene efectos CRIPTO pendientes y exige que el drenado los tenga en cuenta.
    def test_14_crypto_pendiente(self):
        r,mem,_=self.observar('nop | nop | syer x0,x1,0 | setpwd x0\n')
        self.comprobar_positivo(r,mem)
        self.assertTrue(r['evidencia_pipeline'][0]['wb_crypto_pendiente'])
        self.assertEqual(r['metricas']['arquitectura']['estado_crypto'],0)

    # Provoca una excepción y comprueba que impida aceptar un corte, aunque exista un retorno
    # candidato.
    def test_15_excepcion(self):
        r,mem,_=self.observar('nop | nop | syer x0,x1,0 | ell 0,0,x0,x0\n')
        self.assertEqual(r['conclusion'],'ERROR')
        self.assertTrue(r['excepciones_observadas']);self.assertFalse(mem.exists())

    # Agota el presupuesto antes del punto seguro y exige ausencia de captura positiva.
    def test_16_presupuesto_insuficiente(self):
        for n in (0,1,2,8,9):
            r,mem,_=self.observar(presupuesto=n)
            self.assertEqual(r['conclusion'],'PRESUPUESTO_AGOTADO');self.assertIsNone(r['punto_corte']);self.assertFalse(mem.exists())

    # Coloca instrucciones en el camino especulativo y verifica que el flush las descarte sin
    # efectos capturados.
    def test_17_especulativas_descartadas(self):
        # Estas instrucciones están detrás del ret y el flush debe impedir
        # sus efectos; la prueba inspecciona GPR/DMEM, no solo la clasificación.
        r,mem,_=self.observar('nop | nop | syer x0,x1,0 | nop\nsumai x18,x0,99 | nop | nop | nop\nnop | guardap x0,x1,8 | nop | nop\n')
        self.comprobar_positivo(r,mem)
        self.assertEqual(r['metricas']['arquitectura']['registros'][18],0)
        self.assertEqual(mem.read_bytes()[8:12],bytes(4))
        self.assertGreaterEqual(r['metricas']['anulados_salto'],1)

    # Contrasta ocupaciones de etapas diferentes sin exigir que el pipeline quede
    # artificialmente vacío.
    def test_18_ocupaciones_diferentes(self):
        for gap in (0,1,2,3):
            r,mem,base=self.observar(gap=gap);self.comprobar_positivo(r,mem)
            op=r['evidencia_pipeline'][-1]['if_id']['operaciones']
            self.assertEqual(op,['nop','nop','sye','nop'] if gap==0 else ['nop']*4)
            self.assertLess(r['punto_corte']['ciclo'],base['bucle_final_observado']['ciclo'])

    # Inyecta un fallo persistente de memoria en M1 y comprueba que no se publique un flanco no
    # observado.
    def test_19_m1_memoria_persistente(self):
        r,mem,_=self.observar(sonda=self.fallos,env={'B5_OBSERVAR':'siempre'})
        self.assertEqual(r['conclusion'],'ERROR');self.assertIsNone(r['metricas']);self.assertFalse(mem.exists())

    # Inyecta un fallo recuperable de observación M1 y comprueba que el reintento no duplique
    # métricas.
    def test_20_m1_reintento(self):
        r,mem,_=self.observar(sonda=self.fallos,env={'B5_OBSERVAR':'una'})
        self.comprobar_positivo(r,mem)

    # Provoca un error fatal M1 y exige que B5 no lo trate como una observación recuperable.
    def test_21_m1_fatal(self):
        r,mem,_=self.observar(sonda=self.fallos,env={'B5_OBSERVAR':'fatal'})
        self.assertEqual(r['conclusion'],'ERROR');self.assertFalse(mem.exists())

    # Carga entradas inválidas y BALB corrupto y verifica que el fallo no publique una captura.
    def test_22_carga_y_balb_corrupto(self):
        d,args,_=self.construir();Path(args[0]).write_bytes(b'BALB')
        self.run_cmd([self.sonda,*args,100,d/'corte.dmem'],codigo=1)
        Path(args[0]).unlink();self.run_cmd([self.sonda,*args,100,d/'corte.dmem'],codigo=1)

    # Fuerza fallos de reserva en distintos puntos y comprueba la limpieza sin resultados
    # parciales válidos.
    def test_23_fallos_memoria(self):
        d,args,_=self.construir()
        for familia in ('B5_CALLOC','B5_REALLOC'):
            fallaron=0
            for n in range(1,10):
                p=self.run_cmd([self.fallos,*args,100,d/(familia+str(n)+'.dmem')],codigo=None,env={familia:str(n)})
                self.assertIn(p.returncode,(0,1));fallaron+=p.returncode==1
            self.assertGreater(fallaron,0)

    # Modifica el último byte de la copia M2 y exige detectar la diferencia de DMEM completa.
    def test_24_diferencia_m2(self):
        r,mem,_=self.observar(sonda=self.fallos,env={'B5_DIFERENCIA':'1'})
        self.assertEqual(r['conclusion'],'ERROR');self.assertFalse(r['comparacion_m2']['memoria_completa_igual'])
        self.assertFalse(mem.exists());self.assertIsNone(r['punto_corte'])
        self.assertTrue(r['comparacion_m2']['metricas_iguales'])

    # Comprueba que un efecto pendiente no quede oculto detrás de una conclusión positiva.
    def test_25_pendiente_no_oculto(self):
        d,args,_=self.construir('sumai x14,x0,77 | nop | syer x0,x1,0 | nop\n')
        ret=json.loads(self.run_cmd([self.sonda,*args,100,d/'primero.dmem']).stdout)['retorno_candidato']['ciclo']
        mem=d/'temprano.dmem'
        r=json.loads(self.run_cmd([self.sonda,*args,ret,mem]).stdout)
        self.assertEqual(r['conclusion'],'PRESUPUESTO_AGOTADO');self.assertFalse(mem.exists())
        self.assertTrue(r['evidencia_pipeline'][-1]['wb_gpr_pendiente'])

    # Conserva los oráculos B3/B4 anteriores y verifica que el análisis de corte no altere sus
    # conclusiones.
    def test_26_oraculos_previos(self):
        for c,esp in zip(self.reales['casos'],({65528:120,65532:6},None,{65520:24,65524:0,65528:13,65532:37})):
            if esp:
                datos=(self.reales_dir/c['snapshot_arquitectonico']['dmem']['ruta']).read_bytes()
                for direccion,valor in esp.items():self.assertEqual(struct.unpack_from('<I',datos,direccion)[0],valor)

    # Compara identidades de los archivos protegidos antes y después de ejecutar B5.
    def test_27_archivos_protegidos(self):
        archivos=[ROOT/'src/Simulador_VLIW/sim_pipeline.c',ROOT/'src/Simulador_VLIW/sim_metricas.c',ROOT/'src/Simulador_VLIW/sim_ejecucion.c',ROOT/'tools/vliw/sonda_fin_bal.c',ROOT/'Makefile',self.fuente]
        antes=[hashlib.sha256(p.read_bytes()).hexdigest() for p in archivos]
        b5.analizar(self.config())
        self.assertEqual(antes,[hashlib.sha256(p.read_bytes()).hexdigest() for p in archivos])
        self.assertEqual(list(self.fuente.parent.iterdir()),[self.fuente])

    # Repite la misma entrada y comprueba que las evidencias arquitectónicas sean deterministas.
    def test_28_determinismo(self):
        a=self.config();r=b5.analizar(a);a.salida=self.c/'segunda'
        self.assertEqual(r,b5.analizar(a))

    # Comprueba la liberación de temporales y recursos después del análisis.
    def test_29_limpieza(self):
        with mock.patch.object(b5.os,'rename',side_effect=OSError('fallo publicación')):
            with self.assertRaises(OSError):b5.analizar(self.config())
        self.assertFalse(list(self.c.glob('.b5-*')));self.assertFalse((self.c/'salida').exists())

    # Ejecuta desde otro cwd con espacios en las rutas y exige el mismo contrato de resultados.
    def test_30_otro_cwd_y_espacios(self):
        p=self.fuente.with_name('fuente con espacios.bal');self.fuente.rename(p)
        self.run_cmd([sys.executable,ROOT/'tools/vliw/analizar_corte_bal.py','--balc',self.balc,'--sonda',self.sonda,
                      '--sonda-fin',self.b4,'--fuente',p,'--presupuesto','1000','--salida',self.c/'salida con espacios'],cwd='/tmp')

    # Corrompe una métrica del protocolo y comprueba que la validación la rechace.
    def test_31_metrica_corrupta(self):
        r,mem,_=self.observar();r['metricas']['ciclos']+=1
        with self.assertRaises(ValueError):b5.validar(r,1000,mem)

    # Trunca la captura DMEM y exige exactamente los 65536 bytes del estado completo.
    def test_32_captura_truncada(self):
        r,mem,_=self.observar();mem.write_bytes(bytes(65535))
        with self.assertRaises(ValueError):b5.validar(r,1000,mem)

    # Suprime garantías de retiro o drenado y comprueba que una captura incompleta no se acepte.
    def test_33_garantias_incompletas(self):
        r,mem,_=self.observar()
        for campo in ('wb_gpr_pendiente','wb_crypto_pendiente','ex_lsu_pendiente','ex_crypto_pendiente'):
            d=copy.deepcopy(r);d['evidencia_pipeline'][-1][campo]=True
            with self.assertRaises(ValueError):b5.validar(d,1000,mem)
        d=copy.deepcopy(r);d['evidencia_pipeline'][-1]['if_id']['operaciones']=['guardap']*4
        with self.assertRaises(ValueError):b5.validar(d,1000,mem)
        d=copy.deepcopy(r);d['retorno_main_certificado']=True
        with self.assertRaises(ValueError):b5.validar(d,1000,mem)

    # Rechaza presupuestos y timeouts de tipo o rango inválido antes de ejecutar el análisis.
    def test_34_presupuesto_y_timeout_invalidos(self):
        base=['--balc',str(self.balc),'--sonda',str(self.sonda),'--sonda-fin',str(self.b4),'--fuente',str(self.fuente),'--salida',str(self.c/'x')]
        for n in ('-1','1000001','1.5'):
            self.assertEqual(b5.main(base+['--presupuesto',n]),64)
        self.assertEqual(b5.main(base+['--presupuesto','1','--timeout','nan']),64)

    # Procesa varias fuentes y verifica que cada caso conserve su propia evidencia.
    def test_35_varias_fuentes(self):
        otra=self.fuente.with_name('otra.bal');otra.write_bytes(self.fuente.read_bytes())
        r=b5.analizar(self.config(fuente=[self.fuente,otra]))
        self.assertEqual(len(r['casos']),2)
        self.assertTrue(all(c['punto_corte'] is not None for c in r['casos']))

    # Fuerza un fallo de ejecución y comprueba que se informe como error, no como corte
    # candidato.
    def test_36_error_ejecucion(self):
        r,mem,_=self.observar('nop | nop | syer x0,x0,-16 | nop\n')
        self.assertEqual(r['conclusion'],'ERROR');self.assertIsNone(r['punto_corte']);self.assertFalse(mem.exists())

    # Introduce efectos en la continuación y exige rechazar el corredor que debía contener solo
    # NOP.
    def test_37_continuacion_con_efectos(self):
        # Un efecto entre enlace y sentinela invalida el corredor NOP, aunque
        # pudiera completarse en otra ventana. No se busca un corte alternativo.
        texto=('sumai x13,x0,96 | nop | nop | nop\nnop\nnop\n'
               'nop | nop | syer x1,x13,0 | nop\n'
               'sumai x6,x0,7 | nop | nop | nop\n# __arranque_fin:\n'
               'nop | nop | sye x0,0 | nop\n# __fn_main:\n'
               'nop | nop | syer x0,x1,0 | nop\nnop\nnop\n')
        r,mem,_=self.observar(completo=texto)
        # Un retorno compatible con el enlace no vuelve inocua la continuación. Si el corredor
        # contiene operaciones con efectos, no puede publicarse una captura bajo este contrato.
        self.assertFalse(r['contrato_local']);self.assertIsNone(r['punto_corte']);self.assertFalse(mem.exists())

    # Usa un destino de salida ya existente y comprueba que no se reemplace.
    def test_38_salida_existente(self):
        a=self.config();a.salida.mkdir();(a.salida/'ajeno').write_text('conservar')
        with self.assertRaises(ValueError):b5.analizar(a)
        self.assertEqual((a.salida/'ajeno').read_text(),'conservar')

    # Repite un PC sin demostrar un retorno retirado y exige que no se certifique un corte por
    # esa observación aislada.
    def test_39_sin_certificacion_por_pc_repetido(self):
        r,mem,_=self.observar(completo='# __arranque_fin:\nnop | nop | sye x0,0 | nop\n')
        self.assertIsNotNone(r['primer_bucle_final']);self.assertEqual(r['conclusion'],'CORTE_NO_DEMOSTRADO')
        self.assertFalse(mem.exists())

    # Comprueba que la captura conserve los 32 registros generales, no solo los modificados.
    def test_40_registros_completos(self):
        for c in self.reales['casos']:
            self.assertEqual(c['snapshot_arquitectonico']['registros'],c['metricas']['arquitectura']['registros'])
            self.assertEqual(len(c['snapshot_arquitectonico']['registros']),32)
            self.assertEqual(c['snapshot_arquitectonico']['dmem']['bytes'],65536)

    # Hace coincidir el retiro del retorno con un WB anterior y comprueba que ese resultado
    # quede publicado.
    def test_41_wb_anterior_en_flanco_del_retorno(self):
        r,mem,_=self.observar('sumai x14,x0,77 | nop | nop | nop\nnop | nop | syer x0,x1,0 | nop\n')
        self.comprobar_positivo(r,mem)
        self.assertEqual(r['metricas']['arquitectura']['registros'][14],77)

    # Comprueba que una copia DMEM existente no se sobrescriba para aparentar una publicación
    # válida.
    def test_42_copia_existente_no_se_reemplaza(self):
        d,args,_=self.construir();mem=d/'protegido.dmem';mem.write_bytes(b'conservar')
        self.run_cmd([self.sonda,*args,1000,mem],codigo=1)
        self.assertEqual(mem.read_bytes(),b'conservar')

    # Entrega JSON corrupto desde una herramienta y exige un diagnóstico de error.
    def test_43_json_corrupto(self):
        exe=self.c/'roto';exe.write_text('#!'+sys.executable+'\nprint("{roto")\n');exe.chmod(0o700)
        c=b5.analizar(self.config(sonda=exe))['casos'][0]
        self.assertEqual(c['conclusion'],'ERROR');self.assertIsNone(c['punto_corte'])

    # Fuerza un timeout de la sonda y comprueba que no se publique evidencia incompleta.
    def test_44_timeout_sonda(self):
        exe=self.c/'lento';exe.write_text('#!'+sys.executable+'\nimport time; time.sleep(10)\n');exe.chmod(0o700)
        c=b5.analizar(self.config(sonda=exe,timeout=.1))['casos'][0]
        self.assertEqual(c['conclusion'],'ERROR');self.assertIsNone(c['snapshot_arquitectonico'])

    # Altera la coherencia entre estado del simulador y métricas M1 y exige rechazo.
    def test_45_estado_m1_incompatible(self):
        r,mem,_=self.observar();r['metricas']['estado']='TERMINADO'
        with self.assertRaises(ValueError):b5.validar(r,1000,mem)

    # Observa el retorno en EX sin su retiro en WB y comprueba que todavía no se acepte el
    # corte.
    def test_46_retorno_sin_retiro_no_es_positivo(self):
        r,mem,_=self.observar();r['evidencia_pipeline'][-1]['retorno_retirado_wb']=False
        with self.assertRaises(ValueError):b5.validar(r,1000,mem)

    # Ejecuta una función que no regresa y verifica que no se invente terminación ni captura.
    def test_47_funcion_que_no_regresa(self):
        self.fuente.write_text('create_funk #declare_int# f() { whale (1 == 1) {} stop; give 0; }\ncreate_funk #declare_infinite_void# main() { n * declare_int : f(); }\n')
        c=b5.analizar(self.config())['casos'][0]
        self.assertEqual(c['conclusion'],'PRESUPUESTO_AGOTADO');self.assertIsNone(c['punto_corte'])

    # Compila un BAL inválido y comprueba que el rechazo no se convierta en un resultado
    # arquitectónico.
    def test_48_fuente_invalida(self):
        self.fuente.write_text('fuente BAL inválido {\n')
        c=b5.analizar(self.config())['casos'][0]
        self.assertEqual(c['conclusion'],'ERROR');self.assertIsNone(c['metricas'])


if __name__=='__main__':unittest.main(verbosity=2)
