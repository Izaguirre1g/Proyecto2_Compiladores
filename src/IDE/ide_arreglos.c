#include "ide_arreglos.h"

#include "lexer.h"
#include "ll1.h"

#include <stdint.h>
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
    char *lexema;                   /* Lo escrito, solo en los nombres (NULL en los demás).
                                     * Sirve para reconocer palabras mal escritas. */
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
        TokenUbicado u = {t.type, t.line, t.column, lexer.line, lexer.column, NULL};
        if (!sin_memoria && t.type == TOKEN_IDENTIFIER && t.lexeme != NULL) {
            u.lexema = malloc(t.length + 1);  /* Copia propia: el token se libera ya. */
            if (u.lexema == NULL) {
                sin_memoria = 1;
            } else {
                memcpy(u.lexema, t.lexeme, t.length + 1);
            }
        }
        token_dispose(&t);
        if (sin_memoria) return -1;

        if (lista->cantidad == lista->capacidad) {
            size_t nueva = (lista->capacidad == 0) ? 64 : lista->capacidad * 2;
            TokenUbicado *mas = realloc(lista->elementos, nueva * sizeof *mas);
            if (mas == NULL) {
                free(u.lexema);
                return -1;
            }
            lista->elementos = mas;
            lista->capacidad = nueva;
        }
        lista->elementos[lista->cantidad++] = u;

        if (u.tipo == TOKEN_EOF) return 0;
    }
}

