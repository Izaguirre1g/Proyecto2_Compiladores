#ifndef BAL_LOOP_UNROLLING_H
#define BAL_LOOP_UNROLLING_H
#include <stddef.h>
#include <stdio.h>
#include "ir.h"

/* Loop unrolling sobre la IR de tres direcciones (Proyecto II).
 *
 * Solo se desenrollan ciclos con forma canonica (la que genera `cycle` y
 * un `whale` contador):
 *
 *     $Lx:
 *     $tc = i < limite          (o <=)
 *     ifFalse $tc goto $Ly
 *     ...cuerpo...
 *     $tp = i gauss paso        (paso entero constante > 0)
 *     i = $tp
 *     goto $Lx
 *     $Ly:
 *
 * Detalle de reglas, precondiciones y heuristica en loop_unrolling/README.md. */

typedef struct {
    unsigned factor;            /* k del desenrollado parcial (>= 2). */
    size_t max_vueltas_total;   /* N maximo para desenrollado total. */
    size_t max_instr_total;     /* N * (cuerpo + 2) maximo para total. */
    size_t max_cuerpo;          /* Cuerpo mas grande que se desenrolla. */
    size_t max_instr_parcial;   /* k * (cuerpo + 2) maximo; reduce k si hace falta. */
} LuOpciones;

/* Que se hizo con un ciclo. */
typedef enum {
    LU_TOTAL,           /* N conocido y pequeno: N copias, sin ciclo */
    LU_PARCIAL_CONST,   /* N conocido y grande: k copias + N mod k sueltas */
    LU_PARCIAL_VAR,     /* N desconocido: k copias + ciclo residuo */
    LU_RECHAZADO        /* No se cambio; ver `motivo` */
} LuDecision;

/* Detalle de un ciclo, para reportes. Los textos se copian (pueden
 * quedar recortados si son muy largos); `motivo` es una cadena fija. */
typedef struct {
    char etiqueta[24];          /* Cabecera del ciclo ($Lx) */
    char iter[48], limite[48];  /* Vacios si el ciclo no es canonico */
    char relacion[4];           /* "<" o "<=" */
    long long paso;
    size_t linea;               /* Linea del fuente */
    LuDecision decision;
    unsigned factor;            /* k usado (solo parciales) */
    unsigned long long vueltas; /* N (si vueltas_conocidas) */
    int vueltas_conocidas;
    const char *motivo;         /* Solo si decision == LU_RECHAZADO */
} LuDetalle;

typedef struct {
    size_t ciclos;              /* Ciclos encontrados (canonicos o no). */
    size_t totales;             /* Desenrollados por completo. */
    size_t parciales;           /* Desenrollados con factor k. */
    size_t rechazados;          /* Ciclos que se dejaron igual. */
    size_t instr_antes, instr_despues; /* Instrucciones IR estaticas. */
    LuDetalle *detalle;         /* Un elemento por ciclo, en orden; liberar */
    size_t cantidad_detalle;    /* con lu_resumen_liberar(). */
} LuResumen;

/* Libera el detalle de un resumen lleno por lu_desenrollar. */
void lu_resumen_liberar(LuResumen *res);

/* Valores por defecto: factor 4, total si N <= 16 y N*(cuerpo+2) <= 64,
 * cuerpo maximo 32 y k*(cuerpo+2) <= 64. */
void lu_opciones_defecto(LuOpciones *op);

/* Desenrolla los ciclos de `ir` y recalcula sus bloques basicos.
 * `op` puede ser NULL (valores por defecto); `res` y `log` son opcionales.
 * Si se pasa `res`, hay que liberarlo despues con lu_resumen_liberar().
 * Devuelve 0 en exito y 2 si falta memoria o la IR es invalida. Ante un
 * error la IR queda valida (sin la transformacion que fallo). */
int lu_desenrollar(IrPrograma *ir, const LuOpciones *op, LuResumen *res, FILE *log);

#endif
