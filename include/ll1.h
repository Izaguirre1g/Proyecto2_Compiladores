#ifndef LL1_H
#define LL1_H

/*
 * Análisis sintáctico predictivo LL(1) del lenguaje BAL.
 *
 * El compilador ya tiene un parser hecho con Bison, que es ASCENDENTE
 * (LALR(1)): construye el árbol desde las hojas hacia la raíz. Este módulo
 * es un analizador DESCENDENTE predictivo, como el de las diapositivas de
 * Análisis Sintáctico (secciones 4 a 6):
 *
 *   1. Una gramática de BAL reescrita para que sea LL(1): sin recursividad
 *      por la izquierda y factorizada por la izquierda (ll1_gramatica.c).
 *   2. Los conjuntos PRIMERO y SIGUIENTE, y con ellos la tabla de análisis
 *      predictivo M[A, a] (ll1_tabla.c).
 *   3. El algoritmo de análisis predictivo no recursivo: una pila, la
 *      entrada y la tabla (ll1_analizador.c).
 *
 * ¿Para qué sirve si ya está Bison? Bison sigue siendo el parser que
 * construye el AST. El LL(1) se usa en el IDE para PREDECIR: consultando
 * la tabla se sabe exactamente qué tokens pueden venir en cada punto del
 * programa. Con eso el IDE:
 *   - sugiere solo palabras que tienen sentido donde está el cursor, y
 *   - ante un error, propone UNA corrección: el token que la tabla predice
 *     que falta (en LL(1) la predicción es única, ver ll1_insercion_predicha).
 *
 * Los dos parsers reconocen el mismo lenguaje: la prueba "make test-ll1"
 * compara ambos sobre todos los programas de ejemplo y sobre cientos de
 * variantes con errores, y revisa que fallen en el mismo token.
 */

#include <stddef.h>
#include <stdio.h>

#include "token.h"

/* ------------------------------------------------------------------ */
/* Símbolos de la gramática                                            */
/* ------------------------------------------------------------------ */

/*
 * Un símbolo es un número entero:
 *   - Los TERMINALES son los mismos tipos de token del lexer (TokenType),
 *     del 0 (TOKEN_EOF) al TOKEN_HASH. Así no hace falta traducir nada.
 *   - El fin de la entrada, que en las diapositivas se escribe $, es
 *     TOKEN_EOF.
 *   - Los NO TERMINALES van después del último terminal.
 */
#define LL1_TERMINALES (TOKEN_HASH + 1)  /* Cantidad de terminales. */
#define LL1_FIN TOKEN_EOF                /* El símbolo $. */

typedef enum {
    /* Programa y elementos de nivel superior. */
    NT_PROGRAMA = LL1_TERMINALES,
    NT_MAS_ELEMENTOS,
    NT_ELEMENTO_SUPERIOR,
    NT_FUNCION,
    NT_TIPO_RETORNO,
    NT_NOMBRE_FUNCION,
    NT_PARAMETROS_OPCIONALES,
    NT_MAS_PARAMETROS,
    NT_PARAMETRO,
    NT_TIPO_PARAMETRO,
    NT_DIMENSIONES_PARAMETRO,
    NT_DIMENSION_PARAMETRO_RESTO,
    NT_IMPORTACION,
    NT_ALIAS_OPCIONAL,
    NT_CONSTANTE,
    NT_TIPO,
    NT_TIPO_VARIABLE,
    NT_DIMENSIONES,
    /* Bloques y sentencias. */
    NT_BLOQUE,
    NT_SENTENCIAS,
    NT_SENTENCIA,
    NT_RESTO_IDENTIFICADOR,
    NT_RESTO_REFERENCIA,
    NT_MAS_DECLARADORES,
    NT_INICIALIZADOR_OPCIONAL,
    NT_LLAMADA_ESPECIAL,
    NT_DECLARACION_LISTA,
    NT_DECLARADOR_LISTA,
    NT_MAS_LISTAS,
    NT_CONDICIONAL,
    NT_ALTERNATIVA,
    NT_CICLO,
    NT_RETORNO,
    NT_RESTO_RETORNO,
    NT_RECORRIDO,
    NT_PASO_OPCIONAL,
    NT_INTENTAR,
    NT_MAS_CAPTURAS,
    NT_CAPTURA,
    NT_ARGUMENTOS_OPCIONALES,
    NT_MAS_ARGUMENTOS,
    /* Expresiones: un nivel por cada precedencia, de menor a mayor. */
    NT_EXPRESION,
    NT_RESTO_OR,
    NT_EXCLUSIVA,
    NT_RESTO_XOR,
    NT_CONJUNCION,
    NT_RESTO_AND,
    NT_COMPARACION,
    NT_RESTO_COMPARACION,
    NT_OPERADOR_RELACIONAL,
    NT_SUMA,
    NT_RESTO_SUMA,
    NT_PRODUCTO,
    NT_RESTO_PRODUCTO,
    NT_UNARIO,
    NT_POTENCIA,
    NT_RESTO_POTENCIA,
    NT_PRIMARIO,
    NT_SUFIJOS_REFERENCIA,
    NT_LLAMADA_OPCIONAL,
    NT_ELEMENTOS_OPCIONALES,
    NT_MAS_ELEMENTOS_LISTA,

    LL1_TOTAL_SIMBOLOS  /* No es un símbolo: marca el final de la lista. */
} Ll1NoTerminal;

