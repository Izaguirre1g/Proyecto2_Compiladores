/* Primera etapa: declaraciones, asignaciones, expresiones, bloques, condicionales y ciclos.
 * Incluye funciones, parametros, give y llamadas. Bison construye el AST usando las funciones de ast.c.
 * puente_lexer.c traduce TOKEN_* a los tokens de Bison mediante yylex.
 */

/* Cada analisis recibe su propio contexto; no se comparte un lexer global. */
%define api.pure full
%define parse.error detailed
%define parse.lac full
/* Cada simbolo transporta un puntero a un nodo. Ast es dueno de su memoria. */
%define api.value.type {AstNodo *}
%locations
%define api.location.type {AstUbicacion}
%initial-action { @$ = (AstUbicacion){1, 1, 1, 1}; }
%parse-param { ContextoSintactico *ctx }
%lex-param { ContextoSintactico *ctx }

%code requires {
#include "sintactico.h"
}

%code provides {
int yylex(YYSTYPE *valor, YYLTYPE *ubicacion, ContextoSintactico *ctx);
void yyerror(YYLTYPE *ubicacion, ContextoSintactico *ctx, const char *mensaje);
}

/* Tokens: las piezas que recibe el parser desde el lexer de Javier. */
%token IDENTIFIER INTEGER STRING CHARACTER
%token DECLARE_INT DECLARE_BOOLEAN DECLARE_TEXT DECLARE_CHAR
%token DECLARE_TRUE DECLARE_FALSE
%token STAR ASSIGN SEMICOLON COMMA
%token GAUSS NEUMANN PITAGORAS EUCLIDES EULER DESCARTES
%token AND OR NOT XOR
%token CYCLE LET UNTIL STEP ENDGAME
%token LBRACKET RBRACKET DOT
%token DECLARE_LIST ADD REMOVE SIZE
%token BRING AKA DECLARE_CONST SEEK SEIZE
%token LPAREN RPAREN
%token LBRACE RBRACE WHETHER ALIF ALSO
%token WHALE STOP GIVE
%token CREATE_FUNK HASH MAIN DECLARE_INFINITE_VOID
%token EQUAL NOT_EQUAL LESS GREATER LESS_EQUAL GREATER_EQUAL

/* Precedencia propuesta, de menor a mayor. %left asocia a la izquierda.
 * GAUSS: suma; NEUMANN: resta; PITAGORAS: multiplicacion; EUCLIDES: division.
 * Potencia asocia a la derecha y tiene prioridad sobre la negacion.
 * Logica: OR < XOR < AND < comparaciones; NOT es unario.
 * NEGATIVO es una marca interna de precedencia, no un token del lexer.
 */
/* Comparaciones: menor prioridad que la aritmetica.
 * %nonassoc rechaza cadenas sin parentesis como a < b < c.
 * EQUAL: ==; NOT_EQUAL: =/=; LESS: <; GREATER: >;
 * LESS_EQUAL: <=; GREATER_EQUAL: >=.
 */
%left OR
%left XOR
%left AND
%nonassoc EQUAL NOT_EQUAL LESS GREATER LESS_EQUAL GREATER_EQUAL
%left GAUSS NEUMANN
%left PITAGORAS EUCLIDES EULER
%precedence NEGATIVO NOT
%right DESCARTES

/* Primera forma de programa: una o mas funciones con parametros opcionales.
 * Las sentencias quedan dentro de los cuerpos de las funciones.
 */
%start programa

/* Reglas de gramatica entre separadores %% */
%%

/* Las funciones se declaran al nivel del programa, no dentro de sentencias.
 * La existencia y unicidad de main se verificaran en la etapa semantica.
 */
programa:
    elemento_superior
      {
        $$ = ast_crear(ctx->arbol, AST_PROGRAMA, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ctx->arbol->raiz = $$;
      }
  | programa elemento_superior
      { $$ = $1; $$->ubicacion = @$; ast_agregar_hijo($$, $2); }
;

/* Ejemplo: create_funk #declare_infinite_void# main() { ... } */
funcion:
    CREATE_FUNK HASH tipo_retorno HASH nombre_funcion LPAREN parametros_opcionales RPAREN bloque
      {
        $$ = ast_crear(ctx->arbol, AST_FUNCION, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $5);
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $7);
        ast_agregar_hijo($$, $9);
      }
;

/* Los parentesis pueden estar vacios o contener parametros separados por comas. */
parametros_opcionales:
    %empty
      {
        $$ = ast_crear(ctx->arbol, AST_PARAMETROS, NULL, @$);
        if (!$$) YYNOMEM;
      }
  | parametros
      { $$ = $1; }
