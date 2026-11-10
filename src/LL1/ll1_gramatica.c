/*
 * Gramática LL(1) del lenguaje BAL.
 *
 * Es la MISMA gramática de src/Analisis_Sintactico/gramatica.y (Bison),
 * reescrita con las dos transformaciones de las diapositivas 10 y 11 para
 * que un analizador descendente pueda decidir con un solo token:
 *
 *  1. Eliminación de la recursividad por la izquierda (diapositiva 10).
 *     Bison acepta reglas como
 *         programa → programa elemento_superior | elemento_superior
 *     pero un analizador descendente entraría en ciclo infinito. Con la
 *     transformación  A → A α | β   ==>   A → β A'    A' → α A' | ε
 *     queda:
 *         programa      → elemento_superior mas_elementos
 *         mas_elementos → elemento_superior mas_elementos | ε
 *     Lo mismo se hizo con parametros, sentencias, declaradores, listas,
 *     capturas, argumentos, dimensiones, referencia y TODAS las
 *     expresiones binarias.
 *
 *  2. Factorización por la izquierda (diapositiva 11).
 *     En Bison, una sentencia que empieza con un nombre puede ser:
 *         declaracion:  x * declare_int : 5;
 *         asignacion:   x : 5;        x[1].y : 5;
 *         llamada:      x(5);
 *     Las tres empiezan con IDENTIFIER, así que con un token no se sabe
 *     cuál elegir. Se saca el prefijo común:
 *         sentencia           → IDENTIFIER resto_identificador
 *         resto_identificador → STAR ... (declaración)
 *                             | sufijos_referencia resto_referencia
 *         resto_referencia    → ASSIGN ... (asignación) | LPAREN ... (llamada)
 *
 *  3. Precedencia de operadores (diapositiva 9, "reescribir la gramática").
 *     Bison resuelve la ambigüedad de  expresion → expresion op expresion
 *     con %left/%right. Aquí no hay esas directivas, así que se usa un no
 *     terminal por cada nivel de precedencia, igual que en el ejercicio de
 *     la diapositiva 29 (E, R, T, F, P), de menor a mayor:
 *         ||  <  ^ (xor)  <  &&  <  comparaciones  <  gauss neumann
 *             <  pitagoras euclides euler  <  ~ y - (unarios)  <  descartes
 *     Las comparaciones no se encadenan (a < b < c es error), como el
 *     %nonassoc de Bison, y descartes asocia a la derecha (como %right).
 *
 * Que esta gramática sea LL(1) no se supone: ll1_tabla.c construye la
 * tabla y cuenta los conflictos, y "make test-ll1" exige que sean 0.
 */
#include "ll1.h"

/* Abreviatura para cerrar cada cuerpo; un cuerpo que solo tiene FIN es ε. */
#define FIN LL1_FIN_CUERPO

