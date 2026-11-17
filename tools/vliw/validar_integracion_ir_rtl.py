"""Valida el productor parcial IR-VLIW contra M2 y RTL en copias externas.

Recibe casos/perfiles de prueba, dependencias reales y un destino nuevo. Posee
sus ejecutables/artefactos; los repositorios son fuentes de lectura. El arnés
ISA aporta SP y observación estable; el fragmento original conserva sus bytes.
No es un backend BAL, ni configura opciones oficiales -O de la CLI.
"""
from pathlib import Path
import argparse
import importlib.util
import json
import hashlib
import os
import shlex
import shutil
import subprocess
import sys
import time
sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
CASOS = ('copia', 'suma', 'resta', 'negacion', 'negativos', 'minimo', 'maximo',
         'independientes', 'raw', 'war', 'waw', 'presion', 'tomado', 'no_tomado', 'directo', 'backedge', 'dce')
FUENTES = ['src/ir_vliw.c', 'src/ir_spilling.c', 'src/ir_spilling_marco.c',
    'src/ir_spilling_backend.c', 'src/Generacion_Codigo/funciones_marco.c',
    'src/Representacion_Intermedia/ir.c', 'src/Analisis_Sintactico/ast.c',
    'src/Codificacion_Binaria/codificacion_binaria.c', 'src/DCE/dce.c',
    'reordenamiento/reordenamiento.c', 'loop_unrolling/loop_unrolling.c'] + [
    'src/Calendarizacion_VLIW/' + n + '.c' for n in ('vliw_proyeccion_fisica',
    'vliw_dag_fisico', 'vliw_cfg_fisico', 'vliw_metadatos', 'vliw_integracion',
    'vliw_cadena_fisica', 'vliw_scheduler', 'vliw_emisor', 'vliw_visualizador')] + [
    'src/Simulador_VLIW/sim_' + n + '.c' for n in ('decode', 'cargador', 'ejecucion',
    'metricas', 'pipeline', 'alu', 'lsu', 'bru', 'crypto')]


# PR #68 publica USE/DEF y mapeo físico como dependencias reales de DCE,
# reordenamiento y productor. Las referencias anteriores carecen de estos
# módulos: el inventario de fuentes determina qué snapshot se está enlazando.
FUENTES += [str(p.relative_to(ROOT)) for nombre in
    ('ir_defuse','ir_registros_fisicos','ir_latencias_efectos')
    if (p := ROOT/'src/Representacion_Intermedia'/(nombre+'.c')).is_file()]


class Rechazo(RuntimeError):
    """La prueba no puede acreditar resultados; conserva la causa y los logs."""


# Las salidas completas y códigos quedan asociados al comando real. Errores de
# infraestructura no se convierten en aprobaciones ni en omisiones silenciosas.
def ejecutar(cmd, destino, cwd=ROOT, esperado=0, timeout=180):
    inicio = time.monotonic()
    p = subprocess.run(list(map(str, cmd)), cwd=cwd, capture_output=True, text=True,
                       timeout=timeout, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
    registro = dict(comando=list(map(str, cmd)), cwd=str(cwd), codigo=p.returncode,
                    esperado=esperado, segundos=time.monotonic()-inicio, stdout=p.stdout, stderr=p.stderr)
    with (destino/'comandos.jsonl').open('a') as f:
        f.write(json.dumps(registro, ensure_ascii=False)+'\n')
    if p.returncode != esperado:
        raise Rechazo(f'código {p.returncode}, esperado {esperado}: {cmd}\n{p.stdout}\n{p.stderr}')
    return p.stdout


# Compila el productor y consumidores originales junto al único driver nuevo.
# Los wrappers se enlazan solo al evaluar reservas; el resto llama APIs reales.
def compilar(destino, recursos=False):
    destino.mkdir(parents=True, exist_ok=True)
    exe = destino/('recursos' if recursos else 'integracion')
    cc = shlex.split(os.environ.get('CC', 'cc'))
    flags = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2',
             '-Iinclude', '-Ireordenamiento', '-Iloop_unrolling']
    extra = (['-DI3_FALLOS']+['-Wl,--wrap='+n for n in ('malloc','calloc','realloc')]) if recursos else []
    ejecutar(cc+flags+extra+FUENTES+['tests/vliw/test_integracion_ir_rtl.c','-o',exe], destino)
    return exe