;

parametros:
    parametro
      {
        $$ = ast_crear(ctx->arbol, AST_PARAMETROS, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
      }
  | parametros COMMA parametro
      { $$ = $1; $$->ubicacion = @$; ast_agregar_hijo($$, $3); }
;

/* Sintaxis adoptada: nombre * tipo, sin valor inicial ni punto y coma.
 * Ejemplo: a * declare_int, b * declare_int
 */
parametro:
    IDENTIFIER STAR tipo_parametro
      {
        $$ = ast_crear(ctx->arbol, AST_PARAMETRO, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
;

tipo_retorno:
    tipo
  | DECLARE_INFINITE_VOID
;

/* main tiene su propio token en el lexer; no es un IDENTIFIER. */
nombre_funcion:
    MAIN
  | IDENTIFIER
;

/* Una sentencia, o una lista seguida por otra sentencia. */
sentencias:
    sentencia
      {
        $$ = ast_crear(ctx->arbol, AST_BLOQUE, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
      }
  | sentencias sentencia
      { $$ = $1; ast_agregar_hijo($$, $2); }
;

/* Los bloques, condicionales y ciclos son sentencias: pueden anidarse. */
sentencia:
    declaracion
  | asignacion
  | bloque
  | condicional
  | ciclo
  | retorno
  | constante
  | declaracion_lista
  | recorrido
  | intentar
  | llamada SEMICOLON
;

/* El tipo de retorno y la presencia de valor se verifican semantica. */
retorno:
    GIVE expresion SEMICOLON
      {
        $$ = ast_crear(ctx->arbol, AST_RETORNO, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
      }
  | GIVE SEMICOLON
      {
        $$ = ast_crear(ctx->arbol, AST_RETORNO, NULL, @$);
        if (!$$) YYNOMEM;
      }
;

/* Una llamada puede usarse como expresion o como sentencia seguida de ;.
 * Cada argumento es una expresion: permite operaciones y llamadas anidadas.
 */
llamada:
    destino_llamada LPAREN argumentos_opcionales RPAREN
      {
        $$ = ast_crear(ctx->arbol, AST_LLAMADA, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | ADD LPAREN expresion COMMA expresion RPAREN
      {
        $$ = ast_crear(ctx->arbol, AST_AGREGAR, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $5);
      }
  | REMOVE LPAREN expresion COMMA expresion RPAREN
      {
        $$ = ast_crear(ctx->arbol, AST_ELIMINAR, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $5);
      }
  | SIZE LPAREN expresion RPAREN
      {
        $$ = ast_crear(ctx->arbol, AST_TAMANO, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $3);
      }
;

argumentos_opcionales:
    %empty
      {
        $$ = ast_crear(ctx->arbol, AST_ARGUMENTOS, NULL, @$);
        if (!$$) YYNOMEM;
      }
  | argumentos
      { $$ = $1; }
;

argumentos:
    expresion
      {
        $$ = ast_crear(ctx->arbol, AST_ARGUMENTOS, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
      }
  | argumentos COMMA expresion
      { $$ = $1; $$->ubicacion = @$; ast_agregar_hijo($$, $3); }
;

/* Un bloque tiene llaves obligatorias y puede estar vacio. */
bloque:
    LBRACE contenido_bloque RBRACE
      { $$ = $2; $$->ubicacion = @$; }
;

contenido_bloque:
    %empty
      {
        $$ = ast_crear(ctx->arbol, AST_BLOQUE, NULL, @$);
        if (!$$) YYNOMEM;
      }
  | sentencias
      { $$ = $1; }
;

/* whether (condicion) { ... }, cero o mas alif y un also final opcional.
 * Las llaves delimitan cada rama, incluso cuando hay otro whether dentro.
 * No se escribe punto y coma despues de estas estructuras.
 */
condicional:
    WHETHER LPAREN expresion RPAREN bloque alternativa
      {
        $$ = ast_crear(ctx->arbol, AST_SI, "whether", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $5);
        ast_agregar_hijo($$, $6);
      }
;

/* La recursion permite varios alif; ALSO cierra la cadena de alternativas. */
alternativa:
    %empty
      { $$ = NULL; }
  | ALIF LPAREN expresion RPAREN bloque alternativa
      {
        $$ = ast_crear(ctx->arbol, AST_SI, "alif", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $5);
        ast_agregar_hijo($$, $6);
      }
  | ALSO bloque
      { $$ = $2; }
;

/* Sigue el ejemplo de Javier: whale (condicion) { ... } stop;
 * En esta regla STOP cierra el ciclo; no es una sentencia break independiente.
 * Las llaves, stop y el punto y coma son obligatorios.
 */
ciclo:
    WHALE LPAREN expresion RPAREN bloque STOP SEMICOLON
      {
        $$ = ast_crear(ctx->arbol, AST_MIENTRAS, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $5);
      }
;

/* Regla principal. Ejemplo: n * declare_int : 5; */
declaracion:
    declaradores SEMICOLON
      { $$ = $1; $$->ubicacion = @$; }
;

/* Regla para asignar valores a variables; Dylan verificara su declaracion.
 * Ejemplo: resultado : 1;
 */
asignacion:
    referencia ASSIGN expresion SEMICOLON
      {
        $$ = ast_crear(ctx->arbol, AST_ASIGNACION, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
;

tipo:
    DECLARE_INT
  | DECLARE_BOOLEAN
  | DECLARE_TEXT
  | DECLARE_CHAR
;

/* Valores, variables, operaciones, comparaciones y parentesis.
 * La compatibilidad de tipos se verifica en el analisis semantico.
 */
expresion:
    INTEGER
  | STRING
  | CHARACTER
  | DECLARE_TRUE
  | DECLARE_FALSE
  | referencia
  | literal_coleccion
  | llamada
  | expresion EQUAL expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "==", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion NOT_EQUAL expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "=/=", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion LESS expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "<", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion GREATER expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, ">", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion LESS_EQUAL expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "<=", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion GREATER_EQUAL expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, ">=", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion GAUSS expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "gauss", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion NEUMANN expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "neumann", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion PITAGORAS expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "pitagoras", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion EUCLIDES expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "euclides", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion AND expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "&&", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion OR expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "||", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion XOR expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "^", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion EULER expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "euler", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | expresion DESCARTES expresion
      {
        $$ = ast_crear(ctx->arbol, AST_BINARIO, "descartes", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | NOT expresion
      {
        $$ = ast_crear(ctx->arbol, AST_UNARIO, "~", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
      }
  | NEUMANN expresion %prec NEGATIVO
      {
        $$ = ast_crear(ctx->arbol, AST_UNARIO, "neumann", @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
      }
  | LPAREN expresion RPAREN
      { $$ = $2; } /* Los parentesis ya cumplieron su funcion de agrupacion. */
;

/* Extensiones de declaraciones, colecciones, modulos y control. */

elemento_superior:
    funcion
      { $$ = $1; }
  | importacion
      { $$ = $1; }
  | constante
      { $$ = $1; }
;

importacion:
    BRING IDENTIFIER alias_opcional SEMICOLON
      {
        $$ = ast_crear(ctx->arbol, AST_IMPORTACION, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
        ast_agregar_hijo($$, $3);
      }
;

alias_opcional:
    %empty
      { $$ = NULL; }
  | AKA IDENTIFIER
      { $$ = $2; }
;

tipo_parametro:
    tipo
      { $$ = $1; }
  | tipo dimensiones_parametro
      {
        $$ = ast_crear(ctx->arbol, AST_TIPO_ARREGLO, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $2);
      }
  | DECLARE_LIST tipo
      {
        $$ = ast_crear(ctx->arbol, AST_TIPO_LISTA, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
      }
;

tipo_variable:
    tipo
      { $$ = $1; }
  | tipo dimensiones
      {
        $$ = ast_crear(ctx->arbol, AST_TIPO_ARREGLO, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $2);
      }
;

dimensiones:
    dimension
      {
        $$ = ast_crear(ctx->arbol, AST_DIMENSIONES, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
      }
  | dimensiones dimension
      { $$ = $1; $$->ubicacion = @$; ast_agregar_hijo($$, $2); }
;

dimensiones_parametro:
    dimension_parametro
      {
        $$ = ast_crear(ctx->arbol, AST_DIMENSIONES, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
      }
  | dimensiones_parametro dimension_parametro
      { $$ = $1; $$->ubicacion = @$; ast_agregar_hijo($$, $2); }
;

dimension:
    LBRACKET expresion RBRACKET
      {
        $$ = ast_crear(ctx->arbol, AST_DIMENSION, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
      }
;

dimension_parametro:
    dimension
      { $$ = $1; }
  | LBRACKET RBRACKET
      {
        $$ = ast_crear(ctx->arbol, AST_DIMENSION, NULL, @$);
        if (!$$) YYNOMEM;
      }
;

declarador:
    IDENTIFIER STAR tipo_variable inicializador_opcional
      {
        $$ = ast_crear(ctx->arbol, AST_DECLARACION, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $4);
      }
;

inicializador_opcional:
    %empty
      { $$ = NULL; }
  | ASSIGN expresion
      { $$ = $2; }
;

declaradores:
    declarador
      { $$ = $1; }
  | declaradores COMMA declarador
      {
        $$ = $1;
        if ($$->tipo != AST_DECLARACIONES) {
            $$ = ast_crear(ctx->arbol, AST_DECLARACIONES, NULL, @$);
            if (!$$) YYNOMEM;
            ast_agregar_hijo($$, $1);
        }
        $$->ubicacion = @$;
        ast_agregar_hijo($$, $3);
      }
;

listas:
    declarador_lista
      { $$ = $1; }
  | listas COMMA declarador_lista
      {
        $$ = $1;
        if ($$->tipo != AST_DECLARACIONES) {
            $$ = ast_crear(ctx->arbol, AST_DECLARACIONES, NULL, @$);
            if (!$$) YYNOMEM;
            ast_agregar_hijo($$, $1);
        }
        $$->ubicacion = @$;
        ast_agregar_hijo($$, $3);
      }
;

constante:
    DECLARE_CONST IDENTIFIER STAR tipo_variable ASSIGN expresion SEMICOLON
      {
        $$ = ast_crear(ctx->arbol, AST_CONSTANTE, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
        ast_agregar_hijo($$, $4);
        ast_agregar_hijo($$, $6);
      }
;

declaracion_lista:
    DECLARE_LIST listas SEMICOLON
      { $$ = $2; $$->ubicacion = @$; }
;

declarador_lista:
    IDENTIFIER STAR tipo inicializador_opcional
      {
        $$ = ast_crear(ctx->arbol, AST_LISTA, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $4);
      }
;

recorrido:
    CYCLE IDENTIFIER LET expresion UNTIL expresion paso_opcional bloque ENDGAME SEMICOLON
      {
        $$ = ast_crear(ctx->arbol, AST_RECORRIDO, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
        ast_agregar_hijo($$, $4);
        ast_agregar_hijo($$, $6);
        ast_agregar_hijo($$, $7);
        ast_agregar_hijo($$, $8);
      }
;

paso_opcional:
    %empty
      {
        $$ = ast_crear(ctx->arbol, AST_ENTERO, "1", @$);
        if (!$$) YYNOMEM;
      }
  | STEP expresion
      { $$ = $2; }
;

intentar:
    SEEK bloque capturas
      {
        $$ = ast_crear(ctx->arbol, AST_INTENTAR, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $2);
        ast_agregar_hijo($$, $3);
      }
;

capturas:
    captura
      {
        $$ = ast_crear(ctx->arbol, AST_CAPTURAS, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
      }
  | capturas captura
      { $$ = $1; $$->ubicacion = @$; ast_agregar_hijo($$, $2); }
;

captura:
    SEIZE LPAREN IDENTIFIER IDENTIFIER RPAREN bloque
      {
        $$ = ast_crear(ctx->arbol, AST_CAPTURA, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $3);
        ast_agregar_hijo($$, $4);
        ast_agregar_hijo($$, $6);
      }
;

referencia:
    IDENTIFIER
      { $$ = $1; }
  | referencia DOT IDENTIFIER
      {
        $$ = ast_crear(ctx->arbol, AST_ACCESO_MIEMBRO, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
  | referencia LBRACKET expresion RBRACKET
      {
        $$ = ast_crear(ctx->arbol, AST_INDICE, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
        ast_agregar_hijo($$, $3);
      }
;

destino_llamada:
    referencia
      { $$ = $1; }
  | MAIN
      { $$ = $1; }
;

literal_coleccion:
    LBRACKET elementos_opcionales RBRACKET
      { $$ = $2; $$->ubicacion = @$; }
;

elementos_opcionales:
    %empty
      {
        $$ = ast_crear(ctx->arbol, AST_LITERAL_COLECCION, NULL, @$);
        if (!$$) YYNOMEM;
      }
  | elementos
      { $$ = $1; }
;

elementos:
    expresion
      {
        $$ = ast_crear(ctx->arbol, AST_LITERAL_COLECCION, NULL, @$);
        if (!$$) YYNOMEM;
        ast_agregar_hijo($$, $1);
      }
  | elementos COMMA expresion
      { $$ = $1; ast_agregar_hijo($$, $3); }
;

%%

/* yylex y yyerror estan implementadas en puente_lexer.c.
 * Despues de este segundo separador solo va codigo C, no reglas.
 * Ejemplo de entrada:
 * create_funk #declare_infinite_void# main() {
 *     resultado * declare_int : 0;
 *     resultado : 2 gauss 3 pitagoras 4;
 *     cumple * declare_boolean : resultado <= 14;
 * }
 */