/* Libera la lista de tokens (y los lexemas copiados). */
static void liberar_tokens(ListaTokens *lista)
{
    for (size_t i = 0; i < lista->cantidad; ++i) free(lista->elementos[i].lexema);
    free(lista->elementos);
    memset(lista, 0, sizeof *lista);
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

/* ------------------------------------------------------------------ */
/* Una variable usada como función: era una asignación                 */
/* ------------------------------------------------------------------ */

/*
 * Error semántico "'i' no es una funcion" en una sentencia como
 *
 *     i ( i gauss 1);      ->      i : i gauss 1;
 *
 * La sintaxis es la de una llamada, pero 'i' es una variable: lo más
 * probable es que se quisiera ASIGNAR. El arreglo cambia "( ... )" por
 * ": ...". Solo se propone si:
 *   - el nombre empieza la sentencia y después viene '(' ... ')' ';'
 *     (todo en la misma línea);
 *   - entre los paréntesis hay UNA expresión (sin comas: con varios
 *     argumentos no hay una asignación clara);
 *   - el resultado es válido según la tabla LL(1).
 *
 * Devuelve 1 si hay arreglo, 0 si no, -1 sin memoria.
 */
static int arreglar_llamada_a_variable(const char *texto, size_t largo,
                                       const ListaTokens *tokens, const Diagnostico *d,
                                       IdeArreglo *a)
{
    long encontrado = buscar_token(tokens, d->linea, d->columna);
    if (encontrado < 0) return 0;
    size_t k = (size_t)encontrado, n = tokens->cantidad;
    const TokenUbicado *e = tokens->elementos;

    /* El nombre empieza la sentencia y lo sigue un '('. */
    if (e[k].tipo != TOKEN_IDENTIFIER || e[k].lexema == NULL || k + 1 >= n) return 0;
    if (k > 0 && e[k - 1].tipo != TOKEN_SEMICOLON && e[k - 1].tipo != TOKEN_LBRACE &&
        e[k - 1].tipo != TOKEN_RBRACE) {
        return 0;
    }
    size_t abre = k + 1;
    if (e[abre].tipo != TOKEN_LPAREN) return 0;

    /* Buscar el ')' que cierra, y que no haya comas a ese nivel. */
    size_t cierra = abre;
    int nivel = 0;
    for (size_t i = abre; i < n; ++i) {
        if (e[i].tipo == TOKEN_LPAREN) nivel++;
        if (e[i].tipo == TOKEN_RPAREN && --nivel == 0) {
            cierra = i;
            break;
        }
        if (e[i].tipo == TOKEN_COMMA && nivel == 1) return 0;  /* Varios argumentos. */
        if (e[i].tipo == TOKEN_EOF) return 0;
    }
    if (cierra == abre || cierra == abre + 1) return 0;           /* Sin cierre, o "()" vacío. */
    if (cierra + 1 >= n || e[cierra + 1].tipo != TOKEN_SEMICOLON) return 0;
    if (e[abre].linea != e[cierra].linea_fin) return 0;           /* Todo en una línea. */

    /* Validar con la tabla LL(1): el mismo programa, con ':' en vez de '('
     * y sin el ')', tiene que ser válido. */
    TokenType *tipos = malloc(n * sizeof *tipos);
    if (tipos == NULL) return -1;
    size_t m = 0;
    for (size_t i = 0; i < n; ++i) {
        if (i == cierra) continue;
        tipos[m++] = (i == abre) ? TOKEN_ASSIGN : e[i].tipo;
    }
    Ll1Resultado r;
    int estado = ll1_analizar(tipos, m, &r);
    free(tipos);
    if (estado != 0) return -1;
    if (r.estado != LL1_ACEPTADA) return 0;

    /* Lo que estaba entre los paréntesis, sin espacios a los lados. */
    size_t desde, hasta;
    if (byte_de_posicion(texto, largo, e[abre].linea_fin, e[abre].columna_fin, &desde) != 0 ||
        byte_de_posicion(texto, largo, e[cierra].linea, e[cierra].columna, &hasta) != 0) {
        return 0;
    }
    while (desde < hasta && (texto[desde] == ' ' || texto[desde] == '\t')) desde++;
    while (hasta > desde && (texto[hasta - 1] == ' ' || texto[hasta - 1] == '\t')) hasta--;

    size_t largo_expresion = hasta - desde;
    char *insertar = malloc(largo_expresion + 3);
    if (insertar == NULL) return -1;
    memcpy(insertar, ": ", 2);
    memcpy(insertar + 2, texto + desde, largo_expresion);
    insertar[largo_expresion + 2] = '\0';

    /* Se reemplaza desde el '(' hasta el ')' (inclusive) por ": expresión".
     * Si había un espacio entre el nombre y el '(' se conserva: "i (" -> "i :". */
    snprintf(a->descripcion, sizeof a->descripcion, "es una asignacion: escribir '%s %.80s'",
             e[k].lexema, insertar);
    int resultado = preparar(a, e[abre].linea, e[abre].columna,
                             e[cierra].columna_fin - e[abre].columna, insertar);
    free(insertar);
    return resultado;
}

/* ------------------------------------------------------------------ */
/* Arreglo para los errores del parser: lo predice la tabla LL(1)      */
/* ------------------------------------------------------------------ */

/* Está más abajo, junto con lo demás del LL(1). */
static int arreglar_con_ll1(const char *texto, size_t largo, const ListaTokens *tokens,
                            const Diagnostico *d, IdeArreglo *a);

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

    /* De los errores semánticos solo se arregla uno: una variable usada
     * como función en "i (expresión);", que era una asignación. Los demás
     * (tipos, variables sin declarar...) necesitan que la persona decida. */
    if (error->fase == DIAG_SEMANTICO) {
        size_t largo_mensaje = strlen(error->mensaje);
        const char *final = " no es una funcion";
        size_t largo_final = strlen(final);
        if (largo_mensaje < largo_final ||
            strcmp(error->mensaje + largo_mensaje - largo_final, final) != 0) {
            return 0;
        }
        ListaTokens tokens = {0};
        int r = leer_tokens(texto, largo, &tokens);
        if (r == 0) r = arreglar_llamada_a_variable(texto, largo, &tokens, error, arreglo);
        liberar_tokens(&tokens);
        return r;
    }
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
        resultado = arreglar_con_ll1(texto, largo, &tokens, error, arreglo);
    } else {
        resultado = arreglar_sin_cerrar(texto, largo, &tokens, error, arreglo);
    }
    liberar_tokens(&tokens);
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

/* ------------------------------------------------------------------ */
/* El arreglo con la tabla LL(1)                                       */
/* ------------------------------------------------------------------ */

