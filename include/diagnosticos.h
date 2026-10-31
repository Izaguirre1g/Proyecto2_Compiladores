#ifndef DIAGNOSTICOS_H
#define DIAGNOSTICOS_H

/*
 * ¿Qué hace este módulo?
 *
 * Cuando el compilador encuentra un error, lo escribe como una línea de texto:
 *
 *     ejemplo.bal:3:5: error semantico: identificador 'x' no declarado
 *
 * Eso es fácil de leer para una persona, pero no para un programa.
 * Este módulo toma esas líneas, separa sus partes (archivo, línea, columna,
 * fase y mensaje) y las escribe en formato JSON.
 *
 * El IDE usa ese JSON para saber dónde subrayar cada error.
 */

#include <stddef.h>
#include <stdio.h>

/* En qué parte del compilador se encontró el error. */
typedef enum {
    DIAG_LEXICO,      /* Hay un carácter o palabra que el lenguaje no reconoce. */
    DIAG_SINTACTICO,  /* Las palabras están en un orden que la gramática no permite. */
    DIAG_SEMANTICO,   /* Todo está bien escrito, pero no tiene sentido (tipos, variables sin declarar...). */
    DIAG_GENERAL      /* Otro problema, por ejemplo que el archivo no existe. */
} DiagFase;

/* Un error, ya separado en sus partes. */
typedef struct {
    DiagFase fase;    /* Qué parte del compilador lo encontró. */
    char *archivo;    /* En qué archivo está (puede ser un archivo importado). */
    size_t linea;     /* En qué línea está. Empieza en 1. Si vale 0, no se sabe. */
    size_t columna;   /* En qué columna está. Empieza en 1. Si vale 0, no se sabe. */
    char *mensaje;    /* La explicación del error. */
} Diagnostico;

/* Una lista de errores que crece sola. Se crea vacía así: DiagLista lista = {0}; */
typedef struct {
    Diagnostico *elementos;  /* Los errores guardados. */
    size_t cantidad;         /* Cuántos errores hay guardados. */
    size_t capacidad;        /* Cuántos caben antes de tener que pedir más memoria. */
} DiagLista;

/* Devuelve el nombre de la fase como texto: "lexico", "sintactico", etc. */
const char *diag_fase_nombre(DiagFase fase);

/*
 * Revisa una sola línea de texto.
 *
 * Devuelve:
 *    1  si la línea es un error. Sus partes quedan guardadas en "salida".
 *    0  si la línea no es un error (por ejemplo, un mensaje informativo).
 *   -1  si no hubo memoria suficiente.
 *
 * Quien llama esta función debe liberar salida->archivo y salida->mensaje.
 */
int diag_interpretar_linea(const char *linea, Diagnostico *salida);

/*
 * Lee un archivo completo, línea por línea, y guarda en la lista
 * cada error que encuentre.
 *
 * Devuelve 0 si todo salió bien, o -1 si no hubo memoria suficiente.
 */
int diag_leer_flujo(FILE *flujo, DiagLista *lista);

/*
 * Escribe todos los errores de la lista en formato JSON, así:
 *
 *   {"archivo": "ejemplo.bal", "errores": 1, "diagnosticos": [
 *     {"fase": "semantico", "archivo": "ejemplo.bal", "linea": 3,
 *      "columna": 5, "mensaje": "identificador 'x' no declarado"}
 *   ]}
 *
 * Devuelve 0 si se pudo escribir, o -1 si hubo un problema al escribir.
 */
int diag_escribir_json(const char *archivo_principal, const DiagLista *lista,
                       FILE *salida);

/* Libera toda la memoria de la lista y la deja vacía otra vez. */
void diag_liberar(DiagLista *lista);

#endif