# Importa el ensamblador existente sin escribir bytecode. No reimplementa ISA.
# El módulo conserva su nombre único para las dataclasses que consulta Python.
def ensamblador(arquitectura):
    nombre = 'assembler_i1a3'
    spec = importlib.util.spec_from_file_location(nombre, arquitectura/'tools/assembler.py')
    mod = importlib.util.module_from_spec(spec)
    sys.modules[nombre] = mod
    spec.loader.exec_module(mod)
    return mod


# Una copia completa de fuentes preserva ROOT del runner y sus includes. Nunca
# se ejecuta make ni se generan archivos sobre el árbol original de Arquitectura.
def copiar_arquitectura(origen, destino):
    if not (origen/'rtl-p2/top.sv').is_file():
        raise Rechazo('se requiere top real de Arquitectura Proyecto II')
    copia = destino/'arquitectura'
    shutil.copytree(origen, copia, ignore=shutil.ignore_patterns('.git','build','__pycache__'))
    consulta = subprocess.run(['git','-C',str(origen),'rev-parse','HEAD'],capture_output=True,text=True)
    (destino/'referencia-arquitectura.json').write_text(json.dumps(dict(origen=str(origen),commit=consulta.stdout.strip() if consulta.returncode==0 else None),indent=2)+'\n')
    return copia


# Compara contenido de las capturas completas después del vaciado acreditado.
# La igualdad se refiere a GPR/DMEM; los ciclos de caché permanecen separados.
def contrastar_rtl(carpeta, registros, memoria):
    metadata = json.loads((carpeta/'execution.json').read_text())
    pares = [line.split() for line in (carpeta/'registers.txt').read_text().splitlines()]
    rtl = [0]*32
    if len(pares)!=32 or {int(p[0].removeprefix('x')) for p in pares}!=set(range(32)):
        raise Rechazo('dump GPR incompleto, duplicado o fuera de rango')
    for indice, valor in pares:
        rtl[int(indice.removeprefix("x"))] = int(valor,16)
    datos = b''.join(int(line,16).to_bytes(4,'little') for line in (carpeta/'memory.txt').read_text().splitlines())
    if rtl != registros or datos != memoria:
        dr = [i for i in range(32) if rtl[i] != registros[i]]
        dm = next((i for i,(a,b) in enumerate(zip(datos,memoria)) if a != b),None)
        raise Rechazo(f'estados distintos en {carpeta}: registros={dr}, primer byte={dm}')
    if len(datos)!=65536:
        raise Rechazo('DMEM incompleta')
    return metadata


