"""Banco rápido del comparador interno y resultados reales O0/O1/O2.

Compila la receta real del proyecto en una copia temporal externa. Incluye
ide_optimizacion.c en el banco C para probar su comparador privado sin añadir
una API pública. No modifica los binarios versionados ni los fixtures BAL.
--fuente-base permite demostrar el defecto anterior con la misma prueba.
"""
from pathlib import Path
import argparse, os, shlex, shutil, subprocess, tempfile, time
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fuente-base',type=Path)
    parser.add_argument('--solo-comparador',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='o1-comparador-') as tmp:
        copy=Path(tmp)/'codigo'
        shutil.copytree(ROOT,copy,ignore=shutil.ignore_patterns('.git','__pycache__','build'))
        recipe=subprocess.check_output(['make','-Bn','tests/test_ide_optimizacion'],cwd=copy,text=True)
        line=next(l for l in recipe.splitlines() if ' -o tests/test_ide_optimizacion ' in l)
        cmd=shlex.split(line)
        cmd[cmd.index('tests/test_ide_optimizacion.c')]='tests/test_ide_optimizacion_comparador.c'
        cmd.remove('src/IDE/ide_optimizacion.c')
        driver=Path(tmp)/'comparador';cmd[cmd.index('-o')+1]=str(driver)
        if args.fuente_base:cmd.insert(1,'-DO1_COMPARADOR_SOURCE="'+str(args.fuente_base.resolve())+'"')
        subprocess.run(cmd,cwd=copy,check=True)
        for grupo in (['comparador'] if args.solo_comparador else ['comparador','programas']):
            inicio=time.monotonic()
            p=subprocess.run([str(driver),grupo],cwd=copy,env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1'))
            print(f'{grupo}: código={p.returncode}; segundos={time.monotonic()-inicio:.3f}',flush=True)
            if p.returncode:return p.returncode
    return 0

if __name__=='__main__':raise SystemExit(main())
