#include "token.h"   // Definiciones de los tokens y sus errores.
#include <stdlib.h>  // Necesario para liberar memoria con free.

// Devuelve el nombre del token para poder mostrarlo en pantalla.
const char *token_type_name(TokenType type) {

    // Los nombres están en el mismo orden que en el enum TokenType.
    static const char *const names[] = {
        "EOF", "ERROR", "IDENTIFIER", "INTEGER", "STRING", "CHARACTER",
        "MAIN", "DECLARE_CONST", "CREATE_FUNK", "GIVE",
        "WHETHER", "ALIF", "ALSO", "WHALE", "STOP",
        "CYCLE", "LET", "UNTIL", "ENDGAME", "STEP",
        "BRING", "AKA", "SEEK", "SEIZE", "SHOW",
        "DECLARE_LIST", "ADD", "REMOVE", "SIZE",
        "DECLARE_INT", "DECLARE_BOOLEAN", "DECLARE_TEXT",
        "DECLARE_CHAR", "DECLARE_INFINITE_VOID",
        "DECLARE_FALSE", "DECLARE_TRUE",
        "GAUSS", "NEUMANN", "PITAGORAS", "EUCLIDES",
        "EULER", "DESCARTES",
        "EQUAL", "NOT_EQUAL", "LESS", "GREATER",
        "LESS_EQUAL", "GREATER_EQUAL", "AND", "OR", "NOT", "XOR",
        "ASSIGN", "DOT", "LPAREN", "RPAREN",
        "LBRACKET", "RBRACKET", "LBRACE", "RBRACE",
        "COMMA", "SEMICOLON", "STAR", "HASH"
    };

    // Si el tipo existe, devolvemos su nombre. Si no, UNKNOWN.
    return (unsigned)type < sizeof(names) / sizeof(names[0]) ? names[type] : "UNKNOWN";
}


// Devuelve un mensaje según el error que encontró el lexer.
const char *token_error_message(TokenError error) {
    switch (error) {

        case TOKEN_INVALID_CHARACTER:
            return "carácter no permitido";

        case TOKEN_UNTERMINATED_STRING:
            return "cadena sin cerrar";

        case TOKEN_UNTERMINATED_CHARACTER:
            return "literal de carácter sin cerrar";

        case TOKEN_INVALID_CHARACTER_LITERAL:
            return "el literal debe contener exactamente un carácter";

        case TOKEN_UNTERMINATED_COMMENT:
            return "comentario de bloque sin cerrar";

        case TOKEN_INVALID_NUMBER:
            return "literal numérico inválido (solo enteros)";

        case TOKEN_OUT_OF_MEMORY:
            return "memoria insuficiente";

        case TOKEN_NO_ERROR:
            return "sin error";
    }

    // Se utiliza si recibimos un error que no está definido.
    return "error desconocido";
}


// Libera la memoria que se reservó para el texto del token.
void token_dispose(Token *token) {

    // Si no recibimos un token válido, no hacemos nada.
    if (!token) return;

    // Liberamos la memoria donde estaba guardado el lexema.
    free(token->lexeme);

    // Dejamos el puntero en NULL para evitar reutilizarlo.
    token->lexeme = NULL;

    // Reiniciamos la longitud del lexema.
    token->length = 0;
}