# Un escenario atraviesa el productor real, preserva bytes y valida el oráculo
# lógico antes de ejecutar RTL. El horizonte termina en el bucle del arnés;
# no se presenta como retorno semántico de main ni terminación del pipeline.
def validar_caso(exe, caso, perfil, destino, arquitectura, modos=(0,2), horizonte=1000):
    if caso not in CASOS or perfil not in ('base','dce','reorder','unroll') or any(type(m) is not int or m not in (0,1,2) for m in modos):
        raise Rechazo('caso o perfil no reconocido')
    destino.mkdir()
    ejecutar([exe,'generar',caso,destino,perfil],destino)
    oraculo = json.loads((destino/'oraculo.json').read_text())
    asm = ensamblador(arquitectura)
    programa = asm.assemble_source((destino/'programa.asm').read_text())
    lineas = programa.memory_text().splitlines()
    fragmento = (destino/'fragmento.mem').read_text().splitlines()
    if lineas[3:3+len(fragmento)] != fragmento or len(lineas)!=len(fragmento)+6:
        raise Rechazo('el arnés modificó el fragmento o sus límites')
    original = (destino/'fragmento.bin').read_bytes()
    if original != b''.join(bytes.fromhex(l)[::-1] for l in fragmento):
        raise Rechazo('CB binario y MEM distintos')
    (destino/'programa.mem').write_text(programa.memory_text())
    ejecutar([exe,'sim',destino/'programa.mem',destino,str(horizonte)],destino)
    registros = json.loads((destino/'sim-registros.json').read_text())
    memoria = (destino/'sim-memoria.bin').read_bytes()
    if registros[0]!=0 or registros[2]!=1000 or registros[1]!=0:
        raise Rechazo('x0, SP o RA no restaurados')
    for direccion, valor in oraculo['memoria'].items():
        address = int(direccion)
        if int.from_bytes(memoria[address:address+4],'little') != valor:
            raise Rechazo(f'oráculo lógico incorrecto: {caso}, dirección {address}')
    resultados = dict(caso=caso,perfil=perfil,oraculo=oraculo,simulador=json.loads((destino/'sim-metricas.json').read_text()),rtl=[])
    for modo in modos:
        carpeta = destino/('rtl-'+str(modo))
        config = destino/'config.json'
        config.write_text(json.dumps(dict(L2_MODEL=0)))
        ejecutar([sys.executable,arquitectura/'tools/run_program.py','--input',destino/'programa.asm',
            '--workdir',carpeta,'--cycles',str(horizonte),'--mem-mode',str(modo),'--config',config,'--no-vcd'],destino,cwd=arquitectura)
        metadata = contrastar_rtl(carpeta,registros,memoria)
        if metadata['effective_mode'] != modo or metadata['configuration']['L2_MODEL'] != 0 or metadata['validation'] != 'PASS':
            raise Rechazo('modo/configuración efectiva incompatible')
        publico = resultados['simulador']['arquitectura']
        if metadata['architecture_state']['crypto'] != publico['estado_crypto'] or metadata['architecture_state']['traps'] != publico['trap_count32']:
            raise Rechazo('estado criptográfico o traps distintos')
        resultados['rtl'].append(dict(modo=modo,metadata=metadata,comparacion='PASS'))
    resultados['huellas']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in destino.iterdir() if p.is_file()}
    (destino/'resultado.json').write_text(json.dumps(resultados,ensure_ascii=False,indent=2)+'\n')
    return resultados


# Recorre BAL/frontend/AST/IR reales para el ejemplo admitido por la CLI.
# El arranque externo se identifica por separado y conserva el fragmento exacto.
# Este programa no tiene observables de datos BAL: acredita ruta/control/estado.
def validar_preview_cli(exe,balc,arquitectura,destino):
    destino.mkdir()
    fuente=destino/'preview.bal'
    shutil.copy2(ROOT/'examples/vliw_ir_preview.bal',fuente)
    ejecutar([balc,'--vliw-ir=3','-v','-s','-p',fuente],destino)
    asm=ensamblador(arquitectura)
    fragmento=(destino/'preview.vliw.asm').read_text()
    codigo=asm.assemble_source(fragmento)
    if codigo.memory_text()!=(destino/'preview.vliw.mem').read_text():
        raise Rechazo('CLI física distinta de Arquitectura')
    texto='sumai x2,x0,1000 | nop | nop | nop\nnop\nnop\n'+fragmento+'nop\nnop\nnop | nop | sye x0,0 | nop\n'
    programa=asm.assemble_source(texto)
    if programa.binary()[48:48+len(codigo.binary())]!=codigo.binary():
        raise Rechazo('el arnés alteró los bytes de CLI')
    (destino/'programa.asm').write_text(texto)
    (destino/'programa.mem').write_text(programa.memory_text())
    ejecutar([exe,'sim',destino/'programa.mem',destino,'1000'],destino)
    registros=json.loads((destino/'sim-registros.json').read_text())
    memoria=(destino/'sim-memoria.bin').read_bytes()
    if registros[2]!=1000 or registros[1]!=0:
        raise Rechazo('CLI no restauró SP/RA')
    filas=[]
    for modo in (0,2):
        carpeta=destino/('rtl-'+str(modo))
        ejecutar([sys.executable,arquitectura/'tools/run_program.py','--input',destino/'programa.asm','--workdir',carpeta,'--cycles','1000','--mem-mode',str(modo),'--no-vcd'],destino,cwd=arquitectura)
        md=contrastar_rtl(carpeta,registros,memoria)
        if md['configuration']['L2_MODEL']!=0:
            raise Rechazo('CLI contrastada con modelo L2 provisional')
        filas.append(md)
    resultado=dict(ruta='BAL/frontend/AST/IR/spilling/ISA/cadena',fuente_sha256=hashlib.sha256(fuente.read_bytes()).hexdigest(),fragmento_inicio=48,fragmento_bytes=len(codigo.binary()),rtl=filas,autocontenido=False,main_certificado=False)
    (destino/'resultado.json').write_text(json.dumps(resultado,indent=2)+'\n')
    return resultado


