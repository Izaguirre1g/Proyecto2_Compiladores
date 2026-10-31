#include "diagnosticos.h"

#include <stdlib.h>
#include <string.h>

/*
 * Cada parte del compilador escribe sus errores con un texto diferente
 * justo después de "archivo:línea:columna: ". Esta tabla dice qué texto
 * corresponde a cada fase.
 *
 * Si algún día una fase cambia la forma en que escribe sus errores,
 * solo hay que actualizar esta tabla.
 */
static const struct {
    const char *prefijo;  /* El texto que escribe la fase. */
    DiagFase fase;        /* La fase a la que corresponde. */
} PREFIJOS[] = {
    {"error léxico: ", DIAG_LEXICO},          /* Lo escribe puente_lexer.c */
    {"error del parser: ", DIAG_SINTACTICO},  /* Lo escribe yyerror() en puente_lexer.c */
    {"error semantico: ", DIAG_SEMANTICO},    /* Lo escriben semantic.c y semantic_modules.c */
    {"error: ", DIAG_GENERAL},                /* Por ejemplo, cuando el archivo no existe */
};

/* Cantidad de filas que tiene la tabla PREFIJOS. */
#define CANTIDAD_PREFIJOS (sizeof PREFIJOS / sizeof PREFIJOS[0])

const char *diag_fase_nombre(DiagFase fase)
{
    switch (fase) {
        case DIAG_LEXICO: return "lexico";
        case DIAG_SINTACTICO: return "sintactico";
        case DIAG_SEMANTICO: return "semantico";
        case DIAG_GENERAL: return "general";
    }
    return "general";
}

/*
 * Crea una copia nueva de los primeros "n" caracteres de un texto.
 * Devuelve NULL si no hay memoria.
 */
static char *copiar_texto(const char *texto, size_t n)
{
    char *copia = malloc(n + 1);  /* +1 para el '\0' que marca el final. */
    if (copia == NULL) return NULL;
    memcpy(copia, texto, n);
    copia[n] = '\0';
    return copia;
}

/*
 * Lee un número escrito justo antes de la posición "*fin", caminando
 * hacia la izquierda.
 *
 * Ejemplo: en "a.bal:3:5" con *fin apuntando al final, lee el 5.
 *
 * Si encuentra un número, guarda su valor en *valor, mueve *fin al primer
 * dígito del número y devuelve 1. Si no hay número, devuelve 0.
 */
static int leer_numero_hacia_atras(const char *inicio, const char **fin, size_t *valor)
{
    const char *p = *fin;

    /* Retroceder mientras haya dígitos. */
    while (p > inicio && p[-1] >= '0' && p[-1] <= '9') {
        --p;
    }
    if (p == *fin) return 0;  /* No había ningún dígito. */

    /* Convertir los dígitos a número, de izquierda a derecha. */
    size_t numero = 0;
    for (const char *d = p; d < *fin; ++d) {
        numero = numero * 10 + (size_t)(*d - '0');
    }

    *valor = numero;
    *fin = p;
    return 1;
}

int diag_interpretar_linea(const char *linea, Diagnostico *salida)
{
    /*
     * Paso 1: buscar dónde empieza la palabra "error".
     *
     * Buscamos ": error" en lugar de separar por ':' porque el nombre del
     * archivo también puede tener ':' (por ejemplo "C:\proyecto\a.bal").
     * Si la línea no tiene ": error", no es un error y la ignoramos.
     */
    const char *marca = strstr(linea, ": error");
    if (marca == NULL) return 0;

    const char *derecha = marca + 2;  /* Saltamos ": " y quedamos en "error...". */

    /* Paso 2: averiguar qué fase escribió el error, comparando con la tabla. */
    size_t i;
    for (i = 0; i < CANTIDAD_PREFIJOS; ++i) {
        size_t largo = strlen(PREFIJOS[i].prefijo);
        if (strncmp(derecha, PREFIJOS[i].prefijo, largo) == 0) {
            derecha += largo;  /* Ahora "derecha" apunta solo al mensaje. */
            break;
        }
    }
    if (i == CANTIDAD_PREFIJOS) return 0;  /* No reconocemos el formato: lo ignoramos. */

    /*
     * Paso 3: separar el archivo, la línea y la columna.
     *
     * Lo que está a la izquierda de ": error" puede ser:
     *   "archivo:línea:columna"   (lo normal)
     *   "archivo"                 (cuando el error no tiene posición)
     *
     * Leemos de derecha a izquierda: primero la columna, luego la línea.
     * Si no encontramos los dos números, todo es el nombre del archivo.
     */
    const char *fin_archivo = marca;
    size_t numero_linea = 0;
    size_t numero_columna = 0;

    const char *p = marca;
    size_t columna, fila;
    if (leer_numero_hacia_atras(linea, &p, &columna) && p > linea && p[-1] == ':') {
        --p;  /* Saltar el ':' que separa la línea de la columna. */
        if (leer_numero_hacia_atras(linea, &p, &fila) && p > linea && p[-1] == ':') {
            fin_archivo = p - 1;  /* El archivo termina antes de este ':'. */
            numero_linea = fila;
            numero_columna = columna;
        }
    }

    /* Paso 4: guardar una copia de cada parte en el diagnóstico. */
    Diagnostico d;
    d.fase = PREFIJOS[i].fase;
    d.linea = numero_linea;
    d.columna = numero_columna;
    d.archivo = copiar_texto(linea, (size_t)(fin_archivo - linea));
    d.mensaje = copiar_texto(derecha, strlen(derecha));

    if (d.archivo == NULL || d.mensaje == NULL) {  /* Faltó memoria. */
        free(d.archivo);
        free(d.mensaje);
        return -1;
    }

    *salida = d;
    return 1;
}

