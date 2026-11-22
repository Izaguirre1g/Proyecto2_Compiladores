"""Campaña O0/O1/O2 sobre el manifiesto oficial, M2 y RTL reales.

Reutiliza B5/B1 y los oráculos de evaluar_entrega_final. La sonda O2 acredita
el retorno real de main sin inventar HALT; B1 continúa juzgando estados activos.
Los artefactos pertenecen a un destino nuevo externo; Arquitectura es lectura.
"""
from pathlib import Path
import argparse,csv,hashlib,importlib.util,json,os,re,shlex,sys
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2]
import evaluar_entrega_final as ev
import validar_integracion_ir_rtl as i3

def observables(caso,meta,datos):
    """Proyecta identidades originales a su marco final, sin cambiar oráculos.

    Algunos nombres del manifiesto son etiquetas lógicas (cuadrado4), no
    símbolos BAL. Sus offsets O0 explícitos se contrastan con la tabla original
    que publica O2, y después se trasladan al marco O2. No se reutiliza el SP O0.
    """
    fn=next(x for x in meta['funciones'] if x['nombre']=='main');salida={}
    objetos=[x for x in meta['objetos'] if x['funcion']=='main']
    for nombre,esperado in caso['observables'].items():
        m=re.fullmatch(r'(\w+)(?:\[([0-9]+)\])?',nombre)
        if not m:raise ValueError('observable sin representación acreditada')
        coincidencias=[x for x in objetos if x['nombre']==m[1]]
        origen='identidad_semantica'
        if len(coincidencias)==1:
            obj=coincidencias[0];desplazamiento=4*int(m[2] or 0)
        elif not coincidencias:
            relativo=caso['offsets_o0'][nombre]
            candidatos=[x for x in objetos if x['offset']<=relativo<x['offset']+x['bytes']]
            if len(candidatos)!=1:raise ValueError('alias lógico ambiguo en tabla original')
            obj=candidatos[0];desplazamiento=relativo-obj['offset'];origen='offset_o0_en_tabla_original'
        else:raise ValueError('observable ambiguo por shadowing')
        if desplazamiento+4>obj['bytes']:raise ValueError('observable fuera del objeto')
        direccion=65536-fn['marco']+obj['offset']+desplazamiento
        valor=int.from_bytes(datos[direccion:direccion+4],'little')
        salida[nombre]=dict(esperado=esperado,valor=valor,direccion=direccion,id=obj['id'],origen=origen)
        if valor!=esperado:raise ValueError('resultado lógico distinto: '+nombre)
    return salida

def categoria(caso,meta):
    """Exige cambios de la pasada específica, separados de corrección y B1."""
    tipo=caso['categoria']
    if tipo=='loop_unrolling':return meta['unroll']>0
    if tipo=='renombramiento':return meta['rename']>0 and meta['war_eliminadas']+meta['waw_eliminadas']>0
    if tipo=='dce':return meta['dce']>0
    if tipo=='spilling':return meta['loads']>0 and meta['stores']>0
    return tipo=='saltos_funciones'

