/*
 * Pruebas del botón "Código muerto" del IDE (src/IDE/ide_dce.c), sin abrir
 * ventanas: se le da el texto del editor y se revisa qué líneas marca.
 *
 * Se ejecuta con: make test-ide-dce
 */
#include "ide_dce.h"

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

/* La entrada de la línea "linea", o NULL si esa línea no tiene código muerto. */
static const IdeDceLinea *buscar(const IdeDce *d, size_t linea)
{
    for (size_t i = 0; i < d->cantidad_lineas; ++i) {
        if (d->lineas[i].linea == linea) return &d->lineas[i];
    }
    return NULL;
}

/* El programa 1 de las pruebas del DCE, tal como lo vería el IDE. */
static void prueba_llamadas(void)
{
    size_t largo = 0;
    char *texto = leer_archivo("examples/dce/dce_llamadas.bal", &largo);
    VERIFICAR(texto != NULL);
    if (texto == NULL) return;

    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, largo, "examples/dce/dce_llamadas.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_LISTO);
    VERIFICAR(d.resultado.cantidad_eliminadas == 6);
    VERIFICAR(d.cantidad_lineas == 4);

    /* Línea 12, "b * declare_int : a pitagoras 2;": se elimina COMPLETA. */
    const IdeDceLinea *b = buscar(&d, 12);
    VERIFICAR(b != NULL && b->completa && b->eliminadas == 2);
    VERIFICAR(b != NULL && strcmp(b->detalle, "$t2 = a pitagoras 2; b = $t2") == 0);

    /* Línea 13, "c * declare_int : registrar(v, a);": solo se va la
     * copia a c; la llamada (y sus argumentos) se queda: NO es completa. */
    const IdeDceLinea *c = buscar(&d, 13);
    VERIFICAR(c != NULL && !c->completa && c->eliminadas == 1 && c->total == 4);

    /* Línea 15, "d * declare_int : a gauss 1;": completa. */
    const IdeDceLinea *dd = buscar(&d, 15);
    VERIFICAR(dd != NULL && dd->completa);

    /* La línea 14 es solo una llamada: no se toca. */
    VERIFICAR(buscar(&d, 14) == NULL);

    /* Línea 23 (en main), "total * declare_int : 0;": se reemplaza antes de
     * leerse. La línea 24 (el valor final de total) no se toca. */
    const IdeDceLinea *t = buscar(&d, 23);
    VERIFICAR(t != NULL && t->completa);
    VERIFICAR(buscar(&d, 24) == NULL);

    /* Los textos para la ventana de detalle. */
    VERIFICAR(d.ir_antes != NULL && strstr(d.ir_antes, "b = $t2") != NULL);
    VERIFICAR(d.ir_despues != NULL && strstr(d.ir_despues, "b = $t2") == NULL);
    VERIFICAR(d.ir_despues != NULL && strstr(d.ir_despues, "call registrar") != NULL);
    VERIFICAR(d.vivas != NULL && strstr(d.vivas, "IN  =") != NULL);

    /* El código nuevo: sin b ni d; c queda solo en la llamada; y la
     * declaración de total se une con su asignación (el backend necesita
     * declaraciones con valor). La llamada registrar(v, 7) no se toca. */
    VERIFICAR(d.codigo_nuevo != NULL);
    if (d.codigo_nuevo != NULL) {
        VERIFICAR(strstr(d.codigo_nuevo, "pitagoras") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "d * declare_int") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "    registrar(v, a);") != NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "c * declare_int") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "total * declare_int : procesar(datos, 4);") != NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "total * declare_int : 0;") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "registrar(v, 7);") != NULL);
    }
    VERIFICAR(d.sentencias_borradas == 2 && d.sentencias_cambiadas == 2);
    /* El backend no acepta arreglos como parámetros: no hay ensamblador. */
    VERIFICAR(!d.hay_ensamblador && d.motivo_ensamblador != NULL);

    ide_dce_liberar(&d);
    free(texto);
}

/*
 * El ejemplo con solo parámetros int: el backend sí genera su ensamblador,
 * y el del código nuevo tiene MENOS bundles. Si se pasa "ruta", el código
 * nuevo se escribe ahí (make test-ide-dce lo compila con balc y lo ejecuta
 * en el simulador, para comprobar que da el mismo resultado).
 */
