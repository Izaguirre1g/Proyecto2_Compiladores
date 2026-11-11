#include "ide_analisis.h"

#include "ide_mensajes.h"
#include "ide_nucleo.h"
#include "lexer.h"
#include "ll1.h"
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
char *ide_ruta_temporal(const char *ruta)
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
int ide_escribir_archivo(const char *ruta, const char *texto, size_t largo)
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

/*
 * Quita de la lista los errores léxicos y de sintaxis de este archivo que
 * puso Bison. Se reemplazan por los del LL(1), que son los mismos (los dos
 * parsers reconocen el mismo lenguaje) más todos los que vienen después.
 */
static void quitar_errores_de_sintaxis(DiagLista *lista, size_t desde, const char *archivo)
{
    size_t quedan = desde;
    for (size_t i = desde; i < lista->cantidad; ++i) {
        Diagnostico *d = &lista->elementos[i];
        if ((d->fase == DIAG_SINTACTICO || d->fase == DIAG_LEXICO) &&
            strcmp(d->archivo, archivo) == 0) {
            free(d->archivo);
            free(d->mensaje);
            continue;
        }
        lista->elementos[quedan++] = *d;
    }
    lista->cantidad = quedan;
}

/* El nombre que usa Bison para un token (el mismo de token_type_name, salvo el final). */
static const char *nombre_bison(int terminal)
{
    return terminal == TOKEN_EOF ? "end of file" : token_type_name((TokenType)terminal);
}

/*
 * TODOS los errores de sintaxis del archivo, no solo el primero.
 *
 * Bison se detiene en el primer error. Para mostrar todos, se analiza el
 * texto con el LL(1) con recuperación a nivel de frase
 * (ll1_analizar_con_recuperacion): en cada error se aplica la corrección
 * que predice la tabla y se sigue. Los errores léxicos y de sintaxis que
 * puso Bison se REEMPLAZAN por los del LL(1): el primero es el mismo, y
 * así dos errores en el mismo token (por ejemplo "wheter" mal escrita y,
 * ahí mismo, el ';' que faltaba antes) se muestran los dos. Cada error se
 * agrega así:
 *   - si el token es un símbolo inválido, como error léxico (Bison solo
 *     reporta el primero);
 *   - si es una palabra clave mal escrita ("whethr"), con un mensaje que
 *     dice cuál palabra parece;
 *   - si no, como error de sintaxis, con un mensaje en el mismo formato que
 *     el de Bison ("syntax error, unexpected X, expecting A or B"), y se
 *     traduce con ide_traducir_mensaje para que todos se lean igual.
 *
 * Solo se hace si Bison encontró un error en este archivo: si Bison no
 * encontró ninguno, el LL(1) tampoco (los dos reconocen el mismo lenguaje).
 *
 * En "explicados" queda 1 si, con las correcciones del LL(1), el programa
 * completo queda válido: entonces esos errores explican todo, incluidos
 * los paréntesis y llaves desbalanceados.
 */
