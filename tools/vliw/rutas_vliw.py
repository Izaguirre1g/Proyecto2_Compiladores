"""Localiza dependencias compartidas sin moverlas ni duplicar sus interfaces."""
from pathlib import Path


def fuente_compartida(raiz, relativa):
    """Devuelve la única fuente existente en el árbol plano o por módulos.

    Recibe la raíz del proyecto y una ruta histórica src/nombre.c. Rechaza
    ausencia o duplicación: elegir silenciosamente una copia podría enlazar
    otro codificador después de integrar develop. No depende del cwd.
    """
    raiz = Path(raiz)
    relativa = Path(relativa)
    if relativa.parent != Path('src'):
        raise ValueError('se requiere una fuente compartida directa de src/')
    candidatas = [raiz / relativa]
    candidatas.extend(sorted((raiz / 'src').glob('*/' + relativa.name)))
    # Buscamos tanto el árbol histórico plano como los módulos reorganizados. Rechazar cero o
    # varias coincidencias evita enlazar silenciosamente un codificador distinto según el
    # entorno.
    existentes = [p for p in candidatas if p.is_file()]
    if len(existentes) != 1:
        raise FileNotFoundError(f'fuente compartida ausente o ambigua: {relativa}: {existentes}')
    return existentes[0]


def top_rtl(raiz):
    """Localiza el top existente sin copiarlo ni crear sustitutos del RTL.

    El Proyecto I lo guardaba en rtl; Arquitectura lo trasladó a rtl-p2.
    Si falta en ambos árboles, devuelve la segunda ruta para que las suites
    con comprobación previa mantengan su clasificación de dependencia ausente.
    """
    raiz = Path(raiz)
    historico = raiz / 'rtl/top.sv'
    return historico if historico.is_file() else raiz / 'rtl-p2/top.sv'


def argumentos_rtl(raiz):
    """Devuelve includes y fuentes de la organización real de Arquitectura.

    Conserva los módulos originales. Cuando top está en rtl-p2 se añaden sus
    dependencias de ese directorio; no se cambia ninguna política ni parámetro.
    """
    raiz = Path(raiz)
    # El top localizado determina si además de rtl hay dependencias en rtl-p2. Construimos
    # includes y fuentes reales de ambos directorios, sin copiar ni sustituir módulos de
    # Arquitectura.
    directorios = [raiz / 'rtl']
    if top_rtl(raiz).parent == raiz / 'rtl-p2':
        directorios.append(raiz / 'rtl-p2')
    return (['-I' + str(d) for d in directorios] +
            [str(p) for d in directorios for p in sorted(d.glob('*.sv'))])
