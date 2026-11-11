/* Incluye la interfaz del parser generado por Bison y los tipos necesarios para conectarlo con el lexer. */
#include "parser.h"

/* Traduccion explicita: las enumeraciones no tienen los mismos numeros. */
/* Convierte los tipos de token del lexer a los códigos de token usados por Bison. */
static int token_bison(TokenType tipo) {
    /* Selecciona la traducción según el tipo recibido. */
    switch (tipo) {
        /* Traduce el fin de la entrada al código YYEOF de Bison. */
        case TOKEN_EOF: return YYEOF; /* EOF es 0 en Bison. Informa que Bison ha terminado. */
        /* Traduce el token TOKEN_IDENTIFIER del lexer al token IDENTIFIER de Bison. */
        case TOKEN_IDENTIFIER: return IDENTIFIER;
        /* Traduce el token TOKEN_INTEGER del lexer al token INTEGER de Bison. */
        case TOKEN_INTEGER: return INTEGER;
        /* Traduce el token TOKEN_STRING del lexer al token STRING de Bison. */
        case TOKEN_STRING: return STRING;
        /* Traduce el token TOKEN_CHARACTER del lexer al token CHARACTER de Bison. */
        case TOKEN_CHARACTER: return CHARACTER;
        /* Traduce el token TOKEN_DECLARE_INT del lexer al token DECLARE_INT de Bison. */
        case TOKEN_DECLARE_INT: return DECLARE_INT;
        /* Traduce el token TOKEN_DECLARE_BOOLEAN del lexer al token DECLARE_BOOLEAN de Bison. */
        case TOKEN_DECLARE_BOOLEAN: return DECLARE_BOOLEAN;
        /* Traduce el token TOKEN_DECLARE_TEXT del lexer al token DECLARE_TEXT de Bison. */
        case TOKEN_DECLARE_TEXT: return DECLARE_TEXT;
        /* Traduce el token TOKEN_DECLARE_CHAR del lexer al token DECLARE_CHAR de Bison. */
        case TOKEN_DECLARE_CHAR: return DECLARE_CHAR;
        /* Traduce el token TOKEN_DECLARE_TRUE del lexer al token DECLARE_TRUE de Bison. */
        case TOKEN_DECLARE_TRUE: return DECLARE_TRUE;
        /* Traduce el token TOKEN_DECLARE_FALSE del lexer al token DECLARE_FALSE de Bison. */
        case TOKEN_DECLARE_FALSE: return DECLARE_FALSE;
        /* Traduce el token TOKEN_STAR del lexer al token STAR de Bison. */
        case TOKEN_STAR: return STAR;
        /* Traduce el token TOKEN_ASSIGN del lexer al token ASSIGN de Bison. */
        case TOKEN_ASSIGN: return ASSIGN;
        /* Traduce el token TOKEN_SEMICOLON del lexer al token SEMICOLON de Bison. */
        case TOKEN_SEMICOLON: return SEMICOLON;
        /* Traduce el token TOKEN_COMMA del lexer al token COMMA de Bison. */
        case TOKEN_COMMA: return COMMA;
        /* Traduce el token TOKEN_GAUSS del lexer al token GAUSS de Bison. */
        case TOKEN_GAUSS: return GAUSS;
        /* Traduce el token TOKEN_NEUMANN del lexer al token NEUMANN de Bison. */
        case TOKEN_NEUMANN: return NEUMANN;
        /* Traduce el token TOKEN_PITAGORAS del lexer al token PITAGORAS de Bison. */
        case TOKEN_PITAGORAS: return PITAGORAS;
        /* Traduce el token TOKEN_EUCLIDES del lexer al token EUCLIDES de Bison. */
        case TOKEN_EUCLIDES: return EUCLIDES;
        /* Traduce el token TOKEN_LPAREN del lexer al token LPAREN de Bison. */
        case TOKEN_LPAREN: return LPAREN;
        /* Traduce el token TOKEN_RPAREN del lexer al token RPAREN de Bison. */
        case TOKEN_RPAREN: return RPAREN;
        /* Traduce el token TOKEN_LBRACE del lexer al token LBRACE de Bison. */
        case TOKEN_LBRACE: return LBRACE;
        /* Traduce el token TOKEN_RBRACE del lexer al token RBRACE de Bison. */
        case TOKEN_RBRACE: return RBRACE;
        /* Traduce el token TOKEN_WHETHER del lexer al token WHETHER de Bison. */
        case TOKEN_WHETHER: return WHETHER;
        /* Traduce el token TOKEN_ALIF del lexer al token ALIF de Bison. */
        case TOKEN_ALIF: return ALIF;
        /* Traduce el token TOKEN_ALSO del lexer al token ALSO de Bison. */
        case TOKEN_ALSO: return ALSO;
        /* Traduce el token TOKEN_WHALE del lexer al token WHALE de Bison. */
        case TOKEN_WHALE: return WHALE;
        /* Traduce el token TOKEN_STOP del lexer al token STOP de Bison. */
        case TOKEN_STOP: return STOP;
        /* Traduce el token TOKEN_GIVE del lexer al token GIVE de Bison. */
        case TOKEN_GIVE: return GIVE;
        /* Traduce el token TOKEN_CREATE_FUNK del lexer al token CREATE_FUNK de Bison. */
        case TOKEN_CREATE_FUNK: return CREATE_FUNK;
        /* Traduce el token TOKEN_HASH del lexer al token HASH de Bison. */
        case TOKEN_HASH: return HASH;
        /* Traduce el token TOKEN_MAIN del lexer al token MAIN de Bison. */
        case TOKEN_MAIN: return MAIN;
        /* Traduce el token TOKEN_DECLARE_INFINITE_VOID del lexer al token DECLARE_INFINITE_VOID de Bison. */
        case TOKEN_DECLARE_INFINITE_VOID: return DECLARE_INFINITE_VOID;
        /* Traduce el token TOKEN_EQUAL del lexer al token EQUAL de Bison. */
        case TOKEN_EQUAL: return EQUAL;
        /* Traduce el token TOKEN_NOT_EQUAL del lexer al token NOT_EQUAL de Bison. */
        case TOKEN_NOT_EQUAL: return NOT_EQUAL;
        /* Traduce el token TOKEN_LESS del lexer al token LESS de Bison. */
        case TOKEN_LESS: return LESS;
        /* Traduce el token TOKEN_GREATER del lexer al token GREATER de Bison. */
        case TOKEN_GREATER: return GREATER;
        /* Traduce el token TOKEN_LESS_EQUAL del lexer al token LESS_EQUAL de Bison. */
        case TOKEN_LESS_EQUAL: return LESS_EQUAL;
        /* Traduce el token TOKEN_GREATER_EQUAL del lexer al token GREATER_EQUAL de Bison. */
        case TOKEN_GREATER_EQUAL: return GREATER_EQUAL;
        /* Traduce el token TOKEN_EULER del lexer al token EULER de Bison. */
        case TOKEN_EULER: return EULER;
        /* Traduce el token TOKEN_DESCARTES del lexer al token DESCARTES de Bison. */
        case TOKEN_DESCARTES: return DESCARTES;
        /* Traduce el token TOKEN_AND del lexer al token AND de Bison. */
        case TOKEN_AND: return AND;
        /* Traduce el token TOKEN_OR del lexer al token OR de Bison. */
        case TOKEN_OR: return OR;
        /* Traduce el token TOKEN_NOT del lexer al token NOT de Bison. */
        case TOKEN_NOT: return NOT;
        /* Traduce el token TOKEN_XOR del lexer al token XOR de Bison. */
        case TOKEN_XOR: return XOR;
        /* Traduce el token TOKEN_CYCLE del lexer al token CYCLE de Bison. */
        case TOKEN_CYCLE: return CYCLE;
        /* Traduce el token TOKEN_LET del lexer al token LET de Bison. */
        case TOKEN_LET: return LET;
        /* Traduce el token TOKEN_UNTIL del lexer al token UNTIL de Bison. */
        case TOKEN_UNTIL: return UNTIL;
        /* Traduce el token TOKEN_STEP del lexer al token STEP de Bison. */
        case TOKEN_STEP: return STEP;
        /* Traduce el token TOKEN_ENDGAME del lexer al token ENDGAME de Bison. */
        case TOKEN_ENDGAME: return ENDGAME;
        /* Traduce el token TOKEN_LBRACKET del lexer al token LBRACKET de Bison. */
        case TOKEN_LBRACKET: return LBRACKET;
        /* Traduce el token TOKEN_RBRACKET del lexer al token RBRACKET de Bison. */
        case TOKEN_RBRACKET: return RBRACKET;
        /* Traduce el token TOKEN_DOT del lexer al token DOT de Bison. */
        case TOKEN_DOT: return DOT;
        /* Traduce el token TOKEN_DECLARE_LIST del lexer al token DECLARE_LIST de Bison. */
        case TOKEN_DECLARE_LIST: return DECLARE_LIST;
        /* Traduce el token TOKEN_ADD del lexer al token ADD de Bison. */
        case TOKEN_ADD: return ADD;
        /* Traduce el token TOKEN_REMOVE del lexer al token REMOVE de Bison. */
        case TOKEN_REMOVE: return REMOVE;
        /* Traduce el token TOKEN_SIZE del lexer al token SIZE de Bison. */
        case TOKEN_SIZE: return SIZE;
        /* Traduce el token TOKEN_BRING del lexer al token BRING de Bison. */
        case TOKEN_BRING: return BRING;
        /* Traduce el token TOKEN_AKA del lexer al token AKA de Bison. */
        case TOKEN_AKA: return AKA;
        /* Traduce el token TOKEN_DECLARE_CONST del lexer al token DECLARE_CONST de Bison. */
        case TOKEN_DECLARE_CONST: return DECLARE_CONST;
        /* Traduce el token TOKEN_SEEK del lexer al token SEEK de Bison. */
        case TOKEN_SEEK: return SEEK;
        /* Traduce el token TOKEN_SEIZE del lexer al token SEIZE de Bison. */
        case TOKEN_SEIZE: return SEIZE;
        /* Devuelve el código de token no definido si no hay una traducción explícita. */
        default: return YYUNDEF; /*indica que el token no está definido en la gramática*/
    /* Finaliza la selección del token. */
    }
/* Finaliza la función de traducción. */
}

