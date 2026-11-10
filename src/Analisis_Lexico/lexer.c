#include "lexer.h"
#include "lexer_flex.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned char peek(const Lexer *l, size_t ahead) {
    if (l->offset >= l->length || ahead >= l->length - l->offset) return 0;
    return (unsigned char)l->source[l->offset + ahead];
}

static size_t utf8_width(const Lexer *l) {
    unsigned char a = peek(l, 0);
    if (a < 0x80) return 1;

    size_t n = a >= 0xC2 && a <= 0xDF ? 2 :
               a >= 0xE0 && a <= 0xEF ? 3 :
               a >= 0xF0 && a <= 0xF4 ? 4 : 0;
    if (!n || n > l->length - l->offset) return 0;

    for (size_t i = 1; i < n; ++i)
        if ((peek(l, i) & 0xC0) != 0x80) return 0;

    unsigned char b = peek(l, 1);
    if ((a == 0xE0 && b < 0xA0) || (a == 0xED && b >= 0xA0) ||
        (a == 0xF0 && b < 0x90) || (a == 0xF4 && b >= 0x90)) return 0;
    return n;
}

/* Mantiene exactamente la convención anterior de posiciones:
 * CRLF cuenta como un salto y un escalar UTF-8 válido como una columna.
 */
static void advance(Lexer *l) {
    unsigned char c = peek(l, 0);
    if (c == '\r') {
        ++l->offset;
        if (l->offset < l->length && l->source[l->offset] == '\n') ++l->offset;
        ++l->line;
        l->column = 1;
    } else if (c == '\n') {
        ++l->offset;
        ++l->line;
        l->column = 1;
    } else {
        size_t width = c >= 0x80 ? utf8_width(l) : 1;
        l->offset += width ? width : 1;
        ++l->column;
    }
}

static void advance_bytes(Lexer *l, size_t bytes) {
    size_t target = l->offset + bytes;
    while (l->offset < target) advance(l);
}

static Token oom_token(size_t line, size_t column) {
    Token t = {TOKEN_ERROR, TOKEN_OUT_OF_MEMORY, NULL, 0, line, column};
    return t;
}

static Token make_token(const Lexer *l, TokenType type, TokenError error,
                        size_t begin, size_t line, size_t column) {
    Token t = {type, error, NULL, l->offset - begin, line, column};
    if (t.length == SIZE_MAX) return oom_token(line, column);

    t.lexeme = malloc(t.length + 1);
    if (!t.lexeme) return oom_token(line, column);

    if (t.length) memcpy(t.lexeme, l->source + begin, t.length);
    t.lexeme[t.length] = '\0';
    return t;
}

void lexer_init(Lexer *l, const char *source, size_t length,
                const char *filename) {
    *l = (Lexer){source, filename, length, 0, 1, 1};
}

Token lexer_next(Lexer *l) {
    LexerFlexMatch match = {0};
    size_t remaining = l->length - l->offset;

    if (!lexer_flex_match(l->source + l->offset, remaining, &match))
        return oom_token(l->line, l->column);

    if (match.skip_length > remaining ||
        match.token_length > remaining - match.skip_length)
        return oom_token(l->line, l->column);

    advance_bytes(l, match.skip_length);
    size_t begin = l->offset;
    size_t line = l->line;
    size_t column = l->column;
    advance_bytes(l, match.token_length);

    return make_token(l, match.type, match.error, begin, line, column);
}
