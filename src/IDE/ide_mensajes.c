#include "ide_mensajes.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tabla de tokens: nombre de Bison -> descripción en español          */
/* ------------------------------------------------------------------ */

/*
 * Cada fila dice cómo nombrar un token en un mensaje.
 *
 *   bison:    el nombre que usa Bison (el de los %token de gramatica.y).
 *   general:  cómo se nombra en general: "un nombre", "';'".
 *   concreto: solo para tokens que cambian de texto (nombres, números,
 *             textos y caracteres). Va antes de lo que estaba escrito:
 *             "el nombre " + "numero". NULL si el token siempre se escribe igual.
 *   comillas: 1 si lo escrito se pone entre comillas simples ('numero').
 *             Los textos y caracteres ya traen sus propias comillas ("hola", $a$).
 */
typedef struct {
    const char *bison;
    const char *general;
    const char *concreto;
    int comillas;
} NombreToken;

static const NombreToken TOKENS[] = {
    /* Tokens que cambian según lo escrito. */
    {"IDENTIFIER", "un nombre",   "el nombre ",   1},
    {"INTEGER",    "un número",   "el número ",   1},
    {"STRING",     "un texto",    "el texto ",    0},
    {"CHARACTER",  "un carácter", "el carácter ", 0},

    /* Signos. */
    {"SEMICOLON",     "';'",  NULL, 0},
    {"COMMA",         "','",  NULL, 0},
    {"ASSIGN",        "':'",  NULL, 0},
    {"STAR",          "'*'",  NULL, 0},
    {"HASH",          "'#'",  NULL, 0},
    {"DOT",           "'.'",  NULL, 0},
    {"LPAREN",        "'('",  NULL, 0},
    {"RPAREN",        "')'",  NULL, 0},
    {"LBRACKET",      "'['",  NULL, 0},
    {"RBRACKET",      "']'",  NULL, 0},
    {"LBRACE",        "'{'",  NULL, 0},
    {"RBRACE",        "'}'",  NULL, 0},
    {"EQUAL",         "'=='", NULL, 0},
    {"NOT_EQUAL",     "'=/='", NULL, 0},
    {"LESS",          "'<'",  NULL, 0},
    {"GREATER",       "'>'",  NULL, 0},
    {"LESS_EQUAL",    "'<='", NULL, 0},
    {"GREATER_EQUAL", "'>='", NULL, 0},
    {"AND",           "'&&'", NULL, 0},
    {"OR",            "'||'", NULL, 0},
    {"NOT",           "'~'",  NULL, 0},
    {"XOR",           "'^'",  NULL, 0},

    /* Palabras reservadas del lenguaje. */
    {"MAIN",                  "la palabra 'main'",                  NULL, 0},
    {"CREATE_FUNK",           "la palabra 'create_funk'",           NULL, 0},
    {"GIVE",                  "la palabra 'give'",                  NULL, 0},
    {"WHETHER",               "la palabra 'whether'",               NULL, 0},
    {"ALIF",                  "la palabra 'alif'",                  NULL, 0},
    {"ALSO",                  "la palabra 'also'",                  NULL, 0},
    {"WHALE",                 "la palabra 'whale'",                 NULL, 0},
    {"STOP",                  "la palabra 'stop'",                  NULL, 0},
    {"CYCLE",                 "la palabra 'cycle'",                 NULL, 0},
    {"LET",                   "la palabra 'let'",                   NULL, 0},
    {"UNTIL",                 "la palabra 'until'",                 NULL, 0},
    {"STEP",                  "la palabra 'step'",                  NULL, 0},
    {"ENDGAME",               "la palabra 'endgame'",               NULL, 0},
    {"BRING",                 "la palabra 'bring'",                 NULL, 0},
    {"AKA",                   "la palabra 'aka'",                   NULL, 0},
    {"SEEK",                  "la palabra 'seek'",                  NULL, 0},
    {"SEIZE",                 "la palabra 'seize'",                 NULL, 0},
    {"DECLARE_CONST",         "la palabra 'declare_const'",         NULL, 0},
    {"DECLARE_LIST",          "la palabra 'declare_list'",          NULL, 0},
    {"ADD",                   "la palabra 'add'",                   NULL, 0},
    {"REMOVE",                "la palabra 'remove'",                NULL, 0},
    {"SIZE",                  "la palabra 'size'",                  NULL, 0},
    {"DECLARE_INT",           "la palabra 'declare_int'",           NULL, 0},
    {"DECLARE_BOOLEAN",       "la palabra 'declare_boolean'",       NULL, 0},
    {"DECLARE_TEXT",          "la palabra 'declare_text'",          NULL, 0},
    {"DECLARE_CHAR",          "la palabra 'declare_char'",          NULL, 0},
    {"DECLARE_INFINITE_VOID", "la palabra 'declare_infinite_void'", NULL, 0},
    {"DECLARE_TRUE",          "la palabra 'declare_true'",          NULL, 0},
    {"DECLARE_FALSE",         "la palabra 'declare_false'",         NULL, 0},
    {"GAUSS",                 "la palabra 'gauss' (suma)",          NULL, 0},
    {"NEUMANN",               "la palabra 'neumann' (resta)",       NULL, 0},
    {"PITAGORAS",             "la palabra 'pitagoras' (multiplicación)", NULL, 0},
    {"EUCLIDES",              "la palabra 'euclides' (división)",   NULL, 0},
    {"EULER",                 "la palabra 'euler' (residuo)",       NULL, 0},
    {"DESCARTES",             "la palabra 'descartes' (potencia)",  NULL, 0},

    /* Nombres especiales que Bison usa por su cuenta. */
    {"end of file",   "el final del archivo", NULL, 0},
    {"invalid token", "un símbolo no válido", NULL, 0},
};