#define LL1_NO_TERMINALES (LL1_TOTAL_SIMBOLOS - LL1_TERMINALES)
#define LL1_ES_TERMINAL(simbolo) ((simbolo) < LL1_TERMINALES)

/* Posición de un no terminal en las tablas (0, 1, 2, ...). */
#define LL1_INDICE(no_terminal) ((no_terminal) - LL1_TERMINALES)

/* ------------------------------------------------------------------ */
/* Producciones                                                        */
/* ------------------------------------------------------------------ */

/* Marca el final del cuerpo de una producción. Un cuerpo vacío es ε. */
#define LL1_FIN_CUERPO (-1)

/* El cuerpo más largo (recorrido) tiene 10 símbolos; se deja espacio de sobra. */
#define LL1_MAX_CUERPO 12

/* Una producción  cabeza → cuerpo[0] cuerpo[1] ...  */
typedef struct {
    int cabeza;
    int cuerpo[LL1_MAX_CUERPO + 1];  /* Termina en LL1_FIN_CUERPO. */
} Ll1Produccion;

/* Todas las producciones de la gramática LL(1) de BAL. */
extern const Ll1Produccion ll1_producciones[];
extern const int ll1_cantidad_producciones;

/* Cuántos símbolos tiene el cuerpo de una producción (0 si es ε). */
int ll1_largo_cuerpo(const Ll1Produccion *produccion);

/* Nombre de un símbolo para reportes: "expresion", "SEMICOLON", "$". */
const char *ll1_nombre(int simbolo);

/*
 * Cómo se escribe un terminal en el código BAL, para mostrárselo a la
 * persona: ";" , "whether", ")". Para los tokens que no tienen una
 * escritura fija devuelve una descripción: "un nombre", "un número".
 */
const char *ll1_texto(int terminal);

/*
 * TRUE (1) si el terminal se escribe siempre igual (";", "whether"...).
 * Esos son los únicos que el IDE puede insertar solo; para un nombre o un
 * número tendría que inventarse el valor.
 */
int ll1_texto_fijo(int terminal);

/* ------------------------------------------------------------------ */
/* Conjuntos y tabla de análisis                                       */
/* ------------------------------------------------------------------ */

/* Un conjunto de terminales, más la marca ε (para PRIMERO). */
typedef struct {
    unsigned char tiene[LL1_TERMINALES];  /* tiene[t] = 1 si el terminal t está. */
    unsigned char epsilon;                /* 1 si ε está en el conjunto. */
} Ll1Conjunto;

/* Un conflicto: dos producciones en la misma celda M[A, a]. */
typedef struct {
    int no_terminal, terminal;
    int produccion1, produccion2;
} Ll1Conflicto;

#define LL1_MAX_CONFLICTOS 32

typedef struct {
    Ll1Conjunto primero[LL1_NO_TERMINALES];    /* PRIMERO(A) para cada no terminal. */
    Ll1Conjunto siguiente[LL1_NO_TERMINALES];  /* SIGUIENTE(A) para cada no terminal. */

    /* M[A][a]: número de la producción que se usa, o -1 si la celda está
     * vacía (error). */
    short m[LL1_NO_TERMINALES][LL1_TERMINALES];

    int conflictos;  /* Celdas con más de una producción. 0 = la gramática es LL(1). */
    Ll1Conflicto lista_conflictos[LL1_MAX_CONFLICTOS];
} Ll1Tabla;