const Ll1Produccion ll1_producciones[] = {
    /* ---------------- Programa ---------------- */

    /* programa → elemento_superior mas_elementos   (un programa tiene 1 o más elementos) */
    {NT_PROGRAMA, {NT_ELEMENTO_SUPERIOR, NT_MAS_ELEMENTOS, FIN}},
    {NT_MAS_ELEMENTOS, {NT_ELEMENTO_SUPERIOR, NT_MAS_ELEMENTOS, FIN}},
    {NT_MAS_ELEMENTOS, {FIN}},

    /* elemento_superior → funcion | importacion | constante */
    {NT_ELEMENTO_SUPERIOR, {NT_FUNCION, FIN}},
    {NT_ELEMENTO_SUPERIOR, {NT_IMPORTACION, FIN}},
    {NT_ELEMENTO_SUPERIOR, {NT_CONSTANTE, FIN}},

    /* funcion → create_funk # tipo_retorno # nombre ( parametros ) bloque */
    {NT_FUNCION, {TOKEN_CREATE_FUNK, TOKEN_HASH, NT_TIPO_RETORNO, TOKEN_HASH,
                  NT_NOMBRE_FUNCION, TOKEN_LPAREN, NT_PARAMETROS_OPCIONALES,
                  TOKEN_RPAREN, NT_BLOQUE, FIN}},
    {NT_TIPO_RETORNO, {NT_TIPO, FIN}},
    {NT_TIPO_RETORNO, {TOKEN_DECLARE_INFINITE_VOID, FIN}},
    {NT_NOMBRE_FUNCION, {TOKEN_MAIN, FIN}},
    {NT_NOMBRE_FUNCION, {TOKEN_IDENTIFIER, FIN}},

    /* Parámetros separados por comas (sin recursividad izquierda). */
    {NT_PARAMETROS_OPCIONALES, {NT_PARAMETRO, NT_MAS_PARAMETROS, FIN}},
    {NT_PARAMETROS_OPCIONALES, {FIN}},
    {NT_MAS_PARAMETROS, {TOKEN_COMMA, NT_PARAMETRO, NT_MAS_PARAMETROS, FIN}},
    {NT_MAS_PARAMETROS, {FIN}},

    /* parametro → nombre * tipo_parametro       ej.  datos * declare_int[][3] */
    {NT_PARAMETRO, {TOKEN_IDENTIFIER, TOKEN_STAR, NT_TIPO_PARAMETRO, FIN}},
    {NT_TIPO_PARAMETRO, {NT_TIPO, NT_DIMENSIONES_PARAMETRO, FIN}},
    {NT_TIPO_PARAMETRO, {TOKEN_DECLARE_LIST, NT_TIPO, FIN}},

    /* Dimensiones de un parámetro: [] o [expresion], cero o más veces.
     * Factorizado: las dos formas empiezan con '['. */
    {NT_DIMENSIONES_PARAMETRO, {TOKEN_LBRACKET, NT_DIMENSION_PARAMETRO_RESTO,
                                NT_DIMENSIONES_PARAMETRO, FIN}},
    {NT_DIMENSIONES_PARAMETRO, {FIN}},
    {NT_DIMENSION_PARAMETRO_RESTO, {TOKEN_RBRACKET, FIN}},
    {NT_DIMENSION_PARAMETRO_RESTO, {NT_EXPRESION, TOKEN_RBRACKET, FIN}},

    /* importacion → bring nombre [aka alias] ; */
    {NT_IMPORTACION, {TOKEN_BRING, TOKEN_IDENTIFIER, NT_ALIAS_OPCIONAL, TOKEN_SEMICOLON, FIN}},
    {NT_ALIAS_OPCIONAL, {TOKEN_AKA, TOKEN_IDENTIFIER, FIN}},
    {NT_ALIAS_OPCIONAL, {FIN}},

    /* constante → declare_const nombre * tipo : expresion ; */
    {NT_CONSTANTE, {TOKEN_DECLARE_CONST, TOKEN_IDENTIFIER, TOKEN_STAR, NT_TIPO_VARIABLE,
                    TOKEN_ASSIGN, NT_EXPRESION, TOKEN_SEMICOLON, FIN}},

    /* ---------------- Tipos ---------------- */

    {NT_TIPO, {TOKEN_DECLARE_INT, FIN}},
    {NT_TIPO, {TOKEN_DECLARE_BOOLEAN, FIN}},
    {NT_TIPO, {TOKEN_DECLARE_TEXT, FIN}},
    {NT_TIPO, {TOKEN_DECLARE_CHAR, FIN}},

    /* tipo_variable → tipo dimensiones          ej.  declare_int[2][3] */
    {NT_TIPO_VARIABLE, {NT_TIPO, NT_DIMENSIONES, FIN}},
    {NT_DIMENSIONES, {TOKEN_LBRACKET, NT_EXPRESION, TOKEN_RBRACKET, NT_DIMENSIONES, FIN}},
    {NT_DIMENSIONES, {FIN}},

    /* ---------------- Bloques y sentencias ---------------- */

    /* bloque → { sentencias }   (las sentencias pueden faltar) */
    {NT_BLOQUE, {TOKEN_LBRACE, NT_SENTENCIAS, TOKEN_RBRACE, FIN}},
    {NT_SENTENCIAS, {NT_SENTENCIA, NT_SENTENCIAS, FIN}},
    {NT_SENTENCIAS, {FIN}},

    /* Sentencias que empiezan con un nombre: factorizadas (ver arriba). */
    {NT_SENTENCIA, {TOKEN_IDENTIFIER, NT_RESTO_IDENTIFICADOR, FIN}},
    {NT_SENTENCIA, {NT_BLOQUE, FIN}},
    {NT_SENTENCIA, {NT_CONDICIONAL, FIN}},
    {NT_SENTENCIA, {NT_CICLO, FIN}},
    {NT_SENTENCIA, {NT_RETORNO, FIN}},
    {NT_SENTENCIA, {NT_CONSTANTE, FIN}},
    {NT_SENTENCIA, {NT_DECLARACION_LISTA, FIN}},
    {NT_SENTENCIA, {NT_RECORRIDO, FIN}},
    {NT_SENTENCIA, {NT_INTENTAR, FIN}},
    /* Llamadas que no empiezan con un nombre: main(...), add(...), remove(...), size(...). */
    {NT_SENTENCIA, {NT_LLAMADA_ESPECIAL, TOKEN_SEMICOLON, FIN}},

    /* Declaración:  x * declare_int : 5, y * declare_int;  */
    {NT_RESTO_IDENTIFICADOR, {TOKEN_STAR, NT_TIPO_VARIABLE, NT_INICIALIZADOR_OPCIONAL,
                              NT_MAS_DECLARADORES, TOKEN_SEMICOLON, FIN}},
    /* Asignación o llamada:  x[1].y : 5;   o   f(5);  */
    {NT_RESTO_IDENTIFICADOR, {NT_SUFIJOS_REFERENCIA, NT_RESTO_REFERENCIA, FIN}},
    {NT_RESTO_REFERENCIA, {TOKEN_ASSIGN, NT_EXPRESION, TOKEN_SEMICOLON, FIN}},
    {NT_RESTO_REFERENCIA, {TOKEN_LPAREN, NT_ARGUMENTOS_OPCIONALES, TOKEN_RPAREN,
                           TOKEN_SEMICOLON, FIN}},

    {NT_MAS_DECLARADORES, {TOKEN_COMMA, TOKEN_IDENTIFIER, TOKEN_STAR, NT_TIPO_VARIABLE,
                           NT_INICIALIZADOR_OPCIONAL, NT_MAS_DECLARADORES, FIN}},
    {NT_MAS_DECLARADORES, {FIN}},
    {NT_INICIALIZADOR_OPCIONAL, {TOKEN_ASSIGN, NT_EXPRESION, FIN}},
    {NT_INICIALIZADOR_OPCIONAL, {FIN}},

    {NT_LLAMADA_ESPECIAL, {TOKEN_MAIN, TOKEN_LPAREN, NT_ARGUMENTOS_OPCIONALES,
                           TOKEN_RPAREN, FIN}},
    {NT_LLAMADA_ESPECIAL, {TOKEN_ADD, TOKEN_LPAREN, NT_EXPRESION, TOKEN_COMMA,
                           NT_EXPRESION, TOKEN_RPAREN, FIN}},
    {NT_LLAMADA_ESPECIAL, {TOKEN_REMOVE, TOKEN_LPAREN, NT_EXPRESION, TOKEN_COMMA,
                           NT_EXPRESION, TOKEN_RPAREN, FIN}},
    {NT_LLAMADA_ESPECIAL, {TOKEN_SIZE, TOKEN_LPAREN, NT_EXPRESION, TOKEN_RPAREN, FIN}},

    /* declare_list nombres * tipo [: valor], ... ; */
    {NT_DECLARACION_LISTA, {TOKEN_DECLARE_LIST, NT_DECLARADOR_LISTA, NT_MAS_LISTAS,
                            TOKEN_SEMICOLON, FIN}},
    {NT_DECLARADOR_LISTA, {TOKEN_IDENTIFIER, TOKEN_STAR, NT_TIPO,
                           NT_INICIALIZADOR_OPCIONAL, FIN}},
    {NT_MAS_LISTAS, {TOKEN_COMMA, NT_DECLARADOR_LISTA, NT_MAS_LISTAS, FIN}},
    {NT_MAS_LISTAS, {FIN}},

    /* whether (c) {...} alif (c) {...} also {...}
     * No hay "else colgante" (diapositiva 11) porque los bloques llevan
     * llaves obligatorias: cada alif/also queda pegado a su bloque. */
    {NT_CONDICIONAL, {TOKEN_WHETHER, TOKEN_LPAREN, NT_EXPRESION, TOKEN_RPAREN,
                      NT_BLOQUE, NT_ALTERNATIVA, FIN}},
    {NT_ALTERNATIVA, {TOKEN_ALIF, TOKEN_LPAREN, NT_EXPRESION, TOKEN_RPAREN,
                      NT_BLOQUE, NT_ALTERNATIVA, FIN}},
    {NT_ALTERNATIVA, {TOKEN_ALSO, NT_BLOQUE, FIN}},
    {NT_ALTERNATIVA, {FIN}},

    /* whale (c) {...} stop; */
    {NT_CICLO, {TOKEN_WHALE, TOKEN_LPAREN, NT_EXPRESION, TOKEN_RPAREN, NT_BLOQUE,
                TOKEN_STOP, TOKEN_SEMICOLON, FIN}},

    /* give expresion;  o  give;   (factorizado: las dos empiezan con give) */
    {NT_RETORNO, {TOKEN_GIVE, NT_RESTO_RETORNO, FIN}},
    {NT_RESTO_RETORNO, {NT_EXPRESION, TOKEN_SEMICOLON, FIN}},
    {NT_RESTO_RETORNO, {TOKEN_SEMICOLON, FIN}},

    /* cycle i let inicio until fin [step paso] {...} endgame; */
    {NT_RECORRIDO, {TOKEN_CYCLE, TOKEN_IDENTIFIER, TOKEN_LET, NT_EXPRESION, TOKEN_UNTIL,
                    NT_EXPRESION, NT_PASO_OPCIONAL, NT_BLOQUE, TOKEN_ENDGAME,
                    TOKEN_SEMICOLON, FIN}},
    {NT_PASO_OPCIONAL, {TOKEN_STEP, NT_EXPRESION, FIN}},
    {NT_PASO_OPCIONAL, {FIN}},

    /* seek {...} seize (Tipo nombre) {...} ...   (una o más capturas) */
    {NT_INTENTAR, {TOKEN_SEEK, NT_BLOQUE, NT_CAPTURA, NT_MAS_CAPTURAS, FIN}},
    {NT_MAS_CAPTURAS, {NT_CAPTURA, NT_MAS_CAPTURAS, FIN}},
    {NT_MAS_CAPTURAS, {FIN}},
    {NT_CAPTURA, {TOKEN_SEIZE, TOKEN_LPAREN, TOKEN_IDENTIFIER, TOKEN_IDENTIFIER,
                  TOKEN_RPAREN, NT_BLOQUE, FIN}},

    /* Argumentos de una llamada, separados por comas. */
    {NT_ARGUMENTOS_OPCIONALES, {NT_EXPRESION, NT_MAS_ARGUMENTOS, FIN}},
    {NT_ARGUMENTOS_OPCIONALES, {FIN}},
    {NT_MAS_ARGUMENTOS, {TOKEN_COMMA, NT_EXPRESION, NT_MAS_ARGUMENTOS, FIN}},
    {NT_MAS_ARGUMENTOS, {FIN}},

    /* ---------------- Expresiones ----------------
     * Cada nivel es  X → Y resto_X   y   resto_X → op Y resto_X | ε,
     * que es lo que queda al quitar la recursividad izquierda de
     * X → X op Y | Y  (por eso asocian a la izquierda). */

    /* Nivel 1: || */
    {NT_EXPRESION, {NT_EXCLUSIVA, NT_RESTO_OR, FIN}},
    {NT_RESTO_OR, {TOKEN_OR, NT_EXCLUSIVA, NT_RESTO_OR, FIN}},
    {NT_RESTO_OR, {FIN}},

    /* Nivel 2: ^ (o exclusivo) */
    {NT_EXCLUSIVA, {NT_CONJUNCION, NT_RESTO_XOR, FIN}},
    {NT_RESTO_XOR, {TOKEN_XOR, NT_CONJUNCION, NT_RESTO_XOR, FIN}},
    {NT_RESTO_XOR, {FIN}},

    /* Nivel 3: && */
    {NT_CONJUNCION, {NT_COMPARACION, NT_RESTO_AND, FIN}},
    {NT_RESTO_AND, {TOKEN_AND, NT_COMPARACION, NT_RESTO_AND, FIN}},
    {NT_RESTO_AND, {FIN}},

    /* Nivel 4: comparaciones. Como mucho UNA (no asociativas, igual que
     * %nonassoc en Bison): resto_comparacion no se llama a sí mismo. */
    {NT_COMPARACION, {NT_SUMA, NT_RESTO_COMPARACION, FIN}},
    {NT_RESTO_COMPARACION, {NT_OPERADOR_RELACIONAL, NT_SUMA, FIN}},
    {NT_RESTO_COMPARACION, {FIN}},
    {NT_OPERADOR_RELACIONAL, {TOKEN_EQUAL, FIN}},
    {NT_OPERADOR_RELACIONAL, {TOKEN_NOT_EQUAL, FIN}},
    {NT_OPERADOR_RELACIONAL, {TOKEN_LESS, FIN}},
    {NT_OPERADOR_RELACIONAL, {TOKEN_GREATER, FIN}},
    {NT_OPERADOR_RELACIONAL, {TOKEN_LESS_EQUAL, FIN}},
    {NT_OPERADOR_RELACIONAL, {TOKEN_GREATER_EQUAL, FIN}},

    /* Nivel 5: gauss (suma) y neumann (resta) */
    {NT_SUMA, {NT_PRODUCTO, NT_RESTO_SUMA, FIN}},
    {NT_RESTO_SUMA, {TOKEN_GAUSS, NT_PRODUCTO, NT_RESTO_SUMA, FIN}},
    {NT_RESTO_SUMA, {TOKEN_NEUMANN, NT_PRODUCTO, NT_RESTO_SUMA, FIN}},
    {NT_RESTO_SUMA, {FIN}},

    /* Nivel 6: pitagoras (multiplicación), euclides (división), euler (residuo) */
    {NT_PRODUCTO, {NT_UNARIO, NT_RESTO_PRODUCTO, FIN}},
    {NT_RESTO_PRODUCTO, {TOKEN_PITAGORAS, NT_UNARIO, NT_RESTO_PRODUCTO, FIN}},
    {NT_RESTO_PRODUCTO, {TOKEN_EUCLIDES, NT_UNARIO, NT_RESTO_PRODUCTO, FIN}},
    {NT_RESTO_PRODUCTO, {TOKEN_EULER, NT_UNARIO, NT_RESTO_PRODUCTO, FIN}},
    {NT_RESTO_PRODUCTO, {FIN}},

    /* Nivel 7: unarios ~ (negación) y neumann (signo menos) */
    {NT_UNARIO, {TOKEN_NOT, NT_UNARIO, FIN}},
    {NT_UNARIO, {TOKEN_NEUMANN, NT_UNARIO, FIN}},
    {NT_UNARIO, {NT_POTENCIA, FIN}},

    /* Nivel 8: descartes (potencia). Asocia a la DERECHA: después del
     * operador vuelve a "unario", que puede contener otra potencia.
     * Así  a descartes b descartes c  =  a descartes (b descartes c). */
    {NT_POTENCIA, {NT_PRIMARIO, NT_RESTO_POTENCIA, FIN}},
    {NT_RESTO_POTENCIA, {TOKEN_DESCARTES, NT_UNARIO, FIN}},
    {NT_RESTO_POTENCIA, {FIN}},

    /* Los valores más básicos. */
    {NT_PRIMARIO, {TOKEN_INTEGER, FIN}},
    {NT_PRIMARIO, {TOKEN_STRING, FIN}},
    {NT_PRIMARIO, {TOKEN_CHARACTER, FIN}},
    {NT_PRIMARIO, {TOKEN_DECLARE_TRUE, FIN}},
    {NT_PRIMARIO, {TOKEN_DECLARE_FALSE, FIN}},
    {NT_PRIMARIO, {TOKEN_LPAREN, NT_EXPRESION, TOKEN_RPAREN, FIN}},
    /* Lista literal: [1, 2, 3] */
    {NT_PRIMARIO, {TOKEN_LBRACKET, NT_ELEMENTOS_OPCIONALES, TOKEN_RBRACKET, FIN}},
    /* Variable, posición de arreglo, campo, o llamada:  x   x[1]   x.y   f(1) */
    {NT_PRIMARIO, {TOKEN_IDENTIFIER, NT_SUFIJOS_REFERENCIA, NT_LLAMADA_OPCIONAL, FIN}},
    {NT_PRIMARIO, {NT_LLAMADA_ESPECIAL, FIN}},

    /* Sufijos de una referencia: .campo y [indice], cero o más veces.
     * Viene de quitar la recursividad izquierda de
     * referencia → referencia . IDENTIFIER | referencia [ expresion ] | IDENTIFIER */
    {NT_SUFIJOS_REFERENCIA, {TOKEN_DOT, TOKEN_IDENTIFIER, NT_SUFIJOS_REFERENCIA, FIN}},
    {NT_SUFIJOS_REFERENCIA, {TOKEN_LBRACKET, NT_EXPRESION, TOKEN_RBRACKET,
                             NT_SUFIJOS_REFERENCIA, FIN}},
    {NT_SUFIJOS_REFERENCIA, {FIN}},
    {NT_LLAMADA_OPCIONAL, {TOKEN_LPAREN, NT_ARGUMENTOS_OPCIONALES, TOKEN_RPAREN, FIN}},
    {NT_LLAMADA_OPCIONAL, {FIN}},

    /* Elementos de una lista literal, separados por comas. */
    {NT_ELEMENTOS_OPCIONALES, {NT_EXPRESION, NT_MAS_ELEMENTOS_LISTA, FIN}},
    {NT_ELEMENTOS_OPCIONALES, {FIN}},
    {NT_MAS_ELEMENTOS_LISTA, {TOKEN_COMMA, NT_EXPRESION, NT_MAS_ELEMENTOS_LISTA, FIN}},
    {NT_MAS_ELEMENTOS_LISTA, {FIN}},
};

