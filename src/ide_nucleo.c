#include "ide_nucleo.h"

#include "lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================== */
/* 1. Sugerencias de código                                            */
/* ================================================================== */

/*
 * Palabras reservadas del lenguaje BAL, con una ayuda corta para cada una.
 * La lista sale de la tabla "keywords" de src/lexer.l y los significados
 * de docs/lenguaje_actual.md.
 * Está en orden alfabético para que las sugerencias salgan ordenadas.
 */
static const struct {
    const char *palabra;
    const char *descripcion;
} PALABRAS_CLAVE[] = {
    {"add", "agrega un elemento a una lista"},
    {"aka", "da un alias a un modulo importado"},
    {"alif", "condicional alternativo (sino si)"},
    {"also", "rama final de un condicional (sino)"},
    {"bring", "importa otro archivo .bal"},
    {"create_funk", "declara una funcion"},
    {"cycle", "ciclo con contador (para)"},
    {"declare_boolean", "tipo booleano"},
    {"declare_char", "tipo caracter"},
    {"declare_const", "declara una constante"},
    {"declare_false", "valor falso"},
    {"declare_infinite_void", "tipo vacio (la funcion no devuelve nada)"},
    {"declare_int", "tipo entero"},
    {"declare_list", "tipo lista"},
    {"declare_text", "tipo texto"},
    {"declare_true", "valor verdadero"},
    {"descartes", "operador potencia"},
    {"endgame", "cierra el ciclo cycle: } endgame;"},
    {"euclides", "operador division (/)"},
    {"euler", "operador residuo (%)"},
    {"gauss", "operador suma (+)"},
    {"give", "devuelve un valor (return)"},
    {"let", "valor inicial del ciclo cycle"},
    {"main", "funcion principal"},
    {"neumann", "operador resta (-)"},
    {"pitagoras", "operador multiplicacion (*)"},
    {"remove", "quita un elemento de una lista"},
    {"seek", "intenta ejecutar un bloque (try)"},
    {"seize", "atrapa el error de seek (catch)"},
    /* "show" es palabra reservada, pero el lenguaje todavía no la admite,
     * así que no se sugiere (ver docs/lenguaje_actual.md). */
    {"size", "cantidad de elementos de una lista"},
    {"step", "incremento del ciclo cycle"},
    {"stop", "cierra el ciclo whale: } stop;"},
    {"until", "limite del ciclo cycle"},
    {"whale", "ciclo mientras (while)"},
    {"whether", "condicional (si)"},
};

#define CANTIDAD_PALABRAS (sizeof PALABRAS_CLAVE / sizeof PALABRAS_CLAVE[0])

/* Devuelve 1 si "palabra" empieza con "prefijo". */
static int empieza_con(const char *palabra, const char *prefijo)
{
    return strncmp(palabra, prefijo, strlen(prefijo)) == 0;
}

/* Devuelve 1 si la lista ya tiene esa palabra (para no repetirla). */
static int ya_esta(const IdeSugerencias *lista, const char *texto)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        if (strcmp(lista->elementos[i].texto, texto) == 0) return 1;
    }
    return 0;
}

/*
 * Agrega una sugerencia al final de la lista (guarda una copia del texto).
 * Devuelve 0 si funcionó, o -1 si no hubo memoria.
 */
static int agregar_sugerencia(IdeSugerencias *lista, const char *texto, size_t largo,
                              IdeTipoSugerencia tipo, const char *descripcion)
{
    /* Si la lista está llena, pedir el doble de espacio. */
    if (lista->cantidad == lista->capacidad) {
        size_t nueva_capacidad = (lista->capacidad == 0) ? 16 : lista->capacidad * 2;
        IdeSugerencia *nuevo = realloc(lista->elementos, nueva_capacidad * sizeof *nuevo);
        if (nuevo == NULL) return -1;
        lista->elementos = nuevo;
        lista->capacidad = nueva_capacidad;
    }

    char *copia = malloc(largo + 1);
    if (copia == NULL) return -1;
    memcpy(copia, texto, largo);
    copia[largo] = '\0';

    IdeSugerencia s;
    s.texto = copia;
    s.tipo = tipo;
    s.descripcion = descripcion;
    lista->elementos[lista->cantidad] = s;
    lista->cantidad++;
    return 0;
}