# Ejecuta tres fuentes BAL existentes y calcula cortes B5 actuales. El arnés
# aporta centinelas, sin reubicar ni modificar el código tradicional generado.
def validar_tradicionales(exe, balc, arquitectura, destino):
    destino.mkdir()
    sim = [s for s in FUENTES if s.startswith('src/Simulador_VLIW/')]
    cc = shlex.split(os.environ.get('CC','cc'))
    flags = ['-std=c11','-Wall','-Wextra','-Werror','-pedantic','-O2','-Iinclude']
    for nombre,fuente,extra in [('fin','sonda_fin_bal.c',['src/Codificacion_Binaria/codificacion_binaria.c']),('corte','sonda_corte_bal.c',[])]:
        ejecutar(cc+flags+['tools/vliw/'+fuente]+sim+extra+['-o',destino/nombre],destino)
    cmd = [sys.executable,ROOT/'tools/vliw/analizar_corte_bal.py','--balc',balc,
        '--sonda',destino/'corte','--sonda-fin',destino/'fin','--salida',destino/'b5',
        '--presupuesto','20000','--timeout','60']
    for nombre in ('condicional','whale','llamadas'):
        cmd += ['--fuente',ROOT/'examples'/(nombre+'.bal')]
    ejecutar(cmd,destino,timeout=300)
    reporte=json.loads((destino/'b5/reporte.json').read_text())
    sys.path.insert(0,str(arquitectura/'tools'))
    spec=importlib.util.spec_from_file_location('runner_i1a3_trad',arquitectura/'tools/run_program.py')
    runner=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    filas=[]
    # Los tres oráculos proceden de la fuente: 20, 3 vueltas que suman 21,
    # y llamadas anidadas (2+3)+(4+5)=14. El offset es el layout público actual.
    for caso,(nombre,offset,valor) in zip(reporte['casos'],[('condicional',8,20),('whale',8,21),('llamadas',4,14)]):
        if caso['conclusion']!='CORTE_ARQUITECTONICO_CANDIDATO':
            raise Rechazo('B5 no acreditó corte para '+nombre)
        horizonte=caso['punto_corte']['ciclo']
        d=destino/nombre;d.mkdir()
        artefactos=caso['b4']['identidad_artefactos']
        asm=destino/'b5/b4'/artefactos['asm']['ruta']
        mem=destino/'b5/b4'/artefactos['mem']['ruta']
        programa=runner.assemble_source(asm.read_text())
        if programa.memory_text()!=mem.read_text():
            raise Rechazo('codificación tradicional distinta del ensamblador de Arquitectura')
        ejecutar([exe,'preservar',mem,d,str(horizonte)],d)
        registros=json.loads((d/'sim-registros.json').read_text())
        memoria=(d/'sim-memoria.bin').read_bytes()
        texto=asm.read_text().split('# __fn_main:',1)[1]
        import re
        marco=int(re.search(r'restai x2,x2,([0-9]+)',texto)[1])
        direccion=65536-marco+offset
        if int.from_bytes(memoria[direccion:direccion+4],'little')!=valor:
            raise Rechazo('resultado BAL incorrecto en '+nombre)
        metricas=json.loads((d/'sim-metricas.json').read_text())
        if nombre=='condicional' and not (metricas['saltos_tomados'] and metricas['saltos_no_tomados']):
            raise Rechazo('no se ejercitaron ambas decisiones de salto')
        resultados=[]
        # Instanciamos sin editar el banco público: conserva sus métricas,
        # watchdogs y flush. El wrapper solo inicializa s0–s15 durante el arranque.
        rtl=[str(p) for dr in ('rtl','rtl-p2') for p in sorted((arquitectura/dr).glob('*.sv'))]
        for modo in (0,2):
            work=d/('rtl-'+str(modo));work.mkdir()
            binario=work/'sim.vvp'
            compile_cmd=['iverilog','-g2012','-Wall','-Wno-timescale','-I'+str(arquitectura),'-I'+str(arquitectura/'rtl'),'-I'+str(arquitectura/'rtl-p2'),'-I'+str(arquitectura/'tb'),
                '-s','tb_i1a3_preservacion',f'-Ptb_i1a3_preservacion.MEM_MODE={modo}','-o',binario,*rtl,
                arquitectura/'tests/rtl/tb_assembled_program.sv',ROOT/'tests/vliw/rtl/tb_i1a3_preservacion.sv']
            ejecutar(compile_cmd,work)
            salida=ejecutar(['vvp','-n',binario,f'+MEM={mem}',f'+REGDUMP={work}/registers.txt',f'+MEMDUMP={work}/memory.txt',f'+BUNDLES={len(programa.bundles)}',f'+CYCLES={horizonte}',f'+REQUESTED_MEM_MODE={modo}'],work)
            metadata=runner.execution_metadata(salida,modo,len(programa.bundles),horizonte,dict(L2_MODEL=0))
            metadata.update(validation='PASS',cut='B5_POST_NBA_before_flush',compile_command=list(map(str,compile_cmd)))
            (work/'execution.json').write_text(json.dumps(metadata,indent=2)+'\n')
            contrastar_rtl(work,registros,memoria)
            resultados.append(dict(modo=modo,comparacion='PASS',metadata=metadata))
        fila=dict(caso=nombre,ruta='backend tradicional',corte=horizonte,marco=marco,direccion=direccion,esperado=valor,registros_preservados=list(range(16,32)),metricas=metricas,rtl=resultados,retorno_main_certificado=False)
        (d/'resultado.json').write_text(json.dumps(fila,ensure_ascii=False,indent=2)+'\n');filas.append(fila)
    (destino/'resumen.json').write_text(json.dumps(filas,ensure_ascii=False,indent=2)+'\n')
    return filas


