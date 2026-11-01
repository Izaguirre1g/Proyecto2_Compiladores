#ifndef IDE_ANALISIS_H
#define IDE_ANALISIS_H

/*
 * Análisis del texto del editor.
 *
 * El IDE necesita revisar el código MIENTRAS se escribe, aunque todavía
 * no se haya guardado. Esta función hace todo el trabajo:
 *
 *   1. Guarda el texto del editor en un archivo temporal oculto, en la
 *      MISMA carpeta del archivo original. Tiene que ser la misma carpeta
 *      porque los "bring" buscan sus módulos junto al archivo que importa.
 *   2. Corre el análisis léxico, sintáctico y semántico del compilador
 *      sobre ese archivo temporal (lo mismo que hace balc --diagnosticos).
 *   3. Borra el archivo temporal.
 *   4. Traduce los mensajes del parser a español claro (ide_mensajes.c).
 *   5. Revisa los paréntesis, corchetes y llaves (ide_nucleo.c).
 *
 * Los errores quedan en la lista con el nombre del archivo ORIGINAL, no
 * con el del archivo temporal, para que el IDE sepa dónde subrayarlos.
 */

#include <stddef.h>

#include "diagnosticos.h"

/*
 * Revisa el texto del editor.
 *
 *   texto, largo: el contenido del editor.
 *   ruta:         la ruta del archivo abierto, o NULL si todavía no se ha
 *                 guardado nunca (en ese caso se usa la carpeta actual).
 *   lista:        aquí se agregan los errores encontrados.
 *
 * Devuelve 0 si se pudo revisar, o -1 si falló algo interno
 * (no se pudo escribir el archivo temporal o faltó memoria).
 */
int ide_analizar_texto(const char *texto, size_t largo, const char *ruta,
                       DiagLista *lista);

/* El nombre que se usa en los errores cuando el archivo no tiene ruta. */
#define IDE_SIN_NOMBRE "sin_nombre.bal"

#endif
