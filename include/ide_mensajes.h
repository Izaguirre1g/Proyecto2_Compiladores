#ifndef IDE_MENSAJES_H
#define IDE_MENSAJES_H

/*
 * Traducción de los mensajes del parser a español claro.
 *
 * El parser (Bison) escribe sus errores en inglés y con los nombres internos
 * de los tokens, por ejemplo:
 *
 *   syntax error, unexpected LBRACE, expecting IDENTIFIER or RPAREN
 *
 * Esa línea es difícil de entender para quien escribe código BAL, porque no
 * sabe que LBRACE es '{' ni que IDENTIFIER es un nombre. Este módulo la
 * convierte en:
 *
 *   no se esperaba '{' en este lugar; se esperaba un nombre o ')'
 *
 * No se toca la gramática (gramatica.y): solo se traduce el texto que
 * Bison ya produce. Así el compilador de los compañeros queda igual.
 */

/*
 * Traduce un mensaje de error del parser.
 *
 *   mensaje:    el texto de Bison, por ejemplo "syntax error, unexpected SEMICOLON".
 *   encontrado: lo que estaba escrito en el lugar del error, por ejemplo "numero".
 *               Sirve para decir "el nombre 'numero'" en vez de solo "un nombre".
 *               Puede ser NULL si no se conoce.
 *
 * Si el mensaje no es de Bison, se devuelve una copia sin cambios.
 * Devuelve memoria nueva (liberar con free), o NULL si no hay memoria.
 */
char *ide_traducir_mensaje(const char *mensaje, const char *encontrado);

/*
 * Describe un token de Bison en español.
 *
 *   "SEMICOLON"   -> "';'"
 *   "MAIN"        -> "la palabra 'main'"
 *   "IDENTIFIER"  -> "un nombre"
 *   "end of file" -> "el final del archivo"
 *
 * Devuelve NULL si el nombre no se conoce.
 * El texto devuelto es fijo: no hay que liberarlo.
 */
const char *ide_describir_token(const char *nombre_bison);

#endif