static int agregar_demas_errores(const char *texto, size_t largo, const char *nombre,
                                 DiagLista *lista, size_t desde, int *explicados)
{
    *explicados = 0;
    int bison_fallo = 0;
    for (size_t i = desde; i < lista->cantidad; ++i) {
        const Diagnostico *d = &lista->elementos[i];
        if ((d->fase == DIAG_SINTACTICO || d->fase == DIAG_LEXICO) &&
            strcmp(d->archivo, nombre) == 0) {
            bison_fallo = 1;
        }
    }
    if (!bison_fallo) return 0;

    /* Los tokens del texto, con su posición y su tipo de error léxico. */
    typedef struct {
        TokenType tipo;
        TokenError error;
        size_t linea, columna;
        char *lexema;  /* Lo escrito, solo en los nombres (para las palabras mal escritas). */
    } TokenLeido;
    TokenLeido *leidos = NULL;
    TokenType *tipos = NULL;
    const char **lexemas = NULL;
    size_t cantidad = 0, capacidad = 0;
    int estado = 0;

    Lexer lexer;
    lexer_init(&lexer, texto, largo, nombre);
    for (;;) {
        Token t = lexer_next(&lexer);
        TokenLeido leido = {t.type, t.error, t.line, t.column, NULL};
        int sin_memoria = (t.error == TOKEN_OUT_OF_MEMORY);
        if (!sin_memoria && t.type == TOKEN_IDENTIFIER && t.lexeme != NULL) {
            leido.lexema = malloc(t.length + 1);  /* Copia propia: el token se libera ya. */
            if (leido.lexema == NULL) {
                sin_memoria = 1;
            } else {
                memcpy(leido.lexema, t.lexeme, t.length + 1);
            }
        }
        token_dispose(&t);
        if (sin_memoria) {
            estado = -1;
            goto fin;
        }
        if (cantidad == capacidad) {
            capacidad = (capacidad == 0) ? 256 : capacidad * 2;
            TokenLeido *mas = realloc(leidos, capacidad * sizeof *mas);
            if (mas == NULL) {
                free(leido.lexema);
                estado = -1;
                goto fin;
            }
            leidos = mas;
        }
        leidos[cantidad++] = leido;
        if (leido.tipo == TOKEN_EOF) break;
    }

    tipos = malloc(cantidad * sizeof *tipos);
    lexemas = malloc(cantidad * sizeof *lexemas);
    if (tipos == NULL || lexemas == NULL) {
        estado = -1;
        goto fin;
    }
    for (size_t i = 0; i < cantidad; ++i) {
        tipos[i] = leidos[i].tipo;
        lexemas[i] = leidos[i].lexema;
    }

    Ll1Errores errores;
    if (ll1_analizar_con_recuperacion(tipos, lexemas, cantidad, &errores) != 0) {
        estado = -1;
        goto fin;
    }
    *explicados = errores.completo;

    /* Si el LL(1) no encontró nada (no debería pasar), quedan los de Bison. */
    if (errores.cantidad > 0) quitar_errores_de_sintaxis(lista, desde, nombre);

    for (size_t i = 0; i < errores.cantidad && estado == 0; ++i) {
        const Ll1Error *e = &errores.elementos[i];
        const TokenLeido *t = &leidos[e->posicion];

        if (t->tipo == TOKEN_ERROR) {
            estado = diag_agregar(lista, DIAG_LEXICO, nombre, t->linea, t->columna,
                                  token_error_message(t->error));
            continue;
        }

        if (e->palabra_mal_escrita && t->lexema != NULL) {
            char aviso[256];
            snprintf(aviso, sizeof aviso,
                     "no se esperaba el nombre '%s' en este lugar; parece la palabra '%s' mal escrita",
                     t->lexema, ll1_texto(e->correccion.terminales[0]));
            estado = diag_agregar(lista, DIAG_SINTACTICO, nombre, t->linea, t->columna, aviso);
            continue;
        }

        /* A una operación le falta un operando: el error se marca en el
         * operador. No tiene arreglo automático (no se sabe qué valor iba). */
        if (e->falta_operando != LL1_SIN_FALTA) {
            char aviso[256];
            if (e->operador == TOKEN_NOT) {
                snprintf(aviso, sizeof aviso, "a '%s' le falta su operando", ll1_texto(e->operador));
            } else {
                snprintf(aviso, sizeof aviso, "a '%s' le falta el operando de la %s",
                         ll1_texto(e->operador),
                         e->falta_operando == LL1_FALTA_DERECHO ? "derecha" : "izquierda");
            }
            estado = diag_agregar(lista, DIAG_SINTACTICO, nombre, t->linea, t->columna, aviso);
            continue;
        }

        /* Mensaje con el formato de Bison. Bison enumera lo esperado solo
         * si son 4 o menos; se hace igual. */
        char mensaje[512];
        /* El token que el parser encontró: normalmente el del archivo, pero
         * puede ser uno que una corrección anterior cambió ("wheter" ya
         * cambiado por "whether"). */
        int encontrado_tipo = e->encontrado;
        int largo_mensaje = snprintf(mensaje, sizeof mensaje, "syntax error, unexpected %s",
                                     nombre_bison(encontrado_tipo));
        int esperados = 0;
        for (int k = 0; k < LL1_TERMINALES; ++k) esperados += e->esperados.tiene[k];
        if (e->tiene_correccion && e->correccion.predicha) {
            /* La tabla PREDICE qué falta (en LL(1) es único): se dice eso. */
            largo_mensaje += snprintf(mensaje + largo_mensaje, sizeof mensaje - (size_t)largo_mensaje,
                                      ", expecting %s", nombre_bison(e->correccion.terminales[0]));
        } else if (esperados > 0 && esperados <= 4) {
            const char *union_ = ", expecting ";
            for (int k = 0; k < LL1_TERMINALES && largo_mensaje < (int)sizeof mensaje; ++k) {
                if (!e->esperados.tiene[k]) continue;
                largo_mensaje += snprintf(mensaje + largo_mensaje, sizeof mensaje - (size_t)largo_mensaje,
                                          "%s%s", union_, nombre_bison(k));
                union_ = " or ";
            }
        }

        /* Lo escrito en el archivo solo sirve si es ese mismo token. */
        char *encontrado = (encontrado_tipo == (int)t->tipo)
                               ? texto_en_posicion(texto, largo, t->linea, t->columna)
                               : NULL;
        char *traducido = ide_traducir_mensaje(mensaje, encontrado);
        free(encontrado);
        if (traducido == NULL) {
            estado = -1;
            break;
        }
        estado = diag_agregar(lista, DIAG_SINTACTICO, nombre, t->linea, t->columna, traducido);
        free(traducido);
    }
    ll1_liberar_errores(&errores);

fin:
    for (size_t i = 0; i < cantidad; ++i) free(leidos[i].lexema);
    free(leidos);
    free(tipos);
    free(lexemas);
    return estado;
}