/*
 * Agrega un diagnóstico al final de la lista.
 * Si la lista está llena, pide el doble de espacio.
 * Devuelve 0 si funcionó, o -1 si no hubo memoria.
 */
static int agregar_a_lista(DiagLista *lista, Diagnostico d)
{
    if (lista->cantidad == lista->capacidad) {
        size_t nueva_capacidad = (lista->capacidad == 0) ? 8 : lista->capacidad * 2;
        Diagnostico *nuevo = realloc(lista->elementos, nueva_capacidad * sizeof *nuevo);
        if (nuevo == NULL) return -1;
        lista->elementos = nuevo;
        lista->capacidad = nueva_capacidad;
    }
    lista->elementos[lista->cantidad] = d;
    lista->cantidad++;
    return 0;
}

/*
 * Lee una línea completa, sin importar qué tan larga sea.
 *
 * Devuelve el texto de la línea (sin el salto de línea del final),
 * o NULL cuando ya no hay más líneas.
 * Si devuelve NULL porque faltó memoria, pone *sin_memoria en 1.
 *
 * Nota: la función getline() haría esto, pero no es parte de C11,
 * por eso la escribimos nosotros.
 */
static char *leer_linea(FILE *flujo, int *sin_memoria)
{
    size_t capacidad = 128;
    size_t largo = 0;
    char *texto = malloc(capacidad);

    *sin_memoria = (texto == NULL);
    if (texto == NULL) return NULL;

    int c;
    while ((c = fgetc(flujo)) != EOF && c != '\n') {
        /* Si se llenó el espacio, pedir el doble. */
        if (largo + 1 == capacidad) {
            char *mas = realloc(texto, capacidad * 2);
            if (mas == NULL) {
                free(texto);
                *sin_memoria = 1;
                return NULL;
            }
            texto = mas;
            capacidad *= 2;
        }
        texto[largo] = (char)c;
        largo++;
    }

    /* Si el archivo terminó y no leímos nada, ya no hay más líneas. */
    if (c == EOF && largo == 0) {
        free(texto);
        return NULL;
    }

    /* En Windows las líneas terminan en "\r\n": quitamos también el '\r'. */
    if (largo > 0 && texto[largo - 1] == '\r') {
        largo--;
    }

    texto[largo] = '\0';
    return texto;
}

int diag_leer_flujo(FILE *flujo, DiagLista *lista)
{
    int sin_memoria = 0;
    char *linea;

    while ((linea = leer_linea(flujo, &sin_memoria)) != NULL) {
        Diagnostico d;
        int resultado = diag_interpretar_linea(linea, &d);
        free(linea);

        if (resultado == -1) return -1;  /* Faltó memoria. */

        /* Solo guardamos las líneas que sí eran errores. */
        if (resultado == 1 && agregar_a_lista(lista, d) != 0) {
            free(d.archivo);
            free(d.mensaje);
            return -1;
        }
    }

    return sin_memoria ? -1 : 0;
}

/*
 * Escribe un texto como cadena JSON: entre comillas y con los caracteres
 * especiales "escapados" (por ejemplo, una comilla " se escribe \").
 *
 * Las letras con tilde y la ñ se escriben tal cual, porque JSON acepta UTF-8.
 */
static void escribir_texto_json(const char *texto, FILE *salida)
{
    fputc('"', salida);
    for (const unsigned char *p = (const unsigned char *)texto; *p != '\0'; ++p) {
        if (*p == '"') {
            fputs("\\\"", salida);       /* Comilla */
        } else if (*p == '\\') {
            fputs("\\\\", salida);       /* Barra invertida */
        } else if (*p == '\n') {
            fputs("\\n", salida);        /* Salto de línea */
        } else if (*p == '\t') {
            fputs("\\t", salida);        /* Tabulador */
        } else if (*p < 0x20) {
            fprintf(salida, "\\u%04x", *p);  /* Otros caracteres invisibles */
        } else {
            fputc(*p, salida);           /* Cualquier otro carácter, tal cual */
        }
    }
    fputc('"', salida);
}

int diag_escribir_json(const char *archivo_principal, const DiagLista *lista,
                       FILE *salida)
{
    /* Encabezado: el archivo revisado y cuántos errores tiene. */
    fputs("{\"archivo\": ", salida);
    escribir_texto_json(archivo_principal, salida);
    fprintf(salida, ", \"errores\": %zu, \"diagnosticos\": [", lista->cantidad);

    /* Un objeto JSON por cada error, separados por comas. */
    for (size_t i = 0; i < lista->cantidad; ++i) {
        const Diagnostico *d = &lista->elementos[i];

        if (i > 0) fputc(',', salida);
        fputs("\n  {", salida);

        fprintf(salida, "\"fase\": \"%s\", ", diag_fase_nombre(d->fase));
        fputs("\"archivo\": ", salida);
        escribir_texto_json(d->archivo, salida);
        fprintf(salida, ", \"linea\": %zu, \"columna\": %zu, ", d->linea, d->columna);
        fputs("\"mensaje\": ", salida);
        escribir_texto_json(d->mensaje, salida);

        fputc('}', salida);
    }

    /* Cerrar la lista y el objeto principal. */
    if (lista->cantidad > 0) fputc('\n', salida);
    fputs("]}\n", salida);

    return ferror(salida) ? -1 : 0;
}

void diag_liberar(DiagLista *lista)
{
    for (size_t i = 0; i < lista->cantidad; ++i) {
        free(lista->elementos[i].archivo);
        free(lista->elementos[i].mensaje);
    }
    free(lista->elementos);

    lista->elementos = NULL;
    lista->cantidad = 0;
    lista->capacidad = 0;
}