def evaluar(balc,arquitectura,destino,manifest,ids=None,rtl=True):
    """Construye ambas referencias y O2 de nuevo, conservando cada rechazo.

    No acepta campañas históricas como sustituto de ejecuciones. BIN/MEM/ASM
    se contrastan entre sí y contra el ensamblador ISA antes de ejecutar RTL.
    """
    balc=Path(balc).resolve();arquitectura=Path(arquitectura).resolve();destino=Path(destino).resolve()
    m=ev.leer_manifest(manifest)
    if destino.exists() or destino.is_relative_to(ROOT) or destino.is_relative_to(arquitectura):raise ValueError('destino externo nuevo requerido')
    destino.mkdir(parents=True)
    anterior=ev.evaluar(balc,arquitectura,destino/'O0-O1',manifest,ids,rtl)
    arq=i3.copiar_arquitectura(arquitectura,destino);bancos=ev.compilar_bancos(arq,destino) if rtl else {}
    sim=[s for s in i3.FUENTES if s.startswith('src/Simulador_VLIW/')]
    i3.ejecutar(shlex.split(os.environ.get('CC','cc'))+['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-Iinclude','tools/vliw/sonda_o2.c',*sim,'-o',destino/'sonda'],destino)
    spec=importlib.util.spec_from_file_location('runner_o2',arq/'tools/run_program.py');runner=importlib.util.module_from_spec(spec);sys.path.insert(0,str(arq/'tools'));spec.loader.exec_module(runner)
    filas=[]
    for caso in m['casos']:
        if ids and caso['id'] not in ids:continue
        d=destino/caso['id'];d.mkdir();fila=dict(id=caso['id'],categoria=caso['categoria'],estado='BLOQUEADO',semantica=False,categoria_acreditada=False,rtl=[],b1_o0_o2=None)
        try:
            fuente=d/'programa.bal';fuente.write_bytes((ROOT/caso['fuente']).read_bytes())
            i3.ejecutar([balc,'-O2','-s',fuente],d)
            meta=json.loads(fuente.with_suffix('.o2.json').read_text());fn=next(x for x in meta['funciones'] if x['nombre']=='main')
            bundles=ev.validar_imagen(fuente.with_suffix('.bin'),fuente.with_suffix('.mem'))
            if bundles!=meta['bundles'] or bundles>1023:raise ValueError('capacidad/identidad de imagen')
            if i3.ensamblador(arq).assemble_source(fuente.with_suffix('.asm').read_text()).memory_text()!=fuente.with_suffix('.mem').read_text():raise ValueError('codificación ISA diferente')
            ops=meta['operaciones']
            if len(ops)!=meta['cantidad_operaciones'] or [op['indice'] for op in ops]!=list(range(len(ops))):raise ValueError('procedencia física incompleta')
            for op in ops:
                indice=op['ir_spilling']
                if indice is not None and not 0<=indice<meta['cantidad_ir_spilling']:raise ValueError('origen IR fuera de rango')
                if not 0<=op['bundle']<bundles:raise ValueError('bundle físico fuera de imagen')
            i3.ejecutar([destino/'sonda',fuente.with_suffix('.mem'),str(meta['pc_fin']),str(fn['inicio_pc']),str(fn['fin_pc']),d,'100000'],d)
            retorno=json.loads((d/'retorno.json').read_text());metricas=json.loads((d/'sim-metricas.json').read_text());regs=json.loads((d/'sim-registros.json').read_text());datos=(d/'sim-memoria.bin').read_bytes();hor=retorno['corte']
            obs=observables(caso,meta,datos)
            for modo in ((0,2) if rtl else ()):
                sub=d/('rtl-'+str(modo));sub.mkdir()
                out=i3.ejecutar(['vvp','-n',bancos[modo],f'+MEM={fuente.with_suffix(".mem")}',f'+REGDUMP={sub}/registers.txt',f'+MEMDUMP={sub}/memory.txt',f'+BUNDLES={bundles}',f'+CYCLES={hor}',f'+REQUESTED_MEM_MODE={modo}'],sub)
                md=runner.execution_metadata(out,modo,bundles,hor,dict(L2_MODEL=0));md['architecture_state']=ev.estado_publico(out)
                (sub/'execution.json').write_text(json.dumps(md,indent=2)+'\n');i3.contrastar_rtl(sub,regs,datos)
                publico=metricas['arquitectura'];estado=md['architecture_state']
                if (estado['crypto'],estado['traps'],estado['trap_pc'])!=(publico['estado_crypto'],publico['trap_count32'],publico['trap_pc']):raise ValueError('estado CRIPTO/traps distinto')
                fila['rtl'].append(md)
            ref=next(x for x in anterior['casos'] if x['id']==caso['id'])
            if not ref['semantica_acreditada']:raise ValueError('referencias O0/O1 no acreditadas')
            b1=ev.comparar_b1(destino/'O0-O1/comparador',ref['variantes'][0],dict(ruta_mem=str(fuente.with_suffix('.mem')),horizonte=hor),d)
            fila.update(estado='PASS',semantica=True,categoria_acreditada=categoria(caso,meta),meta=meta,metricas=metricas,observables=obs,retorno=retorno,b1_o0_o2=b1,mem_sha256=hashlib.sha256(fuente.with_suffix('.mem').read_bytes()).hexdigest())
        except (OSError,ValueError,RuntimeError,StopIteration) as exc:fila['diagnostico']=str(exc)
        filas.append(fila);(d/'resultado.json').write_text(json.dumps(fila,ensure_ascii=False,indent=2)+'\n');print(caso['id'],fila['estado'],flush=True)
    resultado=dict(compiler_sha256=hashlib.sha256(balc.read_bytes()).hexdigest(),casos=filas,referencias=anterior,semanticos=sum(x['semantica'] for x in filas),categorias=sum(x['categoria_acreditada'] for x in filas),estrictos=sum(x.get('b1_o0_o2',{}).get('resultado')=='EQUIVALENTE' for x in filas if x.get('b1_o0_o2')))
    (destino/'resultado.json').write_text(json.dumps(resultado,ensure_ascii=False,indent=2)+'\n')
    with (destino/'matriz.csv').open('w',newline='') as f:
        writer=csv.writer(f);writer.writerow(['id','categoria','semantica_O0_O1_O2','transformacion_O2','RTL_O2','B1_O0_O2'])
        for x in filas:writer.writerow([x['id'],x['categoria'],x['semantica'],x['categoria_acreditada'],len(x['rtl']),x.get('b1_o0_o2')])
    return resultado

def main():
    """CLI explícita: un bloqueo devuelve 2 y permanece en su fila."""
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--balc',type=Path,required=True);p.add_argument('--arquitectura',type=Path,required=True);p.add_argument('--salida',type=Path,required=True);p.add_argument('--manifest',type=Path,default=ROOT/'tests/vliw/programas_finales/manifest.json');p.add_argument('--id',action='append');p.add_argument('--sin-rtl',action='store_true');a=p.parse_args()
    try:r=evaluar(a.balc,a.arquitectura,a.salida,a.manifest,a.id,not a.sin_rtl)
    except (OSError,ValueError,RuntimeError) as exc:print(exc,file=sys.stderr);return 1
    print('Semánticos:',r['semanticos'],'categorías:',r['categorias'],'estrictos:',r['estrictos'])
    return 0 if r['semanticos']==len(r['casos']) and r['categorias']==len(r['casos']) else 2
if __name__=='__main__':raise SystemExit(main())