/* Los tokens que se pegan al anterior sin espacio: "x;"  "f(x)"  "a[1]". */
static int va_pegado(int terminal)
{
    return terminal == TOKEN_SEMICOLON || terminal == TOKEN_COMMA || terminal == TOKEN_RPAREN ||
           terminal == TOKEN_RBRACKET || terminal == TOKEN_DOT;
}

/*
 * Cómo nombrar un token del archivo en una descripción, entre comillas:
 * 'gauss', ';', o lo que esté escrito si es un nombre o un número
 * ('declar_int', '12'). Si no se puede leer del texto, "un nombre".
 */
static void nombrar_token(const char *texto, size_t largo, const TokenUbicado *t,
                          char *destino, size_t tamano)
{
    if (ll1_texto_fijo(t->tipo)) {
        snprintf(destino, tamano, "'%s'", ll1_texto(t->tipo));
        return;
    }
    size_t inicio, fin;
    if (t->linea == t->linea_fin &&
        byte_de_posicion(texto, largo, t->linea, t->columna, &inicio) == 0 &&
        byte_de_posicion(texto, largo, t->linea_fin, t->columna_fin, &fin) == 0 &&
        fin > inicio && fin - inicio + 3 <= tamano) {
        snprintf(destino, tamano, "'%.*s'", (int)(fin - inicio), texto + inicio);
        return;
    }
    snprintf(destino, tamano, "%s", ll1_texto(t->tipo));
}

/*
 * Arreglo que borra el token i. Si entre él y el token anterior hay un solo
 * carácter en la misma línea (un espacio), se borra también, para no dejar
 * dos espacios seguidos: "x : 5 gauss ;" -> "x : 5 ;".
 */
static int borrar_token(const char *texto, size_t largo, const ListaTokens *tokens,
                        size_t i, IdeArreglo *a)
{
    const TokenUbicado *t = &tokens->elementos[i];
    char nombre[48];
    nombrar_token(texto, largo, t, nombre, sizeof nombre);
    if (ll1_texto_fijo(t->tipo)) {
        snprintf(a->descripcion, sizeof a->descripcion, "borrar %s de la linea %zu",
                 nombre, t->linea);
    } else {
        /* Un nombre o un número puede repetirse en la línea: se dice la columna. */
        snprintf(a->descripcion, sizeof a->descripcion, "borrar %s de la linea %zu, columna %zu",
                 nombre, t->linea, t->columna);
    }

    size_t columna = t->columna;
    size_t cuantos = (t->linea_fin == t->linea) ? t->columna_fin - t->columna : 1;
    if (i > 0) {
        const TokenUbicado *antes = &tokens->elementos[i - 1];
        if (antes->linea_fin == t->linea && antes->columna_fin + 1 == t->columna) {
            columna--;
            cuantos++;
        }
    }
    return preparar(a, t->linea, columna, cuantos, "");
}

/*
 * Convierte la corrección del LL(1) en un arreglo concreto del texto
 * (posición, qué borrar, qué escribir, descripción).
 *
 *   k:        índice del token del error.
 *   anterior: índice del token que el analizador tenía antes del error.
 */
