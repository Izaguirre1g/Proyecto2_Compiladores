"""Coordina B4 y una captura arquitectónica anterior al bucle final BAL."""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

sys.dont_write_bytecode=True
_spec=importlib.util.spec_from_file_location('evidencia_b4',Path(__file__).with_name('analizar_fin_bal.py'))
b4=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(b4)
b3,b2=b4.b3,b4.b2
CONTRATO='bal-corte-v1-retorno-wb-corredor-nop'
LIMITACIONES=[
    'El retorno es compatible con el enlace inicial; no certifica identidad semántica de main.',
    'Los ciclos hasta el corte incluyen arranque, llamada, NOP, ejecución y retiro del retorno; no son exclusivos de main.',
    'Se acepta únicamente una continuación estática de NOP hasta la sentinela verificada por B4.',
    'Cualquier excepción observada impide aceptar el corte, aunque un contrato futuro pudiera admitirla.',
    'La sonda se detiene sin alterar el estado S6; el contraste M2 utiliza avances individuales al mismo horizonte.',
    'El primer bucle final pertenece a la ejecución independiente B4; no se ejecuta para capturar el corte B5.',
    'Igualdad pública con M2 demuestra consistencia de instrumentación, no corrección semántica universal.',
    'No se modifican B1/B2, no se certifica terminación ni se declara speedup u optimización.',
]


def validar(d,presupuesto,memoria):
    b2.exigir(type(d) is dict and type(d.get('version')) is int and d['version']==1,'protocolo B5 incompatible')
    b2.exigir(d.get('retorno_main_certificado') is False,'certificación semántica no autorizada')
    b2.exigir(d.get('conclusion') in ('CORTE_ARQUITECTONICO_CANDIDATO','CORTE_NO_DEMOSTRADO',
                                    'PRESUPUESTO_AGOTADO','ERROR'),'clasificación desconocida')
    b2.entero(d.get('ciclos_observados'),'ciclos observados',presupuesto)
    b2.entero(d.get('estado_simulador'),'estado S6',10)
    b2.entero(d.get('observacion_m1'),'estado M1',9)
    for k in ('contrato_local','patron_local','excepciones_observadas'):b2.booleano(d.get(k),k)
    c=d.get('comparacion_m2')
    b2.campos(c,('disponible','memoria_completa_igual','metricas_iguales','estado_igual'))
    for k in c:b2.booleano(c[k],k)
    punto=d.get('punto_corte');m=d.get('metricas')
    if d['conclusion']!='CORTE_ARQUITECTONICO_CANDIDATO':
        b2.exigir(punto is None and m is None,'captura publicada sin corte aceptado')
        b2.exigir(not memoria.exists(),'DMEM publicada sin corte aceptado')
        return
    b2.exigir(d['contrato_local'] and d['patron_local'] and not d['excepciones_observadas'] and
              d['observacion_m1']==0 and all(c.values()),'garantías de corte incompletas')
    b2.campos(punto,('ciclo','flanco_aceptado','observado_m1'))
    b2.exigir(punto['flanco_aceptado'] is True and punto['observado_m1'] is True and
              punto['ciclo']==d['ciclos_observados'],'flanco de corte inválido')
    b2.exigir(d.get('primer_bucle_final') is None,'sentinela ya ejecutada antes del corte')
    ret,ll=d.get('retorno_candidato'),d.get('llamada_inicial')
    for e in (ret,ll):
        b2.campos(e,('ciclo','ex_pc','destino','enlace'))
        b2.entero(e['ciclo'],'ciclo de control',punto['ciclo'],1)
        for k in ('ex_pc','destino','enlace'):b2.entero(e[k],k,(1<<32)-1)
    b2.exigir(ll['ciclo']<ret['ciclo']<punto['ciclo'] and ret['destino']==ll['enlace'],'secuencia de retorno inválida')
    b2.exigir(d['estado_simulador']<=2,'estado S6 incompatible con una captura válida')
    b2.exigir(type(m) is dict and type(m.get('version')) is int and m['version']==1 and
              m.get('naturaleza')=='dinamica' and m.get('estado')==('ACTIVO','TERMINADO','LIMITE')[d['estado_simulador']],
              'snapshot M1/estado incompatible')
    for k in ('ciclos','admitidos','completados','instrucciones_utiles','bundles_nop','anulados','traps'):
        b2.entero(m.get(k),k)
    b2.exigir(m['ciclos']==punto['ciclo'] and m['traps']==0,'horizonte M1 o excepciones incompatibles')
    b2.numero(m.get('ipc'),'IPC',True)
    a=m.get('arquitectura');b2.exigir(type(a) is dict and type(a.get('registros')) is list and len(a['registros'])==32,'GPR incompletos')
    for v in a['registros']:b2.entero(v,'GPR',(1<<32)-1)
    for k in ('pc','estado_crypto','trap_count32','trap_pc'):b2.entero(a.get(k),k,(1<<32)-1)
    b2.exigir(m.get('excepciones')==[],'excepciones no vacías')
    evidencia=d.get('evidencia_pipeline')
    b2.exigir(type(evidencia) is list and 1<=len(evidencia)<=16,'evidencia de pipeline ausente')
    e=evidencia[-1]
    b2.exigir(e.get('ciclo')==punto['ciclo'] and e.get('ruta_continuacion') is True and
              e.get('retorno_retirado_wb') is True,'drenado o ruta no demostrados')
    for k in ('wb_gpr_pendiente','wb_crypto_pendiente','ex_lsu_pendiente','ex_crypto_pendiente'):
        b2.exigir(e.get(k) is False,'efecto pendiente: '+k)
    b2.exigir(e['if_id']['valido'] is True and e['if_id']['pc']==ll['enlace'],'continuación no alcanzada')
    sent=d.get('direccion_sentinela');b2.entero(sent,'sentinela',16368)
    b2.exigir(ll['enlace']<=sent,'corredor de continuación inválido')
    for nombre in ('if_id','id_ex','ex_wb'):
        etapa=e[nombre];b2.booleano(etapa.get('valido'),'validez de etapa')
        if not etapa['valido']:continue
        op=etapa.get('operaciones');b2.exigir(type(op) is list and len(op)==4,'operaciones pendientes incompletas')
        b2.entero(etapa.get('pc'),'PC de etapa',16368)
        if nombre=='ex_wb':
            b2.exigir(op==['nop']*4,'retiro posterior no inocuo pendiente')
        else:
            b2.exigir(ll['enlace']<=etapa['pc']<=sent and
                      (op==['nop']*4 or (etapa['pc']==sent and op==['nop','nop','sye','nop'])),
                      'instrucción pendiente fuera de la continuación segura')
    b2.exigir(m.get('pendientes_por_etapa')==[int(e[k]['valido']) for k in ('if_id','id_ex','ex_wb')],
              'ocupación M1/pipeline inconsistente')
    b2.exigir(len(b3.leer(memoria))==65536,'captura DMEM debe contener 65536 bytes')


