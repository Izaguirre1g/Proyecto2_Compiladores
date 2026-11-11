/*
 * Eliminación de código muerto vista desde el IDE (botón "Código muerto").
 *
 * Corre sobre el texto del editor lo mismo que "balc --dce":
 *   análisis semántico -> IR de tres direcciones -> variables vivas -> DCE
 * y deja el resultado listo para mostrarlo:
 *   - qué LÍNEAS del código BAL tenían código muerto, y si se eliminó la
 *     línea completa o solo una parte (por ejemplo, en
 *     "c * declare_int : registrar(datos, a);" se elimina la copia a c,
 *     pero la llamada se conserva porque puede tener efectos);
 *   - los textos de la IR antes, de las variables vivas y de la IR después,
 *     para la ventana de detalle.
 *
 * No usa GTK: así se puede probar sin abrir ventanas (tests/test_ide_dce.c).
 * La explicación del DCE está en include/dce.h y en docs/dce.md.
 */
#ifndef IDE_DCE_H
#define IDE_DCE_H

#include <stddef.h>

#include "dce.h"

/* Cómo terminó el análisis. */
typedef enum {
    IDE_DCE_LISTO = 0,        /* Se hizo el DCE (aunque no haya eliminado nada). */
    IDE_DCE_CON_ERRORES = 1,  /* El código tiene errores: hay que corregirlos antes. */
    IDE_DCE_SIN_IR = 2        /* No tiene errores, pero la IR todavía no soporta algo
                               * (ver "motivo"). */
} IdeDceEstado;

/* Una línea del código BAL que tenía código muerto. */
typedef struct {
    size_t linea;
    size_t eliminadas;   /* Instrucciones de la IR de esta línea que se eliminaron. */
    size_t total;        /* Instrucciones de la IR que tenía esta línea. */
    int completa;        /* 1 si se eliminaron TODAS (la línea entera era código muerto). */
    int inalcanzable;    /* 1 si algo de la línea es inalcanzable (tipo 2: nunca se ejecuta). */
    char *detalle;       /* Lo eliminado, separado por "; " ("$t5 = a pitagoras 2; b = $t5"). */
} IdeDceLinea;

/*
 * Una declaración o asignación del código, tomada del AST (para poder
 * reescribirla en el código nuevo). Solo las de una variable simple:
 *     nombre * tipo : valor;        (declaración)
 *     nombre : valor;               (asignación)
 */
typedef struct {
    int es_otra;            /* 1 si es otra sentencia (whether, whale, cycle, give, una
                             * llamada, una escritura en un arreglo...): solo se borra
                             * entera, si TODO lo de adentro es código muerto. */
    int es_declaracion;     /* 1 declaración, 0 asignación. */
    size_t linea, columna;  /* Dónde empieza. */
    char *nombre;           /* La variable (NULL con es_otra). */
    int tiene_valor;        /* La declaración tiene ": valor". */
    int valor_es_llamada;   /* El valor es una llamada: "nombre : f(a);". */
} IdeDceSentencia;

/* El resultado completo. Se crea vacío así: IdeDce r = {0}; */
typedef struct {
    IdeDceEstado estado;
    char *motivo;              /* Con IDE_DCE_SIN_IR: qué no soporta la IR y en qué línea. */
    DceResultado resultado;    /* Lo que hizo el DCE (cantidades, pasadas, eliminadas). */
    IdeDceLinea *lineas;       /* Ordenadas por número de línea. */
    size_t cantidad_lineas;
    char *ir_antes;            /* Textos para la ventana de detalle. */
    char *vivas;
    char *ir_despues;

    /*
     * El CÓDIGO BAL NUEVO, sin el código muerto (ide_dce_codigo.c), o NULL
     * si no cambia nada. El backend genera el ensamblador desde el código
     * fuente, así que compilando este código el ensamblador y el binario
     * salen SIN el código muerto.
     */
    char *codigo_nuevo;
    size_t sentencias_borradas;    /* Sentencias quitadas del código. */
    size_t sentencias_cambiadas;   /* Sentencias reescritas o unidas. */
    size_t sentencias_conservadas; /* Con algo muerto, pero que BAL o el backend no
                                    * permiten escribir de otra forma. */
    /* Líneas donde empieza una sentencia con bloque (whether, whale, cycle...)
     * que tiene código muerto adentro pero NO se borra entera (por ejemplo,
     * un whether cuya rama also se queda). Esas líneas no se tachan en el
     * editor: la sentencia sigue en el código. */
    size_t *cabeceras_que_quedan;
    size_t cantidad_cabeceras;

    /* El ensamblador del código original y del nuevo (en bundles), si el
     * backend lo puede generar. */
    int hay_ensamblador;          /* 1: los dos se generan. 2: SOLO el nuevo (el original
                                   * no compila, por ejemplo por código después de un
                                   * give, y el DCE lo quitó). 0: ninguno. */
    size_t bundles_antes, bundles_despues;
    char *motivo_ensamblador;      /* Si no se pudo: por qué (lo dice el backend). */
} IdeDce;

/*
 * Arma el código BAL nuevo a partir del original y de lo que eliminó el
 * DCE (ide_dce_codigo.c). Por cada declaración o asignación que ocupa sus
 * propias líneas:
 *   - si se eliminaron TODAS sus instrucciones de la IR, se borra;
 *   - si solo se eliminó la copia del resultado de una llamada, queda solo
 *     la llamada ("registrar(v, a);"), porque puede tener efectos;
 *   - si es una declaración muerta pero la variable se usa después, se une
 *     con la asignación siguiente ("r * declare_int : 0; r : f(4);" pasa a
 *     "r * declare_int : f(4);"), porque el backend necesita declaraciones
 *     con valor; si no se puede, se deja igual;
 *   - en otro caso (por ejemplo, una división que puede fallar) se deja igual.
 *   por_linea[l]: cuántas instrucciones de la IR tenía la línea l antes del DCE.
 * Deja el código en salida->codigo_nuevo (NULL si no cambia nada) y cuenta
 * las sentencias en salida->sentencias_*. Devuelve 0 bien o -1 sin memoria.
 */
int ide_dce_codigo_nuevo(const char *texto, size_t largo,
                         const IdeDceSentencia *sentencias, size_t cantidad,
                         const size_t *por_linea, size_t lineas,
                         const DceResultado *resultado, IdeDce *salida);

/*
 * Analiza el texto del editor.
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo (para los "bring", que buscan sus
 *                 módulos en su carpeta), o NULL si no se ha guardado.
 * Devuelve 0 si se pudo analizar (el resultado dice cómo terminó) o -1 si
 * falló algo interno (archivo temporal o memoria).
 */
int ide_dce_analizar(const char *texto, size_t largo, const char *ruta, IdeDce *salida);

void ide_dce_liberar(IdeDce *dce);

#endif
