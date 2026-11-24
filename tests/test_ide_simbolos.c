/*
 * Pruebas del botón "Tabla de símbolos" del IDE (src/IDE/ide_simbolos.c),
 * sin abrir ventanas.
 *
 * Lo que muestra el IDE debe coincidir con la tabla de "balc -m" (por
 * ejemplo examples/ejercicio2_busqueda_matriz.simbolos.md): mismos símbolos,
 * en el mismo orden, con la misma clase, tipo, ámbito, almacén, bytes,
 * desplazamiento y dirección de código.
 *
 * Se ejecuta con: make test-ide-simbolos
 */
#include "ide_simbolos.h"

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

/* Busca un símbolo por nombre (el primero con ese nombre), o NULL. */
static const IdeSimbolo *buscar(const IdeSimbolos *s, const char *nombre)
{
    for (size_t i = 0; i < s->cantidad; ++i)
        if (strcmp(s->simbolos[i].nombre, nombre) == 0) return &s->simbolos[i];
    return NULL;
}

/* Analiza un archivo con el IDE. 0 si se pudo. */
static int analizar(const char *ruta, IdeSimbolos *s)
{
    size_t largo = 0;
    char *texto = leer_archivo(ruta, &largo);
    VERIFICAR(texto != NULL);
    if (texto == NULL) return -1;
    int r = ide_simbolos_generar(texto, largo, ruta, s);
    free(texto);
    return r;
}

/* La misma tabla que examples/ejercicio2_busqueda_matriz.simbolos.md (balc -m). */
static void prueba_igual_que_balc(void)
{
    IdeSimbolos s;
    VERIFICAR(analizar("examples/ejercicio2_busqueda_matriz.bal", &s) == 0);
    VERIFICAR(s.estado == IDE_SIM_LISTO);
    VERIFICAR(s.consistente == 1);
    VERIFICAR(s.con_memoria == 1);
    VERIFICAR(s.funciones == 1 && s.marco_maximo == 48 && s.gp == 0x4000);
    VERIFICAR(s.bytes_globales == 0 && s.sp == 0x10000 && s.con_codigo == 1);
    VERIFICAR(s.cantidad == 7);
    if (s.cantidad == 7) {
        /* Mismo orden que balc -m: main y luego las variables como se declararon. */
        const char *orden[] = {"main", "matriz", "objetivo", "fila", "columna",
                               "fila_encontrada", "columna_encontrada"};
        for (size_t i = 0; i < 7; ++i) VERIFICAR(strcmp(s.simbolos[i].nombre, orden[i]) == 0);

        const IdeSimbolo *m = &s.simbolos[0];
        VERIFICAR(strcmp(m->clase, "funcion") == 0);
        VERIFICAR(strcmp(m->tipo, "declare_infinite_void") == 0);
        VERIFICAR(strcmp(m->ambito, "global") == 0 && m->nivel == 0);
        VERIFICAR(strcmp(m->almacen, "codigo") == 0 && !m->con_desplazamiento);
        VERIFICAR(m->con_direccion && m->direccion == 0x0330);   /* Como en el .simbolos.md. */

        const IdeSimbolo *x = &s.simbolos[1];   /* matriz * declare_int[2][3] */
        VERIFICAR(strcmp(x->clase, "variable") == 0);
        VERIFICAR(strcmp(x->tipo, "declare_int") == 0);
        VERIFICAR(strcmp(x->ambito, "main") == 0 && x->nivel == 1);
        VERIFICAR(strcmp(x->dimensiones, "[2][3]") == 0);
        VERIFICAR(strcmp(x->almacen, "local(sp)") == 0);
        VERIFICAR(x->bytes == 24 && x->con_desplazamiento && x->desplazamiento == 4);
        VERIFICAR(x->linea == 4 && !x->con_direccion);   /* Una variable no tiene dirección de código. */

        const IdeSimbolo *o = &s.simbolos[2];   /* objetivo: después de los 24 bytes */
        VERIFICAR(o->bytes == 4 && o->desplazamiento == 28 && o->dimensiones[0] == '\0');
        VERIFICAR(s.simbolos[6].desplazamiento == 44);
    }
    /* Ámbitos: el global, el de main y uno por cada bloque { } anidado (los
     * dos whale y el whether), aunque no declaren nada. */
    VERIFICAR(s.cantidad_ambitos == 5);
    if (s.cantidad_ambitos == 5) {
        VERIFICAR(strcmp(s.ambitos[0].nombre, "global") == 0 && s.ambitos[0].nivel == 0);
        VERIFICAR(s.ambitos[0].padre[0] == '\0' && s.ambitos[0].simbolos == 1);
        VERIFICAR(strcmp(s.ambitos[1].nombre, "main") == 0 && s.ambitos[1].nivel == 1);
        VERIFICAR(strcmp(s.ambitos[1].padre, "global") == 0 && s.ambitos[1].simbolos == 6);
        VERIFICAR(s.ambitos[4].nivel == 4 && s.ambitos[4].simbolos == 0);
        VERIFICAR(strcmp(s.ambitos[4].padre, s.ambitos[3].nombre) == 0);
    }
    /* El listado de sim_imprimir (symbol_table.c). */
    VERIFICAR(s.listado != NULL && strstr(s.listado, "Nombre") != NULL);
    VERIFICAR(s.listado != NULL && strstr(s.listado, "columna_encontrada") != NULL);
    ide_simbolos_liberar(&s);
}

