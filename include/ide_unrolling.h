/*
 * Loop unrolling visto desde el IDE (botón "Loop unrolling").
 *
 * Corre sobre el texto del editor lo mismo que "balc --unroll", con el
 * módulo de Meibel (loop_unrolling/loop_unrolling.c y lu_reporte.c):
 *   análisis semántico -> IR -> lu_desenrollar
 * y guarda dos "fotos" de la IR (lu_vista_crear): antes y después del
 * desenrollado, con las instrucciones nuevas marcadas.
 *
 * No usa GTK: así se puede probar sin abrir ventanas
 * (tests/test_ide_unrolling.c).
 */
#ifndef IDE_UNROLLING_H
#define IDE_UNROLLING_H

#include <stddef.h>

#include "loop_unrolling.h"
#include "lu_reporte.h"

/* Cómo terminó el análisis. */
typedef enum {
    IDE_LU_LISTO = 0,        /* Se hizo el loop unrolling (aunque no cambie nada). */
    IDE_LU_CON_ERRORES = 1,  /* El código tiene errores: hay que corregirlos antes. */
    IDE_LU_SIN_IR = 2        /* No tiene errores, pero la IR no soporta algo (ver "motivo"). */
} IdeLuEstado;

/* El resultado. Se crea vacío así: IdeUnrolling u = {0}; */
typedef struct {
    IdeLuEstado estado;
    char *motivo;          /* Con IDE_LU_SIN_IR: qué no soporta la IR y en qué línea. */
    unsigned factor;       /* El factor k que se usó. */
    LuResumen resumen;     /* Qué se hizo con cada ciclo (de lu_desenrollar). */
    LuVista antes;         /* La IR antes, línea por línea, con sus métricas. */
    LuVista despues;       /* La IR después; las líneas nuevas tienen tipo 'N'. */
} IdeUnrolling;

/*
 * Hace el loop unrolling del texto del editor.
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo (para los "bring"), o NULL.
 *   factor:       el factor k del desenrollado parcial (2 a 64); 0 usa el
 *                 valor por defecto del módulo (4).
 * Devuelve 0 si se pudo analizar (el estado dice cómo terminó) o -1 si
 * falló algo interno (archivo temporal o memoria).
 */
int ide_unrolling_analizar(const char *texto, size_t largo, const char *ruta,
                           unsigned factor, IdeUnrolling *salida);

/* El texto de la decisión de un ciclo, por ejemplo "TOTAL: 5 copias, sin
 * saltos" o "sin cambios: el cuerpo contiene un return". Escribe en "texto". */
void ide_unrolling_decision(const LuDetalle *d, char *texto, size_t largo);

void ide_unrolling_liberar(IdeUnrolling *u);

#endif
