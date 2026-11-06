"""Contrasta B4 con BAL real, ensamblado aprobado y entradas adversariales."""
import argparse
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('b4',ROOT/'tools/analizar_fin_bal.py')
b4=importlib.util.module_from_spec(spec);spec.loader.exec_module(b4)


class FinBalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='pruebas-b4-');cls.addClassCleanup(cls.tmp.cleanup)
        cls.d=Path(os.environ.get('B4_ARTEFACTOS',cls.tmp.name));cls.d.mkdir(parents=True,exist_ok=True)
        cls.comandos=[]
        if os.environ.get('B4_BALC'):cls.balc=Path(os.environ['B4_BALC']).resolve()
        else:
            copia=cls.d/'compilador'
            shutil.copytree(ROOT,copia,ignore=shutil.ignore_patterns('.git','__pycache__'))
            cls.ejecutar(['make','-B','balc'],cwd=copia);cls.balc=copia/'balc'
        cls.sonda=cls.d/'sonda';cls.fallos=cls.d/'sonda-fallos'
        cc=shlex.split(os.environ.get('CC','cc'))
        flags=['-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-O2','-Iinclude']
        fuentes=['tools/sonda_fin_bal.c','src/codificacion_binaria.c']+['src/sim_'+n+'.c' for n in
                   ('ejecucion','metricas','pipeline','alu','lsu','bru','crypto','decode','cargador')]
        cls.ejecutar(cc+flags+fuentes+['-o',cls.sonda])
        # Las envolturas solo existen en el ejecutable adversarial de pruebas.
        # Se fuerzan fallos públicos sin editar M1 ni fabricar estados privados.
        w=cls.d/'fallos.c';w.write_text('''#include "sim_metricas.h"
#include <stdlib.h>
#include <string.h>
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) {
 static unsigned llamadas; const char *f=getenv("B4_MALLOC");
 if(f && ++llamadas==strtoul(f,NULL,10))return NULL;
 return __real_malloc(n);
}
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t k) {
 static unsigned llamadas;const char *f=getenv("B4_CALLOC");
 if(f && ++llamadas==strtoul(f,NULL,10))return NULL;
 return __real_calloc(n,k);
}
void *__real_realloc(void *,size_t);
void *__wrap_realloc(void *p,size_t n) {
 static unsigned llamadas;const char *f=getenv("B4_REALLOC");
 if(f && ++llamadas==strtoul(f,NULL,10))return NULL;
 return __real_realloc(p,n);
}
SimMetricasEstado __real_sim_metricas_observar(SimMetricas *,const SimPipeline *);
SimMetricasEstado __wrap_sim_metricas_observar(SimMetricas *m,const SimPipeline *s) {
 static unsigned fallos;const char *f=getenv("B4_OBSERVAR");
 if(f && !strcmp(f,"siempre"))return SIM_METRICAS_MEMORIA;
 if(f && !strcmp(f,"inconsistencia"))return SIM_METRICAS_INCONSISTENCIA;
 if(f && !strcmp(f,"una") && !fallos++)return SIM_METRICAS_MEMORIA;
 return __real_sim_metricas_observar(m,s);
}
''')
        cls.ejecutar(cc+flags+fuentes+[w,'-Wl,--wrap=malloc','-Wl,--wrap=calloc','-Wl,--wrap=realloc','-Wl,--wrap=sim_metricas_observar','-o',cls.fallos])
        # Los tres ejemplos originales se copian por B4 y se compilan sin
        # parchear ASM/BALB/MEM. Se conserva la evidencia para inspección.
        cls.real=cls.d/'reales'
        args=[sys.executable,ROOT/'tools/analizar_fin_bal.py','--balc',cls.balc,'--sonda',cls.sonda,
              '--presupuesto','10000','--salida',cls.real]
        for n in ('factorial','whale','ejercicio3_puntajes'):args+=['--fuente',ROOT/'examples'/(n+'.bal')]
        cls.ejecutar(args);cls.reales=json.loads((cls.real/'reporte.json').read_text())

    @classmethod
    def ejecutar(cls,args,codigo=0,cwd=ROOT,env=None):
        p=subprocess.run(list(map(str,args)),cwd=cwd,capture_output=True,timeout=120,
                         env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',**(env or {})))
        cls.comandos.append(dict(comando=list(map(str,args)),codigo=p.returncode,cwd=str(cwd),
              stdout=p.stdout.decode(errors='backslashreplace'),stderr=p.stderr.decode(errors='backslashreplace')))
        if codigo is not None and p.returncode!=codigo:raise AssertionError(f'{args}: {p.returncode} != {codigo}: {p.stderr!r}')
        return p

    @classmethod
    def tearDownClass(cls):
        if os.environ.get('B4_EVIDENCIA'):
            Path(os.environ['B4_EVIDENCIA']).write_text(json.dumps(dict(comandos=cls.comandos,reales=cls.reales),indent=2)+'\n')

    def setUp(self):
        t=tempfile.TemporaryDirectory(prefix='caso-',dir=self.d);self.addCleanup(t.cleanup);self.c=Path(t.name)
        (self.c/'fuentes').mkdir();self.fuente=self.c/'fuentes/demo.bal'
        self.fuente.write_text('create_funk #declare_infinite_void# main() {}\n')

    def configurar(self,**cambios):
        a=argparse.Namespace(balc=self.balc,sonda=self.sonda,fuente=[self.fuente],
                             salida=self.c/'resultado',presupuesto=100,timeout=10)
        for k,v in cambios.items():setattr(a,k,v)
        return a

    def ensamblar(self,texto):
        a=self.c/'demo.asm';a.write_text(texto);b=self.c/'demo.bin'
        self.ejecutar([self.balc,'-x','-o',b,a]);return a,b

    def observar(self,texto,presupuesto=100,sonda=None,env=None):
        a,b=self.ensamblar(texto)
        sent=b4.localizar(texto,'__arranque_fin');main=b4.localizar(texto,'__fn_main')
        p=self.ejecutar([sonda or self.sonda,a,b,sent['linea'] if sent else 0,main['linea'] if main else 0,presupuesto],env=env)
        d=json.loads(p.stdout);return d,b4.clasificar(d,sent)

    def test_01_argumentos(self):
        self.assertEqual(b4.main([]),64)
        self.assertEqual(self.ejecutar([self.sonda],codigo=64).returncode,64)

    def test_02_inexistentes(self):
        for k in ('balc','sonda'):
            with self.assertRaises(OSError):b4.analizar(self.configurar(**{k:self.c/'ausente'}))
        with self.assertRaises(OSError):b4.analizar(self.configurar(fuente=[self.c/'ausente.bal']))

    def test_03_balb_corrupto(self):
        a,b=self.ensamblar('nop\n');b.write_bytes(b'BALB')
        self.ejecutar([self.sonda,a,b,0,0,100],codigo=1)

    def test_04_asm_invalido(self):
        a,b=self.ensamblar('nop\n');a.write_text('operacion_inexistente\n')
        self.ejecutar([self.sonda,a,b,0,0,100],codigo=1)

    def test_05_etiqueta_ausente(self):
        d,r=self.observar('nop\n');self.assertEqual(r['conclusion'],'FIN_NO_DEMOSTRADO')
        self.assertIsNone(d['direccion_sentinela'])

    def test_06_etiqueta_duplicada(self):
        for s in ('__arranque_fin:\n__arranque_fin:\n','# __arranque_fin:\n# __arranque_fin:\n'):
            with self.assertRaises(ValueError):b4.localizar(s,'__arranque_fin')

    def test_07_sentinela_malformada(self):
        d,r=self.observar('# __arranque_fin:\nnop\n')
        self.assertFalse(d['patron_valido']);self.assertEqual(r['conclusion'],'FIN_NO_DEMOSTRADO')

    def test_08_mapeo_no_es_linea(self):
        d,r=self.observar('# comentario\n\n# __arranque_fin:\n\n nop | nop | sye x0,0 | nop\n')
        self.assertEqual(d['direccion_sentinela'],0);self.assertEqual(d['linea_bundle'],5)
        self.assertEqual(r['conclusion'],'BUCLE_FINAL_OBSERVADO')

    def test_09_bru_incorrecta(self):
        d,r=self.observar('# __arranque_fin:\nnop | nop | igualsi x0,x0,0 | nop\n')
        self.assertFalse(d['patron_valido']);self.assertNotIn('BUCLE_FINAL_OBSERVADO',r['hechos'])

    def test_10_bucle_legitimo_real(self):
        self.assertTrue(all(c['conclusion']=='BUCLE_FINAL_OBSERVADO' for c in self.reales['casos']))
        self.assertTrue(all(not c['retorno_main_certificado'] for c in self.reales['casos']))

    def test_11_bucle_ordinario(self):
        d,r=self.observar('ordinario:\nnop | nop | sye x0,ordinario | nop\n')
        self.assertGreater(d['eventos_totales'],1);self.assertEqual(r['conclusion'],'FIN_NO_DEMOSTRADO')

    def test_12_ret_no_observado(self):
        d,r=self.observar('# __arranque_fin:\nnop | nop | sye x0,0 | nop\n')
        self.assertIsNone(d['retorno_candidato']);self.assertFalse(r['retorno_main_certificado'])

    def test_13_presupuesto_insuficiente(self):
        for n in (0,1,2):
            d,r=self.observar('# __arranque_fin:\nnop | nop | sye x0,0 | nop\n',n)
            self.assertEqual(r['conclusion'],'FIN_NO_DEMOSTRADO');self.assertEqual(d['metricas']['ciclos'],n)
            # Cada iteración usa archivos nuevos aunque pertenezca al mismo test.

    def test_14_excepcion(self):
        d,r=self.observar('nop | nop | nop | ell 0,0,x0,x0\n')
        self.assertGreater(d['metricas']['traps'],0)
        self.assertTrue(any(e['excepcion'] for e in d['eventos_control']))
        self.assertFalse(r['retorno_main_certificado'])

    def test_15_carga_fallida(self):
        a,b=self.ensamblar('nop\n');b.unlink()
        self.ejecutar([self.sonda,a,b,0,0,100],codigo=1)

    def test_16_observacion_persistente(self):
        d,r=self.observar('nop\n',sonda=self.fallos,env={'B4_OBSERVAR':'siempre'})
        self.assertEqual(d['observacion_m1'],7);self.assertEqual(r['conclusion'],'ERROR')
        self.assertIsNone(d['metricas'])

    def test_17_observacion_reintentada(self):
        d,r=self.observar('nop\n',sonda=self.fallos,env={'B4_OBSERVAR':'una'})
        self.assertTrue(d['comparacion_m2']['metricas_iguales']);self.assertEqual(d['metricas']['ciclos'],4)

    def test_18_metricas_inconsistentes(self):
        d,r=self.observar('nop\n');d['comparacion_m2']['metricas_iguales']=False
        self.assertEqual(b4.clasificar(d,None)['conclusion'],'ERROR')
        d['metricas']['ciclos']=-1
        with self.assertRaises(ValueError):b4.clasificar(d,None)

    def test_19_m2_reales(self):
        for c in self.reales['casos']:
            self.assertTrue(all(c['sonda']['comparacion_m2'].values()))
            self.assertEqual(c['estado_simulador'],'LIMITE')
            self.assertEqual(c['sonda']['metricas']['ciclos'],10000)

    def test_20_determinismo(self):
        a=self.configurar();r1=b4.analizar(a)
        a.salida=self.c/'segundo';r2=b4.analizar(a)
        self.assertEqual(r1,r2)

    def test_21_recursos_y_publicacion(self):
        with mock.patch.object(b4.os,'rename',side_effect=OSError('fallo controlado')):
            with self.assertRaises(OSError):b4.analizar(self.configurar())
        self.assertFalse(list(self.c.glob('.b4-*')));self.assertFalse((self.c/'resultado').exists())

    def test_22_fallos_memoria(self):
        a,b=self.ensamblar('nop\n')
        for familia in ('B4_MALLOC','B4_CALLOC','B4_REALLOC'):
            fallaron=0
            for n in range(1,13):
                p=self.ejecutar([self.fallos,a,b,0,0,100],codigo=None,env={familia:str(n)})
                self.assertIn(p.returncode,(0,1));fallaron+=p.returncode==1
            self.assertGreaterEqual(fallaron,1)

    def test_23_archivos_protegidos(self):
        archivos=[ROOT/'src/enlace_funciones.c',ROOT/'src/sim_pipeline.c',ROOT/'src/sim_metricas.c',
                  ROOT/'src/sim_ejecucion.c',ROOT/'tools/validar_pipeline_bal.py',self.fuente]
        antes=[hashlib.sha256(p.read_bytes()).hexdigest() for p in archivos]
        b4.analizar(self.configurar())
        self.assertEqual(antes,[hashlib.sha256(p.read_bytes()).hexdigest() for p in archivos])
        self.assertEqual(list(self.fuente.parent.iterdir()),[self.fuente])

    def test_24_entrada_inesperada_a_sentinela(self):
        # Llega mediante salto directo ajeno a ret; se observa el bucle pero
        # jamás se eleva esa redirección a retorno certificado de main.
        d,r=self.observar('nop | nop | sye x0,fin | nop\nnop\nnop\nfin:\n# __arranque_fin:\nnop | nop | sye x0,0 | nop\n')
        self.assertIsNotNone(d['redireccion_a_sentinela']);self.assertIsNone(d['retorno_candidato'])
        self.assertEqual(r['conclusion'],'BUCLE_FINAL_OBSERVADO');self.assertFalse(r['retorno_main_certificado'])

    def test_25_error_ejecucion(self):
        d,r=self.observar('nop | nop | syer x0,x0,-16 | nop\n')
        self.assertTrue(d['error_dinamico']);self.assertEqual(r['conclusion'],'ERROR')

    def test_26_codigo_distinto(self):
        a,b=self.ensamblar('nop\n');a.write_text('sumai x4,x0,1 | nop | nop | nop\n')
        p=self.ejecutar([self.sonda,a,b,0,0,100],codigo=1)
        self.assertIn(b'no corresponden',p.stderr)

    def test_27_nombre_en_comentario_no_es_suficiente(self):
        self.assertIsNone(b4.localizar('# mencionar __arranque_fin: no es marcador\n','__arranque_fin'))
        d,r=self.observar('# __arranque_fin:\nsumai x0,x0,0 | nop | sye x0,0 | nop\n')
        self.assertFalse(d['patron_valido'])

    def test_28_pendientes_y_flancos_posteriores(self):
        for c in self.reales['casos']:
            s=c['sonda'];e=s['bucle_final_observado']
            self.assertEqual(e['pendientes_post'],[0,0,1])
            self.assertEqual(e['metricas_post']['ciclos'],e['ciclo'])
            self.assertEqual([x['ciclo'] for x in s['flancos_posteriores']],[e['ciclo']+1,e['ciclo']+2])

    def test_29_error_m1_no_recuperable(self):
        d,r=self.observar('nop\n',sonda=self.fallos,env={'B4_OBSERVAR':'inconsistencia'})
        self.assertEqual(d['observacion_m1'],5);self.assertEqual(r['conclusion'],'ERROR')

    def test_30_prefijo_acotado(self):
        d,r=self.observar('# __arranque_fin:\nnop | nop | sye x0,0 | nop\n',1000)
        self.assertEqual(len(d['eventos_control']),128);self.assertTrue(d['eventos_truncados'])
        self.assertEqual(r['conclusion'],'BUCLE_FINAL_OBSERVADO')

    def test_31_while_y_cycle_reales_no_retornan(self):
        # Son demostraciones temporales de bucles infinitos, no optimizaciones
        # oficiales. El marcador estático existe, pero nunca se ejecuta.
        for i,cuerpo in enumerate(('whale (1 == 1) {} stop;',
                                  'cycle i let 0 until 10000 {} endgame;')):
            self.fuente.write_text('create_funk #declare_infinite_void# main() { '+cuerpo+' }\n')
            a=self.configurar(salida=self.c/('bucle-'+str(i)),presupuesto=1000)
            r=b4.analizar(a)['casos'][0]
            self.assertEqual(r['compilacion']['codigo'],0)
            self.assertEqual(r['conclusion'],'FIN_NO_DEMOSTRADO')
            self.assertFalse(r['retorno_main_certificado'])

    def test_32_rutas_con_espacios_otro_cwd(self):
        p=self.fuente.with_name('demo con espacios.bal');self.fuente.rename(p)
        self.ejecutar([sys.executable,ROOT/'tools/analizar_fin_bal.py','--balc',self.balc,'--sonda',self.sonda,
                      '--fuente',p,'--presupuesto','100','--salida',self.c/'con espacios'],cwd='/tmp')

    def test_33_fuentes_y_salida_invalidas(self):
        with self.assertRaises(ValueError):b4.analizar(self.configurar(fuente=[self.fuente,self.fuente]))
        with self.assertRaises(ValueError):b4.analizar(self.configurar(salida=self.fuente.parent/'salida'))
        a=self.configurar();a.salida.mkdir()
        with self.assertRaises(ValueError):b4.analizar(a)

    def test_34_evento_falso(self):
        c=copy.deepcopy(self.reales['casos'][0]);d=c['sonda'];d['bucle_final_observado']['ex_pc']+=16
        with self.assertRaises(ValueError):b4.clasificar(d,c['patron_estatico']['sentinela'])
        d=copy.deepcopy(c['sonda']);d['version']=True
        with self.assertRaises(ValueError):b4.clasificar(d,c['patron_estatico']['sentinela'])
        d=copy.deepcopy(c['sonda']);e=copy.deepcopy(d['retorno_candidato'])
        d['redireccion_a_sentinela']=e
        with self.assertRaises(ValueError):b4.clasificar(d,c['patron_estatico']['sentinela'])

    def test_35_presupuesto_y_timeout_invalidos(self):
        base=['--balc',str(self.balc),'--sonda',str(self.sonda),'--fuente',str(self.fuente),'--salida',str(self.c/'x')]
        for n in ('-1','1000001','1.5'):
            self.assertEqual(b4.main(base+['--presupuesto',n]),64)
        self.assertEqual(b4.main(base+['--presupuesto','1','--timeout','nan']),64)

    def test_36_funcion_sigue_activa(self):
        self.fuente.write_text('create_funk #declare_int# f() { whale (1 == 1) {} stop; give 0; }\ncreate_funk #declare_infinite_void# main() { n * declare_int : f(); }\n')
        c=b4.analizar(self.configurar(presupuesto=500))['casos'][0]
        self.assertEqual(c['conclusion'],'FIN_NO_DEMOSTRADO')
        self.assertIsNone(c['sonda']['retorno_candidato'])
        self.assertIsNotNone(c['sonda']['llamada_inicial_observada'])

    def test_37_timeout_compilador(self):
        exe=self.c/'lento';exe.write_text('#!'+sys.executable+'\nimport time; time.sleep(10)\n');exe.chmod(0o700)
        c=b4.analizar(self.configurar(balc=exe,timeout=.1))['casos'][0]
        self.assertEqual(c['conclusion'],'ERROR')
        self.assertIsNotNone(c['compilacion']['motivo'])

    def test_38_timeout_sonda(self):
        exe=self.c/'lento';exe.write_text('#!'+sys.executable+'\nimport time; time.sleep(10)\n');exe.chmod(0o700)
        c=b4.analizar(self.configurar(sonda=exe,timeout=.1))['casos'][0]
        self.assertEqual(c['conclusion'],'ERROR')

    def test_39_json_corrupto(self):
        exe=self.c/'roto';exe.write_text('#!'+sys.executable+'\nprint("{roto")\n');exe.chmod(0o700)
        c=b4.analizar(self.configurar(sonda=exe))['casos'][0]
        self.assertEqual(c['conclusion'],'ERROR')

    def test_40_varios_casos(self):
        otra=self.fuente.with_name('otra.bal');otra.write_bytes(self.fuente.read_bytes())
        r=b4.analizar(self.configurar(fuente=[self.fuente,otra]))
        self.assertEqual(len(r['casos']),2)

    def test_41_etiqueta_real_e_inline(self):
        d,r=self.observar('__arranque_fin: nop | nop | sye x0,__arranque_fin | nop\n')
        self.assertTrue(d['patron_valido']);self.assertEqual(r['conclusion'],'BUCLE_FINAL_OBSERVADO')

    def test_42_no_hay_flancos_posteriores_disponibles(self):
        d,r=self.observar('# __arranque_fin:\nnop | nop | sye x0,0 | nop\n',3)
        self.assertEqual(d['bucle_final_observado']['ciclo'],3)
        self.assertEqual(d['flancos_posteriores'],[])
        self.assertEqual(d['metricas']['ciclos'],3)


if __name__=='__main__':unittest.main(verbosity=2)
