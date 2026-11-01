#include "ide_arreglos.h"

#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Lista de tokens con su posición                                     */
/* ------------------------------------------------------------------ */

/*
 * Para decidir el arreglo hay que mirar los tokens alrededor del error
 * (por ejemplo, cuál fue el último token antes del error). Por eso primero
 * se pasa el texto por el lexer y se guarda cada token con su posición.
 */
typedef struct {
    TokenType tipo;
    size_t linea, columna;          /* Dónde empieza. */
    size_t linea_fin, columna_fin;  /* Dónde termina (la posición justo después). */
} TokenUbicado;

typedef struct {
    TokenUbicado *elementos;
    size_t cantidad;
    size_t capacidad;
} ListaTokens;

/*
 * Pasa todo el texto por el lexer y guarda los tokens, incluido el último
 * (TOKEN_EOF, el final del archivo). Devuelve 0 si funcionó, -1 si no hubo memoria.
 */
static int leer_tokens(const char *texto, size_t largo, ListaTokens *lista)
{
    Lexer lexer;
    lexer_init(&lexer, texto, largo, "<editor>");

    for (;;) {
        Token t = lexer_next(&lexer);
        int sin_memoria = (t.error == TOKEN_OUT_OF_MEMORY);

        /* Después de lexer_next, el lexer quedó justo al final del token. */
        TokenUbicado u = {t.type, t.line, t.column, lexer.line, lexer.column};
        token_dispose(&t);
        if (sin_memoria) return -1;

        if (lista->cantidad == lista->capacidad) {
            size_t nueva = (lista->capacidad == 0) ? 64 : lista->capacidad * 2;
            TokenUbicado *mas = realloc(lista->elementos, nueva * sizeof *mas);
            if (mas == NULL) return -1;
            lista->elementos = mas;
            lista->capacidad = nueva;
        }
        lista->elementos[lista->cantidad++] = u;

        if (u.tipo == TOKEN_EOF) return 0;
    }
}

