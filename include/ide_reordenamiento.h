/*
 * Reordenamiento de instrucciones para el IDE (botón "Reordenamiento").
 *
 * Hace lo mismo que "balc --reorder" (y "balc --unroll --reorder" si se
 * pide desenrollar primero), pero desde el texto del editor y sin abrir
 * ventanas, para poder probarlo:
 *
 *   análisis semántico -> IR -> [loop unrolling] -> foto de la IR
 *   -> grafo de dependencias de cada bloque (ro_analizar_bloque)
 *   -> ro_reordenar (módulo de Meibel, reordenamiento/) -> foto de la IR
 *
 * El reordenamiento cambia el orden de las instrucciones DENTRO de cada
 * bloque básico para separar una escritura de la lectura que la usa (el
 * procesador VLIW tarda "latencia" ciclos en tener listo un valor), sin
 * romper ninguna dependencia RAW, WAR, WAW, de memoria, de efectos o de
 * control. No cambia el módulo de Meibel: solo lo llama.
 */
#ifndef IDE_REORDENAMIENTO_H
#define IDE_REORDENAMIENTO_H

#include <stddef.h>

#include "reordenamiento.h"

/* Cómo terminó. */
typedef enum {
    IDE_RO_LISTO = 0,        /* Se reordenó (aunque ningún bloque haya cambiado). */
    IDE_RO_CON_ERRORES = 1,  /* El código tiene errores: hay que corregirlos antes. */
    IDE_RO_SIN_IR = 2        /* Sin errores, pero la IR todavía no soporta algo (ver motivo). */
} IdeRoEstado;

/* Una línea para mostrar (IR o dependencias). */
typedef struct {
    char tipo;     /* 'B' encabezado de bloque, 'I' instrucción, 'M' instrucción que se
                    * movió, 'E' dependencia, 'C' dependencia de la ruta crítica,
                    * 'R' texto de la ruta crítica. */
    char *texto;
} IdeRoLinea;

/* Una lista de líneas. */
typedef struct {
    IdeRoLinea *lineas;
    size_t cantidad, capacidad;
} IdeRoTexto;

typedef struct {
    IdeRoEstado estado;
    char *motivo;              /* Con IDE_RO_SIN_IR: qué no soporta la IR y en qué línea. */
    unsigned latencia;         /* La que se usó (0 al pedir = la de Meibel, 3). */
    int desenrollado;          /* 1 si se hizo loop unrolling antes de reordenar. */
    RoResumen resumen;         /* Lo que dijo ro_reordenar (por bloque y en total). */
    IdeRoTexto antes;          /* La IR antes, por bloques. */
    IdeRoTexto despues;        /* La IR después; lo que se movió lleva 'M' y '*'. */
    IdeRoTexto dependencias;   /* Grafo de los bloques que cambiaron: quién debe ir
                                * antes de quién y por qué. */
} IdeReordenamiento;

/*
 * Reordena el texto del editor.
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo (para los "bring"), o NULL si no se ha guardado.
 *   latencia:     ciclos entre escribir un valor y poder leerlo (0 = por defecto).
 *   desenrollar:  1 para hacer loop unrolling primero (como balc --unroll --reorder).
 * Devuelve 0 si se pudo intentar (el estado dice cómo terminó) o -1 si
 * falló algo interno (archivo temporal o memoria).
 */
int ide_reordenamiento_analizar(const char *texto, size_t largo, const char *ruta,
                                unsigned latencia, int desenrollar, IdeReordenamiento *r);

/* Nombres de los tipos de una dependencia (bits RO_DEP_*): "RAW + memoria". */
void ide_reordenamiento_tipos(unsigned tipos, char *texto, size_t largo);

void ide_reordenamiento_liberar(IdeReordenamiento *r);

#endif