/* Solo los tokens con datos crean hojas. Puntuacion y operadores se usan
 * en las reglas de Bison; no se guardan como nodos independientes.
 */
/* Determina si el token necesita una hoja y escribe su tipo mediante el puntero recibido; devuelve 1 si corresponde crearla. */
static int tipo_hoja(TokenType token, AstTipo *tipo) {
    /* Selecciona el tipo de hoja según el token. */
    switch (token) {
        /* Asigna el tipo identificador a los nombres y al token de la función principal, e indica que requieren una hoja. */
        case TOKEN_IDENTIFIER: case TOKEN_MAIN: *tipo = AST_IDENTIFICADOR; return 1;
        /* Asigna el tipo entero e indica que se requiere una hoja. */
        case TOKEN_INTEGER: *tipo = AST_ENTERO; return 1;
        /* Asigna el tipo cadena e indica que se requiere una hoja. */
        case TOKEN_STRING: *tipo = AST_CADENA; return 1;
        /* Asigna el tipo carácter e indica que se requiere una hoja. */
        case TOKEN_CHARACTER: *tipo = AST_CARACTER; return 1;
        /* Asigna el tipo booleano a verdadero y falso, e indica que requieren una hoja. */
        case TOKEN_DECLARE_TRUE: case TOKEN_DECLARE_FALSE: *tipo = AST_BOOLEANO; return 1;
        /* Agrupa los tokens de los tipos entero y booleano con los casos siguientes. */
        case TOKEN_DECLARE_INT: case TOKEN_DECLARE_BOOLEAN:
        /* Añade los tokens de los tipos texto y carácter al mismo grupo. */
        case TOKEN_DECLARE_TEXT: case TOKEN_DECLARE_CHAR:
        /* Incluye el tipo vacío y asigna AST_TIPO a todos los casos agrupados, indicando que requieren una hoja. */
        case TOKEN_DECLARE_INFINITE_VOID: *tipo = AST_TIPO; return 1;
        /* Indica que los demás tokens no requieren una hoja creada por esta función. */
        default: return 0;
    /* Finaliza la selección del tipo de hoja. */
    }
/* Finaliza la función que identifica los tokens con hoja. */
}