/* Busca el token que empieza en (linea, columna). Devuelve su índice, o -1. */
static long buscar_token(const ListaTokens *lista, size_t linea, size_t columna)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        if (lista->elementos[i].linea == linea && lista->elementos[i].columna == columna) {
            return (long)i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Posiciones en el texto                                              */
/* ------------------------------------------------------------------ */

/*
 * Convierte (linea, columna) en la posición en bytes dentro del texto.
 *
 * Hace falta porque las columnas cuentan CARACTERES, y una letra como 'á'
 * ocupa 2 bytes en UTF-8. Los saltos de línea se cuentan igual que en el
 * lexer: "\n", "\r\n" y "\r" son un solo salto.
 *
 * Devuelve 0 si la posición existe, o -1 si está fuera del texto.
 */
static int byte_de_posicion(const char *texto, size_t largo,
                            size_t linea, size_t columna, size_t *byte)
{
    size_t l = 1, c = 1, i = 0;
    for (;;) {
        if (l == linea && c == columna) {
            *byte = i;
            return 0;
        }
        if (i >= largo) return -1;

        unsigned char actual = (unsigned char)texto[i];
        if (actual == '\r' || actual == '\n') {
            /* Una posición más allá del final de la línea no existe. */
            if (l == linea) return -1;
            i++;
            if (actual == '\r' && i < largo && texto[i] == '\n') i++;
            l++;
            c = 1;
        } else {
            /* Avanzar un carácter completo: los bytes 10xxxxxx son la
             * continuación de una letra de varios bytes. */
            i++;
            while (i < largo && ((unsigned char)texto[i] & 0xC0) == 0x80) i++;
            c++;
        }
    }
}

/* La posición justo después del último carácter del texto. */
static void posicion_final(const char *texto, size_t largo, size_t *linea, size_t *columna)
{
    Lexer lexer;
    lexer_init(&lexer, texto, largo, "<editor>");
    for (;;) {
        Token t = lexer_next(&lexer);
        TokenType tipo = t.type;
        token_dispose(&t);
        if (tipo == TOKEN_EOF) break;
    }
    /* El lexer se salta espacios y comentarios del final, así que tras
     * el EOF quedó exactamente al final del texto. */
    *linea = lexer.line;
    *columna = lexer.column;
}

/*
 * Copia la sangría (espacios y tabs del inicio) de una línea.
 * Así el '}' que se agrega queda alineado con la línea que abrió el '{'.
 * Devuelve memoria nueva (liberar con free), o NULL si no hubo memoria.
 */
static char *sangria_de_linea(const char *texto, size_t largo, size_t linea)
{
    size_t inicio = 0;
    if (byte_de_posicion(texto, largo, linea, 1, &inicio) != 0) inicio = largo;

    size_t fin = inicio;
    while (fin < largo && (texto[fin] == ' ' || texto[fin] == '\t')) fin++;

    char *sangria = malloc(fin - inicio + 1);
    if (sangria == NULL) return NULL;
    memcpy(sangria, texto + inicio, fin - inicio);
    sangria[fin - inicio] = '\0';
    return sangria;
}

/* ------------------------------------------------------------------ */
/* Armar un arreglo                                                    */
/* ------------------------------------------------------------------ */

/*
 * Llena el arreglo. "insertar" se copia. Devuelve 1 (hay arreglo)
 * o -1 si no hubo memoria, para poder escribir "return preparar(...)".
 */
static int preparar(IdeArreglo *a, size_t linea, size_t columna, size_t borrar,
                    const char *insertar)
{
    a->linea = linea;
    a->columna = columna;
    a->borrar = borrar;
    a->insertar = malloc(strlen(insertar) + 1);
    if (a->insertar == NULL) return -1;
    strcpy(a->insertar, insertar);
    return 1;
}

/* ¿Puede este token ser el final de una sentencia? Por ejemplo, en
 * "numero : 10" el último token es el número 10. Después de estos tokens
 * es donde suele faltar el ';'. */
static int puede_terminar_sentencia(TokenType tipo)
{
    switch (tipo) {
        case TOKEN_IDENTIFIER:
        case TOKEN_INTEGER:
        case TOKEN_STRING:
        case TOKEN_CHARACTER:
        case TOKEN_RPAREN:
        case TOKEN_RBRACKET:
        case TOKEN_DECLARE_TRUE:
        case TOKEN_DECLARE_FALSE:
        case TOKEN_STOP:
            return 1;
        default:
            return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Arreglos para los errores del parser                                */
/* ------------------------------------------------------------------ */

/*
 * El parser marca el token donde NO pudo seguir. Mirando ese token y el
 * anterior se adivina qué faltaba.
 */
static int arreglar_error_del_parser(const ListaTokens *tokens, const Diagnostico *d,
                                     IdeArreglo *a)
{
    long k = buscar_token(tokens, d->linea, d->columna);
    if (k <= 0) return 0;  /* No se encontró el token, o no hay nada antes. */

    const TokenUbicado *donde = &tokens->elementos[k];
    const TokenUbicado *antes = &tokens->elementos[k - 1];

    /*
     * Caso 1: falta ')' antes de '{'.
     *   whether (x == 1 {      ->   whether (x == 1) {
     * Se cuentan los paréntesis de esa línea antes del '{': si quedó
     * alguno abierto, el ')' que falta va justo antes del '{'.
     */
    if (donde->tipo == TOKEN_LBRACE) {
        long abiertos = 0;
        for (long i = k - 1; i >= 0 && tokens->elementos[i].linea == donde->linea; --i) {
            if (tokens->elementos[i].tipo == TOKEN_LPAREN) abiertos++;
            if (tokens->elementos[i].tipo == TOKEN_RPAREN) abiertos--;
        }
        if (abiertos > 0) {
            snprintf(a->descripcion, sizeof a->descripcion,
                     "agregar ')' antes de '{' en la linea %zu", donde->linea);
            return preparar(a, antes->linea_fin, antes->columna_fin, 0, ")");
        }
    }

    /*
     * Caso 2: falta ';' al final de la línea anterior.
     *   numero * declare_int : 10      <- aquí falta el ';'
     *   numero : 5;                    <- pero el parser se queja aquí
     * El token anterior al error está en otra línea y puede cerrar una
     * sentencia, así que el ';' va pegado a él.
     */
    int otra_linea = (antes->linea_fin < donde->linea) || donde->tipo == TOKEN_EOF;
    if (otra_linea && puede_terminar_sentencia(antes->tipo)) {
        snprintf(a->descripcion, sizeof a->descripcion,
                 "agregar ';' al final de la linea %zu", antes->linea_fin);
        return preparar(a, antes->linea_fin, antes->columna_fin, 0, ";");
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Arreglos para los paréntesis, corchetes y llaves                    */
/* ------------------------------------------------------------------ */

/*
 * Un '{', '(' o '[' que nunca se cerró (mensaje "falta cerrar este ...").
 *
 *   '{': se agrega '}' en una línea nueva al final del archivo, con la
 *        misma sangría que la línea que abrió la llave.
 *   '(' y '[': se agrega el cierre al final de esa misma línea, pero antes
 *        de un ';' o '{' final:  x : (1 gauss 2;  ->  x : (1 gauss 2);
 */
static int arreglar_sin_cerrar(const char *texto, size_t largo, const ListaTokens *tokens,
                               const Diagnostico *d, IdeArreglo *a)
{
    long k = buscar_token(tokens, d->linea, d->columna);
    if (k < 0) return 0;
    TokenType abre = tokens->elementos[k].tipo;

    if (abre == TOKEN_LBRACE) {
        size_t linea_fin, columna_fin;
        posicion_final(texto, largo, &linea_fin, &columna_fin);

        char *sangria = sangria_de_linea(texto, largo, d->linea);
        if (sangria == NULL) return -1;

        /* Si el texto no termina en salto de línea, primero se agrega uno. */
        int falta_salto = (largo > 0 && texto[largo - 1] != '\n' && texto[largo - 1] != '\r');
        char *insertar = malloc(strlen(sangria) + 4);
        if (insertar == NULL) {
            free(sangria);
            return -1;
        }
        sprintf(insertar, "%s%s}\n", falta_salto ? "\n" : "", sangria);
        free(sangria);

        snprintf(a->descripcion, sizeof a->descripcion,
                 "agregar '}' al final del archivo para cerrar la '{' de la linea %zu",
                 d->linea);
        int r = preparar(a, linea_fin, columna_fin, 0, insertar);
        free(insertar);
        return r;
    }

    if (abre != TOKEN_LPAREN && abre != TOKEN_LBRACKET) return 0;
    const char *cierre = (abre == TOKEN_LPAREN) ? ")" : "]";

    /* El último token de la línea del paréntesis... */
    size_t ultimo = (size_t)k;
    while (ultimo + 1 < tokens->cantidad &&
           tokens->elementos[ultimo + 1].tipo != TOKEN_EOF &&
           tokens->elementos[ultimo + 1].linea == d->linea) {
        ultimo++;
    }
    /* ...pero si es un ';' o un '{', el cierre va antes de él. */
    TokenType tipo_ultimo = tokens->elementos[ultimo].tipo;
    if (ultimo > (size_t)k && (tipo_ultimo == TOKEN_SEMICOLON || tipo_ultimo == TOKEN_LBRACE)) {
        ultimo--;
    }

    const TokenUbicado *despues_de = &tokens->elementos[ultimo];
    snprintf(a->descripcion, sizeof a->descripcion,
             "agregar '%s' para cerrar el '%c' de la linea %zu",
             cierre, (abre == TOKEN_LPAREN) ? '(' : '[', d->linea);
    return preparar(a, despues_de->linea_fin, despues_de->columna_fin, 0, cierre);
}

/*
 * Un cierre equivocado (mensaje "se esperaba ')' para cerrar el '(' de la
 * linea N, ..."). Solo se arregla si la apertura está en la MISMA línea:
 *
 *   lista[(1 gauss 2]  ->  lista[(1 gauss 2)
 *
 * Si están en líneas distintas, lo más probable es que falte un cierre en
 * otro lado, y cambiar este símbolo empeoraría el código.
 */
static int arreglar_cierre_equivocado(const Diagnostico *d, IdeArreglo *a)
{
    char esperado, abre, encontrado;
    size_t linea_apertura;
    if (sscanf(d->mensaje, "se esperaba '%c' para cerrar el '%c' de la linea %zu",
               &esperado, &abre, &linea_apertura) != 3) {
        return 0;
    }
    if (linea_apertura != d->linea) return 0;

    /* El símbolo encontrado es el último carácter entre comillas del mensaje. */
    const char *final = strrchr(d->mensaje, '\'');
    encontrado = (final != NULL && final - d->mensaje >= 2) ? final[-1] : '?';

    char insertar[2] = {esperado, '\0'};
    snprintf(a->descripcion, sizeof a->descripcion,
             "cambiar '%c' por '%c'", encontrado, esperado);
    return preparar(a, d->linea, d->columna, 1, insertar);
}

/* ------------------------------------------------------------------ */
/* Funciones públicas                                                  */
/* ------------------------------------------------------------------ */

/* ¿El texto empieza con este prefijo? */
static int empieza_con(const char *texto, const char *prefijo)
{
    return strncmp(texto, prefijo, strlen(prefijo)) == 0;
}

int ide_proponer_arreglo(const char *texto, size_t largo,
                         const Diagnostico *error, IdeArreglo *arreglo)
{
    memset(arreglo, 0, sizeof *arreglo);

    /* Solo se arreglan errores de sintaxis: los semánticos (tipos,
     * variables sin declarar...) necesitan que la persona decida. */
    if (error->fase != DIAG_SINTACTICO) return 0;

    /*
     * Cada tipo de error se reconoce por cómo empieza su mensaje:
     *   - los del parser vienen de ide_mensajes.c;
     *   - los de delimitadores vienen de ide_revisar_delimitadores (ide_nucleo.c).
     * Si alguno de esos mensajes cambia, hay que cambiarlo aquí también.
     */
    if (empieza_con(error->mensaje, "se esperaba '")) {
        return arreglar_cierre_equivocado(error, arreglo);
    }
    if (empieza_con(error->mensaje, "el '") && strstr(error->mensaje, "sobra") != NULL) {
        /* Un cierre que sobra: se borra. El símbolo es el carácter después de "el '". */
        snprintf(arreglo->descripcion, sizeof arreglo->descripcion,
                 "borrar el '%c' que sobra", error->mensaje[4]);
        return preparar(arreglo, error->linea, error->columna, 1, "");
    }

    int es_del_parser = empieza_con(error->mensaje, "no se esperaba ") ||
                        empieza_con(error->mensaje, "el archivo termina");
    int es_sin_cerrar = empieza_con(error->mensaje, "falta cerrar este '");
    if (!es_del_parser && !es_sin_cerrar) return 0;

    /* Para estos dos casos hay que mirar los tokens alrededor del error. */
    ListaTokens tokens = {0};
    int resultado;
    if (leer_tokens(texto, largo, &tokens) != 0) {
        resultado = -1;
    } else if (es_del_parser) {
        resultado = arreglar_error_del_parser(&tokens, error, arreglo);
    } else {
        resultado = arreglar_sin_cerrar(texto, largo, &tokens, error, arreglo);
    }
    free(tokens.elementos);
    return resultado;
}

void ide_liberar_arreglo(IdeArreglo *arreglo)
{
    free(arreglo->insertar);
    arreglo->insertar = NULL;
}

char *ide_aplicar_arreglo(const char *texto, size_t largo,
                          const IdeArreglo *arreglo, size_t *nuevo_largo)
{
    /* Dónde empieza el cambio, en bytes. */
    size_t inicio;
    if (byte_de_posicion(texto, largo, arreglo->linea, arreglo->columna, &inicio) != 0) {
        return NULL;
    }

    /* Dónde termina lo que se borra: avanzar "borrar" caracteres completos. */
    size_t fin = inicio;
    for (size_t i = 0; i < arreglo->borrar && fin < largo; ++i) {
        fin++;
        while (fin < largo && ((unsigned char)texto[fin] & 0xC0) == 0x80) fin++;
    }

    /* Texto nuevo = lo de antes + lo insertado + lo de después. */
    size_t largo_insertar = strlen(arreglo->insertar);
    size_t total = inicio + largo_insertar + (largo - fin);
    char *nuevo = malloc(total + 1);
    if (nuevo == NULL) return NULL;

    memcpy(nuevo, texto, inicio);
    memcpy(nuevo + inicio, arreglo->insertar, largo_insertar);
    memcpy(nuevo + inicio + largo_insertar, texto + fin, largo - fin);
    nuevo[total] = '\0';

    *nuevo_largo = total;
    return nuevo;
}
