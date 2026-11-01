#include "ide_analisis.h"

#include "ide_nucleo.h"
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

    /*
     * Paso 5: revisar paréntesis, corchetes y llaves. El parser se detiene
     * en el primer error; esta revisión encuentra todos los delimitadores
     * mal cerrados y explica cada uno con más detalle.
     */
    if (resultado == 0 &&
        ide_revisar_delimitadores(texto, largo, nombre_original, lista) != 0) {
        resultado = -1;
    }

    return resultado;
}
