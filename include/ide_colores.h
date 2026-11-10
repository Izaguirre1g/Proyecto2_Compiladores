/*
 * Colores del código en el editor del IDE (resaltado de sintaxis), con los
 * colores del tema "Dark+" de VS Code.
 *
 * Se usa el MISMO lexer del compilador para partir el texto en tokens, así
 * que una palabra se colorea exactamente como el compilador la entiende.
 * Cada token recibe una categoría:
 *
 *   IDE_COLOR_CONTROL     whether, alif, also, whale, stop, cycle, let,
 *                         until, step, endgame, give, seek, seize, bring, aka
 *   IDE_COLOR_PALABRA     create_funk, declare_const, declare_list,
 *                         declare_true, declare_false y los operadores con
 *                         nombre (gauss, neumann, pitagoras, euclides,
 *                         euler, descartes)
 *   IDE_COLOR_TIPO        declare_int, declare_boolean, declare_text,
 *                         declare_char, declare_infinite_void
 *   IDE_COLOR_FUNCION     un nombre seguido de "(" (una llamada o la
 *                         definición), main, y add, remove, size y show
 *   IDE_COLOR_VARIABLE    los demás nombres
 *   IDE_COLOR_NUMERO      números
 *   IDE_COLOR_TEXTO       textos entre comillas y caracteres
 *   IDE_COLOR_COMENTARIO  comentarios (%% ... y %%// ... //%%): el lexer los
 *                         salta, así que son lo que queda ENTRE dos tokens
 *   IDE_COLOR_PAR_1..3    ( ) [ ] { }, con un color por nivel de anidación,
 *                         como el coloreado de pares de VS Code
 *
 * No usa GTK: así se puede probar sin abrir ventanas (tests/test_ide_colores.c).
 */
#ifndef IDE_COLORES_H
#define IDE_COLORES_H

#include <stddef.h>

typedef enum {
    IDE_COLOR_CONTROL,
    IDE_COLOR_PALABRA,
    IDE_COLOR_TIPO,
    IDE_COLOR_FUNCION,
    IDE_COLOR_VARIABLE,
    IDE_COLOR_NUMERO,
    IDE_COLOR_TEXTO,
    IDE_COLOR_COMENTARIO,
    IDE_COLOR_PAR_1,
    IDE_COLOR_PAR_2,
    IDE_COLOR_PAR_3,
    IDE_COLOR_CANTIDAD   /* No es una categoría: cuántas hay. */
} IdeColor;

/* Un pedazo del texto que va de un color. Posiciones desde 1; la columna
 * cuenta caracteres (no bytes) y el final es justo después del pedazo. */
typedef struct {
    IdeColor color;
    size_t linea, columna;
    size_t linea_fin, columna_fin;
} IdeTramoColor;

typedef struct {
    IdeTramoColor *elementos;
    size_t cantidad;
    size_t capacidad;
} IdeColores;

/* Calcula los colores del texto. Se crea vacío así: IdeColores c = {0};
 * Devuelve 0 si funcionó o -1 si no hubo memoria. */
int ide_colorear(const char *texto, size_t largo, IdeColores *colores);

void ide_liberar_colores(IdeColores *colores);

#endif