/*
 * Ordena los errores nuevos como en un editor: primero los de otros
 * archivos (módulos importados), y los de este archivo por línea y columna.
 * Es un ordenamiento por inserción: estable y suficiente para pocos errores.
 */
static void ordenar_errores(DiagLista *lista, size_t desde, const char *nombre)
{
    for (size_t i = desde + 1; i < lista->cantidad; ++i) {
        Diagnostico actual = lista->elementos[i];
        int actual_propio = strcmp(actual.archivo, nombre) == 0;
        size_t j = i;
        while (j > desde) {
            const Diagnostico *previo = &lista->elementos[j - 1];
            int previo_propio = strcmp(previo->archivo, nombre) == 0;
            int va_antes = (actual_propio != previo_propio)
                               ? !actual_propio
                               : (actual.linea < previo->linea ||
                                  (actual.linea == previo->linea && actual.columna < previo->columna));
            if (!va_antes) break;
            lista->elementos[j] = lista->elementos[j - 1];
            j--;
        }
        lista->elementos[j] = actual;
    }
}

int ide_analizar_texto(const char *texto, size_t largo, const char *ruta,
                       DiagLista *lista)
{
    const char *nombre_original = (ruta != NULL) ? ruta : IDE_SIN_NOMBRE;
    size_t primero_nuevo = lista->cantidad;  /* Desde aquí empiezan los errores de esta revisión. */

    /* Paso 1: guardar el texto del editor en el archivo temporal. */
    char *temporal = ide_ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (ide_escribir_archivo(temporal, texto, largo) != 0) {
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

    /* Paso 5b: el parser se detiene en el primer error; los siguientes los
     * encuentra el LL(1) con recuperación a nivel de frase. */
    int explicados = 0;
    if (resultado == 0 &&
        agregar_demas_errores(texto, largo, nombre_original, lista, primero_nuevo,
                              &explicados) != 0) {
        resultado = -1;
    }

    /*
     * Paso 6: revisar paréntesis, corchetes y llaves, contándolos en el
     * texto. Solo hace falta si el LL(1) no explicó ya todos los errores:
     * si con sus correcciones el programa queda válido, los delimitadores
     * también quedan balanceados, y esta revisión solo repetiría lo mismo
     * con un arreglo que podría contradecirlo. Por ejemplo, si falta un '{',
     * esta revisión diría que sobra el último '}', y "borrarlo" junto con
     * "agregar '{'" dañaría el código.
     */
    if (resultado == 0 && !explicados &&
        ide_revisar_delimitadores(texto, largo, nombre_original, lista) != 0) {
        resultado = -1;
    }

    /* Paso 7: dejar los errores en orden de línea, como en un editor. */
    if (resultado == 0) ordenar_errores(lista, primero_nuevo, nombre_original);

    return resultado;
}
