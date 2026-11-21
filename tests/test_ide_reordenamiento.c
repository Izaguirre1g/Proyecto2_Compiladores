/*
 * Pruebas del botón "Reordenamiento" del IDE (src/IDE/ide_reordenamiento.c),
 * sin abrir ventanas. Usa los ejemplos de Meibel (reordenamiento/pruebas/)
 * y revisa que el IDE dé los mismos números que "balc --reorder".
 *
 * Se ejecuta con: make test-ide-reordenamiento
 */
#include "ide_reordenamiento.h"

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

/* Lee un archivo completo (NULL si no se pudo). */
static char *leer_archivo(const char *ruta, size_t *largo)
{
    FILE *f = fopen(ruta, "rb");
    if (f == NULL) return NULL;
    char *texto = malloc(1 << 16);
    if (texto != NULL) {
        *largo = fread(texto, 1, (1 << 16) - 1, f);
        texto[*largo] = '\0';
    }
    fclose(f);
    return texto;
}

/* Reordena un ejemplo de Meibel. */
static int analizar(const char *ruta, unsigned latencia, int desenrollar, IdeReordenamiento *r)
{
    size_t largo = 0;
    char *texto = leer_archivo(ruta, &largo);
    if (texto == NULL) return -1;
    int estado = ide_reordenamiento_analizar(texto, largo, ruta, latencia, desenrollar, r);
    free(texto);
    return estado;
}

/* Cuántas líneas de un tipo hay. */
static size_t contar(const IdeRoTexto *t, char tipo)
{
    size_t n = 0;
    for (size_t i = 0; i < t->cantidad; ++i) n += t->lineas[i].tipo == tipo;
    return n;
}

/* 1 si alguna línea de ese tipo contiene "buscado". */
static int contiene(const IdeRoTexto *t, char tipo, const char *buscado)
{
    for (size_t i = 0; i < t->cantidad; ++i)
        if (t->lineas[i].tipo == tipo && strstr(t->lineas[i].texto, buscado) != NULL) return 1;
    return 0;
}

int main(void)
{
    IdeReordenamiento r;

    /* 01 (WAR): 3 instrucciones movidas, 16 -> 13 ciclos con latencia 3. */
    VERIFICAR(analizar("reordenamiento/pruebas/01_war.bal", 0, 0, &r) == 0);
    VERIFICAR(r.estado == IDE_RO_LISTO && r.latencia == 3 && !r.desenrollado);
    VERIFICAR(r.resumen.movidas == 3 && r.resumen.instrucciones == 11);
    VERIFICAR(r.resumen.ciclos_antes == 16 && r.resumen.ciclos_despues == 13);
    VERIFICAR(r.resumen.raw == 7 && r.resumen.war == 1 && r.resumen.waw == 1);
    VERIFICAR(r.resumen.bloques_cambiados == 1);
    /* Las movidas se marcan con '*' en las dos fotos, y son las mismas 3. */
    VERIFICAR(contar(&r.antes, 'M') == 3 && contar(&r.despues, 'M') == 3);
    /* La foto de después conserva el número original: "a = 20" (la 0005)
     * subió al lugar 4. */
    VERIFICAR(contiene(&r.despues, 'M', "0005  a = 20"));
    VERIFICAR(contiene(&r.despues, 'B', "16 → 13 ciclos"));
    /* La dependencia WAR por "a": la lectura de a (0003) va antes de a = 20 (0005). */
    VERIFICAR(contiene(&r.dependencias, 'E', "0003") || contiene(&r.dependencias, 'C', "0003"));
    VERIFICAR(contiene(&r.dependencias, 'E', "WAR por a") || contiene(&r.dependencias, 'C', "WAR por a"));
    VERIFICAR(contar(&r.dependencias, 'R') == 1);
    ide_reordenamiento_liberar(&r);

    /* Con latencia 1 no hay que esperar a nadie: nada se mueve. */
    VERIFICAR(analizar("reordenamiento/pruebas/01_war.bal", 1, 0, &r) == 0);
    VERIFICAR(r.latencia == 1 && r.resumen.movidas == 0 && r.resumen.ciclos_antes == 9);
    VERIFICAR(contar(&r.despues, 'M') == 0 && r.dependencias.cantidad == 0);
    ide_reordenamiento_liberar(&r);

    /* Con latencia 5: 24 -> 21 ciclos (como balc --reorder=5). */
    VERIFICAR(analizar("reordenamiento/pruebas/01_war.bal", 5, 0, &r) == 0);
    VERIFICAR(r.resumen.ciclos_antes == 24 && r.resumen.ciclos_despues == 21);
    ide_reordenamiento_liberar(&r);

    /* 05: las llamadas son barreras (dependencias de efecto). */
    VERIFICAR(analizar("reordenamiento/pruebas/05_llamadas.bal", 0, 0, &r) == 0);
    VERIFICAR(r.resumen.movidas == 4 && r.resumen.efecto == 23);
    VERIFICAR(r.resumen.ciclos_antes == 43 && r.resumen.ciclos_despues == 38);
    ide_reordenamiento_liberar(&r);

    /* 06 con loop unrolling primero (balc --unroll --reorder): 161 -> 128. */
    VERIFICAR(analizar("reordenamiento/pruebas/06_ciclo_desenrollado.bal", 0, 1, &r) == 0);
    VERIFICAR(r.desenrollado && r.resumen.instrucciones == 96 && r.resumen.movidas == 34);
    VERIFICAR(r.resumen.ciclos_antes == 161 && r.resumen.ciclos_despues == 128);
    ide_reordenamiento_liberar(&r);

    /* Sin unrolling, el mismo archivo: 50 -> 43. */
    VERIFICAR(analizar("reordenamiento/pruebas/06_ciclo_desenrollado.bal", 0, 0, &r) == 0);
    VERIFICAR(r.resumen.ciclos_antes == 50 && r.resumen.ciclos_despues == 43);
    ide_reordenamiento_liberar(&r);

    /* Un código con errores no se analiza. */
    const char *con_errores = "create_funk #declare_infinite_void# main() {\n    x : 5;\n}\n";
    VERIFICAR(ide_reordenamiento_analizar(con_errores, strlen(con_errores),
                                          "reordenamiento/pruebas/editor.bal", 0, 0, &r) == 0);
    VERIFICAR(r.estado == IDE_RO_CON_ERRORES);
    ide_reordenamiento_liberar(&r);

    /* Nombres de los tipos de una dependencia. */
    char tipos[64];
    ide_reordenamiento_tipos(RO_DEP_RAW | RO_DEP_MEMORIA, tipos, sizeof tipos);
    VERIFICAR(strcmp(tipos, "RAW + memoria") == 0);

    if (fallos > 0) {
        fprintf(stderr, "test_ide_reordenamiento: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_reordenamiento: OK");
    return 0;
}
