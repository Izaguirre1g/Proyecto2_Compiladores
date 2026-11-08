/*
 * Pruebas del analizador LL(1) (src/LL1/).
 *
 * 1. La tabla no tiene conflictos: la gramática es LL(1).
 * 2. Algunos conjuntos PRIMERO y SIGUIENTE calculados a mano.
 * 3. Equivalencia con Bison: para cada archivo .bal recibido en la línea
 *    de comandos, el LL(1) y el parser de Bison deben dar el mismo
 *    resultado. Y no solo con el archivo original: también con muchas
 *    variantes rotas (borrando un token, o repitiéndolo). En cada variante
 *    se compara:
 *      - si el programa es válido o no;
 *      - la línea y columna del primer error;
 *      - cuando Bison dice "expecting A or B ...", que el LL(1) espere
 *        exactamente esos mismos tokens.
 *
 * Uso: ./tests/test_ll1 archivo1.bal archivo2.bal ...
 */
#include "ll1.h"
#include "lexer.h"
#include "sintactico.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fallos = 0;

#define VERIFICAR(condicion, ...)                                   \
    do {                                                            \
        if (!(condicion)) {                                         \
            fprintf(stderr, "FALLO %s:%d: ", __FILE__, __LINE__);   \
            fprintf(stderr, __VA_ARGS__);                           \
            fprintf(stderr, "\n");                                  \
            fallos++;                                               \
        }                                                           \
    } while (0)

/* ------------------------------------------------------------------ */
/* 1 y 2: la tabla                                                      */
/* ------------------------------------------------------------------ */

static void probar_tabla(void)
{
    const Ll1Tabla *t = ll1_tabla();
    VERIFICAR(t->conflictos == 0, "la tabla tiene %d conflictos", t->conflictos);

    /* PRIMERO(expresion): todo lo que puede iniciar un valor. */
    const Ll1Conjunto *pe = &t->primero[LL1_INDICE(NT_EXPRESION)];
    VERIFICAR(pe->tiene[TOKEN_IDENTIFIER] && pe->tiene[TOKEN_INTEGER] &&
              pe->tiene[TOKEN_LPAREN] && pe->tiene[TOKEN_NOT] && pe->tiene[TOKEN_NEUMANN],
              "PRIMERO(expresion) incompleto");
    VERIFICAR(!pe->epsilon, "expresion no puede ser vacia");

    /* resto_suma puede ser ε; sentencias también. */
    VERIFICAR(t->primero[LL1_INDICE(NT_RESTO_SUMA)].epsilon, "resto_suma deberia tener ε");
    VERIFICAR(t->primero[LL1_INDICE(NT_SENTENCIAS)].epsilon, "sentencias deberia tener ε");

    /* SIGUIENTE(programa) = { $ } */
    const Ll1Conjunto *sp = &t->siguiente[LL1_INDICE(NT_PROGRAMA)];
    int cuantos = 0;
    for (int k = 0; k < LL1_TERMINALES; ++k) cuantos += sp->tiene[k];
    VERIFICAR(cuantos == 1 && sp->tiene[LL1_FIN], "SIGUIENTE(programa) deberia ser { $ }");

    /* Después de una alternativa no puede venir otro alif (no hay else colgante). */
    VERIFICAR(!t->siguiente[LL1_INDICE(NT_ALTERNATIVA)].tiene[TOKEN_ALIF],
              "alif no deberia estar en SIGUIENTE(alternativa)");

    /* Las comparaciones no se encadenan: tras resto_comparacion no viene '<'. */
    VERIFICAR(!t->siguiente[LL1_INDICE(NT_RESTO_COMPARACION)].tiene[TOKEN_LESS],
              "'<' no deberia seguir a una comparacion");
}

/* ------------------------------------------------------------------ */
/* 3: equivalencia con Bison                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    TokenType tipo;
    size_t linea, columna;
    size_t inicio, fin;  /* Bytes del token dentro del texto. */
} TokenPrueba;

/* Tokens del texto, con su posición en bytes. Devuelve la cantidad (incluye EOF). */
static size_t tokenizar(const char *texto, size_t largo, TokenPrueba **salida)
{
    Lexer lexer;
    lexer_init(&lexer, texto, largo, "prueba");
    TokenPrueba *lista = NULL;
    size_t cantidad = 0, capacidad = 0;
    for (;;) {
        Token t = lexer_next(&lexer);
        if (cantidad == capacidad) {
            capacidad = capacidad ? capacidad * 2 : 256;
            lista = realloc(lista, capacidad * sizeof *lista);
            if (lista == NULL) {
                fprintf(stderr, "sin memoria\n");
                exit(2);
            }
        }
        /* Después de lexer_next, offset quedó al final del token. */
        lista[cantidad++] = (TokenPrueba){t.type, t.line, t.column,
                                          lexer.offset - t.length, lexer.offset};
        TokenType tipo = t.type;
        token_dispose(&t);
        if (tipo == TOKEN_EOF) break;
    }
    *salida = lista;
    return cantidad;
}

