#ifndef BAL_REORDENAMIENTO_H
#define BAL_REORDENAMIENTO_H
#include <stddef.h>
#include "ir.h"

/* Reordenamiento seguro de instrucciones dentro de cada bloque basico
 * (Proyecto II). Cambia el orden de las instrucciones para separar una
 * escritura de su lectura (el procesador VLIW lee un valor 3 bundles
 * despues de escribirlo y no tiene forwarding), sin cambiar el resultado.
 *
 * Reglas (detalle en reordenamiento/README.md):
 *   - RAW: una lectura queda despues de la escritura que la alimenta.
 *   - WAR: una escritura queda despues de las lecturas del valor anterior.
 *   - WAW: dos escrituras a la misma variable conservan su orden.
 *   - Memoria: store/listas/arreglos conservan su orden con loads.
 *   - Efectos: arg y call son barreras; nada las cruza.
 *   - Control: etiquetas quedan al inicio, el salto/return al final,
 *     y ninguna instruccion sale de su bloque basico. */

typedef struct {
    unsigned latencia;   /* Ciclos entre escribir un valor y poder leerlo (>= 1). */
} RoOpciones;

/* Resultado de un bloque basico. */
typedef struct {
    size_t inicio, fin;              /* Rango [inicio, fin) en la IR */
    size_t movidas;                  /* Instrucciones que cambiaron de lugar */
    size_t ciclos_antes, ciclos_despues; /* Estimacion con el modelo de latencia */
    size_t raw, war, waw, memoria, efecto, control; /* Dependencias encontradas */
} RoBloque;

typedef struct {
    size_t bloques, bloques_cambiados;
    size_t instrucciones, movidas;
    size_t ciclos_antes, ciclos_despues;
    size_t raw, war, waw, memoria, efecto, control;
    RoBloque *detalle;               /* Un elemento por bloque basico */
    size_t cantidad_detalle;
    size_t *origen;                  /* origen[i] = posicion original de la instruccion i */
    size_t cantidad_origen;
} RoResumen;

/* Valor por defecto: latencia 3 (escritura en N, lectura desde N+3). */
void ro_opciones_defecto(RoOpciones *op);

/* Reordena cada bloque basico de `ir` (que ya debe tener bloques). Un
 * bloque solo cambia si el nuevo orden necesita MENOS ciclos estimados.
 * La cantidad de instrucciones y los bloques no cambian.
 * `op` puede ser NULL; `res` es opcional y se libera con ro_resumen_liberar.
 * Devuelve 0 en exito y 2 si falta memoria o la IR no tiene bloques. */
int ro_reordenar(IrPrograma *ir, const RoOpciones *op, RoResumen *res);

void ro_resumen_liberar(RoResumen *res);

/* Ciclos estimados de ejecutar ir->codigo[inicio, fin) en orden, emitiendo
 * una instruccion por ciclo y esperando `latencia` ciclos entre escribir y
 * leer un valor. Etiquetas, func, endfunc y param no ocupan ciclos. */
size_t ro_ciclos(const IrPrograma *ir, size_t inicio, size_t fin, unsigned latencia);

#endif
