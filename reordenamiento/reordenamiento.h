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

/* ---------------------------------------------------------------------
 * Grafo de un bloque (para exportarlo a JSON y visualizarlo).
 * Es el MISMO analisis que usa ro_reordenar, pero sin cambiar la IR.
 * --------------------------------------------------------------------- */

/* Tipos de dependencia de una arista (una arista puede tener varios). */
enum {
    RO_DEP_RAW = 1, RO_DEP_WAR = 2, RO_DEP_WAW = 4,
    RO_DEP_MEMORIA = 8, RO_DEP_EFECTO = 16, RO_DEP_CONTROL = 32
};

/* Valor de ciclo para instrucciones que no ocupan ciclo (endfunc). */
#define RO_SIN_CICLO ((size_t)-1)

typedef struct {
    size_t pos;                      /* Posicion en la IR (orden original) */
    size_t nueva_pos;                /* Lugar dentro del grafo en el orden final */
    size_t altura;                   /* Camino critico desde este nodo (ciclos) */
    size_t ciclo_antes, ciclo_despues; /* Ciclo en que se emite (RO_SIN_CICLO: ninguno) */
    int critico;                     /* 1 si esta en la ruta critica */
    const char *lee[4];   size_t nlee;   /* Variables que lee (sin constantes) */
    const char *escribe[2]; size_t nesc; /* Variables que escribe */
} RoNodo;

typedef struct {
    size_t de, a;                    /* Indices de nodo: `de` debe ir antes que `a` */
    unsigned latencia;               /* Ciclos minimos entre ambos */
    unsigned tipos;                  /* Bits RO_DEP_* */
    const char *por;                 /* Variable que causa la dependencia (o NULL) */
    int critica;                     /* 1 si es parte de la ruta critica */
} RoArista;

typedef struct {
    size_t inicio, fin;              /* Bloque [inicio, fin) en la IR */
    size_t primero;                  /* Primer nodo (despues de etiquetas/func/param) */
    size_t cantidad_nodos;           /* fin - primero */
    RoNodo *nodos;
    RoArista *aristas;  size_t cantidad_aristas;
    size_t *orden;                   /* orden[k] = nodo que queda en el lugar k */
    size_t *ruta;  size_t largo_ruta;  /* Nodos de la ruta critica, en orden */
    size_t longitud_ruta;            /* Ciclos de la ruta critica (cota inferior) */
    size_t ciclos_antes, ciclos_despues;
    int aplicado;                    /* 1 si ro_reordenar cambia este bloque */
} RoGrafoBloque;

/* Analiza el bloque `b` de `ir` sin modificarla. Devuelve 0 en exito y 2
 * si falta memoria o los argumentos no son validos. Se libera con
 * ro_grafo_liberar. Los nombres (lee, escribe, por) apuntan a cadenas de `ir`. */
int ro_analizar_bloque(const IrPrograma *ir, size_t b, unsigned latencia, RoGrafoBloque *g);
void ro_grafo_liberar(RoGrafoBloque *g);

#endif