/* Funciones con parámetros y variables dentro de un whether (nivel 2). */
static void prueba_funciones_y_bloques(void)
{
    IdeSimbolos s;
    VERIFICAR(analizar("tests/Pruebas_obligatorias/11_reorden_funciones.bal", &s) == 0);
    VERIFICAR(s.estado == IDE_SIM_LISTO && s.con_memoria == 1);

    const IdeSimbolo *f = buscar(&s, "combinar");
    VERIFICAR(f != NULL && strcmp(f->clase, "funcion") == 0);
    VERIFICAR(f != NULL && strcmp(f->tipo, "declare_int") == 0 && f->parametros == 3);
    /* Cada función tiene su dirección, todas distintas y múltiplos de 16 (un bundle). */
    const IdeSimbolo *fact = buscar(&s, "fact"), *principal = buscar(&s, "main");
    VERIFICAR(f != NULL && fact != NULL && principal != NULL);
    if (f != NULL && fact != NULL && principal != NULL) {
        VERIFICAR(f->con_direccion && fact->con_direccion && principal->con_direccion);
        VERIFICAR(fact->direccion != f->direccion && f->direccion != principal->direccion);
        VERIFICAR(fact->direccion % 16 == 0 && principal->direccion % 16 == 0);
    }

    const IdeSimbolo *n = buscar(&s, "n");      /* parámetro de fact */
    VERIFICAR(n != NULL && strcmp(n->clase, "parametro") == 0);
    VERIFICAR(n != NULL && strcmp(n->ambito, "fact") == 0 && n->nivel == 1);

    const IdeSimbolo *sub = buscar(&s, "sub");  /* declarada dentro del whether */
    VERIFICAR(sub != NULL && sub->nivel == 2);

    /* Hay un ámbito de nivel 2 cuyo padre es fact. */
    int bloque = 0;
    for (size_t i = 0; i < s.cantidad_ambitos; ++i)
        bloque |= s.ambitos[i].nivel == 2 && strcmp(s.ambitos[i].padre, "fact") == 0;
    VERIFICAR(bloque);
    ide_simbolos_liberar(&s);
}

/* Un código con errores no tiene tabla (el semántico no la termina). */
static void prueba_con_errores(void)
{
    const char *texto = "create_funk #declare_infinite_void# main() {\n"
                        "    x * declare_int : y;\n"
                        "}\n";
    IdeSimbolos s;
    VERIFICAR(ide_simbolos_generar(texto, strlen(texto), NULL, &s) == 0);
    VERIFICAR(s.estado == IDE_SIM_CON_ERRORES);
    VERIFICAR(s.cantidad == 0 && s.simbolos == NULL && s.listado == NULL);
    ide_simbolos_liberar(&s);
}

int main(void)
{
    prueba_igual_que_balc();
    prueba_funciones_y_bloques();
    prueba_con_errores();
    if (fallos) {
        fprintf(stderr, "test_ide_simbolos: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_simbolos: OK");
    return 0;
}