/* Resultado de un parser: válido, o primer error con posición y esperados. */
typedef struct {
    int valido;
    size_t linea, columna;
    int tiene_esperados;      /* Bison solo los da si son pocos. */
    Ll1Conjunto esperados;
} Veredicto;

/* Busca un tipo de token por su nombre de Bison ("SEMICOLON", "end of file"). */
static int token_por_nombre(const char *nombre, size_t largo)
{
    if (largo == 11 && strncmp(nombre, "end of file", 11) == 0) return TOKEN_EOF;
    for (int k = 0; k < LL1_TERMINALES; ++k) {
        const char *n = token_type_name((TokenType)k);
        if (strlen(n) == largo && strncmp(n, nombre, largo) == 0) return k;
    }
    return -1;
}

/* Corre el parser de Bison y lee su primer mensaje de error. */
static Veredicto con_bison(const char *texto, size_t largo)
{
    Veredicto v = {0};
    FILE *diag = tmpfile();
    if (diag == NULL) exit(2);
    int r = analizar_sintaxis(texto, largo, "prueba", diag);
    v.valido = (r == 0);
    if (!v.valido) {
        char linea[1024] = "";
        rewind(diag);
        if (fgets(linea, sizeof linea, diag) != NULL) {
            sscanf(linea, "prueba:%zu:%zu:", &v.linea, &v.columna);
            /* "... unexpected X, expecting A or B or C" */
            const char *exp = strstr(linea, "expecting ");
            if (exp != NULL) {
                v.tiene_esperados = 1;
                exp += strlen("expecting ");
                for (;;) {
                    size_t n = strcspn(exp, "\n");
                    const char *o = strstr(exp, " or ");
                    if (o != NULL && (size_t)(o - exp) < n) n = (size_t)(o - exp);
                    int k = token_por_nombre(exp, n);
                    if (k >= 0) v.esperados.tiene[k] = 1;
                    if (o == NULL || exp[n] == '\n' || exp[n] == '\0') break;
                    exp = o + 4;
                }
            }
        }
    }
    fclose(diag);
    return v;
}

/* Corre el LL(1) sobre el mismo texto. */
static Veredicto con_ll1(const char *texto, size_t largo)
{
    Veredicto v = {0};
    TokenPrueba *tokens;
    size_t n = tokenizar(texto, largo, &tokens);
    TokenType *tipos = malloc(n * sizeof *tipos);
    if (tipos == NULL) exit(2);
    for (size_t i = 0; i < n; ++i) tipos[i] = tokens[i].tipo;

    Ll1Resultado r;
    if (ll1_analizar(tipos, n, &r) != 0) exit(2);
    v.valido = (r.estado == LL1_ACEPTADA);
    if (!v.valido) {
        v.linea = tokens[r.posicion].linea;
        v.columna = tokens[r.posicion].columna;
        v.esperados = r.esperados;
        v.tiene_esperados = 1;
    }
    free(tipos);
    free(tokens);
    return v;
}

static long comparaciones = 0;

/* Compara los dos parsers sobre un texto. "que" describe la variante. */
static void comparar(const char *texto, size_t largo, const char *archivo, const char *que)
{
    Veredicto b = con_bison(texto, largo);
    Veredicto l = con_ll1(texto, largo);
    comparaciones++;

    if (b.valido != l.valido) {
        VERIFICAR(0, "%s (%s): Bison dice %s y LL(1) dice %s", archivo, que,
                  b.valido ? "valido" : "error", l.valido ? "valido" : "error");
        return;
    }
    if (b.valido) return;
    VERIFICAR(b.linea == l.linea && b.columna == l.columna,
              "%s (%s): Bison falla en %zu:%zu y LL(1) en %zu:%zu", archivo, que,
              b.linea, b.columna, l.linea, l.columna);

    if (b.tiene_esperados) {
        for (int k = 0; k < LL1_TERMINALES; ++k) {
            if (b.esperados.tiene[k] != l.esperados.tiene[k]) {
                VERIFICAR(0, "%s (%s) en %zu:%zu: %s %s esperado por Bison pero %s por LL(1)",
                          archivo, que, b.linea, b.columna, token_type_name((TokenType)k),
                          b.esperados.tiene[k] ? "es" : "no es",
                          l.esperados.tiene[k] ? "si" : "no");
                break;
            }
        }
    }
}