/* Bison llama a esta funcion cada vez que necesita otro token. */
/* Implementa la interfaz que Bison llama para obtener el siguiente token, su valor semántico y su ubicación. */
int yylex(YYSTYPE *valor, YYLTYPE *ubicacion, ContextoSintactico *ctx) {
    /* Inicializa el valor semántico sin nodo asociado. */
    *valor = NULL;
    /* Obtiene el siguiente token del lexer del contexto. */
    Token token = lexer_next(&ctx->lexer);
    /* Guarda la línea inicial del token en el contexto. */
    ctx->linea = token.line;
    /* Guarda la columna inicial del token en el contexto. */
    ctx->columna = token.column;
    /* Comienza la ubicación con la línea y la columna iniciales del token. */
    *ubicacion = (AstUbicacion){token.line, token.column,
                               /* Completa la ubicación con la posición del lexer después de reconocer el token. */
                               ctx->lexer.line, ctx->lexer.column};
    /* Traduce el tipo del token al código esperado por Bison. */
    int tipo = token_bison(token.type);
    /* Comprueba si el lexer informó de un error. */
    if (token.type == TOKEN_ERROR) {
        /* Registra el estado 2 para memoria insuficiente y el estado 1 para los demás errores léxicos. */
        ctx->estado = token.error == TOKEN_OUT_OF_MEMORY ? 2 : 1;
        /* Inicia el diagnóstico léxico con un formato que incluye archivo, posición y mensaje. */
        fprintf(ctx->diagnosticos, "%s:%zu:%zu: error léxico: %s\n",
                /* Proporciona el nombre del archivo y la posición inicial del token. */
                ctx->lexer.filename, ctx->linea, ctx->columna,
                /* Completa el diagnóstico con la descripción del error léxico. */
                token_error_message(token.error));
        /* Selecciona el código de error ya notificado para activar el manejo de errores de Bison. */
        tipo = YYerror; /* El error ya fue informado. */
    /* Comprueba si el token no tiene una traducción admitida por la gramática. */
    } else if (tipo == YYUNDEF) {
        /* Registra un error de análisis en el contexto. */
        ctx->estado = 1;
        /* Comienza a escribir en el flujo de diagnósticos configurado. */
        fprintf(ctx->diagnosticos,
                /* Define el formato del mensaje que identifica un token no admitido. */
                "%s:%zu:%zu: token %s aún no admitido por la gramática\n",
                /* Proporciona el archivo y la posición del token al diagnóstico. */
                ctx->lexer.filename, ctx->linea, ctx->columna,
                /* Completa el mensaje con el nombre legible del tipo de token. */
                token_type_name(token.type));
        /* Selecciona el código de error ya notificado que recibirá Bison. */
        tipo = YYerror;
    /* Procesa los tokens válidos que sí tienen traducción para Bison. */
    } else {
        /* Declara la variable que recibirá el tipo de hoja cuando corresponda crearla. */
        AstTipo hoja;
        /* Comprueba si el token requiere una hoja y obtiene su tipo. */
        if (tipo_hoja(token.type, &hoja)) {
            /* Crea la hoja con una copia del lexema y su ubicación, y la entrega como valor semántico. */
            *valor = ast_crear(ctx->arbol, hoja, token.lexeme, *ubicacion);
            /* Comprueba si falló la creación de la hoja. */
            if (!*valor) {
                /* Registra el estado de memoria insuficiente. */
                ctx->estado = 2;
                /* Inicia el diagnóstico de falta de memoria para el AST. */
                fprintf(ctx->diagnosticos, "%s:%zu:%zu: memoria insuficiente para el AST\n",
                        /* Completa el diagnóstico con el archivo y la posición del token. */
                        ctx->lexer.filename, ctx->linea, ctx->columna);
                /* Selecciona el código de error de Bison ante el fallo de creación. */
                tipo = YYerror;
            /* Finaliza el tratamiento del fallo de reserva de memoria. */
            }
        /* Finaliza la creación opcional de una hoja. */
        }
    /* Finaliza el tratamiento de tokens válidos y errores. */
    }
    /* ast_crear copio el lexema; ya podemos liberar el token de Javier. */
    /* Libera los recursos del token; cualquier hoja creada conserva su propia copia del lexema. */
    token_dispose(&token);
    /* Devuelve a Bison el código de token o error seleccionado. */
    return tipo;
/* Finaliza la función que obtiene tokens para Bison. */
}