/*
 * Devuelve la tabla de análisis. Se calcula la primera vez que se pide
 * (PRIMERO, SIGUIENTE y M) y después se reutiliza.
 */
const Ll1Tabla *ll1_tabla(void);

/*
 * PRIMERO de una cadena de símbolos X1 X2 ... Xn (diapositiva 25).
 * Si todos pueden derivar ε (o n = 0), ε queda en el resultado.
 */
void ll1_primero_de_cadena(const Ll1Tabla *tabla, const int *simbolos, int cantidad,
                           Ll1Conjunto *resultado);

/* Escribe PRIMERO, SIGUIENTE y la tabla M en Markdown (para docs/). */
void ll1_escribir_markdown(FILE *salida);

/* ------------------------------------------------------------------ */
/* Analizador predictivo                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    LL1_ACEPTADA,    /* La entrada completa es un programa válido. */
    LL1_ERROR,       /* Hay un token que no puede ir donde está. */
    LL1_INCOMPLETA   /* Se acabaron los tokens dados sin errores, pero falta el resto. */
} Ll1Estado;

typedef struct {
    Ll1Estado estado;

    /* Con LL1_ERROR: índice del token donde falló. Con LL1_INCOMPLETA:
     * la cantidad de tokens (todos se consumieron). Con LL1_ACEPTADA: el
     * índice del $. */
    size_t posicion;

    /*
     * Con LL1_ERROR o LL1_INCOMPLETA: los terminales que SÍ podían ir en
     * esa posición. Se calculan consultando la tabla M con la pila tal
     * como estaba antes de leer ese token.
     */
    Ll1Conjunto esperados;
} Ll1Resultado;

/*
 * Analiza una lista de tokens con el algoritmo predictivo no recursivo.
 *
 *   tokens, cantidad: los tipos de token en orden. Si el último es
 *                     TOKEN_EOF, se analiza un programa completo; si no,
 *                     se analiza solo ese comienzo (para predecir qué sigue).
 *   resultado:        aquí queda el estado, la posición y los esperados.
 *
 * Devuelve 0 si se pudo analizar, o -1 si no hubo memoria.
 */
int ll1_analizar(const TokenType *tokens, size_t cantidad, Ll1Resultado *resultado);

/*
 * Igual que ll1_analizar, pero escribe la traza paso a paso con las tres
 * columnas de la diapositiva 28: Pila | Entrada | Acción.
 */
int ll1_trazar(const TokenType *tokens, size_t cantidad, FILE *salida);

/* ------------------------------------------------------------------ */
/* Corrección predicha por la tabla                                    */
/* ------------------------------------------------------------------ */

/* Como mucho se insertan 2 tokens seguidos (por ejemplo "stop" y ";"). */
#define LL1_MAX_INSERCION 2

/* Los terminales que la tabla predice que faltan antes del token del error. */
typedef struct {
    int terminales[LL1_MAX_INSERCION];
    int cantidad;
} Ll1Insercion;

/*
 * LA corrección que predice la tabla para el primer error de los tokens.
 *
 * En un parser LL(1) cada celda M[A, a] tiene a lo sumo una producción, así
 * que lo que el parser "espera" se puede seguir sin adivinar. Desde la pila
 * que había antes de leer el token del error:
 *   - un no terminal que puede ser ε se quita (se usa su producción ε);
 *   - un no terminal con UNA sola producción se expande con ella;
 *   - el terminal que queda en el tope es el que el parser predice: ese es
 *     el que falta, y se "lee" como si estuviera.
 * Se repite hasta que el token del error se pueda leer (máximo
 * LL1_MAX_INSERCION terminales).
 *
 * Devuelve 1 si la tabla predice una inserción única (queda en "insercion"),
 * 0 si no la hay (no hay error, el no terminal tiene varias producciones
 * posibles, o lo que falta es un nombre o un número), o -1 sin memoria.
 */
int ll1_insercion_predicha(const TokenType *tokens, size_t cantidad, Ll1Insercion *insercion);

/* ------------------------------------------------------------------ */
/* Recuperación de errores a nivel de frase (ll1_recuperacion.c)       */
/* ------------------------------------------------------------------ */