/* Máximo de variantes por archivo (para que la prueba tarde poco). */
#define MAX_VARIANTES 200

static void probar_archivo(const char *ruta)
{
    FILE *f = fopen(ruta, "rb");
    if (f == NULL) {
        VERIFICAR(0, "no se pudo abrir %s", ruta);
        return;
    }
    char *texto = malloc(1 << 20);
    if (texto == NULL) exit(2);
    size_t largo = fread(texto, 1, (1 << 20) - 1, f);
    fclose(f);

    comparar(texto, largo, ruta, "original");

    TokenPrueba *tokens;
    size_t n = tokenizar(texto, largo, &tokens);
    char *variante = malloc(2 * largo + 16);
    if (variante == NULL) exit(2);

    /* Se reparten las variantes a lo largo del archivo. */
    size_t paso = (n / MAX_VARIANTES) + 1;
    for (size_t k = 0; k + 1 < n; k += paso) {
        const TokenPrueba *t = &tokens[k];
        /* Textos y caracteres pueden tener escapes: su largo en bytes no
         * es el del lexema, así que no se usan para cortar. */
        if (t->tipo == TOKEN_STRING || t->tipo == TOKEN_CHARACTER || t->tipo == TOKEN_ERROR) {
            continue;
        }
        if (t->fin > largo || t->inicio > t->fin) continue;
        char que[64];

        /* Variante 1: borrar el token k. */
        size_t m = 0;
        memcpy(variante, texto, t->inicio);
        m = t->inicio;
        memcpy(variante + m, texto + t->fin, largo - t->fin);
        m += largo - t->fin;
        snprintf(que, sizeof que, "sin el token %zu", k);
        comparar(variante, m, ruta, que);

        /* Variante 2: repetir el token k ("x x", ";;", "( (" ...). */
        memcpy(variante, texto, t->fin);
        m = t->fin;
        variante[m++] = ' ';
        memcpy(variante + m, texto + t->inicio, t->fin - t->inicio);
        m += t->fin - t->inicio;
        memcpy(variante + m, texto + t->fin, largo - t->fin);
        m += largo - t->fin;
        snprintf(que, sizeof que, "token %zu repetido", k);
        comparar(variante, m, ruta, que);
    }
    free(variante);
    free(tokens);
    free(texto);
}

/* Un caso escrito a mano: esperados en un punto concreto. */
static void probar_esperados(void)
{
    /* Falta el ';' después del 5: en esa posición podía venir ';' u un operador. */
    const char *codigo =
        "create_funk #declare_infinite_void# main() {\n"
        "    x * declare_int : 5\n"
        "}\n";
    Veredicto l = con_ll1(codigo, strlen(codigo));
    VERIFICAR(!l.valido && l.linea == 3 && l.columna == 1, "el error debia estar en 3:1");
    VERIFICAR(l.esperados.tiene[TOKEN_SEMICOLON], "';' deberia esperarse");
    VERIFICAR(l.esperados.tiene[TOKEN_GAUSS], "gauss deberia esperarse");
    VERIFICAR(l.esperados.tiene[TOKEN_COMMA], "',' deberia esperarse (otra declaracion)");
    VERIFICAR(!l.esperados.tiene[TOKEN_RPAREN], "')' no deberia esperarse aqui");

    /* Un comienzo de programa sin errores: después de "whether" va '('. */
    TokenType inicio[] = {TOKEN_CREATE_FUNK, TOKEN_HASH, TOKEN_DECLARE_INFINITE_VOID,
                          TOKEN_HASH, TOKEN_MAIN, TOKEN_LPAREN, TOKEN_RPAREN,
                          TOKEN_LBRACE, TOKEN_WHETHER};
    Ll1Resultado r;
    ll1_analizar(inicio, sizeof inicio / sizeof inicio[0], &r);
    int cuantos = 0;
    for (int k = 0; k < LL1_TERMINALES; ++k) cuantos += r.esperados.tiene[k];
    VERIFICAR(r.estado == LL1_INCOMPLETA && cuantos == 1 && r.esperados.tiene[TOKEN_LPAREN],
              "despues de whether solo puede venir '('");
}

/* Inserción que predice la tabla: los tokens de un texto, con su predicción. */
static int predecir(const char *codigo, Ll1Insercion *insercion)
{
    TokenPrueba *tokens;
    size_t n = tokenizar(codigo, strlen(codigo), &tokens);
    TokenType *tipos = malloc(n * sizeof *tipos);
    if (tipos == NULL) exit(2);
    for (size_t i = 0; i < n; ++i) tipos[i] = tokens[i].tipo;
    int r = ll1_insercion_predicha(tipos, n, insercion);
    free(tipos);
    free(tokens);
    return r;
}

