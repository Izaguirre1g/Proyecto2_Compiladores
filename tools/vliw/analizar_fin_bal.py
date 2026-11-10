"""Diagnostica la sentinela BAL mediante correspondencia ASM/BALB y traza S6."""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile

sys.dont_write_bytecode = True
_spec = importlib.util.spec_from_file_location('aislamiento_b3', Path(__file__).with_name('validar_pipeline_bal.py'))
b3 = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(b3)
b2 = b3.b2
LIMITACIONES = [
    'Observar un salto o un PC repetido no certifica terminación semántica.',
    'El compilador conserva etiquetas como comentarios en ASM; el mapeo verifica bytes, no autentica su procedencia.',
    'Un ret compatible con el enlace inicial sigue siendo candidato: no hay identidad dinámica pública de llamadas.',
    'M2 no expone SimCiclo; la sonda diagnóstica usa S6 público y compara M1/DMEM con una sesión M2 independiente.',
    'No se corta el horizonte al observar la sentinela ni se cambia LIMITE por otro estado.',
    'Los dos flancos posteriores se registran si el presupuesto permite ejecutarlos; no se presumen inocuos.',
    'B1/B2 conservan sus contratos y sus resultados; no se acredita optimización.',
    'Solo se conserva un prefijo de 128 eventos, además de observaciones destacadas independientes del prefijo.',
]


def localizar(texto, nombre):
    # La salida real de resolucion_saltos convierte etiquetas en comentarios.
    # Se conserva esa distinción para no atribuirles semántica de símbolo CB.
    encontrados = []
    for n, linea in enumerate(texto.splitlines(), 1):
        m = re.match(r'^\s*(#\s*)?'+re.escape(nombre)+r'\s*:', linea)
        if m:
            encontrados.append(dict(linea=n, naturaleza='anotacion' if m[1] else 'etiqueta'))
    b2.exigir(len(encontrados) <= 1, 'etiqueta/anotación ambigua o duplicada: '+nombre)
    return encontrados[0] if encontrados else None


def clasificar(d, etiqueta):
    # Hecho dinámico y certificación se mantienen separados incluso cuando
    # el candidato coincide con la convención ret/x1 de la llamada inicial.
    b2.exigir(type(d) is dict and type(d.get('version')) is int and d['version'] == 1 and d.get('codigo_coincide') is True,
              'protocolo/correspondencia de sonda inválido')
    for k in ('patron_valido', 'error_dinamico'):
        b2.booleano(d.get(k), k)
    patron = etiqueta is not None and d['patron_valido']
    m = d.get('metricas')
    if m is not None:
        estado = d.get('estado_simulador')
        b2.entero(estado, 'estado S6', 10)
        if estado in (1, 2):
            b3.validar_metricas(m, 0 if estado == 1 else 2, (1 << 64)-1)
    c = d.get('comparacion_m2')
    b2.campos(c, ('disponible','memoria_completa_igual','metricas_iguales','estado_igual'))
    for k in c: b2.booleano(c[k],k)
    for k in ('llamada_inicial_observada','retorno_candidato','redireccion_a_sentinela','bucle_final_observado'):
        e=d.get(k)
        if e is None: continue
        b2.entero(e.get('ciclo'),'ciclo de evento', m['ciclos'] if m else b2.LIMITE_U64,1)
        for p in ('ex_pc','destino','redirect_pc'): b2.entero(e.get(p),p,(1<<32)-1)
        b2.exigir(e.get('bru_valida') is True and e.get('tomado') is True and e.get('excepcion') is False,
                  'evento BRU no aceptado')
    if d.get('bucle_final_observado') is not None:
        e=d['bucle_final_observado']
        b2.exigir(patron and e['ex_pc']==e['destino']==d['direccion_sentinela'] and e.get('operacion')=='sye',
                  'bucle final incoherente')
    if d.get('redireccion_a_sentinela') is not None:
        e=d['redireccion_a_sentinela']
        b2.exigir(patron and e['destino']==d['direccion_sentinela'] and e['ex_pc']!=e['destino'],
                  'redirección a sentinela incoherente')
    if d.get('retorno_candidato') is not None:
        e=d['retorno_candidato'];ll=d.get('llamada_inicial_observada')
        b2.exigir(patron and ll is not None and e['ciclo']>ll['ciclo'] and
                  e.get('operacion')=='syer' and e.get('rd')==0 and e.get('rs1')==1 and
                  e.get('inmediato')==0 and e['destino']==ll.get('enlace'), 'retorno candidato incoherente')
    error = d['error_dinamico'] or not c['disponible'] or not all(c[k] for k in c if k!='disponible')
    hechos=[]
    if patron: hechos.append('PATRON_ESTATICO_VALIDO')
    if patron and d.get('redireccion_a_sentinela'): hechos.append('REDIRECCION_A_SENTINELA_OBSERVADA')
    if patron and d.get('retorno_candidato'): hechos.append('RETORNO_A_CONTINUACION_CANDIDATO')
    if patron and d.get('bucle_final_observado'): hechos.append('BUCLE_FINAL_OBSERVADO')
    return dict(hechos=hechos, retorno_main_certificado=False,
                conclusion='ERROR' if error else 'BUCLE_FINAL_OBSERVADO' if 'BUCLE_FINAL_OBSERVADO' in hechos else 'FIN_NO_DEMOSTRADO',
                inferencia='Convención de retorno compatible; identidad de main no demostrada.'
                if 'RETORNO_A_CONTINUACION_CANDIDATO' in hechos else None,
                estado_simulador=m['estado'] if m is not None else d.get('estado_simulador'))


