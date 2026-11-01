#ifndef IDE_ARREGLOS_H
#define IDE_ARREGLOS_H

/*
 * Corrección automática de errores del IDE.
 *
 * En las diapositivas de Análisis Sintáctico, una de las estrategias de
 * recuperación es la "recuperación a nivel de frase": hacer una corrección
 * LOCAL justo donde está el error (insertar o borrar un símbolo). Este
 * módulo hace eso, pero en vez de corregir en silencio, PROPONE el arreglo
 * y la persona decide si lo aplica.
 *
 * Arreglos que se proponen:
 *
 *   Error                                   Arreglo
 *   --------------------------------------  -------------------------------------
 *   falta un ';' al final de una línea      insertar ';' después del último token
 *   falta ')' antes de '{'                  insertar ')' antes del '{'
 *   un '{' que nunca se cerró               insertar '}' al final del archivo
 *   un '(' o '[' que nunca se cerró         insertar ')' o ']' al final de esa línea
 *   un cierre que sobra                     borrar ese cierre
 *   un cierre equivocado en la misma línea  cambiarlo por el correcto: (a] -> (a)
 *
 * Por qué el ';' va "después del último token" y no donde se marca el error:
 * el parser (Bison, LALR) detecta el error al leer el token SIGUIENTE, que
 * muchas veces está en la línea de abajo. Por eso el arreglo pone el ';'
 * pegado al token anterior.
 *
 * Este módulo no usa GTK: la ventana solo muestra el arreglo y lo aplica.
 */

#include <stddef.h>

#include "diagnosticos.h"

/*
 * Un arreglo: "en esta posición, borrar N caracteres y escribir este texto".
 *
 *   Insertar ';' en la línea 2, columna 31:  borrar = 0, insertar = ";"
 *   Borrar un ')' que sobra:                 borrar = 1, insertar = ""
 *   Cambiar ']' por ')':                     borrar = 1, insertar = ")"
 *
 * Las columnas cuentan caracteres (no bytes), igual que el lexer y GTK.
 */
typedef struct {
    size_t linea;             /* Línea donde se corrige, empieza en 1. */
    size_t columna;           /* Columna donde se corrige, empieza en 1. */
    size_t borrar;            /* Cuántos caracteres se borran desde ahí. */
    char *insertar;           /* Texto que se escribe ahí (puede ser ""). */
    char descripcion[160];    /* Para mostrar, por ejemplo "agregar ';' al final de la linea 2". */
} IdeArreglo;

/*
 * Propone un arreglo para un error.
 *
 *   texto, largo: el contenido del editor (el mismo que se analizó).
 *   error:        un error de la lista que llenó ide_analizar_texto.
 *   arreglo:      aquí se guarda la propuesta.
 *
 * Devuelve 1 si hay un arreglo (liberarlo con ide_liberar_arreglo),
 * 0 si no se sabe cómo arreglar ese error, o -1 si no hubo memoria.
 */
int ide_proponer_arreglo(const char *texto, size_t largo,
                         const Diagnostico *error, IdeArreglo *arreglo);

/* Libera la memoria del arreglo. */
void ide_liberar_arreglo(IdeArreglo *arreglo);

/*
 * Aplica un arreglo al texto y devuelve el texto nuevo.
 *
 * El texto original no cambia. En "nuevo_largo" se guarda el largo del
 * texto nuevo. Devuelve memoria nueva (liberar con free), o NULL si la
 * posición no existe en el texto o no hubo memoria.
 */
char *ide_aplicar_arreglo(const char *texto, size_t largo,
                          const IdeArreglo *arreglo, size_t *nuevo_largo);

#endif