static int armar_arreglo(const char *texto, size_t largo, const ListaTokens *tokens,
                         size_t k, size_t anterior, const Ll1Correccion *c, IdeArreglo *a)
{
    const TokenUbicado *donde = &tokens->elementos[k];
    char nombre_donde[48];
    nombrar_token(texto, largo, donde, nombre_donde, sizeof nombre_donde);

    if (c->tipo == LL1_BORRAR) return borrar_token(texto, largo, tokens, k, a);
    if (c->tipo == LL1_BORRAR_ANTERIOR) {
        if (anterior == SIZE_MAX) return 0;  /* Era un token insertado: no está en el texto. */
        return borrar_token(texto, largo, tokens, anterior, a);
    }

    /* Agregar antes del token anterior: "cycle " justo antes de la 'i' de
     *     i let 0 until 5 {    ->    cycle i let 0 until 5 {            */
    if (c->tipo == LL1_INSERTAR_ANTES_ANTERIOR) {
        if (anterior == SIZE_MAX) return 0;  /* Era un token insertado: no está en el texto. */
        const TokenUbicado *previo = &tokens->elementos[anterior];
        char nombre_previo[48];
        nombrar_token(texto, largo, previo, nombre_previo, sizeof nombre_previo);
        char insertar[64];
        snprintf(insertar, sizeof insertar, "%s ", ll1_texto(c->terminales[0]));
        snprintf(a->descripcion, sizeof a->descripcion, "agregar '%s' antes de %s en la linea %zu",
                 ll1_texto(c->terminales[0]), nombre_previo, previo->linea);
        return preparar(a, previo->linea, previo->columna, 0, insertar);
    }

    if (c->tipo == LL1_CAMBIAR) {
        size_t cuantos = (donde->linea_fin == donde->linea) ? donde->columna_fin - donde->columna : 1;
        snprintf(a->descripcion, sizeof a->descripcion, "cambiar %s por '%s'",
                 nombre_donde, ll1_texto(c->terminales[0]));
        return preparar(a, donde->linea, donde->columna, cuantos, ll1_texto(c->terminales[0]));
    }

    /* INSERTAR: uno o dos tokens, con espacio solo donde hace falta. */
    char nuevo[48];
    snprintf(nuevo, sizeof nuevo, "%s%s%s", ll1_texto(c->terminales[0]),
             (c->cantidad > 1 && !va_pegado(c->terminales[1])) ? " " : "",
             c->cantidad > 1 ? ll1_texto(c->terminales[1]) : "");

    if (k == 0) {  /* No hay token antes: se escribe al inicio, antes del error. */
        char insertar[64];
        snprintf(insertar, sizeof insertar, "%s ", nuevo);
        snprintf(a->descripcion, sizeof a->descripcion, "agregar '%s' al inicio", nuevo);
        return preparar(a, donde->linea, donde->columna, 0, insertar);
    }

    /* Va pegado al token anterior del archivo, porque el parser marca el
     * error en el token SIGUIENTE, que muchas veces está en la línea de abajo. */
    const TokenUbicado *antes = &tokens->elementos[k - 1];
    int otra_linea = antes->linea_fin < donde->linea || donde->tipo == TOKEN_EOF;

    /* Un '}' que falta al final de una línea va en una línea nueva, con la
     * misma sangría que la línea que abrió esa llave:
     *     numero : 10;          numero : 10;
     *                    ->   }
     */
    if (c->terminales[0] == TOKEN_RBRACE && c->cantidad == 1 && otra_linea) {
        /* Buscar hacia atrás la '{' que quedó abierta. */
        long abiertas = 0, i = (long)k - 1;
        for (; i >= 0; --i) {
            if (tokens->elementos[i].tipo == TOKEN_RBRACE) abiertas++;
            if (tokens->elementos[i].tipo == TOKEN_LBRACE && abiertas-- == 0) break;
        }
        if (i >= 0) {
            size_t linea_llave = tokens->elementos[i].linea;
            char *sangria = sangria_de_linea(texto, largo, linea_llave);
            if (sangria == NULL) return -1;
            char *insertar_llave = malloc(strlen(sangria) + 3);
            if (insertar_llave == NULL) {
                free(sangria);
                return -1;
            }
            sprintf(insertar_llave, "\n%s}", sangria);
            free(sangria);
            snprintf(a->descripcion, sizeof a->descripcion,
                     "agregar '}' para cerrar la '{' de la linea %zu", linea_llave);
            int r = preparar(a, antes->linea_fin, antes->columna_fin, 0, insertar_llave);
            free(insertar_llave);
            return r;
        }
    }

    /* Si en la misma línea ya hay 2 o más espacios entre los dos tokens
     * (porque la persona borró el token y dejó sus espacios), se escribe
     * DENTRO del hueco, después del primer espacio:
     *     "n  declare_int"  ->  "n * declare_int"   (y no "n *  declare_int")
     */
    if (!otra_linea && !va_pegado(c->terminales[0]) &&
        donde->columna >= antes->columna_fin + 2) {
        size_t desde, hasta;
        int solo_espacios = byte_de_posicion(texto, largo, antes->linea_fin,
                                             antes->columna_fin, &desde) == 0 &&
                            byte_de_posicion(texto, largo, donde->linea,
                                             donde->columna, &hasta) == 0;
        for (size_t i = desde; solo_espacios && i < hasta; ++i) {
            if (texto[i] != ' ' && texto[i] != '\t') solo_espacios = 0;
        }
        if (solo_espacios) {
            snprintf(a->descripcion, sizeof a->descripcion,
                     "agregar '%s' antes de %s en la linea %zu", nuevo, nombre_donde, donde->linea);
            return preparar(a, antes->linea_fin, antes->columna_fin + 1, 0, nuevo);
        }
    }

    char insertar[64];
    snprintf(insertar, sizeof insertar, "%s%s", va_pegado(c->terminales[0]) ? "" : " ", nuevo);
    if (otra_linea) {
        snprintf(a->descripcion, sizeof a->descripcion, "agregar '%s' al final de la linea %zu",
                 nuevo, antes->linea_fin);
    } else {
        snprintf(a->descripcion, sizeof a->descripcion, "agregar '%s' antes de %s en la linea %zu",
                 nuevo, nombre_donde, donde->linea);
    }
    return preparar(a, antes->linea_fin, antes->columna_fin, 0, insertar);
}

