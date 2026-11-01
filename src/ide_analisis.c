#include "ide_analisis.h"

#include "ide_mensajes.h"
#include "ide_nucleo.h"
#include "lexer.h"
#include "semantic_modules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Arma la ruta del archivo temporal: misma carpeta, nombre oculto.
 *
 *   "ejemplos/factorial.bal"  ->  "ejemplos/.ide_factorial.bal"
 *   NULL (sin guardar)        ->  ".ide_sin_nombre.bal"
 *
 * El punto al inicio del nombre lo oculta en Linux.
 * Devuelve memoria nueva (liberar con free), o NULL si no hay memoria.
 */
static char *ruta_temporal(const char *ruta)
{
    const char *nombre = (ruta != NULL) ? ruta : IDE_SIN_NOMBRE;

    /* Separar la carpeta ("ejemplos/") del nombre ("factorial.bal"). */
    const char *barra = strrchr(nombre, '/');
    size_t largo_carpeta = (barra != NULL) ? (size_t)(barra - nombre + 1) : 0;
    const char *base = nombre + largo_carpeta;

    const char *marca = ".ide_";
    size_t total = largo_carpeta + strlen(marca) + strlen(base) + 1;
    char *temporal = malloc(total);
    if (temporal == NULL) return NULL;

    /* carpeta + ".ide_" + nombre */
    memcpy(temporal, nombre, largo_carpeta);
    strcpy(temporal + largo_carpeta, marca);
    strcat(temporal, base);
    return temporal;
}

/* Escribe el texto completo en un archivo. Devuelve 0 si funcionó, -1 si no. */
static int escribir_archivo(const char *ruta, const char *texto, size_t largo)
{
    FILE *f = fopen(ruta, "wb");
    if (f == NULL) return -1;
    size_t escritos = fwrite(texto, 1, largo, f);
    int error_al_cerrar = fclose(f);
    return (escritos == largo && error_al_cerrar == 0) ? 0 : -1;
}

/*
 * Cambia el nombre del archivo en los errores: donde dice el archivo
 * temporal, pone el nombre original.
 * Devuelve 0 si funcionó, o -1 si no hubo memoria.
 */
static int cambiar_nombre_en_errores(DiagLista *lista, size_t desde,
                                     const char *temporal, const char *original)
{
    for (size_t i = desde; i < lista->cantidad; ++i) {
        Diagnostico *d = &lista->elementos[i];
        if (strcmp(d->archivo, temporal) != 0) continue;  /* Es de otro archivo (un módulo importado). */

        char *copia = malloc(strlen(original) + 1);
        if (copia == NULL) return -1;
        strcpy(copia, original);
        free(d->archivo);
        d->archivo = copia;
    }
    return 0;
}

/*
 * Busca qué estaba escrito en una posición del texto.
 *
 * Por ejemplo, si el parser dice que el error está en la línea 3, columna 5,
 * esta función devuelve la palabra que empieza ahí ("numero"). Se usa el
 * lexer para que la "palabra" sea un token completo, igual que lo ve el parser.
 *
 * Devuelve memoria nueva (liberar con free), o NULL si ahí no empieza
 * ningún token o si faltó memoria.
 */
static char *texto_en_posicion(const char *texto, size_t largo,
                               size_t linea, size_t columna)
{
    char *encontrado = NULL;

    Lexer lexer;
    lexer_init(&lexer, texto, largo, "<editor>");
    for (;;) {
        Token t = lexer_next(&lexer);
        TokenType tipo = t.type;
        int ya_paso = (t.line > linea || (t.line == linea && t.column > columna));

        if (t.line == linea && t.column == columna && t.lexeme != NULL &&
            tipo != TOKEN_EOF) {
            encontrado = malloc(t.length + 1);
            if (encontrado != NULL) memcpy(encontrado, t.lexeme, t.length + 1);
        }
        token_dispose(&t);

        /* Parar al encontrarlo, al pasarse de la posición o al final del texto. */
        if (encontrado != NULL || ya_paso || tipo == TOKEN_EOF) break;
    }
    return encontrado;
}

/*
 * Traduce a español los mensajes del parser (ver ide_mensajes.c).
 *
 *   "syntax error, unexpected IDENTIFIER"
 *     -> "no se esperaba el nombre 'numero' en este lugar"
 *
 * Solo cambia los errores sintácticos desde la posición "desde".
 * Devuelve 0 si funcionó, o -1 si no hubo memoria.
 */
static int traducir_errores_del_parser(DiagLista *lista, size_t desde,
                                       const char *texto, size_t largo,
                                       const char *nombre_original)
{
    for (size_t i = desde; i < lista->cantidad; ++i) {
        Diagnostico *d = &lista->elementos[i];
        if (d->fase != DIAG_SINTACTICO) continue;

        /* Lo escrito en el lugar del error solo se puede buscar si el error es
         * de este archivo (el texto de los módulos importados no lo tenemos). */
        char *encontrado = NULL;
        if (strcmp(d->archivo, nombre_original) == 0) {
            encontrado = texto_en_posicion(texto, largo, d->linea, d->columna);
        }

        char *traducido = ide_traducir_mensaje(d->mensaje, encontrado);
        free(encontrado);
        if (traducido == NULL) return -1;

        free(d->mensaje);
        d->mensaje = traducido;
    }
    return 0;
}

int ide_analizar_texto(const char *texto, size_t largo, const char *ruta,
                       DiagLista *lista)
{
    const char *nombre_original = (ruta != NULL) ? ruta : IDE_SIN_NOMBRE;
    size_t primero_nuevo = lista->cantidad;  /* Desde aquí empiezan los errores de esta revisión. */

    /* Paso 1: guardar el texto del editor en el archivo temporal. */
    char *temporal = ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (escribir_archivo(temporal, texto, largo) != 0) {
        free(temporal);
        return -1;
    }

    /*
     * Paso 2: analizar el archivo temporal con el compilador.
     * Los errores se escriben en "salida" (un archivo en memoria) en vez de
     * la pantalla, y luego se separan en sus partes con diag_leer_flujo.
     */
    int resultado = 0;
    FILE *salida = tmpfile();
    if (salida == NULL) {
        resultado = -1;
    } else {
        sem_analizar_proyecto(temporal, salida);
        rewind(salida);
        if (diag_leer_flujo(salida, lista) != 0) resultado = -1;
        fclose(salida);
    }

    /* Paso 3: el archivo temporal ya no hace falta. */
    remove(temporal);

    /* Paso 4: poner el nombre original en los errores. */
    if (resultado == 0 &&
        cambiar_nombre_en_errores(lista, primero_nuevo, temporal, nombre_original) != 0) {
        resultado = -1;
    }
    free(temporal);

    /* Paso 5: traducir los mensajes del parser a español claro. */
    if (resultado == 0 &&
        traducir_errores_del_parser(lista, primero_nuevo, texto, largo, nombre_original) != 0) {
        resultado = -1;
    }

    /*
     * Paso 6: revisar paréntesis, corchetes y llaves. El parser se detiene
     * en el primer error; esta revisión encuentra todos los delimitadores
     * mal cerrados y explica cada uno con más detalle.
     */
    if (resultado == 0 &&
        ide_revisar_delimitadores(texto, largo, nombre_original, lista) != 0) {
        resultado = -1;
    }

    return resultado;
}