const int ll1_cantidad_producciones =
    (int)(sizeof ll1_producciones / sizeof ll1_producciones[0]);

int ll1_largo_cuerpo(const Ll1Produccion *produccion)
{
    int largo = 0;
    while (produccion->cuerpo[largo] != LL1_FIN_CUERPO) largo++;
    return largo;
}

/* ------------------------------------------------------------------ */
/* Nombres de los símbolos                                             */
/* ------------------------------------------------------------------ */

/* Nombres de los no terminales, en el mismo orden que el enum de ll1.h. */
static const char *const nombres_no_terminales[LL1_NO_TERMINALES] = {
    [LL1_INDICE(NT_PROGRAMA)] = "programa",
    [LL1_INDICE(NT_MAS_ELEMENTOS)] = "mas_elementos",
    [LL1_INDICE(NT_ELEMENTO_SUPERIOR)] = "elemento_superior",
    [LL1_INDICE(NT_FUNCION)] = "funcion",
    [LL1_INDICE(NT_TIPO_RETORNO)] = "tipo_retorno",
    [LL1_INDICE(NT_NOMBRE_FUNCION)] = "nombre_funcion",
    [LL1_INDICE(NT_PARAMETROS_OPCIONALES)] = "parametros_opcionales",
    [LL1_INDICE(NT_MAS_PARAMETROS)] = "mas_parametros",
    [LL1_INDICE(NT_PARAMETRO)] = "parametro",
    [LL1_INDICE(NT_TIPO_PARAMETRO)] = "tipo_parametro",
    [LL1_INDICE(NT_DIMENSIONES_PARAMETRO)] = "dimensiones_parametro",
    [LL1_INDICE(NT_DIMENSION_PARAMETRO_RESTO)] = "dimension_parametro_resto",
    [LL1_INDICE(NT_IMPORTACION)] = "importacion",
    [LL1_INDICE(NT_ALIAS_OPCIONAL)] = "alias_opcional",
    [LL1_INDICE(NT_CONSTANTE)] = "constante",
    [LL1_INDICE(NT_TIPO)] = "tipo",
    [LL1_INDICE(NT_TIPO_VARIABLE)] = "tipo_variable",
    [LL1_INDICE(NT_DIMENSIONES)] = "dimensiones",
    [LL1_INDICE(NT_BLOQUE)] = "bloque",
    [LL1_INDICE(NT_SENTENCIAS)] = "sentencias",
    [LL1_INDICE(NT_SENTENCIA)] = "sentencia",
    [LL1_INDICE(NT_RESTO_IDENTIFICADOR)] = "resto_identificador",
    [LL1_INDICE(NT_RESTO_REFERENCIA)] = "resto_referencia",
    [LL1_INDICE(NT_MAS_DECLARADORES)] = "mas_declaradores",
    [LL1_INDICE(NT_INICIALIZADOR_OPCIONAL)] = "inicializador_opcional",
    [LL1_INDICE(NT_LLAMADA_ESPECIAL)] = "llamada_especial",
    [LL1_INDICE(NT_DECLARACION_LISTA)] = "declaracion_lista",
    [LL1_INDICE(NT_DECLARADOR_LISTA)] = "declarador_lista",
    [LL1_INDICE(NT_MAS_LISTAS)] = "mas_listas",
    [LL1_INDICE(NT_CONDICIONAL)] = "condicional",
    [LL1_INDICE(NT_ALTERNATIVA)] = "alternativa",
    [LL1_INDICE(NT_CICLO)] = "ciclo",
    [LL1_INDICE(NT_RETORNO)] = "retorno",
    [LL1_INDICE(NT_RESTO_RETORNO)] = "resto_retorno",
    [LL1_INDICE(NT_RECORRIDO)] = "recorrido",
    [LL1_INDICE(NT_PASO_OPCIONAL)] = "paso_opcional",
    [LL1_INDICE(NT_INTENTAR)] = "intentar",
    [LL1_INDICE(NT_MAS_CAPTURAS)] = "mas_capturas",
    [LL1_INDICE(NT_CAPTURA)] = "captura",
    [LL1_INDICE(NT_ARGUMENTOS_OPCIONALES)] = "argumentos_opcionales",
    [LL1_INDICE(NT_MAS_ARGUMENTOS)] = "mas_argumentos",
    [LL1_INDICE(NT_EXPRESION)] = "expresion",
    [LL1_INDICE(NT_RESTO_OR)] = "resto_or",
    [LL1_INDICE(NT_EXCLUSIVA)] = "exclusiva",
    [LL1_INDICE(NT_RESTO_XOR)] = "resto_xor",
    [LL1_INDICE(NT_CONJUNCION)] = "conjuncion",
    [LL1_INDICE(NT_RESTO_AND)] = "resto_and",
    [LL1_INDICE(NT_COMPARACION)] = "comparacion",
    [LL1_INDICE(NT_RESTO_COMPARACION)] = "resto_comparacion",
    [LL1_INDICE(NT_OPERADOR_RELACIONAL)] = "operador_relacional",
    [LL1_INDICE(NT_SUMA)] = "suma",
    [LL1_INDICE(NT_RESTO_SUMA)] = "resto_suma",
    [LL1_INDICE(NT_PRODUCTO)] = "producto",
    [LL1_INDICE(NT_RESTO_PRODUCTO)] = "resto_producto",
    [LL1_INDICE(NT_UNARIO)] = "unario",
    [LL1_INDICE(NT_POTENCIA)] = "potencia",
    [LL1_INDICE(NT_RESTO_POTENCIA)] = "resto_potencia",
    [LL1_INDICE(NT_PRIMARIO)] = "primario",
    [LL1_INDICE(NT_SUFIJOS_REFERENCIA)] = "sufijos_referencia",
    [LL1_INDICE(NT_LLAMADA_OPCIONAL)] = "llamada_opcional",
    [LL1_INDICE(NT_ELEMENTOS_OPCIONALES)] = "elementos_opcionales",
    [LL1_INDICE(NT_MAS_ELEMENTOS_LISTA)] = "mas_elementos_lista",
};