def analizar(config):
    # B4 conserva la compilación y prueba la correspondencia ASM/BALB completa.
    # B5 consume esa evidencia en vez de añadir otro localizador o ensamblador.
    fuentes=[p.resolve(strict=True) for p in config.fuente]
    salida=config.salida.absolute();padre=salida.parent.resolve(strict=True)
    b2.exigir(not salida.exists() and not salida.is_symlink(),'salida existente')
    for p in fuentes:b2.exigir(not padre.is_relative_to(p.parent),'salida dentro de carpeta fuente')
    sonda=config.sonda.resolve(strict=True);identidad=b3.identidad(sonda)
    b2.exigir(os.access(sonda,os.X_OK),'sonda sin permiso de ejecución')
    casos=[];anfitrion={}
    with tempfile.TemporaryDirectory(prefix='.b5-',dir=padre) as tmp:
        raiz=Path(tmp)/'evidencia';raiz.mkdir()
        base=b4.analizar(argparse.Namespace(balc=config.balc,sonda=config.sonda_fin,fuente=fuentes,
                                           salida=raiz/'b4',presupuesto=config.presupuesto,timeout=config.timeout))
        for i,anterior in enumerate(base['casos'],1):
            id_='caso-%03d'%i;h=[];anfitrion[id_]=h
            c=dict(programa=anterior['programa'],contrato=CONTRATO,b4=anterior,
                   imagen=None,sentinela=None,llamada_inicial=None,retorno_candidato=None,
                   primer_bucle_final=None,punto_corte=None,evidencia_pipeline=None,
                   snapshot_arquitectonico=None,metricas=None,comparacion_m2=None,
                   retorno_main_certificado=False,conclusion='ERROR',limitaciones=LIMITACIONES)
            casos.append(c)
            try:
                if anterior['conclusion']=='ERROR':continue
                b=anterior['sonda'];pc=b['direccion_sentinela'] if b['patron_valido'] else None
                ll=b['llamada_inicial_observada']
                c['sentinela']=dict(pc=pc,verificada=bool(anterior['patron_estatico']['verificado']))
                imagen=raiz/'b4'/anterior['identidad_artefactos']['balb']['ruta']
                c['imagen']=dict(b3.identidad(imagen),ruta='b4/'+anterior['identidad_artefactos']['balb']['ruta'])
                copia=raiz/(id_+'.dmem')
                args=[sonda,imagen,str(pc) if pc is not None else '-',
                      str(ll['ex_pc']) if ll else '-',str(ll['destino']) if ll else '-',str(config.presupuesto),copia]
                rc,out,_,motivo=b3.invocar(args,config.timeout,raiz,id_,h)
                if rc!=0 or motivo:
                    c['diagnostico']=dict(codigo=rc,motivo=motivo);continue
                d=b2.decodificar(out);validar(d,config.presupuesto,copia)
                b2.exigir(b3.identidad(imagen)['sha256']==c['imagen']['sha256'],'imagen cambió durante la sonda')
                for k in ('llamada_inicial','retorno_candidato','punto_corte','evidencia_pipeline','metricas',
                          'comparacion_m2','conclusion','estado_simulador','ciclos_observados','observacion_m1','excepciones_observadas'):
                    c[k]=d[k]
                # B4 ejecuta el horizonte completo por separado. Ese primer
                # salto es referencia posterior, no parte de la captura B5.
                c['primer_bucle_final']=dict(evento=b['bucle_final_observado'],origen='ejecucion_independiente_B4')
                if d['punto_corte'] is not None:
                    b2.exigir(pc is not None and c['sentinela']['verificada'] and d['direccion_sentinela']==pc,
                              'sentinela B4/B5 no verificada')
                    if b['bucle_final_observado']:
                        b2.exigir(d['punto_corte']['ciclo']<b['bucle_final_observado']['ciclo'],'corte posterior al primer salto final')
                    b2.exigir(ll is not None and d['llamada_inicial']['ex_pc']==ll['ex_pc'] and
                              d['retorno_candidato']['destino']==ll['enlace'],'evidencia B4/B5 inconsistente')
                    c['snapshot_arquitectonico']=dict(d['metricas']['arquitectura'],
                        dmem=dict(b3.identidad(copia),ruta=copia.name))
            except (OSError,ValueError,TypeError,KeyError,IndexError,OverflowError,RecursionError) as e:
                c.update(conclusion='ERROR',punto_corte=None,snapshot_arquitectonico=None,metricas=None,diagnostico=str(e))
        b2.exigir(b3.identidad(sonda)==identidad,'sonda cambió durante el análisis')
        r=dict(version=1,contrato=CONTRATO,sonda=identidad,presupuesto=config.presupuesto,casos=casos,limitaciones=LIMITACIONES)
        for n,v in (('reporte.json',r),('anfitrion.json',anfitrion)):
            with (raiz/n).open('x',encoding='utf-8') as f:
                json.dump(v,f,ensure_ascii=False,sort_keys=True,indent=2,allow_nan=False)
                f.write('\n');f.flush();os.fsync(f.fileno())
        b2.exigir(not salida.exists() and not salida.is_symlink(),'salida apareció durante la ejecución')
        os.rename(raiz,salida)
    return r


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('balc','sonda','sonda-fin','salida'):p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--fuente',type=Path,action='append',required=True)
    p.add_argument('--presupuesto',required=True);p.add_argument('--timeout',type=float,default=30)
    try:
        a=p.parse_args(argv)
        if not re.fullmatch('[0-9]{1,7}',a.presupuesto):p.error('presupuesto decimal requerido')
        a.presupuesto=int(a.presupuesto)
        if a.presupuesto>1000000 or not math.isfinite(a.timeout) or not 0<a.timeout<=3600:p.error('presupuesto/timeout fuera de rango')
    except SystemExit as e:return 0 if e.code==0 else 64
    try:
        r=analizar(a);print(a.salida.absolute()/'reporte.json')
        return 1 if any(c['conclusion']=='ERROR' for c in r['casos']) else 0
    except (OSError,ValueError,OverflowError,RecursionError) as e:
        print('corte BAL: '+str(e),file=sys.stderr);return 1


if __name__=='__main__':sys.exit(main())