def analizar(config):
    fuentes=[p.resolve(strict=True) for p in config.fuente]
    b2.exigir(1<=len(fuentes)<=32 and len(set(fuentes))==len(fuentes),'cantidad de fuentes inválida/repetida')
    for p in fuentes:
        b2.exigir(p.suffix=='.bal','fuente .bal requerida');b3.identidad(p)
    herramientas={}
    for k in ('balc','sonda'):
        p=getattr(config,k).resolve(strict=True)
        herramientas[k]=b3.identidad(p);b2.exigir(os.access(p,os.X_OK),'ejecutable sin permiso')
        setattr(config,k,p)
    salida=config.salida.absolute();padre=salida.parent.resolve(strict=True)
    b2.exigir(not salida.exists() and not salida.is_symlink(),'salida existente')
    for p in fuentes: b2.exigir(not padre.is_relative_to(p.parent),'salida dentro de carpeta fuente')
    casos=[];anfitrion={}
    with tempfile.TemporaryDirectory(prefix='.b4-',dir=padre) as tmp:
        raiz=Path(tmp)/'evidencia';raiz.mkdir()
        for i,fuente in enumerate(fuentes,1):
            id_='caso-%03d'%i;d=raiz/id_;d.mkdir();h=[];anfitrion[id_]=h
            c=dict(programa=b3.identidad(fuente),patron_estatico=None,sonda=None,
                   conclusion='ERROR',limitaciones=LIMITACIONES)
            casos.append(c)
            try:
                c['dependencias']=b3.copiar_fuentes(fuente,d/'fuentes')
                copia=d/'fuentes'/fuente.name;binario=d/'programa.bin';asm=copia.with_suffix('.asm')
                rc,_,_,motivo=b3.invocar([config.balc,'-s','-o',binario,copia],config.timeout,d,'compilador',h)
                c['compilacion']=dict(codigo=rc,motivo=motivo)
                if rc!=0 or motivo: continue
                mem=binario.with_suffix('.mem')
                c['formato']=b3.validar_formato(binario,mem)
                identidades={k:dict(b3.identidad(p),ruta=id_+'/'+str(p.relative_to(d)))
                             for k,p in (('asm',asm),('balb',binario),('mem',mem))}
                c['identidad_artefactos']=identidades
                texto=b3.leer(asm).decode('utf-8')
                sent=localizar(texto,'__arranque_fin');main_=localizar(texto,'__fn_main')
                c['patron_estatico']=dict(sentinela=sent,main=main_)
                rc,out,_,motivo=b3.invocar([config.sonda,asm,binario,str(sent['linea'] if sent else 0),
                                          str(main_['linea'] if main_ else 0),str(config.presupuesto)],
                                          config.timeout,d,'sonda',h)
                if rc!=0 or motivo:
                    c['diagnostico']=dict(codigo=rc,motivo=motivo);continue
                s=b2.decodificar(out);resultado=clasificar(s,sent)
                if s.get('metricas') is not None:
                    b2.exigir(s['metricas']['ciclos']<=config.presupuesto,'sonda excedió el presupuesto')
                c.update(resultado);c['sonda']=s
                c['direccion_sentinela']=s['direccion_sentinela']
                c['patron_estatico']['verificado']=bool(sent and s['patron_valido'])
                # El diagnóstico nunca puede legitimar cambios de artefactos.
                for k,p in (('asm',asm),('balb',binario),('mem',mem)):
                    b2.exigir(b3.identidad(p)['sha256']==identidades[k]['sha256'],'artefacto cambió: '+k)
            except (OSError,ValueError,TypeError,KeyError,OverflowError,RecursionError) as e:
                c.update(conclusion='ERROR',diagnostico=str(e))
        for k,v in herramientas.items(): b2.exigir(b3.identidad(getattr(config,k))==v,'herramienta cambió')
        r=dict(version=1,herramientas=herramientas,presupuesto=config.presupuesto,
               casos=casos,limitaciones=LIMITACIONES)
        for n,v in (('reporte.json',r),('anfitrion.json',anfitrion)):
            with (raiz/n).open('x',encoding='utf-8') as f:
                json.dump(v,f,ensure_ascii=False,sort_keys=True,indent=2,allow_nan=False)
                f.write('\n');f.flush();os.fsync(f.fileno())
        b2.exigir(not salida.exists() and not salida.is_symlink(),'salida apareció durante el análisis')
        os.rename(raiz,salida)
    return r


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('balc','sonda','salida'):p.add_argument('--'+k,type=Path,required=True)
    p.add_argument('--fuente',type=Path,action='append',required=True)
    p.add_argument('--presupuesto',required=True)
    p.add_argument('--timeout',type=float,default=30)
    try:
        a=p.parse_args(argv)
        if not re.fullmatch('[0-9]{1,7}',a.presupuesto):p.error('presupuesto decimal requerido')
        a.presupuesto=int(a.presupuesto)
        if a.presupuesto>1000000 or not math.isfinite(a.timeout) or not 0<a.timeout<=3600:
            p.error('presupuesto/timeout fuera de rango')
    except SystemExit as e:return 0 if e.code==0 else 64
    try:
        r=analizar(a);print(a.salida.absolute()/'reporte.json')
        return 1 if any(c['conclusion']=='ERROR' for c in r['casos']) else 0
    except (OSError,ValueError,OverflowError,RecursionError) as e:
        print('análisis BAL: '+str(e),file=sys.stderr);return 1


if __name__=='__main__':sys.exit(main())