/* Compara dos sugerencias por su texto. La usa qsort para ordenarlas. */
static int comparar_sugerencias(const void *a, const void *b)
{
    const IdeSugerencia *x = a;
    const IdeSugerencia *y = b;
    return strcmp(x->texto, y->texto);
}

int ide_sugerencias(const char *texto, size_t largo, const char *prefijo,
                    IdeSugerencias *lista)
{
    size_t inicio = lista->cantidad;  /* Donde empiezan los identificadores nuevos. */

    /*
     * Paso 1: recorrer el archivo con el analizador léxico del compilador
     * y quedarnos con los identificadores que empiezan con el prefijo.
     *
     * Usamos el lexer (y no buscamos palabras a mano) para que no salgan
     * palabras que están dentro de comentarios o de cadenas de texto.
     */
    Lexer lexer;
    lexer_init(&lexer, texto, largo, "<editor>");
    for (;;) {
        Token t = lexer_next(&lexer);
        TokenType tipo = t.type;
        int sin_memoria = (t.error == TOKEN_OUT_OF_MEMORY);

        int sirve = (tipo == TOKEN_IDENTIFIER && t.lexeme != NULL &&
                     empieza_con(t.lexeme, prefijo) &&
                     strcmp(t.lexeme, prefijo) != 0 &&  /* No sugerir lo mismo que ya escribió. */
                     !ya_esta(lista, t.lexeme));
        int fallo = sirve &&
                    agregar_sugerencia(lista, t.lexeme, t.length, IDE_IDENTIFICADOR,
                                       "nombre usado en este archivo") != 0;
        token_dispose(&t);

        if (sin_memoria || fallo) return -1;
        if (tipo == TOKEN_EOF) break;
    }

    /* Paso 2: ordenar alfabéticamente los identificadores encontrados.
     * (Si no se encontró ninguno, no hay nada que ordenar.) */
    if (lista->cantidad > inicio) {
        qsort(lista->elementos + inicio, lista->cantidad - inicio,
              sizeof lista->elementos[0], comparar_sugerencias);
    }

    /* Paso 3: agregar las palabras clave (ya están en orden alfabético). */
    for (size_t i = 0; i < CANTIDAD_PALABRAS; ++i) {
        const char *palabra = PALABRAS_CLAVE[i].palabra;
        if (empieza_con(palabra, prefijo) && strcmp(palabra, prefijo) != 0) {
            if (agregar_sugerencia(lista, palabra, strlen(palabra), IDE_PALABRA_CLAVE,
                                   PALABRAS_CLAVE[i].descripcion) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

void ide_liberar_sugerencias(IdeSugerencias *lista)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        free(lista->elementos[i].texto);
    }
    free(lista->elementos);

    lista->elementos = NULL;
    lista->cantidad = 0;
    lista->capacidad = 0;
}

/* ================================================================== */
/* 2. Autocierre                                                       */
/* ================================================================== */

char ide_cierre_de(char abre)
{
    switch (abre) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        case '"': return '"';  /* Cadena de texto: "hola" */
        case '$': return '$';  /* Carácter: $a$ */
        default: return '\0';  /* No abre nada. */
    }
}

/* ================================================================== */
/* 3. Revisión de delimitadores                                        */
/* ================================================================== */

/* Un delimitador que se abrió y todavía espera su cierre. */
typedef struct {
    char simbolo;    /* '(', '[' o '{' */
    size_t linea;    /* Dónde se abrió, para poder avisar si nunca se cierra. */
    size_t columna;
} Apertura;

/*
 * Pila de aperturas pendientes.
 *
 * ¿Por qué una pila? Porque lo último que se abre es lo primero que se
 * debe cerrar. En "{ ( ) }" el '(' se abre después del '{', así que se
 * cierra antes. Es el lenguaje de "paréntesis balanceados" S -> (S)S | ε
 * visto en clase, que se reconoce con un autómata de pila.
 */
typedef struct {
    Apertura *elementos;
    size_t cantidad;
    size_t capacidad;
} PilaAperturas;

/* Pone una apertura encima de la pila. Devuelve 0 si funcionó, -1 sin memoria. */
static int apilar(PilaAperturas *pila, Apertura a)
{
    if (pila->cantidad == pila->capacidad) {
        size_t nueva_capacidad = (pila->capacidad == 0) ? 16 : pila->capacidad * 2;
        Apertura *nuevo = realloc(pila->elementos, nueva_capacidad * sizeof *nuevo);
        if (nuevo == NULL) return -1;
        pila->elementos = nuevo;
        pila->capacidad = nueva_capacidad;
    }
    pila->elementos[pila->cantidad] = a;
    pila->cantidad++;
    return 0;
}

/* Si el token abre un delimitador, devuelve su símbolo ('(', '[', '{'); si no, '\0'. */
static char simbolo_apertura(TokenType tipo)
{
    switch (tipo) {
        case TOKEN_LPAREN: return '(';
        case TOKEN_LBRACKET: return '[';
        case TOKEN_LBRACE: return '{';
        default: return '\0';
    }
}

/* Si el token cierra un delimitador, devuelve su símbolo (')', ']', '}'); si no, '\0'. */
static char simbolo_cierre(TokenType tipo)
{
    switch (tipo) {
        case TOKEN_RPAREN: return ')';
        case TOKEN_RBRACKET: return ']';
        case TOKEN_RBRACE: return '}';
        default: return '\0';
    }
}

/* Devuelve el carácter que abre a un cierre: ')' -> '(', ']' -> '[', '}' -> '{'. */
static char apertura_de(char cierre)
{
    switch (cierre) {
        case ')': return '(';
        case ']': return '[';
        default: return '{';
    }
}

int ide_revisar_delimitadores(const char *texto, size_t largo,
                              const char *archivo, DiagLista *lista)
{
    PilaAperturas pila = {0};
    char mensaje[200];  /* Aquí se arma el texto de cada error. */
    int resultado = 0;

    Lexer lexer;
    lexer_init(&lexer, texto, largo, archivo);

    for (;;) {
        Token t = lexer_next(&lexer);
        TokenType tipo = t.type;
        size_t linea = t.line;
        size_t columna = t.column;
        int sin_memoria = (t.error == TOKEN_OUT_OF_MEMORY);
        token_dispose(&t);

        if (sin_memoria) { resultado = -1; break; }
        if (tipo == TOKEN_EOF) break;

        char abre = simbolo_apertura(tipo);
        char cierra = simbolo_cierre(tipo);

        if (abre != '\0') {
            /* Caso 1: se abre algo. Lo guardamos en la pila hasta ver su cierre. */
            Apertura a = {abre, linea, columna};
            if (apilar(&pila, a) != 0) { resultado = -1; break; }

        } else if (cierra != '\0') {
            if (pila.cantidad == 0) {
                /* Caso 2: se cierra algo, pero no había nada abierto. */
                snprintf(mensaje, sizeof mensaje,
                         "el '%c' sobra: no hay ningun '%c' abierto antes",
                         cierra, apertura_de(cierra));
                if (diag_agregar(lista, DIAG_SINTACTICO, archivo, linea, columna,
                                 mensaje) != 0) { resultado = -1; break; }
                continue;
            }

            /* Sacamos de la pila la última apertura: es la que debería cerrarse. */
            pila.cantidad--;
            Apertura ultima = pila.elementos[pila.cantidad];

            if (ide_cierre_de(ultima.simbolo) != cierra) {
                /* Caso 3: se cierra con el símbolo equivocado, por ejemplo "( }". */
                snprintf(mensaje, sizeof mensaje,
                         "se esperaba '%c' para cerrar el '%c' de la linea %zu, "
                         "columna %zu, pero se encontro '%c'",
                         ide_cierre_de(ultima.simbolo), ultima.simbolo,
                         ultima.linea, ultima.columna, cierra);
                if (diag_agregar(lista, DIAG_SINTACTICO, archivo, linea, columna,
                                 mensaje) != 0) { resultado = -1; break; }
            }
            /* Si coinciden, todo está bien y no hay nada que reportar. */
        }
    }

    /* Caso 4: al terminar el archivo, lo que quede en la pila nunca se cerró. */
    for (size_t i = 0; resultado == 0 && i < pila.cantidad; ++i) {
        Apertura a = pila.elementos[i];
        snprintf(mensaje, sizeof mensaje, "falta cerrar este '%c' con '%c'",
                 a.simbolo, ide_cierre_de(a.simbolo));
        if (diag_agregar(lista, DIAG_SINTACTICO, archivo, a.linea, a.columna,
                         mensaje) != 0) resultado = -1;
    }

    free(pila.elementos);
    return resultado;
}
