/*
 * Eliminación de código muerto (DCE) sobre la IR de tres direcciones,
 * usando el análisis de VARIABLES VIVAS (liveness).
 *
 * Ideas de la presentación "Optimización de Código":
 *
 *   - Diapositiva 25, variables vivas. Una variable está VIVA en un punto
 *     si su valor se puede usar más adelante, en ALGÚN camino. Es un
 *     análisis hacia atrás que combina los sucesores con unión:
 *
 *         IN[B]  = use_B ∪ (OUT[B] − def_B)
 *         OUT[B] = ∪ IN[S]   para cada sucesor S de B
 *         OUT[SALIDA] = ∅                       (funciones comunes)
 *         OUT[SALIDA de main] = variables de main
 *
 *     use_B: variables que B usa ANTES de asignarlas.
 *     def_B: variables que B asigna.
 *     Se resuelve con el algoritmo iterativo hasta un punto fijo
 *     (diapositiva 27): se recalculan IN y OUT hasta que nada cambie.
 *
 *     ¿Por qué main es distinto? En este proyecto el resultado de un
 *     programa es la memoria final de las variables de main (BAL no tiene
 *     print; el simulador y las pruebas leen ahí, por ejemplo,
 *     "resultado = 120" en factorial.bal). Esa es la salida observable, así
 *     que esas variables están vivas al terminar main. Las variables locales
 *     de las demás funciones desaparecen al hacer "give".
 *
 *   - Diapositiva 28, eliminación global de código muerto. Una asignación
 *     "x = ..." es código muerto si x NO está viva justo después y la
 *     instrucción NO tiene efectos secundarios. Se repite, porque eliminar
 *     código muerto puede dejar más código muerto.
 *
 *   - Diapositiva 12, regla de oro. Solo es válido si el programa hace lo
 *     mismo: mismos valores, mismos errores. Por eso NUNCA se eliminan:
 *       * las llamadas (call): la función puede escribir en un arreglo o
 *         en una lista que recibe, o llamar a otras que lo hagan, aunque
 *         nadie use el valor que devuelve;
 *       * las escrituras en memoria (store, add, remove) y las
 *         declaraciones de arreglos y listas;
 *       * las operaciones que pueden FALLAR en ejecución: una división
 *         (euclides, euler) cuyo divisor no es un número distinto de 0, o
 *         una potencia (descartes) cuyo exponente no es un número >= 0.
 *       * los saltos, etiquetas, retornos y el inicio/fin de las funciones.
 *
 * El DCE trabaja sobre la IR (src/Representacion_Intermedia/ir.c) y no la
 * cambia por dentro: quita instrucciones y vuelve a calcular los bloques
 * básicos con ir_bloques_basicos().
 */
#ifndef BAL_DCE_H
#define BAL_DCE_H

#include <stddef.h>
#include <stdio.h>

#include "ir.h"

/* ------------------------------------------------------------------ */
/* Variables vivas                                                     */
/* ------------------------------------------------------------------ */

/*
 * El resultado del análisis de variables vivas de un programa.
 * Los conjuntos se guardan como tablas de 0/1: para el bloque b y la
 * variable v, entrada[b * cantidad_variables + v] es 1 si v está viva al
 * ENTRAR a b (IN[b]); salida[...] lo mismo al SALIR (OUT[b]).
 */
typedef struct {
    char **variables;           /* Nombres de las variables, en orden alfabético. */
    size_t cantidad_variables;
    size_t cantidad_bloques;    /* Igual a ir->cantidad_bloques. */
    unsigned char *usa;         /* use_B: usadas antes de asignarse en el bloque. */
    unsigned char *define;      /* def_B: asignadas en el bloque. */
    unsigned char *entrada;     /* IN[B]. */
    unsigned char *salida;      /* OUT[B]. */
    size_t iteraciones;         /* Vueltas hasta llegar al punto fijo. */
} DceVivas;

/* Calcula las variables vivas de cada bloque básico de "ir".
 * Devuelve 0 si todo salió bien, 2 si faltó memoria o los argumentos son inválidos. */
int dce_calcular_vivas(const IrPrograma *ir, DceVivas *vivas);

/* 1 si "variable" está viva al entrar (salida = 0) o al salir (salida = 1)
 * del bloque. 0 si no lo está o no existe. */
int dce_esta_viva(const DceVivas *vivas, size_t bloque, const char *variable, int salida);

/* Escribe use, def, IN y OUT de cada bloque, para leerlos. */
int dce_imprimir_vivas(const DceVivas *vivas, FILE *salida);

void dce_liberar_vivas(DceVivas *vivas);

/* ------------------------------------------------------------------ */
/* Eliminación de código muerto                                        */
/* ------------------------------------------------------------------ */

/* Una instrucción eliminada, para mostrarla (por ejemplo, "x = 5"). */
typedef struct {
    size_t linea;      /* Línea del código BAL de donde salió. */
    char texto[128];   /* La instrucción, escrita como en ir_imprimir. */
} DceEliminada;

/* Qué hizo el DCE. Se crea vacío así: DceResultado r = {0}; */
typedef struct {
    DceEliminada *eliminadas;       /* En el orden en que se eliminaron. */
    size_t cantidad_eliminadas;
    size_t capacidad;
    size_t instrucciones_antes;
    size_t instrucciones_despues;
    size_t pasadas;                 /* Vueltas de "calcular vivas + eliminar". */
    /* Instrucciones cuyo resultado nadie usa pero que se CONSERVARON: */
    size_t llamadas_conservadas;    /* call: la función puede tener efectos. */
    size_t riesgosas_conservadas;   /* división o potencia que puede fallar. */
} DceResultado;

/*
 * Elimina el código muerto de "ir" (lo modifica). Repite hasta que no
 * quede nada por eliminar. Devuelve 0 si todo salió bien y 2 si faltó
 * memoria o los argumentos son inválidos.
 */
int dce_eliminar(IrPrograma *ir, DceResultado *resultado);

/* Escribe un resumen: cuánto se eliminó, qué se eliminó y qué se conservó. */
int dce_imprimir_resultado(const DceResultado *resultado, FILE *salida);

void dce_liberar_resultado(DceResultado *resultado);

#endif