/* Bison llama a esta funcion cuando detecta un error de sintaxis o memoria. */
/* Implementa la función que Bison usa para informar de errores del parser. */
void yyerror(YYLTYPE *ubicacion, ContextoSintactico *ctx, const char *mensaje) {
    /* Inicia la escritura del diagnóstico del parser. */
    fprintf(ctx->diagnosticos, "%s:%zu:%zu: error del parser: %s\n",
            /* Incluye el archivo, la ubicación inicial recibida y el mensaje de Bison. */
            ctx->lexer.filename, ubicacion->first_line, ubicacion->first_column, mensaje);
    /* Registra un error de análisis sin sobrescribir un estado previo de memoria insuficiente. */
    if (ctx->estado != 2) ctx->estado = 1;
/* Finaliza la función de diagnóstico del parser. */
}

/* Declara la construcción del AST a partir del texto fuente, su longitud y el nombre usado en los diagnósticos. */
int construir_ast(const char *fuente, size_t longitud, const char *nombre,
                  /* Completa los parámetros con el flujo de diagnósticos y el árbol de salida. */
                  FILE *diagnosticos, Ast *arbol) {
    /* Inicializa el contexto con valores cero y punteros nulos. */
    ContextoSintactico ctx = {0};
    /* Prepara el lexer para leer el texto fuente recibido. */
    lexer_init(&ctx.lexer, fuente, longitud, nombre);
    /* Asocia el árbol de salida al contexto. */
    ctx.arbol = arbol;
    /* Establece el flujo de salida de los diagnósticos. */
    ctx.diagnosticos = diagnosticos;
    /* Inicializa la línea y la columna del contexto en uno. */
    ctx.linea = ctx.columna = 1;
    /* Ejecuta el parser, que obtiene tokens mediante yylex y construye el AST. */
    int resultado = yyparse(&ctx);
    /* Da prioridad al estado 2 si Bison o el contexto detectaron memoria insuficiente. */
    int estado = resultado == 2 || ctx.estado == 2 ? 2 :
                 /* En otro caso, selecciona 1 si hubo algún error y 0 si el análisis fue correcto. */
                 resultado != 0 || ctx.estado != 0 ? 1 : 0;
    /* Incluye nodos parciales y hojas que Bison descarto al abortar. */
    /* Si hubo un error, libera todos los nodos del árbol, incluidos los de una construcción parcial. */
    if (estado != 0) ast_liberar(arbol);
    /* Devuelve el estado final: 0 para éxito, 1 para error de análisis y 2 para memoria insuficiente. */
    return estado;
/* Finaliza la función de construcción del árbol. */
}

/* Conserva la interfaz anterior para quien solo necesite validar. */
/* Declara la validación sintáctica del texto fuente de la longitud indicada. */
int analizar_sintaxis(const char *fuente, size_t longitud,
                     /* Completa los parámetros con el nombre del archivo y el flujo de diagnósticos. */
                     const char *nombre, FILE *diagnosticos) {
    /* Inicializa un árbol temporal vacío para realizar la validación. */
    Ast arbol = {0};
    /* Construye el árbol temporal y guarda el estado del análisis. */
    int estado = construir_ast(fuente, longitud, nombre, diagnosticos, &arbol);
    /* Libera el árbol temporal, ya que esta interfaz solo devuelve el estado de validación. */
    ast_liberar(&arbol);
    /* Devuelve el estado obtenido durante el análisis. */
    return estado;
/* Finaliza la función de validación sintáctica. */
}