static void probar_insercion_predicha(void)
{
    Ll1Insercion ins;

    /* Falta ';': la tabla lo predice (los resto_... se van con ε). */
    VERIFICAR(predecir("create_funk #declare_infinite_void# main() {\n"
                       "    x * declare_int : 5\n    y * declare_int : 3;\n}\n", &ins) == 1 &&
              ins.cantidad == 1 && ins.terminales[0] == TOKEN_SEMICOLON,
              "deberia predecir ';'");

    /* Después del bloque de whale la tabla obliga a 'stop' y luego a ';'. */
    VERIFICAR(predecir("create_funk #declare_infinite_void# main() {\n"
                       "    whale (1 < 2) { }\n}\n", &ins) == 1 &&
              ins.cantidad == 2 && ins.terminales[0] == TOKEN_STOP &&
              ins.terminales[1] == TOKEN_SEMICOLON,
              "deberia predecir 'stop' ';'");

    /* Falta una expresión: 'expresion' tiene varias formas posibles, así
     * que la tabla no obliga a un token único. */
    VERIFICAR(predecir("create_funk #declare_infinite_void# main() {\n"
                       "    x * declare_int : ;\n}\n", &ins) == 0,
              "no deberia predecir nada cuando falta un valor");

    /* Un programa válido no tiene nada que corregir. */
    VERIFICAR(predecir("create_funk #declare_infinite_void# main() {\n}\n", &ins) == 0,
              "un programa valido no tiene insercion");
}

/* Recuperación: los errores de un texto (sin lexemas). */
static Ll1Errores recuperar_texto(const char *codigo, TokenPrueba **tokens_salida)
{
    TokenPrueba *tokens;
    size_t n = tokenizar(codigo, strlen(codigo), &tokens);
    TokenType *tipos = malloc(n * sizeof *tipos);
    if (tipos == NULL) exit(2);
    for (size_t i = 0; i < n; ++i) tipos[i] = tokens[i].tipo;
    Ll1Errores e;
    if (ll1_analizar_con_recuperacion(tipos, NULL, n, &e) != 0) exit(2);
    free(tipos);
    *tokens_salida = tokens;
    return e;
}

static void probar_recuperacion(void)
{
    TokenPrueba *tokens;

    /* Tres errores independientes: se encuentran los tres, cada uno con
     * su corrección, y con ellas el programa queda válido. */
    Ll1Errores e = recuperar_texto(
        "create_funk #declare_infinite_void# main() {\n"
        "    x * declare_int : 5\n"          /* falta ';' */
        "    y * declare_int : (1 gauss 2;\n"  /* falta ')' */
        "    whale (x < y) { }\n"            /* falta 'stop;' */
        "}\n", &tokens);
    VERIFICAR(e.cantidad == 3 && e.completo, "deberian ser 3 errores corregibles (%zu)", e.cantidad);
    if (e.cantidad == 3) {
        VERIFICAR(tokens[e.elementos[0].posicion].linea == 3, "primer error en la linea 3");
        VERIFICAR(e.elementos[0].correccion.terminales[0] == TOKEN_SEMICOLON, "falta ';'");
        VERIFICAR(e.elementos[1].correccion.terminales[0] == TOKEN_RPAREN, "falta ')'");
        VERIFICAR(e.elementos[2].correccion.cantidad == 2 &&
                  e.elementos[2].correccion.terminales[0] == TOKEN_STOP, "falta 'stop;'");
    }
    ll1_liberar_errores(&e);
    free(tokens);

    /* Error en la cabecera (parámetro sin nombre): modo pánico. Se reporta
     * UN error, no uno por cada sentencia del cuerpo. */
    e = recuperar_texto(
        "create_funk #declare_int# f(* declare_int) {\n"
        "    whether (1 < 2) { give 1; }\n"
        "    give 0;\n"
        "}\n", &tokens);
    VERIFICAR(e.cantidad == 1 && !e.completo, "el panico deberia dar 1 error (%zu)", e.cantidad);
    ll1_liberar_errores(&e);
    free(tokens);
}

int main(int argc, char **argv)
{
    probar_recuperacion();
    probar_tabla();
    probar_esperados();
    probar_insercion_predicha();
    for (int i = 1; i < argc; ++i) probar_archivo(argv[i]);

    if (fallos > 0) {
        fprintf(stderr, "test_ll1: %d fallos\n", fallos);
        return 1;
    }
    printf("OK: tabla LL(1) sin conflictos; LL(1) y Bison coinciden en %ld programas "
           "(%d archivos y sus variantes con errores)\n", comparaciones, argc - 1);
    return 0;
}