/* Las correcciones locales que se consideran en el token del error. */
typedef enum {
    LL1_INSERTAR,        /* Agregar uno o dos tokens antes del token del error. */
    LL1_CAMBIAR,         /* Reemplazar el token del error por otro. */
    LL1_BORRAR,          /* Quitar el token del error. */
    LL1_BORRAR_ANTERIOR  /* Quitar el token de antes: en  x : 5 gauss ;  sobra 'gauss'. */
} Ll1TipoCorreccion;

/* UNA corrección para un error. */
typedef struct {
    Ll1TipoCorreccion tipo;
    int terminales[LL1_MAX_INSERCION];  /* Lo que se inserta, o el que reemplaza. */
    int cantidad;                       /* Cuántos terminales (0 al borrar). */
    int predicha;                       /* 1 si la dio la tabla directamente (sin probar). */
} Ll1Correccion;

/*
 * La corrección para el PRIMER error de los tokens:
 *   1. la inserción que predice la tabla (ll1_insercion_predicha), y si no hay,
 *   2. la mejor corrección local (insertar o cambiar por un token esperado,
 *      borrar el token del error o el anterior), probando cada una con el
 *      LL(1) y eligiendo la que más avanza y, si empatan, la más sencilla.
 * Nunca inserta nombres, números ni declare_true/declare_false (habría que
 * inventar el valor).
 *
 * Devuelve 1 si hay corrección, 0 si no (o no hay error), -1 sin memoria.
 */
int ll1_mejor_correccion(const TokenType *tokens, size_t cantidad, Ll1Correccion *correccion);

/* Un error encontrado durante el análisis con recuperación. */
typedef struct {
    size_t posicion;           /* Índice del token del error en la lista ORIGINAL. */
    size_t anterior;           /* Índice ORIGINAL del token de antes (SIZE_MAX si fue insertado). */
    int encontrado;            /* El token que había ahí al analizar. Puede no ser el
                                * del archivo si una corrección anterior lo cambió
                                * ("wheter" -> "whether"). */
    Ll1Conjunto esperados;     /* Lo que podía ir en esa posición. */
    int tiene_correccion;      /* 1 si se encontró una corrección. */
    Ll1Correccion correccion;  /* La corrección con la que se siguió analizando. */

    /* 1 si el error es una palabra clave mal escrita: el token del error es un
     * nombre parecido a una palabra del lenguaje ("whethr" en vez de
     * "whether") y la corrección es LL1_CAMBIAR por esa palabra. */
    int palabra_mal_escrita;
} Ll1Error;

typedef struct {
    Ll1Error *elementos;
    size_t cantidad;
    size_t capacidad;

    /* 1 si, aplicando todas las correcciones, el programa queda válido.
     * Entonces los errores encontrados explican TODOS los problemas de
     * sintaxis del archivo (por ejemplo, también los paréntesis y llaves
     * desbalanceados). */
    int completo;
} Ll1Errores;

/* Para no tardar con un archivo muy roto, se reportan como mucho estos errores. */
#define LL1_MAX_ERRORES 30

/*
 * Analiza TODO el programa aunque tenga errores ("recuperación a nivel de
 * frase", diapositiva 4). En cada error se aplica su corrección a la lista
 * de tokens y se sigue analizando desde ahí. Si no hay ninguna corrección,
 * se descarta el token del error (como el "modo pánico").
 *
 * Así se encuentran todos los errores de sintaxis del archivo, cada uno con
 * su corrección, en vez de detenerse en el primero como Bison.
 *
 * Palabras mal escritas: si se dan los "lexemas" (lo escrito en cada token;
 * NULL en los que no son nombres), en cada error también se buscan, hacia
 * atrás en la misma sentencia, nombres parecidos a una palabra clave
 * ("whethr", "declar_int"). Si cambiar ese nombre por la palabra deja el
 * archivo con MENOS errores, esa es la corrección, y el error se reporta en
 * el nombre (palabra_mal_escrita = 1). Con lexemas = NULL no se busca.
 *
 * Devuelve 0, o -1 sin memoria. Liberar con ll1_liberar_errores.
 */
int ll1_analizar_con_recuperacion(const TokenType *tokens, const char *const *lexemas,
                                  size_t cantidad, Ll1Errores *errores);

void ll1_liberar_errores(Ll1Errores *errores);

#endif