const char *ll1_nombre(int simbolo)
{
    if (simbolo == LL1_FIN) return "$";
    if (LL1_ES_TERMINAL(simbolo)) return token_type_name((TokenType)simbolo);
    if (simbolo < LL1_TOTAL_SIMBOLOS) return nombres_no_terminales[LL1_INDICE(simbolo)];
    return "?";
}

/* Cómo se escribe cada terminal en BAL (copiado de la tabla del lexer). */
static const char *const textos_terminales[LL1_TERMINALES] = {
    [TOKEN_EOF] = "el final del archivo",
    [TOKEN_ERROR] = "un símbolo inválido",
    [TOKEN_IDENTIFIER] = "un nombre",
    [TOKEN_INTEGER] = "un número",
    [TOKEN_STRING] = "un texto entre comillas",
    [TOKEN_CHARACTER] = "un carácter",
    [TOKEN_MAIN] = "main",
    [TOKEN_DECLARE_CONST] = "declare_const",
    [TOKEN_CREATE_FUNK] = "create_funk",
    [TOKEN_GIVE] = "give",
    [TOKEN_WHETHER] = "whether",
    [TOKEN_ALIF] = "alif",
    [TOKEN_ALSO] = "also",
    [TOKEN_WHALE] = "whale",
    [TOKEN_STOP] = "stop",
    [TOKEN_CYCLE] = "cycle",
    [TOKEN_LET] = "let",
    [TOKEN_UNTIL] = "until",
    [TOKEN_ENDGAME] = "endgame",
    [TOKEN_STEP] = "step",
    [TOKEN_BRING] = "bring",
    [TOKEN_AKA] = "aka",
    [TOKEN_SEEK] = "seek",
    [TOKEN_SEIZE] = "seize",
    [TOKEN_SHOW] = "show",
    [TOKEN_DECLARE_LIST] = "declare_list",
    [TOKEN_ADD] = "add",
    [TOKEN_REMOVE] = "remove",
    [TOKEN_SIZE] = "size",
    [TOKEN_DECLARE_INT] = "declare_int",
    [TOKEN_DECLARE_BOOLEAN] = "declare_boolean",
    [TOKEN_DECLARE_TEXT] = "declare_text",
    [TOKEN_DECLARE_CHAR] = "declare_char",
    [TOKEN_DECLARE_INFINITE_VOID] = "declare_infinite_void",
    [TOKEN_DECLARE_FALSE] = "declare_false",
    [TOKEN_DECLARE_TRUE] = "declare_true",
    [TOKEN_GAUSS] = "gauss",
    [TOKEN_NEUMANN] = "neumann",
    [TOKEN_PITAGORAS] = "pitagoras",
    [TOKEN_EUCLIDES] = "euclides",
    [TOKEN_EULER] = "euler",
    [TOKEN_DESCARTES] = "descartes",
    [TOKEN_EQUAL] = "==",
    [TOKEN_NOT_EQUAL] = "=/=",
    [TOKEN_LESS] = "<",
    [TOKEN_GREATER] = ">",
    [TOKEN_LESS_EQUAL] = "<=",
    [TOKEN_GREATER_EQUAL] = ">=",
    [TOKEN_AND] = "&&",
    [TOKEN_OR] = "||",
    [TOKEN_NOT] = "~",
    [TOKEN_XOR] = "^",
    [TOKEN_ASSIGN] = ":",
    [TOKEN_DOT] = ".",
    [TOKEN_LPAREN] = "(",
    [TOKEN_RPAREN] = ")",
    [TOKEN_LBRACKET] = "[",
    [TOKEN_RBRACKET] = "]",
    [TOKEN_LBRACE] = "{",
    [TOKEN_RBRACE] = "}",
    [TOKEN_COMMA] = ",",
    [TOKEN_SEMICOLON] = ";",
    [TOKEN_STAR] = "*",
    [TOKEN_HASH] = "#",
};

const char *ll1_texto(int terminal)
{
    if (terminal < 0 || !LL1_ES_TERMINAL(terminal)) return "?";
    return textos_terminales[terminal];
}

int ll1_texto_fijo(int terminal)
{
    switch (terminal) {
        /* Estos dependen de lo que escriba la persona, o no son texto. */
        case TOKEN_EOF:
        case TOKEN_ERROR:
        case TOKEN_IDENTIFIER:
        case TOKEN_INTEGER:
        case TOKEN_STRING:
        case TOKEN_CHARACTER:
            return 0;
        default:
            return LL1_ES_TERMINAL(terminal);
    }
}