static void prueba_ensamblador(const char *ruta)
{
    size_t largo = 0;
    char *texto = leer_archivo("examples/dce/dce_ensamblador.bal", &largo);
    VERIFICAR(texto != NULL);
    if (texto == NULL) return;

    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, largo, "examples/dce/dce_ensamblador.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_LISTO && d.codigo_nuevo != NULL);
    VERIFICAR(d.hay_ensamblador);
    VERIFICAR(d.bundles_despues < d.bundles_antes);
    if (d.codigo_nuevo != NULL) {
        /* doble y extra (con su uso en el ciclo) desaparecen; aux queda solo
         * en la llamada; r se declara directamente con su valor final. */
        VERIFICAR(strstr(d.codigo_nuevo, "doble") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "extra") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "    cuadrado(n);") != NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "r * declare_int : cuadrado(4);") != NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "total : total gauss i;") != NULL);

        /* El código nuevo ya no tiene código muerto: pasarle el DCE otra vez
         * no elimina nada. */
        IdeDce otra;
        VERIFICAR(ide_dce_analizar(d.codigo_nuevo, strlen(d.codigo_nuevo),
                                   "examples/dce/dce_ensamblador.bal", &otra) == 0);
        VERIFICAR(otra.estado == IDE_DCE_LISTO && otra.resultado.cantidad_eliminadas == 0);
        VERIFICAR(otra.codigo_nuevo == NULL);
        ide_dce_liberar(&otra);

        if (ruta != NULL) {
            FILE *f = fopen(ruta, "wb");
            VERIFICAR(f != NULL);
            if (f != NULL) {
                fputs(d.codigo_nuevo, f);
                fclose(f);
            }
        }
    }
    ide_dce_liberar(&d);
    free(texto);
}

/*
 * Tipo 2 en el IDE: las líneas inalcanzables se marcan como tales, y en el
 * código nuevo desaparecen enteros el whether con condición siempre falsa,
 * el whale que nunca entra y lo que va después del give. Como el backend
 * rechaza sentencias después del give final, el código original no compila
 * pero el nuevo sí.
 */
static void prueba_inalcanzable(void)
{
    const char *texto =
        "create_funk #declare_int# doble(x * declare_int) {\n"      /* 1 */
        "    give x gauss x;\n"                                     /* 2 */
        "    y * declare_int : 5;\n"                                /* 3 */
        "}\n"                                                       /* 4 */
        "create_funk #declare_int# prueba(n * declare_int) {\n"     /* 5 */
        "    s * declare_int : 0;\n"                                /* 6 */
        "    r * declare_int : n;\n"                                /* 7 */
        "    whether (declare_false) {\n"                           /* 8 */
        "        r : r gauss 100;\n"                                /* 9 */
        "    }\n"                                                   /* 10 */
        "    whale (1 > 2) {\n"                                     /* 11 */
        "        r : r pitagoras 2;\n"                              /* 12 */
        "    } stop;\n"                                             /* 13 */
        "    give r;\n"                                             /* 14 */
        "}\n"                                                       /* 15 */
        "create_funk #declare_infinite_void# main() {\n"
        "    x * declare_int : doble(2);\n"
        "    x : x gauss prueba(5);\n"              /* x se lee: es el resultado. */
        "}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "examples/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_LISTO);

    const IdeDceLinea *y = buscar(&d, 3), *cuerpo = buscar(&d, 9), *ciclo = buscar(&d, 12);
    VERIFICAR(y != NULL && y->inalcanzable && y->completa);
    VERIFICAR(cuerpo != NULL && cuerpo->inalcanzable);
    VERIFICAR(ciclo != NULL && ciclo->inalcanzable);
    /* La línea de s es tipo 1 (nunca se lee), no inalcanzable. */
    const IdeDceLinea *ns = buscar(&d, 6);
    VERIFICAR(ns != NULL && !ns->inalcanzable && ns->completa);

    VERIFICAR(d.codigo_nuevo != NULL);
    if (d.codigo_nuevo != NULL) {
        VERIFICAR(strstr(d.codigo_nuevo, "y * declare_int") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "s * declare_int") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "whether") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "whale") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "r * declare_int : n;") != NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "give r;") != NULL);
    }
    /* El original no compila (código después del give final); el nuevo sí. */
    VERIFICAR(d.hay_ensamblador == 2 && d.bundles_despues > 0);
    VERIFICAR(d.motivo_ensamblador != NULL && strstr(d.motivo_ensamblador, "retorno") != NULL);
    ide_dce_liberar(&d);
}

