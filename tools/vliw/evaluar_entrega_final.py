"""Evalúa quince fuentes BAL con niveles reales y observables acreditados.

Reutiliza el CLI, B4/B5, M1/M2, B1/B2 y el banco RTL públicos. La matriz separa
DCE O1 de la transformación requerida por cada categoría; una igualdad a un
corte ACTIVO no certifica terminación ni equivale al resultado estricto de B1.
Los artefactos son propios del destino nuevo externo; las fuentes son préstamos.
"""
from pathlib import Path
import argparse
import csv
import hashlib
import importlib.util
import json
import os
import re
import shlex
import struct
import subprocess
import sys
import time
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/vliw'))
import validar_integracion_ir_rtl as i3
CATEGORIAS=('loop_unrolling','renombramiento','dce','saltos_funciones','spilling')

# El hash identifica el fuente, no prueba por sí mismo la procedencia de una
# imagen. Esa procedencia se completa abajo con compilación y argv registrados.
def leer_manifest(ruta):
    m=json.loads(Path(ruta).read_text())
    if type(m) is not dict or set(m)!={'version','casos'} or type(m.get('version')) is not int or m['version']!=1 or type(m.get('casos')) is not list or len(m['casos'])!=15:
        raise ValueError('se requieren quince casos y versión 1')
    # Identidades y cuota se validan antes de compilar: no se permite rellenar
    # una categoría con copias de un mismo caso ni cambiar su fuente en silencio.
    ids=set();cuentas={c:0 for c in CATEGORIAS}
    for c in m['casos']:
        if type(c) is not dict or set(c)!={'id','categoria','fuente','sha256','observables','offsets_o0','offsets_o1','objetivo','transformacion_requerida'}:
            raise ValueError('campos del caso inválidos')
        if any(type(c[k]) is not str or not c[k] for k in ('id','categoria','fuente','sha256','objetivo','transformacion_requerida')) or any(type(c[k]) is not dict for k in ('observables','offsets_o0','offsets_o1')):
            raise ValueError('tipos de metadatos inválidos')
        if not re.fullmatch('[0-9a-f]{64}',c['sha256']):raise ValueError('hash inválido')
        if not re.fullmatch('[a-z0-9_]+',c['id']) or c['id'] in ids or c['categoria'] not in cuentas:
            raise ValueError('identidad/categoría inválida')
        ids.add(c['id']);cuentas[c['categoria']]+=1
        p=(ROOT/c['fuente']).resolve()
        if not p.is_relative_to(ROOT) or not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=c['sha256']:
            raise ValueError('fuente cambiado o fuera del proyecto')
        # Los valores son oráculos lógicos; los offsets pertenecen a cada
        # variante y deben ser palabras accesibles, nunca bool usados como int.
        if not c['observables'] or set(c['offsets_o0'])!=set(c['observables']) or set(c['offsets_o1'])!=set(c['observables']):
            raise ValueError('observables incompletos')
        for x in c['observables'].values():
            if type(x) is not int or not 0<=x<2**32:raise ValueError('valor observable inválido')
        for key in ('offsets_o0','offsets_o1'):
            for x in c[key].values():
                if type(x) is not int or x<0 or x%4 or x>65532:raise ValueError('offset inválido')
    if set(cuentas.values())!={3}:raise ValueError('se requieren tres casos por categoría')
    return m

# El wrapper acredita exactamente el nivel incorporado por el CLI. B5 vuelve a
# compilar mediante este ejecutable; no se sustituyen binarios tras su análisis.
def wrapper(balc,nivel,d):
    p=d/('balc-O'+str(nivel))
    p.write_text('#!'+sys.executable+'\nimport os,sys\nos.execv('+repr(str(balc))+','+repr([str(balc),'-O'+str(nivel)])+'+sys.argv[1:])\n');p.chmod(0o700)
    return p

# Dos binarios estáticos del banco público bastan para todos los programas.
# Cada vvp comienza de reset con su propia imagen y captura después del flush.
# El wrapper existente solo siembra x16..31; no modifica el datapath ni el reloj.
def compilar_bancos(arq,d):
    bancos={};rtl=[p for sub in ('rtl','rtl-p2') for p in sorted((arq/sub).glob('*.sv'))]
    for modo in (0,2):
        exe=d/('banco-'+str(modo)+'.vvp')
        i3.ejecutar(['iverilog','-g2012','-I'+str(arq),'-I'+str(arq/'rtl'),'-I'+str(arq/'rtl-p2'),'-I'+str(arq/'tb'),'-s','tb_i1a3_preservacion',f'-Ptb_i1a3_preservacion.MEM_MODE={modo}','-o',exe,*rtl,arq/'tests/rtl/tb_assembled_program.sv',ROOT/'tests/vliw/rtl/tb_i1a3_preservacion.sv'],d)
        bancos[modo]=exe
    return bancos

