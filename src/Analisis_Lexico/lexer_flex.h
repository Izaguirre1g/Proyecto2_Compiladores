#ifndef BAL_LEXER_FLEX_H
#define BAL_LEXER_FLEX_H

#include <stddef.h>
#include "token.h"

typedef struct {
    size_t skip_length;
    size_t token_length;
    TokenType type;
    TokenError error;
} LexerFlexMatch;

/* Busca el siguiente token con el scanner generado por Flex.
 * skip_length incluye espacios y comentarios ignorados antes del token.
 */
int lexer_flex_match(const char *source, size_t length, LexerFlexMatch *match);

#endif