#define CANTIDAD_TOKENS (sizeof TOKENS / sizeof TOKENS[0])

/* Lo escrito más largo que se copia en un mensaje. Un texto muy largo
 * ("hola hola hola...") haría el mensaje ilegible; en ese caso se usa
 * la descripción general ("un texto"). */
#define LARGO_MAXIMO_ENCONTRADO 30

/* Busca un token en la tabla. Devuelve NULL si no está. */
static const NombreToken *buscar_token(const char *bison, size_t largo)
{
    for (size_t i = 0; i < CANTIDAD_TOKENS; ++i) {
        if (strlen(TOKENS[i].bison) == largo &&
            strncmp(TOKENS[i].bison, bison, largo) == 0) {
            return &TOKENS[i];
        }
    }
    return NULL;
}

const char *ide_describir_token(const char *nombre_bison)
{
    const NombreToken *t = buscar_token(nombre_bison, strlen(nombre_bison));
    return (t != NULL) ? t->general : NULL;
}

/* ------------------------------------------------------------------ */
/* Texto que crece: para ir armando el mensaje por partes               */
/* ------------------------------------------------------------------ */

typedef struct {
    char *datos;       /* El texto, siempre terminado en '\0'. */
    size_t largo;      /* Cuántos bytes tiene (sin contar el '\0'). */
    size_t capacidad;  /* Cuántos bytes caben antes de pedir más memoria. */
    int sin_memoria;   /* 1 si alguna vez faltó memoria. */
} Texto;

/* Agrega los primeros "largo" bytes de "parte" al final del texto. */
static void agregar_n(Texto *t, const char *parte, size_t largo)
{
    if (t->sin_memoria) return;

    if (t->largo + largo + 1 > t->capacidad) {
        size_t nueva = (t->capacidad == 0) ? 64 : t->capacidad * 2;
        while (nueva < t->largo + largo + 1) nueva *= 2;
        char *mas = realloc(t->datos, nueva);
        if (mas == NULL) {
            t->sin_memoria = 1;
            return;
        }
        t->datos = mas;
        t->capacidad = nueva;
    }
    memcpy(t->datos + t->largo, parte, largo);
    t->largo += largo;
    t->datos[t->largo] = '\0';
}

/* Agrega un texto completo al final. */
static void agregar(Texto *t, const char *parte)
{
    agregar_n(t, parte, strlen(parte));
}

/* Entrega el texto armado (o NULL si faltó memoria). */
static char *terminar(Texto *t)
{
    if (t->sin_memoria) {
        free(t->datos);
        return NULL;
    }
    return t->datos;
}

/* ------------------------------------------------------------------ */
/* Traducción                                                          */
/* ------------------------------------------------------------------ */

/*
 * Agrega la descripción de un token al mensaje.
 *
 *   nombre, largo: el nombre de Bison (no termina en '\0', es un pedazo del mensaje).
 *   encontrado:    lo que estaba escrito, o NULL si no se sabe o no aplica.
 *
 * Con "encontrado" se describe el token exacto ("el nombre 'numero'");
 * sin él, el token en general ("un nombre").
 */
