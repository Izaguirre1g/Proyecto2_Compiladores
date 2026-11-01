#ifndef IDE_NUCLEO_H
#define IDE_NUCLEO_H

/*
 * Núcleo del IDE: la lógica del editor, sin ninguna ventana.
 *
 * Aquí está todo lo que el IDE "piensa":
 *   1. Sugerencias: mientras la persona escribe, qué palabras le proponemos.
 *   2. Autocierre: si escribe '(', qué carácter cierra ese paréntesis.
 *   3. Delimitadores: revisar que cada '(', '[' y '{' tenga su cierre.
 *
 * La ventana (GTK) solo dibuja y llama a estas funciones. Separarlo así
 * permite probar toda la lógica con "make test-ide-nucleo", sin abrir ventanas.
 */

#include <stddef.h>

#include "diagnosticos.h"

/* ------------------------------------------------------------------ */
/* 1. Sugerencias de código                                            */
/* ------------------------------------------------------------------ */

/* De dónde sale una sugerencia. */
typedef enum {
    IDE_IDENTIFICADOR,  /* Un nombre que ya aparece en el archivo (variable, función...). */
    IDE_PALABRA_CLAVE   /* Una palabra reservada del lenguaje BAL (whether, whale...). */
} IdeTipoSugerencia;

/* Una palabra que el IDE le propone a la persona. */
typedef struct {
    char *texto;              /* La palabra completa, por ejemplo "whether". */
    IdeTipoSugerencia tipo;   /* Si es identificador o palabra clave. */
    const char *descripcion;  /* Ayuda corta, por ejemplo "condicional (si)". */
} IdeSugerencia;

/* Lista de sugerencias que crece sola. Se crea vacía así: IdeSugerencias s = {0}; */
typedef struct {
    IdeSugerencia *elementos;
    size_t cantidad;
    size_t capacidad;
} IdeSugerencias;

/*
 * Busca sugerencias para lo que la persona está escribiendo.
 *
 *   texto, largo: todo el contenido del editor.
 *   prefijo:      la parte de la palabra que ya se escribió (por ejemplo "wh").
 *
 * Agrega a la lista, en este orden:
 *   - los identificadores del archivo que empiezan con el prefijo,
 *   - las palabras clave que empiezan con el prefijo.
 * Cada grupo va en orden alfabético y sin repetidos.
 *
 * Devuelve 0 si todo salió bien, o -1 si no hubo memoria suficiente.
 */
int ide_sugerencias(const char *texto, size_t largo, const char *prefijo,
                    IdeSugerencias *lista);

/* Libera la memoria de la lista y la deja vacía otra vez. */
void ide_liberar_sugerencias(IdeSugerencias *lista);

/* ------------------------------------------------------------------ */
/* 2. Autocierre                                                       */
/* ------------------------------------------------------------------ */

/*
 * Si la persona escribe un carácter que "abre" algo, devuelve el carácter
 * que lo cierra. Si no abre nada, devuelve '\0'.
 *
 *   '(' -> ')'    '[' -> ']'    '{' -> '}'
 *   '"' -> '"'    (cadenas de texto)
 *   '$' -> '$'    (caracteres, por ejemplo $a$)
 */
char ide_cierre_de(char abre);

/* ------------------------------------------------------------------ */
/* 3. Revisión de delimitadores                                        */
/* ------------------------------------------------------------------ */

/*
 * Revisa que los paréntesis, corchetes y llaves estén bien cerrados.
 *
 * Agrega un diagnóstico (fase "sintactico") por cada problema:
 *   - un cierre que sobra, por ejemplo un '}' sin '{' antes;
 *   - un cierre equivocado, por ejemplo '(' cerrado con '}';
 *   - una apertura que nunca se cerró.
 *
 * Las comillas no se revisan aquí porque el analizador léxico ya reporta
 * las cadenas sin cerrar. Lo que está dentro de cadenas y comentarios se
 * ignora, porque se trabaja sobre los tokens y no sobre el texto crudo.
 *
 * Devuelve 0 si todo salió bien, o -1 si no hubo memoria suficiente.
 */
int ide_revisar_delimitadores(const char *texto, size_t largo,
                              const char *archivo, DiagLista *lista);

#endif
