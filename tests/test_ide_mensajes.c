/*
 * Pruebas de la traducción de mensajes del parser (src/ide_mensajes.c).
 *
 * Los mensajes de entrada son copias exactas de lo que escribe Bison con
 * los ejemplos reales del lenguaje BAL.
 *
 * Se ejecuta con: make test-ide-mensajes
 */
#include "ide_mensajes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cuántas verificaciones fallaron. */
static int fallos = 0;

/* Si la condición es falsa, muestra la línea que falló y cuenta el fallo. */
#define VERIFICAR(condicion) do { \
    if (!(condicion)) { \
        fprintf(stderr, "FALLO en la linea %d: %s\n", __LINE__, #condicion); \
        fallos++; \
    } \
} while (0)

/*
 * Traduce "mensaje" y revisa que el resultado sea exactamente "esperado".
 * Si no coincide, muestra los dos textos para ver la diferencia.
 */
static void revisar(int linea, const char *mensaje, const char *encontrado,
                    const char *esperado)
{
    char *obtenido = ide_traducir_mensaje(mensaje, encontrado);
    if (obtenido == NULL || strcmp(obtenido, esperado) != 0) {
        fprintf(stderr, "FALLO en la linea %d:\n  esperado: %s\n  obtenido: %s\n",
                linea, esperado, obtenido ? obtenido : "(NULL)");
        fallos++;
    }
    free(obtenido);
}

int main(void)
{
    /* 1. Falta un ';': el parser tropieza con el nombre de la línea siguiente. */
    revisar(__LINE__, "syntax error, unexpected IDENTIFIER", "numero",
            "no se esperaba el nombre 'numero' en este lugar");

    /* 2. Sin saber qué estaba escrito, se usa la descripción general. */
    revisar(__LINE__, "syntax error, unexpected IDENTIFIER", NULL,
            "no se esperaba un nombre en este lugar");

    /* 3. Un signo: siempre se escribe igual, no hace falta "encontrado". */
    revisar(__LINE__, "syntax error, unexpected SEMICOLON", ";",
            "no se esperaba ';' en este lugar");

    /* 4. Con la lista de lo que se esperaba (dos opciones). */
    revisar(__LINE__, "syntax error, unexpected LBRACE, expecting IDENTIFIER or RPAREN", "{",
            "no se esperaba '{' en este lugar; se esperaba un nombre o ')'");

    /* 5. Con tres opciones: comas y una "o" antes de la última. */
    revisar(__LINE__,
            "syntax error, unexpected MAIN, expecting BRING or DECLARE_CONST or CREATE_FUNK",
            "main",
            "no se esperaba la palabra 'main' en este lugar; se esperaba la palabra 'bring', "
            "la palabra 'declare_const' o la palabra 'create_funk'");

    /* 6. Un número. */
    revisar(__LINE__, "syntax error, unexpected INTEGER", "2",
            "no se esperaba el número '2' en este lugar");

    /* 7. Un texto: ya trae sus comillas, no se le ponen otras. */
    revisar(__LINE__, "syntax error, unexpected STRING", "\"hola\"",
            "no se esperaba el texto \"hola\" en este lugar");

    /* 8. Un texto muy largo no se copia entero: se usa "un texto". */
    revisar(__LINE__, "syntax error, unexpected STRING",
            "\"este texto es demasiado largo para el mensaje\"",
            "no se esperaba un texto en este lugar");

    /* 9. Se acabó el archivo: casi siempre falta cerrar algo. */
    revisar(__LINE__, "syntax error, unexpected end of file", NULL,
            "el archivo termina antes de tiempo: puede faltar una '}' o un ';'");

    /* 10. Se acabó el archivo, pero Bison sí dice qué faltaba. */
    revisar(__LINE__, "syntax error, unexpected end of file, expecting RBRACE", NULL,
            "el archivo termina antes de tiempo; se esperaba '}'");

    /* 11. Un operador del lenguaje con su significado. */
    revisar(__LINE__, "syntax error, unexpected GAUSS", "gauss",
            "no se esperaba la palabra 'gauss' (suma) en este lugar");

    /* 12. Un token que la tabla no conoce: se deja su nombre. */
    revisar(__LINE__, "syntax error, unexpected RARO", NULL,
            "no se esperaba 'RARO' en este lugar");

    /* 13. Bison sin detalles. */
    revisar(__LINE__, "syntax error", NULL,
            "esta parte del código no sigue las reglas del lenguaje");

    /* 14. Bison sin memoria. */
    revisar(__LINE__, "memory exhausted", NULL,
            "no hay memoria suficiente para revisar el archivo");

    /* 15. Un mensaje que ya está en español (del semántico, por ejemplo) no cambia. */
    revisar(__LINE__, "variable 'x' no declarada", NULL,
            "variable 'x' no declarada");

    /* 16. La descripción de tokens sueltos. */
    VERIFICAR(strcmp(ide_describir_token("SEMICOLON"), "';'") == 0);
    VERIFICAR(strcmp(ide_describir_token("WHETHER"), "la palabra 'whether'") == 0);
    VERIFICAR(strcmp(ide_describir_token("end of file"), "el final del archivo") == 0);
    VERIFICAR(ide_describir_token("NO_EXISTE") == NULL);

    if (fallos > 0) {
        fprintf(stderr, "test_ide_mensajes: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_mensajes: OK");
    return 0;
}