static void agregar_token(Texto *t, const char *nombre, size_t largo, const char *encontrado)
{
    const NombreToken *token = buscar_token(nombre, largo);

    /* Un token que la tabla no conoce: se deja su nombre, entre comillas. */
    if (token == NULL) {
        agregar(t, "'");
        agregar_n(t, nombre, largo);
        agregar(t, "'");
        return;
    }

    int se_puede_mostrar = (token->concreto != NULL && encontrado != NULL &&
                            encontrado[0] != '\0' &&
                            strlen(encontrado) <= LARGO_MAXIMO_ENCONTRADO);
    if (!se_puede_mostrar) {
        agregar(t, token->general);
        return;
    }

    agregar(t, token->concreto);
    if (token->comillas) agregar(t, "'");
    agregar(t, encontrado);
    if (token->comillas) agregar(t, "'");
}

/*
 * Agrega la lista de lo que se esperaba, con "o" antes del último:
 *
 *   "IDENTIFIER or RPAREN"           ->  "un nombre o ')'"
 *   "BRING or DECLARE_CONST or ..."  ->  "la palabra 'bring', la palabra ... o ..."
 */
static void agregar_esperados(Texto *t, const char *lista)
{
    const char *separador = " or ";
    size_t largo_separador = strlen(separador);

    const char *actual = lista;
    for (;;) {
        const char *siguiente = strstr(actual, separador);
        size_t largo = (siguiente != NULL) ? (size_t)(siguiente - actual) : strlen(actual);
        agregar_token(t, actual, largo, NULL);

        if (siguiente == NULL) break;
        actual = siguiente + largo_separador;

        /* Antes del último va " o "; antes de los demás, ", ". */
        agregar(t, (strstr(actual, separador) == NULL) ? " o " : ", ");
    }
}

char *ide_traducir_mensaje(const char *mensaje, const char *encontrado)
{
    Texto t = {0};

    /* Caso 1: Bison se quedó sin memoria. */
    if (strcmp(mensaje, "memory exhausted") == 0) {
        agregar(&t, "no hay memoria suficiente para revisar el archivo");
        return terminar(&t);
    }

    /* Caso 2: no es un error de sintaxis de Bison: se deja igual. */
    const char *inicio = "syntax error";
    if (strncmp(mensaje, inicio, strlen(inicio)) != 0) {
        agregar(&t, mensaje);
        return terminar(&t);
    }

    /*
     * Caso 3: un error de sintaxis. El mensaje tiene hasta tres partes:
     *
     *   syntax error, unexpected <ENCONTRADO>, expecting <A> or <B> or <C>
     *   \__________/  \_____________________/  \_________________________/
     *     siempre       casi siempre             solo si son pocas opciones
     */
    const char *marca_inesperado = ", unexpected ";
    const char *marca_esperado = ", expecting ";

    const char *inesperado = strstr(mensaje, marca_inesperado);
    const char *esperado = strstr(mensaje, marca_esperado);

    /* Sin la segunda parte, Bison no dice qué encontró. */
    if (inesperado == NULL) {
        agregar(&t, "esta parte del código no sigue las reglas del lenguaje");
        return terminar(&t);
    }

    /* El nombre de lo encontrado va desde después de ", unexpected "
     * hasta ", expecting " (o hasta el final si no hay tercera parte). */
    const char *nombre = inesperado + strlen(marca_inesperado);
    size_t largo_nombre = (esperado != NULL) ? (size_t)(esperado - nombre) : strlen(nombre);

    const char *fin_archivo = "end of file";
    int es_fin = (largo_nombre == strlen(fin_archivo) &&
                  strncmp(nombre, fin_archivo, largo_nombre) == 0);

    if (es_fin) {
        /* Llegar al final antes de tiempo casi siempre es algo sin cerrar. */
        agregar(&t, "el archivo termina antes de tiempo");
        if (esperado == NULL) agregar(&t, ": puede faltar una '}' o un ';'");
    } else {
        agregar(&t, "no se esperaba ");
        agregar_token(&t, nombre, largo_nombre, encontrado);
        agregar(&t, " en este lugar");
    }

    /* La tercera parte: lo que sí habría servido en ese lugar. */
    if (esperado != NULL) {
        agregar(&t, "; se esperaba ");
        agregar_esperados(&t, esperado + strlen(marca_esperado));
    }

    return terminar(&t);
}