/*
 * El arreglo de un error del parser.
 *
 * Se analiza el archivo COMPLETO con recuperación (ll1_analizar_con_recuperacion):
 * el LL(1) corrige cada error y sigue, así que cada error del archivo tiene
 * su corrección, no solo el primero. Aquí se busca el error que está en el
 * token del diagnóstico y se convierte su corrección en un arreglo del texto.
 *
 * La corrección es la que PREDICE la tabla (por ejemplo, el ';' que falta),
 * o si la tabla no obliga a un token único, la mejor corrección local
 * validada con el LL(1). Ver ll1_recuperacion.c.
 *
 * Devuelve 1 si hay arreglo, 0 si no, -1 sin memoria.
 */
static int arreglar_con_ll1(const char *texto, size_t largo, const ListaTokens *tokens,
                            const Diagnostico *d, IdeArreglo *a)
{
    long encontrado = buscar_token(tokens, d->linea, d->columna);
    if (encontrado < 0) return 0;
    size_t k = (size_t)encontrado, n = tokens->cantidad;

    /* El LL(1) recibe los tipos de token y, para reconocer palabras mal
     * escritas, lo escrito en cada nombre. */
    TokenType *tipos = malloc(n * sizeof *tipos);
    const char **lexemas = malloc(n * sizeof *lexemas);
    if (tipos == NULL || lexemas == NULL) {
        free(tipos);
        free(lexemas);
        return -1;
    }
    for (size_t i = 0; i < n; ++i) {
        tipos[i] = tokens->elementos[i].tipo;
        lexemas[i] = tokens->elementos[i].lexema;
    }

    Ll1Errores errores;
    int estado = ll1_analizar_con_recuperacion(tipos, lexemas, n, &errores);
    free(tipos);
    free(lexemas);
    if (estado != 0) return -1;

    /* En un mismo token puede haber dos errores (una palabra mal escrita y,
     * ahí mismo, un ';' que faltaba antes). El del diagnóstico se reconoce
     * por su mensaje: el de la palabra mal escrita lo dice. */
    int pide_palabra = strstr(d->mensaje, "mal escrita") != NULL;
    estado = 0;
    for (size_t i = 0; i < errores.cantidad; ++i) {
        const Ll1Error *e = &errores.elementos[i];
        if (e->posicion != k || e->palabra_mal_escrita != pide_palabra) continue;
        if (e->tiene_correccion) {
            estado = armar_arreglo(texto, largo, tokens, k, e->anterior, &e->correccion, a);
        }
        break;
    }
    ll1_liberar_errores(&errores);
    return estado;
}
