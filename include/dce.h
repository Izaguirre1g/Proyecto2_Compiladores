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
 *         OUT[SALIDA de main] = variables de main que se leen en main
 *
 *     use_B: variables que B usa ANTES de asignarlas.
 *     def_B: variables que B asigna.
 *     Se resuelve con el algoritmo iterativo hasta un punto fijo
 *     (diapositiva 27): se recalculan IN y OUT hasta que nada cambie.
 *
 *     ¿Por qué main es distinto? En este proyecto el resultado de un
 *     programa es la memoria final de las variables de main (BAL no tiene
 *     print; el simulador y las pruebas leen ahí, por ejemplo,
 *     "resultado = 120" en factorial.bal). Son resultado las variables de
 *     main que el programa USA (se leen en algún lugar de main): siguen
 *     vivas al terminar main. Una variable de main que nunca se lee (como
 *     "t * declare_int : 4;") es código muerto. Las variables locales de
 *     las demás funciones desaparecen al hacer "give".
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
 * Hay DOS TIPOS de código muerto, y el DCE elimina los dos:
 *
 *   Tipo 1, asignaciones no usadas: la instrucción SÍ se ejecuta, pero
 *   calcula un valor que nadie lee (variables vivas, arriba).
 *
 *   Tipo 2, código inalcanzable: instrucciones que NUNCA se ejecutan, en
 *   ningún camino del grafo de flujo (diapositiva 15). Pasa:
 *     * después de un salto incondicional: en BAL, después de un "give";
 *     * en una rama cuya condición se conoce al compilar. Para saberlo se
 *       usa PROPAGACIÓN y PLEGADO DE CONSTANTES (diapositiva 20): una
 *       variable asignada UNA sola vez con un valor constante vale eso en
 *       todo el programa ("debug * declare_boolean : declare_false;"), y
 *       una operación con operandos constantes se calcula al compilar
 *       ("1 > 2" es falso). Si la condición de un salto condicional es
 *       constante, el salto se vuelve fijo (o desaparece), y la rama que
 *       nunca se toma queda inalcanzable.
 *   Un bloque es inalcanzable si no hay camino desde el inicio de su
 *   función hasta él: se recorre el grafo desde ahí y lo que no se visita
 *   se elimina (salvo el inicio y el fin de la función).
 *
 * Los dos tipos se repiten en las mismas pasadas: quitar una rama puede
 * dejar variables sin usar (tipo 1), y quitar código muerto puede volver
 * constante una condición (tipo 2).
 *
 * El DCE trabaja sobre la IR (src/Representacion_Intermedia/ir.c) y no la
 * cambia por dentro: quita instrucciones (o vuelve fijo un salto
 * condicional) y vuelve a calcular los bloques básicos con
 * ir_bloques_basicos().
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

/* Por qué se eliminó una instrucción. */
typedef enum {
    DCE_NO_USADA = 1,     /* Tipo 1: calcula un valor que nadie lee. */
    DCE_INALCANZABLE = 2  /* Tipo 2: nunca se ejecuta. */
} DceTipo;

/* Una instrucción eliminada, para mostrarla (por ejemplo, "x = 5"). */
typedef struct {
    size_t linea;      /* Línea del código BAL de donde salió. */
    char texto[128];   /* La instrucción, escrita como en ir_imprimir. */
    DceTipo tipo;      /* Tipo 1 o tipo 2. */
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
    /* Tipo 2: */
    size_t inalcanzables;           /* Instrucciones eliminadas por inalcanzables. */
    size_t condiciones_constantes;  /* Saltos condicionales con condición constante. */
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
