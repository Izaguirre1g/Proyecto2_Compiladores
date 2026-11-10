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
    char *detalle;       /* Lo eliminado, separado por "; " ("$t5 = a pitagoras 2; b = $t5"). */
} IdeDceLinea;

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
} IdeDce;

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