/*
 * Un whether con la condición siempre falsa pero con also: la rama del
 * whether es inalcanzable, pero la sentencia se queda (por el also), así
 * que su primera línea NO se tacha (no es "completa"). Y "whether (n <= 1)"
 * con n una variable no se toca: no se propagan valores de variables.
 */
static void prueba_cabecera_que_queda(void)
{
    const char *texto =
        "create_funk #declare_infinite_void# main() {\n"   /* 1 */
        "    n * declare_int : 5;\n"                       /* 2 */
        "    r * declare_int : 1;\n"                       /* 3 */
        "    whether (declare_false) {\n"                  /* 4 */
        "        r : 2;\n"                                 /* 5: inalcanzable */
        "    } also {\n"                                   /* 6 */
        "        r : r gauss 3;\n"                         /* 7 */
        "    }\n"                                          /* 8 */
        "    whether (n <= 1) {\n"                         /* 9 */
        "        r : r gauss 4;\n"                         /* 10 */
        "    }\n"                                          /* 11 */
        "}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "examples/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_LISTO);
    const IdeDceLinea *cabeza = buscar(&d, 4), *rama = buscar(&d, 5);
    VERIFICAR(cabeza != NULL && cabeza->inalcanzable && !cabeza->completa);
    VERIFICAR(rama != NULL && rama->inalcanzable && rama->completa);
    VERIFICAR(buscar(&d, 9) == NULL && buscar(&d, 10) == NULL);
    if (d.codigo_nuevo != NULL) {
        VERIFICAR(strstr(d.codigo_nuevo, "r : 2;") == NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "r : r gauss 3;") != NULL);
        VERIFICAR(strstr(d.codigo_nuevo, "whether (n <= 1)") != NULL);
    }
    ide_dce_liberar(&d);
}

/* Un programa con errores no se analiza: hay que corregirlo antes. */
static void prueba_con_errores(void)
{
    const char *texto = "create_funk #declare_infinite_void# main() {\n    x : 5;\n}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "examples/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_CON_ERRORES);
    VERIFICAR(d.cantidad_lineas == 0);
    ide_dce_liberar(&d);
}

/* Un programa válido que la IR todavía no soporta: se dice por qué y dónde. */
static void prueba_sin_ir(void)
{
    const char *texto =
        "create_funk #declare_infinite_void# main() {\n"
        "    s * declare_int : 0;\n"
        "    cycle i let 3 until 0 step neumann 1 {\n"
        "        s : s gauss i;\n"
        "    } endgame;\n"
        "}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "examples/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_SIN_IR);
    VERIFICAR(d.motivo != NULL && strncmp(d.motivo, "línea 3: cycle", strlen("línea 3: cycle")) == 0);
    ide_dce_liberar(&d);
}

/* Sin código muerto: listo, sin líneas marcadas. */
static void prueba_sin_codigo_muerto(void)
{
    const char *texto =
        "create_funk #declare_int# doble(n * declare_int) {\n"
        "    give n pitagoras 2;\n"
        "}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    doble(3);\n"
        "}\n";
    IdeDce d;
    VERIFICAR(ide_dce_analizar(texto, strlen(texto), "examples/dce/editor.bal", &d) == 0);
    VERIFICAR(d.estado == IDE_DCE_LISTO);
    VERIFICAR(d.cantidad_lineas == 0);
    VERIFICAR(d.resultado.cantidad_eliminadas == 0);
    ide_dce_liberar(&d);
}

int main(int argc, char **argv)
{
    prueba_llamadas();
    prueba_ensamblador(argc > 1 ? argv[1] : NULL);
    prueba_inalcanzable();
    prueba_cabecera_que_queda();
    prueba_con_errores();
    prueba_sin_ir();
    prueba_sin_codigo_muerto();

    if (fallos > 0) {
        fprintf(stderr, "test_ide_dce: %d fallos\n", fallos);
        return 1;
    }
    puts("test_ide_dce: OK");
    return 0;
}
