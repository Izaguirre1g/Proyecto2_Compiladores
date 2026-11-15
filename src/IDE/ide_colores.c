/*
 * Colores del código (ver include/ide_colores.h).
 *
 * Pasos:
 *   1. Leer todos los tokens con el lexer del compilador, guardando dónde
 *      empieza y termina cada uno.
 *   2. Darle una categoría a cada token. Para saber si un nombre es una
 *      función se mira el token SIGUIENTE: si es "(", es una llamada o la
 *      definición de una función.
 *   3. Los comentarios no son tokens: el lexer los salta. Por eso, lo que
 *      queda entre el final de un token y el inicio del siguiente, sin
 *      contar espacios, es un comentario. Para encontrar ese hueco en el
 *      texto, un cursor avanza por los bytes llevando la línea y la columna.
 */
#include "ide_colores.h"

#include <stdlib.h>
#include <string.h>

#include "lexer.h"

/* Un token con su posición (el lexer da el inicio; el final es donde quedó
 * el lexer justo después de leerlo). */
typedef struct {
    TokenType tipo;
    size_t linea, columna, linea_fin, columna_fin;
} TokenColor;

/* Agrega un tramo de color. 0 bien, -1 sin memoria. */
static int agregar(IdeColores *c, IdeColor color, size_t linea, size_t columna,
                   size_t linea_fin, size_t columna_fin)
{
    if (c->cantidad == c->capacidad) {
        size_t nueva = c->capacidad ? c->capacidad * 2 : 256;
        IdeTramoColor *mas = realloc(c->elementos, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        c->elementos = mas;
        c->capacidad = nueva;
    }
    IdeTramoColor t = {color, linea, columna, linea_fin, columna_fin};
    c->elementos[c->cantidad++] = t;
    return 0;
}

/*
 * La categoría de un token que no es nombre ni paréntesis, o -1 si va con
 * el color normal del texto (por ejemplo ";", ":", "*" o "==").
 */
static int categoria_fija(TokenType t)
{
    switch (t) {
        /* Control del flujo: como return, if y while en VS Code. */
        case TOKEN_WHETHER: case TOKEN_ALIF: case TOKEN_ALSO: case TOKEN_WHALE:
        case TOKEN_STOP: case TOKEN_CYCLE: case TOKEN_LET: case TOKEN_UNTIL:
        case TOKEN_STEP: case TOKEN_ENDGAME: case TOKEN_GIVE: case TOKEN_SEEK:
        case TOKEN_SEIZE: case TOKEN_BRING: case TOKEN_AKA:
            return IDE_COLOR_CONTROL;
        /* Palabras del lenguaje: como static y const. */
        case TOKEN_CREATE_FUNK: case TOKEN_DECLARE_CONST: case TOKEN_DECLARE_LIST:
        case TOKEN_DECLARE_TRUE: case TOKEN_DECLARE_FALSE:
        case TOKEN_GAUSS: case TOKEN_NEUMANN: case TOKEN_PITAGORAS:
        case TOKEN_EUCLIDES: case TOKEN_EULER: case TOKEN_DESCARTES:
            return IDE_COLOR_PALABRA;
        /* Tipos: como AstNodo. */
        case TOKEN_DECLARE_INT: case TOKEN_DECLARE_BOOLEAN: case TOKEN_DECLARE_TEXT:
        case TOKEN_DECLARE_CHAR: case TOKEN_DECLARE_INFINITE_VOID:
            return IDE_COLOR_TIPO;
        /* Funciones del lenguaje y main: como ast_crear. */
        case TOKEN_MAIN: case TOKEN_ADD: case TOKEN_REMOVE: case TOKEN_SIZE: case TOKEN_SHOW:
            return IDE_COLOR_FUNCION;
        case TOKEN_INTEGER:
            return IDE_COLOR_NUMERO;
        case TOKEN_STRING: case TOKEN_CHARACTER:
            return IDE_COLOR_TEXTO;
        default:
            return -1;
    }
}

/* ¿Abre o cierra un par? 1 abre, -1 cierra, 0 ninguno. */
static int par(TokenType t)
{
    if (t == TOKEN_LPAREN || t == TOKEN_LBRACKET || t == TOKEN_LBRACE) return 1;
    if (t == TOKEN_RPAREN || t == TOKEN_RBRACKET || t == TOKEN_RBRACE) return -1;
    return 0;
}

/*
 * Un cursor que recorre el texto llevando a la vez el byte, la línea y la
 * columna (en caracteres, como el lexer). Avanza hasta la posición
 * (linea, columna) o hasta el final del texto.
 */
typedef struct {
    size_t byte, linea, columna;
} Cursor;

static void avanzar(const char *texto, size_t largo, Cursor *c, size_t linea, size_t columna)
{
    while (c->byte < largo && (c->linea < linea || (c->linea == linea && c->columna < columna))) {
        unsigned char ch = (unsigned char)texto[c->byte++];
        if (ch == '\n') {
            c->linea++;
            c->columna = 1;
        } else if ((ch & 0xC0) != 0x80) {
            c->columna++;  /* Solo el primer byte de cada carácter UTF-8 cuenta. */
        }
        /* Saltar los bytes de continuación de este mismo carácter. */
        while (c->byte < largo && ((unsigned char)texto[c->byte] & 0xC0) == 0x80) c->byte++;
    }
}

/*
 * Colorea como comentario lo que hay desde el cursor "desde" hasta el byte
 * "hasta", sin los espacios de las puntas. Entre dos tokens solo puede haber
 * espacios y comentarios, así que lo que no es espacio es comentario.
 */
static int colorear_hueco(const char *texto, Cursor desde, size_t hasta, IdeColores *c)
{
    Cursor cur = desde;
    int hay = 0;
    size_t inicio_l = 0, inicio_c = 0, fin_l = 0, fin_c = 0;
    while (cur.byte < hasta) {
        unsigned char ch = (unsigned char)texto[cur.byte];
        int espacio = ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
        if (!espacio) {
            if (!hay) {
                inicio_l = cur.linea;
                inicio_c = cur.columna;
                hay = 1;
            }
            fin_l = cur.linea;
            fin_c = cur.columna + 1;  /* Justo después de este carácter. */
        }
        /* Un carácter más (con todos sus bytes). */
        avanzar(texto, hasta, &cur, ch == '\n' ? cur.linea + 1 : cur.linea,
                ch == '\n' ? 1 : cur.columna + 1);
    }
    return hay ? agregar(c, IDE_COLOR_COMENTARIO, inicio_l, inicio_c, fin_l, fin_c) : 0;
}

int ide_colorear(const char *texto, size_t largo, IdeColores *c)
{
    memset(c, 0, sizeof *c);

    /* Paso 1: los tokens con su posición. */
    TokenColor *tokens = NULL;
    size_t cantidad = 0, capacidad = 0;
    Lexer lexer;
    lexer_init(&lexer, texto, largo, "<editor>");
    for (;;) {
        Token t = lexer_next(&lexer);
        int sin_memoria = t.error == TOKEN_OUT_OF_MEMORY;
        token_dispose(&t);
        if (sin_memoria) goto sin_memoria;
        if (cantidad == capacidad) {
            capacidad = capacidad ? capacidad * 2 : 256;
            TokenColor *mas = realloc(tokens, capacidad * sizeof *mas);
            if (mas == NULL) goto sin_memoria;
            tokens = mas;
        }
        TokenColor tc = {t.type, t.line, t.column, lexer.line, lexer.column};
        tokens[cantidad++] = tc;
        if (t.type == TOKEN_EOF) break;
    }

    /* Pasos 2 y 3: la categoría de cada token y los comentarios entre ellos.
     * El cursor queda siempre justo después del token anterior. */
    Cursor cursor = {0, 1, 1};
    int nivel = 0;  /* Pares abiertos (para su color). */
    for (size_t i = 0; i < cantidad; ++i) {
        const TokenColor *t = &tokens[i];

        /* El hueco entre el token anterior y este: ahí van los comentarios.
         * Antes del final del archivo, el hueco llega hasta el final. */
        Cursor hueco = cursor;
        if (t->tipo == TOKEN_EOF) {
            if (colorear_hueco(texto, hueco, largo, c) != 0) goto sin_memoria;
            break;
        }
        avanzar(texto, largo, &cursor, t->linea, t->columna);
        if (colorear_hueco(texto, hueco, cursor.byte, c) != 0) goto sin_memoria;
        avanzar(texto, largo, &cursor, t->linea_fin, t->columna_fin);

        /* La categoría del token. */
        int color = categoria_fija(t->tipo);
        if (t->tipo == TOKEN_IDENTIFIER) {
            int llamada = i + 1 < cantidad && tokens[i + 1].tipo == TOKEN_LPAREN;
            color = llamada ? IDE_COLOR_FUNCION : IDE_COLOR_VARIABLE;
        }
        int p = par(t->tipo);
        if (p < 0 && nivel > 0) nivel--;  /* El cierre va con el color de su apertura. */
        if (p != 0) color = IDE_COLOR_PAR_1 + nivel % 3;
        if (p > 0) nivel++;

        if (color >= 0 &&
            agregar(c, (IdeColor)color, t->linea, t->columna, t->linea_fin, t->columna_fin) != 0) {
            goto sin_memoria;
        }
    }
    free(tokens);
    return 0;

sin_memoria:
    free(tokens);
    ide_liberar_colores(c);
    return -1;
}

void ide_liberar_colores(IdeColores *c)
{
    free(c->elementos);
    memset(c, 0, sizeof *c);
}