# Un BALB con datos no cero requiere una imagen inicial RAM explícita. Esta
# campaña solo admite el estado inicial cero y demuestra igualdad con MEM;
# no descarta la sección de datos para obtener un contraste favorable.
def validar_imagen(binario,mem):
    b=binario.read_bytes();codigo=b''.join(bytes.fromhex(l)[::-1] for l in mem.read_text().splitlines())
    if len(b)<48 or b[:4]!=b'BALB':raise ValueError('BALB inválido')
    f=struct.unpack('<11I',b[4:48])
    if f[0:3]!=(1,48,len(b)) or f[3]!=0 or f[4]!=48 or f[5]!=len(codigo) or f[6]!=0 or f[7]!=48+len(codigo) or f[8]!=len(b)-f[7] or f[9]+f[8]>65536 or b[48:f[7]]!=codigo or any(b[f[7]:]):
        raise ValueError('BALB/MEM o estado inicial no acreditado')
    return len(codigo)//16

# El runner público separa metadatos de configuración y estado arquitectónico.
# Exigimos un único marcador completo para no aceptar una captura ambigua.
def estado_publico(texto):
    lineas=[l for l in texto.splitlines() if l.startswith('[ARCH_STATE]')]
    if len(lineas)!=1:raise ValueError('estado público ausente o duplicado')
    m=re.fullmatch(r'\[ARCH_STATE\] crypto=([0-9]+) traps=([0-9]+) trap_pc=([0-9]+)',lineas[0])
    if m is None:raise ValueError('estado público mal formado')
    return dict(zip(('crypto','traps','trap_pc'),map(int,m.groups())))

# Captura una variante al corte realmente acreditado por B5, no al número de
# bundles estimado. Los offsets se declaran por variante porque DCE cambia marco.
def observar(caso,nivel,b5,exe,arq,bancos,d,rtl):
    if b5['conclusion']!='CORTE_ARQUITECTONICO_CANDIDATO':raise ValueError('B5 no acreditó corte')
    horizonte=b5['punto_corte']['ciclo'];asm=d/'programa.asm';mem=d/'programa.mem'
    bundles=validar_imagen(d/'programa.bin',mem)
    if hashlib.sha256(mem.read_bytes()).hexdigest()!=b5['b4']['identidad_artefactos']['mem']['sha256']:
        raise ValueError('B5 corresponde a otra imagen')
    # La captura funcional conserva todas las palabras de DMEM y los GPR.
    # Los oráculos BAL se contrastan además del posterior cotejo completo RTL.
    i3.ejecutar([exe,'preservar',mem,d,str(horizonte)],d)
    registros=json.loads((d/'sim-registros.json').read_text());datos=(d/'sim-memoria.bin').read_bytes();metricas=json.loads((d/'sim-metricas.json').read_text())
    texto=asm.read_text();marco=int(re.search(r'restai x2,x2,(\d+)',texto.split('# __fn_main:',1)[1])[1])
    obs={}
    for nombre,esperado in caso['observables'].items():
        addr=65536-marco+caso['offsets_o'+str(nivel)][nombre]
        if addr%4 or not 0<=addr<=65532:raise ValueError('dirección observable inválida')
        valor=int.from_bytes(datos[addr:addr+4],'little');obs[nombre]=dict(direccion=addr,valor=valor,esperado=esperado)
        if valor!=esperado:raise ValueError('oráculo lógico distinto: '+nombre)
    asm_mod=i3.ensamblador(arq)
    if asm_mod.assemble_source(texto).memory_text()!=mem.read_text():raise ValueError('codificación distinta del ISA de Arquitectura')
    resultados=[]
    for modo in ((0,2) if rtl else ()):
        carpeta=d/('rtl-'+str(modo));carpeta.mkdir()
        out=i3.ejecutar(['vvp','-n',bancos[modo],f'+MEM={mem}',f'+REGDUMP={carpeta}/registers.txt',f'+MEMDUMP={carpeta}/memory.txt',f'+BUNDLES={bundles}',f'+CYCLES={horizonte}',f'+REQUESTED_MEM_MODE={modo}'],carpeta)
        spec=importlib.util.spec_from_file_location('runner_final',arq/'tools/run_program.py');runner=importlib.util.module_from_spec(spec);sys.path.insert(0,str(arq/'tools'));spec.loader.exec_module(runner)
        md=runner.execution_metadata(out,modo,bundles,horizonte,dict(L2_MODEL=0));md.update(architecture_state=estado_publico(out),validation='PASS',cut='B5_POST_NBA_before_flush')
        (carpeta/'execution.json').write_text(json.dumps(md,indent=2)+'\n');i3.contrastar_rtl(carpeta,registros,datos)
        if md['architecture_state']['crypto']!=metricas['arquitectura']['estado_crypto'] or md['architecture_state']['traps']!=metricas['arquitectura']['trap_count32']:raise ValueError('crypto/traps distintos')
        resultados.append(md)
    return dict(nivel='-O'+str(nivel),horizonte=horizonte,marco=marco,observables=obs,metricas=metricas,bundles=bundles,bytes_codigo=16*bundles,rtl=resultados,mem_sha256=hashlib.sha256(mem.read_bytes()).hexdigest(),ruta_mem=str(mem),capacidad_imem=bundles<=1023,main_certificado=False)

