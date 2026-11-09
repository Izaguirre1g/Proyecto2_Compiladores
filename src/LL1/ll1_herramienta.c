/*
 * balc-ll1: herramienta para ver y probar el analizador LL(1).
 *
 * Uso:
 *   ./balc-ll1 --tabla               escribe PRIMERO, SIGUIENTE y la tabla M (Markdown)
 *   ./balc-ll1 archivo.bal           analiza el archivo con el LL(1)
 *   ./balc-ll1 --traza archivo.bal   igual, mostrando la pila paso a paso
 *
 * Códigos de salida: 0 = programa válido, 1 = error de sintaxis,
 * 2 = uso incorrecto, archivo inexistente o sin memoria.
 */
#include "ll1.h"
#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Lee un archivo completo. Devuelve memoria nueva o NULL. */
static char *leer_archivo(const char *ruta, size_t *largo)
{
    FILE *f = fopen(ruta, "rb");
    if (f == NULL) return NULL;
    char *texto = NULL;
    size_t capacidad = 0;
    *largo = 0;
    for (;;) {
        if (*largo + 4096 > capacidad) {
            capacidad = (capacidad == 0) ? 8192 : capacidad * 2;
            char *mas = realloc(texto, capacidad);
            if (mas == NULL) {
                free(texto);
                fclose(f);
                return NULL;
            }
            texto = mas;
        }
        size_t leidos = fread(texto + *largo, 1, 4096, f);
        *largo += leidos;
        if (leidos < 4096) break;
    }
    fclose(f);
    return texto;
}

/* Un token con la posición donde empieza (para los mensajes). */
typedef struct {
    TokenType tipo;
    size_t linea, columna;
} TokenConPosicion;

/*
 * Pasa el texto por el lexer del compilador y guarda los tokens hasta el
 * TOKEN_EOF incluido. Devuelve la cantidad, o 0 si no hubo memoria.
 */
static size_t leer_tokens(const char *texto, size_t largo, const char *nombre,
                          TokenConPosicion **salida)
{
    Lexer lexer;
    lexer_init(&lexer, texto, largo, nombre);
    TokenConPosicion *lista = NULL;
    size_t cantidad = 0, capacidad = 0;
    for (;;) {
        Token t = lexer_next(&lexer);
        if (cantidad == capacidad) {
            capacidad = (capacidad == 0) ? 256 : capacidad * 2;
            TokenConPosicion *mas = realloc(lista, capacidad * sizeof *mas);
            if (mas == NULL) {
                token_dispose(&t);
                free(lista);
                return 0;
            }
            lista = mas;
        }
        lista[cantidad++] = (TokenConPosicion){t.type, t.line, t.column};
        TokenType tipo = t.type;
        token_dispose(&t);
        if (tipo == TOKEN_EOF) break;
    }
    *salida = lista;
    return cantidad;
}

static int uso(void)
{
    fprintf(stderr, "Uso: balc-ll1 --tabla | [--traza] archivo.bal\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--tabla") == 0) {
        ll1_escribir_markdown(stdout);
        return ll1_tabla()->conflictos == 0 ? 0 : 1;
    }

    int con_traza = (argc == 3 && strcmp(argv[1], "--traza") == 0);
    if (argc != 2 && !con_traza) return uso();
    const char *ruta = argv[argc - 1];

    size_t largo;
    char *texto = leer_archivo(ruta, &largo);
    if (texto == NULL) {
        fprintf(stderr, "%s: no se pudo leer el archivo\n", ruta);
        return 2;
    }

    TokenConPosicion *tokens = NULL;
    size_t cantidad = leer_tokens(texto, largo, ruta, &tokens);
    free(texto);
    if (cantidad == 0) return 2;

    /* El analizador solo necesita los tipos de token. */
    TokenType *tipos = malloc(cantidad * sizeof *tipos);
    if (tipos == NULL) {
        free(tokens);
        return 2;
    }
    for (size_t i = 0; i < cantidad; ++i) tipos[i] = tokens[i].tipo;

    if (con_traza) ll1_trazar(tipos, cantidad, stdout);

    Ll1Resultado r;
    int codigo = 2;
    if (ll1_analizar(tipos, cantidad, &r) == 0) {
        if (r.estado == LL1_ACEPTADA) {
            printf("%s: sintaxis correcta (LL(1))\n", ruta);
            codigo = 0;
        } else {
            const TokenConPosicion *t = &tokens[r.posicion];
            printf("%s:%zu:%zu: error LL(1): no se esperaba %s; se esperaba:", ruta,
                   t->linea, t->columna, ll1_texto(t->tipo));
            for (int k = 0; k < LL1_TERMINALES; ++k) {
                if (r.esperados.tiene[k]) printf(" %s", ll1_texto(k));
            }
            printf("\n");
            codigo = 1;
        }
    }
    free(tipos);
    free(tokens);
    return codigo;
}