# La herramienta exige un destino nuevo externo para conservar evidencias y
# evitar sobrescribir fuentes o resultados anteriores; no altera ninguna CLI.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--arquitectura',type=Path,required=True)
    parser.add_argument('--salida',type=Path,required=True)
    parser.add_argument('--caso',choices=CASOS,action='append')
    parser.add_argument('--perfil',choices=('base','dce','reorder','unroll'),default='base')
    parser.add_argument('--balc',type=Path,help='compilador externo: añade los tres casos tradicionales #34')
    parser.add_argument('--sin-rtl',action='store_true',help='solo CB/ensamblador y simulador; no acredita RTL')
    args = parser.parse_args()
    salida = args.salida.resolve()
    if salida.exists() or salida.is_relative_to(ROOT) or salida.is_relative_to(args.arquitectura.resolve()):
        parser.error('salida externa nueva requerida')
    if args.sin_rtl and args.balc:
        parser.error('--balc requiere la validación RTL de los casos tradicionales')
    salida.mkdir(parents=True)
    try:
        arquitectura = copiar_arquitectura(args.arquitectura.resolve(),salida)
        exe = compilar(salida)
        resultados=[]
        for caso in args.caso or CASOS:
            resultados.append(validar_caso(exe,caso,args.perfil,salida/caso,arquitectura,() if args.sin_rtl else (0,2)))
        if args.balc:
            validar_preview_cli(exe,args.balc.resolve(),arquitectura,salida/'preview-cli')
            validar_tradicionales(exe,args.balc.resolve(),arquitectura,salida/'tradicionales')
        (salida/'resumen.json').write_text(json.dumps(resultados,ensure_ascii=False,indent=2)+'\n')
        print(f'PASS: {len(resultados)} casos, perfil={args.perfil}, RTL={not args.sin_rtl}')
        return 0
    except (Rechazo,OSError,ValueError,subprocess.TimeoutExpired) as exc:
        (salida/'error.txt').write_text(str(exc)+'\n')
        print(exc,file=sys.stderr)
        return 1


if __name__=='__main__':
    raise SystemExit(main())
