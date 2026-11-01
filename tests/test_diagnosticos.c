/*
 * Pruebas del módulo de diagnósticos (src/diagnosticos.c).
 *
 * Le damos líneas de error escritas exactamente como las escribe el
 * compilador, y revisamos que cada parte (fase, archivo, línea, columna
 * y mensaje) quede bien separada. También revisamos el JSON final.
 *
 * Se ejecuta con: make test-diagnosticos
 */
#include "diagnosticos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cuántas verificaciones fallaron. */
static int fallos = 0;

/*
 * Si la condición es falsa, muestra en qué línea de este archivo falló
 * y cuenta el fallo. Las pruebas siguen corriendo para ver todos los fallos.
 */
#define VERIFICAR(condicion) do { \
    if (!(condicion)) { \
        fprintf(stderr, "FALLO en la linea %d: %s\n", __LINE__, #condicion); \
        fallos++; \
    } \
} while (0)

/*
 * Revisa que una línea SÍ se reconozca como error y que sus partes
 * sean las esperadas.
 */
static void probar_error(const char *linea, DiagFase fase, const char *archivo,
                         size_t num_linea, size_t num_columna, const char *mensaje)
{
    Diagnostico d;
    int resultado = diag_interpretar_linea(linea, &d);

    VERIFICAR(resultado == 1);
    if (resultado != 1) return;

    VERIFICAR(d.fase == fase);
    VERIFICAR(strcmp(d.archivo, archivo) == 0);
    VERIFICAR(d.linea == num_linea);
    VERIFICAR(d.columna == num_columna);
    VERIFICAR(strcmp(d.mensaje, mensaje) == 0);

    free(d.archivo);
    free(d.mensaje);
}

int main(void)
{
    /* Prueba 1: un error de cada fase, con el texto real que escribe cada una. */
    probar_error("a.bal:2:25: error léxico: carácter no permitido",
                 DIAG_LEXICO, "a.bal", 2, 25, "carácter no permitido");
    probar_error("a.bal:1:1: error del parser: syntax error, unexpected MAIN",
                 DIAG_SINTACTICO, "a.bal", 1, 1, "syntax error, unexpected MAIN");
    probar_error("dir/b.bal:3:9: error semantico: identificador 'y' no declarado",
                 DIAG_SEMANTICO, "dir/b.bal", 3, 9, "identificador 'y' no declarado");

    /* Prueba 2: errores sin posición. La línea y la columna deben quedar en 0. */
    probar_error("x.bal: error: no se pudo abrir el archivo principal",
                 DIAG_GENERAL, "x.bal", 0, 0, "no se pudo abrir el archivo principal");
    probar_error("x.bal: error semantico: demasiados niveles de importacion",
                 DIAG_SEMANTICO, "x.bal", 0, 0, "demasiados niveles de importacion");

    /* Prueba 3: una ruta de Windows también tiene ':', pero solo los dos
     * últimos números son la línea y la columna. */
    probar_error("C:\\p\\a.bal:4:2: error semantico: m",
                 DIAG_SEMANTICO, "C:\\p\\a.bal", 4, 2, "m");

    /* Prueba 4: las líneas que no son errores se deben ignorar. */
    Diagnostico d;
    VERIFICAR(diag_interpretar_linea("[OK] Integridad de tabla: 2 simbolos", &d) == 0);
    VERIFICAR(diag_interpretar_linea("no se pudo generar el ensamblador", &d) == 0);

    /* Prueba 5: leer varias líneas de un archivo y escribir el JSON. */
    FILE *entrada = tmpfile();
    VERIFICAR(entrada != NULL);
    if (entrada != NULL) {
        /* La primera línea termina en "\r\n", como en Windows. */
        fputs("a.bal:3:5: error semantico: dice \"hola\"\r\n"
              "[OK] Integridad de tabla: 2 simbolos, sin offsets\n"
              "a.bal:4:1: error semantico: segundo\n", entrada);
        rewind(entrada);

        DiagLista lista = {0};
        VERIFICAR(diag_leer_flujo(entrada, &lista) == 0);
        VERIFICAR(lista.cantidad == 2);  /* La línea [OK] no cuenta. */
        if (lista.cantidad == 2) {
            VERIFICAR(strcmp(lista.elementos[0].mensaje, "dice \"hola\"") == 0);
            VERIFICAR(lista.elementos[1].linea == 4);
        }
        fclose(entrada);

        /* En el JSON, las comillas del mensaje deben quedar escapadas: \" */
        FILE *json = tmpfile();
        VERIFICAR(json != NULL);
        if (json != NULL) {
            VERIFICAR(diag_escribir_json("a.bal", &lista, json) == 0);
            rewind(json);

            char texto[1024] = {0};
            size_t leidos = fread(texto, 1, sizeof texto - 1, json);
            texto[leidos] = '\0';

            VERIFICAR(strstr(texto, "\"errores\": 2") != NULL);
            VERIFICAR(strstr(texto, "\"mensaje\": \"dice \\\"hola\\\"\"") != NULL);
            fclose(json);
        }

        /* Después de liberar, la lista debe quedar vacía. */
        diag_liberar(&lista);
        VERIFICAR(lista.cantidad == 0 && lista.elementos == NULL);
    }

    if (fallos > 0) {
        fprintf(stderr, "test_diagnosticos: %d fallos\n", fallos);
        return 1;
    }
    puts("test_diagnosticos: OK");
    return 0;
}