# El corte activo se mantiene como tal ante B1. No se cambia el presupuesto ni
# se drena artificialmente el pipeline para convertir INCONCLUSO en EQUIVALENTE.
def comparar_b1(comparador,a,b,d):
    cmd=[str(comparador),'mem',a['ruta_mem'],'mem',b['ruta_mem'],'fisica',str(max(a['horizonte'],b['horizonte'])),'json']
    p=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
    (d/'b1.stdout.json').write_text(p.stdout);(d/'b1.stderr.txt').write_text(p.stderr)
    if p.returncode not in (0,2,3):raise ValueError('B1 falló: '+p.stderr)
    r=json.loads(p.stdout);return dict(codigo=p.returncode,resultado=r['resultado_comparacion'],argv=cmd)

# Publica filas incluso ante bloqueos: cada diagnóstico conserva la fase. Un
# fallo de oráculo no se cuenta como par semántico ni se oculta con estadísticas.
def evaluar(balc,arquitectura,destino,manifest,ids=None,rtl=True):
    destino=Path(destino).resolve();balc=Path(balc).resolve();arquitectura=Path(arquitectura).resolve()
    m=leer_manifest(manifest)
    if ids and not set(ids)<={c['id'] for c in m['casos']}:raise ValueError('ID desconocido')
    if destino.exists() or destino.is_relative_to(ROOT) or destino.is_relative_to(arquitectura):raise ValueError('destino externo nuevo requerido')
    destino.mkdir(parents=True);arq=i3.copiar_arquitectura(arquitectura,destino);exe=i3.compilar(destino)
    sim=[s for s in i3.FUENTES if s.startswith('src/Simulador_VLIW/')];cc=shlex.split(os.environ.get('CC','cc'));flags=['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-Iinclude']
    for n,s,extra in [('fin','tools/vliw/sonda_fin_bal.c',['src/Codificacion_Binaria/codificacion_binaria.c']),('corte','tools/vliw/sonda_corte_bal.c',[]),('comparador','tools/vliw/comparar_vliw.c',['src/Simulador_VLIW/sim_comparador.c'])]:
        i3.ejecutar(cc+flags+[s]+sim+extra+['-o',destino/n],destino)
    # Cada variante parte de estado inicial limpio. Los bancos compilados se
    # comparten como ejecutables, nunca como instancias con memoria persistente.
    bancos=compilar_bancos(arq,destino) if rtl else {};seleccion=[c for c in m['casos'] if not ids or c['id'] in ids];filas=[]
    # Se genera cada imagen por el CLI oficial y se vuelven a compilar por B5.
    # Los wrappers permiten conservar el nivel en esa segunda construcción.
    for caso in seleccion:
        d=destino/caso['id'];d.mkdir();fila=dict(caso,estado_final='BLOQUEADO',variantes=[],diagnostico=None,semantica_acreditada=False,categoria_acreditada=False)
        try:
            for nivel in (0,1):
                sub=d/('O'+str(nivel));sub.mkdir();fuente=sub/'programa.bal';fuente.write_bytes((ROOT/caso['fuente']).read_bytes())
                i3.ejecutar([balc,'-O'+str(nivel),'-s',fuente],sub)
                # Las otras pasadas son análisis reales: sus textos y rechazos se
                # guardan, sin atribuirles el binario que generó O1 mediante DCE.
                analisis={'loop_unrolling':'--unroll=4','renombramiento':'--reorder','spilling':'--ir-spilling'}.get(caso['categoria'],'--dce')
                p=subprocess.run([str(balc),analisis,str(fuente)],capture_output=True,text=True);(sub/'analisis.txt').write_text(p.stdout+p.stderr)
                w=wrapper(balc,nivel,sub)
                i3.ejecutar([sys.executable,ROOT/'tools/vliw/analizar_corte_bal.py','--balc',w,'--sonda',destino/'corte','--sonda-fin',destino/'fin','--salida',d/('corte-O'+str(nivel)),'--presupuesto','100000','--timeout','120','--fuente',fuente],sub,timeout=300)
                b5=json.loads((d/('corte-O'+str(nivel))/'reporte.json').read_text())['casos'][0]
                v=observar(caso,nivel,b5,exe,arq,bancos,sub,rtl);v.update(analisis=analisis,analisis_codigo=p.returncode,compiler_sha256=hashlib.sha256(balc.read_bytes()).hexdigest(),comando=['balc','-O'+str(nivel),'-s',str(fuente)])
                fila['variantes'].append(v)
            a,b=fila['variantes'];fila['b1']=comparar_b1(destino/'comparador',a,b,d)
            fila['transformacion_real']='DCE BAL por -O1';fila['imagenes_distintas']=a['mem_sha256']!=b['mem_sha256'];fila['semantica_acreditada']=True
            fila['categoria_acreditada']=caso['transformacion_requerida'] in ('dce','seguridad_control') and (fila['imagenes_distintas'] or caso['transformacion_requerida']=='seguridad_control')
            fila['estado_final']='SEMÁNTICAMENTE CONTRASTADO' if fila['imagenes_distintas'] else 'EJECUTADO SIN CERTIFICACIÓN'
            if not fila['categoria_acreditada']:fila['diagnostico']='El par O0/O1 acredita DCE, no '+caso['transformacion_requerida']+' productivo; falta lowering general de la IR transformada.'
        except (ValueError,RuntimeError,OSError,subprocess.TimeoutExpired) as err:
            fila['diagnostico']=str(err)
        (d/'resultado.json').write_text(json.dumps(fila,ensure_ascii=False,indent=2)+'\n');filas.append(fila)
        print(caso['id'],fila['estado_final'],fila['diagnostico'] or '',flush=True)
    r=dict(base_compilador='consultar commit y hashes de fuentes de la campaña',compiler_sha256=hashlib.sha256(balc.read_bytes()).hexdigest(),casos=filas,semanticos=sum(f['semantica_acreditada'] for f in filas),categorias_acreditadas=sum(f['categoria_acreditada'] for f in filas),estrictos=sum(f.get('b1',{}).get('resultado')=='EQUIVALENTE' for f in filas),rtl_solicitado=rtl)
    (destino/'resultado.json').write_text(json.dumps(r,ensure_ascii=False,indent=2)+'\n')
    return r

# Los bloqueos de casos conservan filas y devuelven 2; un manifest o entorno
# inválido devuelve 1. Solo una selección totalmente contrastada devuelve 0.
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--balc',type=Path,required=True);p.add_argument('--arquitectura',type=Path,required=True);p.add_argument('--salida',type=Path,required=True);p.add_argument('--manifest',type=Path,default=ROOT/'tests/vliw/programas_finales/manifest.json');p.add_argument('--id',action='append');p.add_argument('--sin-rtl',action='store_true');a=p.parse_args()
    try:r=evaluar(a.balc,a.arquitectura,a.salida,a.manifest,a.id,not a.sin_rtl)
    except (ValueError,OSError,RuntimeError) as err:print(err,file=sys.stderr);return 1
    print('Pares semánticos:',r['semanticos'],'; categorías acreditadas:',r['categorias_acreditadas'],'; estrictos:',r['estrictos'])
    return 0 if r['semanticos']==len(r['casos']) else 2
if __name__=='__main__':raise SystemExit(main())
