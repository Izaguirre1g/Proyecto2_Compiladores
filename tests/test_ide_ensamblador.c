/*
 * Pruebas del botón "Ensamblador" del IDE (src/IDE/ide_ensamblador.c), sin
 * abrir ventanas.
 *
 * La prueba principal: el ensamblador que muestra el IDE debe ser IDÉNTICO
 * al que genera "balc -s" para el mismo programa. Se compara con los .asm
 * que están en examples/ (generados con balc -s).
 *
 * Se ejecuta con: make test-ide-ensamblador
 */
#include "ide_ensamblador.h"

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
    char *texto = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n >= 0 && fseek(f, 0, SEEK_SET) == 0 && (texto = malloc((size_t)n + 1)) != NULL) {
            *largo = fread(texto, 1, (size_t)n, f);
            texto[*largo] = '\0';
        }
    }
    fclose(f);
    return texto;
}

/* El IDE con "fuente" abierto debe dar exactamente el .asm de balc -s. */
static void comparar_con_balc(const char *fuente, const char *asm_balc)
{
    size_t largo = 0, largo_asm = 0;
    char *texto = leer_archivo(fuente, &largo);
    char *esperado = leer_archivo(asm_balc, &largo_asm);
    VERIFICAR(texto != NULL && esperado != NULL);
    if (texto == NULL || esperado == NULL) {
        free(texto);
        free(esperado);
        return;
    }

    IdeEnsamblador a;
    VERIFICAR(ide_ensamblador_generar(texto, largo, fuente, &a) == 0);
    VERIFICAR(a.estado == IDE_ASM_LISTO);
    if (a.ensamblador == NULL || strcmp(a.ensamblador, esperado) != 0) {
        fprintf(stderr, "FALLO: el ensamblador del IDE para %s no es igual a %s\n",
                fuente, asm_balc);
        fallos++;
    }
    VERIFICAR(a.bundles > 0 && a.funciones > 0);
    VERIFICAR(a.binario != NULL && strlen(a.binario) > 0);
    ide_ensamblador_liberar(&a);
    free(texto);
    free(esperado);
}

/* Lo del editor cuenta aunque no esté guardado: un cambio se ve enseguida. */
static void prueba_texto_sin_guardar(void)
{
    const char *texto =
        "create_funk #declare_infinite_void# main() {\n"
        "    x * declare_int : 7;\n"
        "    x : x gauss 35;\n"
        "}\n";
    IdeEnsamblador a;
    VERIFICAR(ide_ensamblador_generar(texto, strlen(texto), "examples/dce/editor.bal", &a) == 0);
    VERIFICAR(a.estado == IDE_ASM_LISTO);
    VERIFICAR(a.ensamblador != NULL && strstr(a.ensamblador, "# __arranque:") != NULL);
    VERIFICAR(a.ensamblador != NULL && strstr(a.ensamblador, "# __fn_main:") != NULL);
    VERIFICAR(a.funciones == 1);
    ide_ensamblador_liberar(&a);
}

/* Con errores no se genera nada: hay que corregirlos antes. */
static void prueba_con_errores(void)
{
    const char *texto = "create_funk #declare_infinite_void# main() {\n    x : 5;\n}\n";
    IdeEnsamblador a;
    VERIFICAR(ide_ensamblador_generar(texto, strlen(texto), "examples/dce/editor.bal", &a) == 0);
    VERIFICAR(a.estado == IDE_ASM_CON_ERRORES);
    VERIFICAR(a.ensamblador == NULL);
    ide_ensamblador_liberar(&a);
}

/*
 * El backend todavía no soporta parámetros que sean arreglos (memory.c).
 * El IDE lo explica en español, sin confundirlo con los avisos "[OK]" del
 * semántico. Los ejemplos del DCE (examples/dce/) usan arreglos como
 * parámetros, así que caen en este caso.
 */
static void prueba_parametro_arreglo(void)
{
    size_t largo = 0;
    char *texto = leer_archivo("examples/dce/dce_llamadas.bal", &largo);
    VERIFICAR(texto != NULL);
    if (texto == NULL) return;
    IdeEnsamblador a;
    VERIFICAR(ide_ensamblador_generar(texto, largo, "examples/dce/dce_llamadas.bal", &a) == 0);
    VERIFICAR(a.estado == IDE_ASM_SIN_BACKEND);
    VERIFICAR(a.motivo != NULL && strstr(a.motivo, "parámetros que sean arreglos") != NULL);
    VERIFICAR(a.motivo != NULL && strstr(a.motivo, "[OK]") == NULL);
    ide_ensamblador_liberar(&a);
    free(texto);
}

int main(void)
{
    comparar_con_balc("examples/factorial.bal", "examples/factorial.asm");
    comparar_con_balc("examples/ejercicio3_puntajes.bal", "examples/ejercicio3_puntajes.asm");
    prueba_texto_sin_guardar();
    prueba_con_errores();
    prueba_parametro_arreglo();

    if (fallos > 0) {
        fprintf(stderr, "test_ide_ensamblador: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_ensamblador: OK");
    return 0;
}
