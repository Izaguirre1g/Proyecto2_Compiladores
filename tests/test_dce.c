/*
 * Pruebas de la eliminación de código muerto (src/DCE/dce.c).
 *
 * Cada prueba convierte un programa BAL en AST (parser), luego en IR de tres
 * direcciones (ir_desde_ast) y le aplica el DCE. Se revisa:
 *   - que se eliminen las asignaciones a variables que no están vivas;
 *   - que NUNCA se eliminen las llamadas ni las escrituras en memoria;
 *   - que se conserven las divisiones que pueden fallar;
 *   - el análisis de variables vivas con el ejercicio 4 de la presentación.
 *
 * Los tres programas con llamadas con efectos están en examples/dce/ y también
 * se pueden ver con:  ./balc --dce examples/dce/dce_llamadas.bal
 *
 * Se ejecuta con: make test-dce
 */
#include "dce.h"
#include "sintactico.h"

#include <stdint.h>
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

/* ------------------------------------------------------------------ */
/* Ayudas                                                              */
/* ------------------------------------------------------------------ */

/* Lee un archivo completo. Devuelve NULL si no se pudo. */
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

/* Texto BAL -> AST -> IR. Devuelve 0 si todo salió bien. */
static int generar_ir(const char *texto, size_t largo, IrPrograma *ir)
{
    Ast ast = {0};
    int estado = construir_ast(texto, largo, "prueba.bal", stderr, &ast);
    if (estado == 0) estado = ir_desde_ast(&ast, ir, stderr);
    ast_liberar(&ast);
    return estado;
}

/* Lo mismo, leyendo el programa desde un archivo. */
static int generar_ir_de_archivo(const char *ruta, IrPrograma *ir)
{
    size_t largo = 0;
    char *texto = leer_archivo(ruta, &largo);
    if (texto == NULL) {
        fprintf(stderr, "no se pudo leer %s\n", ruta);
        return 1;
    }
    int estado = generar_ir(texto, largo, ir);
    free(texto);
    return estado;
}

/* Cuántas instrucciones de la operación "op" tiene la IR. */
static size_t contar(const IrPrograma *ir, IrOp op)
{
    size_t n = 0;
    for (size_t i = 0; i < ir->cantidad; ++i) n += ir->codigo[i].op == op;
    return n;
}

/* ¿Queda alguna asignación (copia) a la variable "nombre"? */
static int se_asigna(const IrPrograma *ir, const char *nombre)
{
    for (size_t i = 0; i < ir->cantidad; ++i) {
        const IrInstr *x = &ir->codigo[i];
        if (x->op == IR_COPY && x->dst && strcmp(x->dst, nombre) == 0) return 1;
    }
    return 0;
}

/* Cuántas eliminadas son del tipo dado (tipo 1 o tipo 2). */
static size_t contar_tipo(const DceResultado *r, DceTipo tipo)
{
    size_t n = 0;
    for (size_t i = 0; i < r->cantidad_eliminadas; ++i) n += r->eliminadas[i].tipo == tipo;
    return n;
}

/* ¿Queda la copia "nombre = valor"? */
static int queda_copia(const IrPrograma *ir, const char *nombre, const char *valor)
{
    for (size_t i = 0; i < ir->cantidad; ++i) {
        const IrInstr *x = &ir->codigo[i];
        if (x->op == IR_COPY && x->dst && x->a && strcmp(x->dst, nombre) == 0 &&
            strcmp(x->a, valor) == 0) return 1;
    }
    return 0;
}

/* ¿Queda una operación binaria "a operador b"? */
static int queda_binaria(const IrPrograma *ir, const char *a, const char *operador, const char *b)
{
    for (size_t i = 0; i < ir->cantidad; ++i) {
        const IrInstr *x = &ir->codigo[i];
        if (x->op == IR_BIN && x->a && x->b && x->operador && strcmp(x->a, a) == 0 &&
            strcmp(x->operador, operador) == 0 && strcmp(x->b, b) == 0) return 1;
    }
    return 0;
}

/*
 * Revisión común a todos los programas: el DCE no puede tocar nada que
 * tenga efectos. Se cuentan antes y después las llamadas, sus argumentos,
 * las escrituras en memoria, los retornos y los saltos.
 */
typedef struct {
    size_t llamadas, argumentos, escrituras, retornos, saltos;
} Conservadas;

static Conservadas contar_conservadas(const IrPrograma *ir)
{
    Conservadas c;
    c.llamadas = contar(ir, IR_CALL);
    c.argumentos = contar(ir, IR_ARG);
    c.escrituras = contar(ir, IR_STORE) + contar(ir, IR_LIST_ADD) + contar(ir, IR_LIST_REMOVE);
    c.retornos = contar(ir, IR_RETURN);
    c.saltos = contar(ir, IR_JUMP) + contar(ir, IR_JFALSE) + contar(ir, IR_JTRUE);
    return c;
}

static void verificar_conservadas(int linea, const Conservadas *antes, const Conservadas *despues)
{
    if (antes->llamadas != despues->llamadas || antes->argumentos != despues->argumentos ||
        antes->escrituras != despues->escrituras || antes->retornos != despues->retornos ||
        antes->saltos != despues->saltos) {
        fprintf(stderr, "FALLO en la linea %d: el DCE quito una instruccion con efectos\n", linea);
        fallos++;
    }
}

/* ------------------------------------------------------------------ */
/* Programa 1: llamadas con efectos                                    */
/* ------------------------------------------------------------------ */

static void prueba_llamadas(void)
{
    IrPrograma ir = {0};
    VERIFICAR(generar_ir_de_archivo("examples/dce/dce_llamadas.bal", &ir) == 0);
    Conservadas antes = contar_conservadas(&ir);
    VERIFICAR(antes.llamadas == 3);  /* registrar dos veces y procesar. */

    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);
    Conservadas despues = contar_conservadas(&ir);
    verificar_conservadas(__LINE__, &antes, &despues);

    /* En procesar(): b y d no se leen nunca: sus cálculos desaparecen. */
    VERIFICAR(!se_asigna(&ir, "b"));
    VERIFICAR(!se_asigna(&ir, "d"));
    VERIFICAR(!queda_binaria(&ir, "a", "pitagoras", "2"));
    VERIFICAR(!queda_binaria(&ir, "a", "gauss", "1"));
    /* c tampoco se lee: se va la copia "c = ...", pero la llamada se queda. */
    VERIFICAR(!se_asigna(&ir, "c"));
    VERIFICAR(r.llamadas_conservadas == 2);
    /* En main: "total : 0" se reemplaza antes de leerse y se va; el valor
     * final de total (lo que devuelve procesar) se queda: es el resultado. */
    VERIFICAR(!queda_copia(&ir, "total", "0"));
    VERIFICAR(se_asigna(&ir, "total"));
    VERIFICAR(r.cantidad_eliminadas == 6);
    VERIFICAR(r.instrucciones_despues == r.instrucciones_antes - 6);

    dce_liberar_resultado(&r);
    ir_liberar(&ir);
}

/* ------------------------------------------------------------------ */
/* Programa 2: ciclo, y código que queda muerto en la segunda pasada   */
/* ------------------------------------------------------------------ */

static void prueba_ciclo(void)
{
    IrPrograma ir = {0};
    VERIFICAR(generar_ir_de_archivo("examples/dce/dce_ciclo.bal", &ir) == 0);
    Conservadas antes = contar_conservadas(&ir);

    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);
    Conservadas despues = contar_conservadas(&ir);
    verificar_conservadas(__LINE__, &antes, &despues);

    /* extra no se lee: se va su valor inicial y lo que se calcula en el ciclo. */
    VERIFICAR(!se_asigna(&ir, "extra"));
    VERIFICAR(!queda_binaria(&ir, "base", "gauss", "i"));
    /* base solo la leía extra: queda muerta y se va... */
    VERIFICAR(!se_asigna(&ir, "base"));
    /* ...pero eso se ve en la segunda pasada: 2 que eliminan + 1 sin cambios. */
    VERIFICAR(r.pasadas == 3);
    /* i sí está viva: la lee la condición del ciclo y la llamada. */
    VERIFICAR(se_asigna(&ir, "i"));
    VERIFICAR(queda_binaria(&ir, "i", "gauss", "1"));
    /* Las llamadas a acumular (escribe en conteo) y a contar se quedan. */
    VERIFICAR(despues.llamadas == 2);
    VERIFICAR(r.llamadas_conservadas == 1);
    /* vueltas es una variable de main: es parte del resultado. */
    VERIFICAR(se_asigna(&ir, "vueltas"));

    dce_liberar_resultado(&r);
    ir_liberar(&ir);
}

/* ------------------------------------------------------------------ */
/* Programa 3: operaciones que pueden fallar                           */
/* ------------------------------------------------------------------ */

static void prueba_division(void)
{
    IrPrograma ir = {0};
    VERIFICAR(generar_ir_de_archivo("examples/dce/dce_division.bal", &ir) == 0);
    Conservadas antes = contar_conservadas(&ir);

    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);
    Conservadas despues = contar_conservadas(&ir);
    verificar_conservadas(__LINE__, &antes, &despues);

    /* En promedio(): doble no se lee y se va; suma sí (se devuelve). */
    VERIFICAR(!se_asigna(&ir, "doble"));
    VERIFICAR(se_asigna(&ir, "suma"));
    VERIFICAR(queda_binaria(&ir, "suma", "euclides", "n"));
    /* 100 euclides 4 no puede fallar: como seguro no se lee, se va. */
    VERIFICAR(!queda_binaria(&ir, "100", "euclides", "4"));
    VERIFICAR(!se_asigna(&ir, "seguro"));
    /* 100 euclides divisor PUEDE fallar (main pasa divisor = 0): se queda.
     * La copia a riesgo sí se va. */
    VERIFICAR(queda_binaria(&ir, "100", "euclides", "divisor"));
    VERIFICAR(!se_asigna(&ir, "riesgo"));
    VERIFICAR(r.riesgosas_conservadas == 1);
    /* p no se lee, pero la llamada a promedio se queda. */
    VERIFICAR(!se_asigna(&ir, "p"));
    VERIFICAR(r.llamadas_conservadas == 1);
    /* r es una variable de main: se queda. */
    VERIFICAR(se_asigna(&ir, "r"));

    dce_liberar_resultado(&r);
    ir_liberar(&ir);
}

/* ------------------------------------------------------------------ */
/* Ejercicio 4 de la presentación (diapositivas 38 y 39)               */
/* ------------------------------------------------------------------ */

/* El bloque básico donde se asigna "nombre" (o SIZE_MAX). */
static size_t bloque_que_asigna(const IrPrograma *ir, const char *nombre)
{
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        for (size_t i = ir->bloques[b].inicio; i < ir->bloques[b].fin; ++i) {
            const IrInstr *x = &ir->codigo[i];
            if (x->op == IR_COPY && x->dst && strcmp(x->dst, nombre) == 0 &&
                x->a && x->a[0] == '$') return b;  /* La asignación con el cálculo. */
        }
    }
    return SIZE_MAX;
}

/*
 * El bloque del ejercicio:
 *     a = b + c;  d = a - b;  x = d + a;  e = x * 2;
 * con OUT[B] = {x}. Para que quede como un bloque propio va dentro de un
 * whether, y para que x esté viva al salir se devuelve después.
 * Resultado de la diapositiva 39: IN[B] = {b, c}.
 * Y el DCE debe quitar "e = x * 2": e no está viva después.
 */
static void prueba_ejercicio_4(void)
{
    const char *programa =
        "create_funk #declare_int# ejercicio(b * declare_int, c * declare_int) {\n"
        "    a * declare_int : 0;\n"
        "    d * declare_int : 0;\n"
        "    x * declare_int : 0;\n"
        "    e * declare_int : 0;\n"
        "    whether (b > 0) {\n"
        "        a : b gauss c;\n"
        "        d : a neumann b;\n"
        "        x : d gauss a;\n"
        "        e : x pitagoras 2;\n"
        "    }\n"
        "    give x;\n"
        "}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    r * declare_int : ejercicio(1, 2);\n"
        "}\n";
    IrPrograma ir = {0};
    VERIFICAR(generar_ir(programa, strlen(programa), &ir) == 0);

    size_t B = bloque_que_asigna(&ir, "e");
    VERIFICAR(B != SIZE_MAX);
    DceVivas v;
    VERIFICAR(dce_calcular_vivas(&ir, &v) == 0);
    if (B != SIZE_MAX) {
        /* OUT[B] = {x} (de las variables del ejercicio). */
        VERIFICAR(dce_esta_viva(&v, B, "x", 1));
        VERIFICAR(!dce_esta_viva(&v, B, "a", 1));
        VERIFICAR(!dce_esta_viva(&v, B, "d", 1));
        VERIFICAR(!dce_esta_viva(&v, B, "e", 1));
        /* IN[B] = {b, c}. */
        VERIFICAR(dce_esta_viva(&v, B, "b", 0));
        VERIFICAR(dce_esta_viva(&v, B, "c", 0));
        VERIFICAR(!dce_esta_viva(&v, B, "a", 0));
        VERIFICAR(!dce_esta_viva(&v, B, "d", 0));
        VERIFICAR(!dce_esta_viva(&v, B, "x", 0));
        VERIFICAR(!dce_esta_viva(&v, B, "e", 0));
    }
    dce_liberar_vivas(&v);

    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);
    /* e = x * 2 se va (las dos instrucciones: el cálculo y la copia),
     * y también "e = 0" del inicio. a, d y x se quedan. */
    VERIFICAR(!se_asigna(&ir, "e"));
    VERIFICAR(!queda_binaria(&ir, "x", "pitagoras", "2"));
    VERIFICAR(queda_binaria(&ir, "d", "gauss", "a"));
    VERIFICAR(se_asigna(&ir, "x"));
    /* "r = ejercicio(1, 2)": r es de main pero nunca se lee, así que no es
     * parte del resultado: la copia a r se va. La llamada se queda. */
    VERIFICAR(!se_asigna(&ir, "r"));
    VERIFICAR(contar(&ir, IR_CALL) == 1);
    dce_liberar_resultado(&r);
    ir_liberar(&ir);
}

/* ------------------------------------------------------------------ */
/* Casos límite                                                        */
/* ------------------------------------------------------------------ */

/* Un programa sin código muerto queda igual, en una sola pasada. */
static void prueba_sin_codigo_muerto(void)
{
    const char *programa =
        "create_funk #declare_int# doble(n * declare_int) {\n"
        "    give n pitagoras 2;\n"
        "}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    doble(3);\n"
        "}\n";
    IrPrograma ir = {0};
    VERIFICAR(generar_ir(programa, strlen(programa), &ir) == 0);
    size_t antes = ir.cantidad;
    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);
    VERIFICAR(r.cantidad_eliminadas == 0);
    VERIFICAR(r.pasadas == 1);
    VERIFICAR(ir.cantidad == antes);
    dce_liberar_resultado(&r);
    ir_liberar(&ir);
}

/* Una potencia con exponente variable puede fallar (exponente negativo);
 * con exponente fijo >= 0, no. */
static void prueba_potencia(void)
{
    const char *programa =
        "create_funk #declare_int# potencias(k * declare_int) {\n"
        "    p * declare_int : 2 descartes k;\n"
        "    q * declare_int : 2 descartes 3;\n"
        "    give k;\n"
        "}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    r * declare_int : potencias(3);\n"
        "}\n";
    IrPrograma ir = {0};
    VERIFICAR(generar_ir(programa, strlen(programa), &ir) == 0);
    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);
    VERIFICAR(queda_binaria(&ir, "2", "descartes", "k"));
    VERIFICAR(!queda_binaria(&ir, "2", "descartes", "3"));
    VERIFICAR(r.riesgosas_conservadas == 1);
    dce_liberar_resultado(&r);
    ir_liberar(&ir);
}

/*
 * Las variables de main son el RESULTADO del programa: el simulador y las
 * pruebas del equipo leen su valor final en memoria (factorial.bal deja
 * resultado = 120). Por eso están vivas al terminar main y el DCE no puede
 * quitar ninguna asignación que decida ese valor final.
 */
static void prueba_variables_de_main(void)
{
    /* factorial.bal: resultado se usa (resultado pitagoras i), así que
     * ninguna de sus asignaciones es tipo 1. Y "whether (n <= 1)" depende
     * de la variable n (un dato del programa): no se propagan valores de
     * variables, así que la rama no es inalcanzable. No se elimina nada. */
    IrPrograma ir = {0};
    VERIFICAR(generar_ir_de_archivo("examples/factorial.bal", &ir) == 0);
    size_t antes = ir.cantidad;
    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);
    VERIFICAR(r.cantidad_eliminadas == 0 && r.condiciones_constantes == 0);
    VERIFICAR(ir.cantidad == antes);
    dce_liberar_resultado(&r);
    ir_liberar(&ir);

    /* Con n como parámetro, igual: la rama "resultado : 1" se queda. */
    const char *con_parametro =
        "create_funk #declare_int# fact(n * declare_int) {\n"
        "    resultado * declare_int : 1;\n"
        "    whether (n <= 1) {\n"
        "        resultado : 1;\n"
        "    }\n"
        "    give resultado;\n"
        "}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    v * declare_int[1] : [0];\n"
        "    v[0] : fact(5);\n"
        "}\n";
    IrPrograma ir_p = {0};
    VERIFICAR(generar_ir(con_parametro, strlen(con_parametro), &ir_p) == 0);
    DceResultado r_p = {0};
    VERIFICAR(dce_eliminar(&ir_p, &r_p) == 0);
    VERIFICAR(r_p.condiciones_constantes == 0 && r_p.inalcanzables == 0);
    dce_liberar_resultado(&r_p);
    ir_liberar(&ir_p);

    /* El mismo factorial con "t * declare_int : 4;": t nunca se usa después
     * de declararse, así que es código muerto. resultado sigue igual. */
    const char *con_t =
        "create_funk #declare_infinite_void# main() {\n"
        "    n * declare_int : 5;\n"
        "    resultado * declare_int : 1;\n"
        "    i * declare_int : 2;\n"
        "    t * declare_int : 4;\n"
        "    whether (n <= 1) {\n"
        "        resultado : 1;\n"
        "    }\n"
        "    also {\n"
        "        whale (i <= n) {\n"
        "            resultado : resultado pitagoras i;\n"
        "            i : i gauss 1;\n"
        "        } stop;\n"
        "    }\n"
        "}\n";
    IrPrograma ir_t = {0};
    VERIFICAR(generar_ir(con_t, strlen(con_t), &ir_t) == 0);
    DceResultado r_t = {0};
    VERIFICAR(dce_eliminar(&ir_t, &r_t) == 0);
    VERIFICAR(!se_asigna(&ir_t, "t"));
    VERIFICAR(r_t.cantidad_eliminadas == 1);
    VERIFICAR(queda_copia(&ir_t, "resultado", "1"));  /* Las dos "resultado = 1" siguen. */
    dce_liberar_resultado(&r_t);
    ir_liberar(&ir_t);

    /* Un valor que se REEMPLAZA antes de leerse está muerto, también en main,
     * aunque la variable sí se use después: "x : 1" nunca se lee. */
    const char *programa =
        "create_funk #declare_infinite_void# main() {\n"
        "    v * declare_int[1] : [0];\n"
        "    x * declare_int : 1;\n"
        "    x : 2;\n"
        "    v[0] : x;\n"
        "}\n";
    IrPrograma ir2 = {0};
    VERIFICAR(generar_ir(programa, strlen(programa), &ir2) == 0);
    DceResultado r2 = {0};
    VERIFICAR(dce_eliminar(&ir2, &r2) == 0);
    VERIFICAR(!queda_copia(&ir2, "x", "1"));
    VERIFICAR(queda_copia(&ir2, "x", "2"));
    VERIFICAR(r2.cantidad_eliminadas == 1);
    dce_liberar_resultado(&r2);
    ir_liberar(&ir2);

    /* Si la única lectura está en código muerto, tampoco cuenta (diapositiva
     * 28: eliminar código muerto puede generar más): suma nunca se lee, y
     * a y b solo se leían en suma. */
    const char *cadena =
        "create_funk #declare_infinite_void# main() {\n"
        "    a * declare_int : 1;\n"
        "    b * declare_int : 2;\n"
        "    suma * declare_int : a gauss b;\n"
        "}\n";
    IrPrograma ir3 = {0};
    VERIFICAR(generar_ir(cadena, strlen(cadena), &ir3) == 0);
    DceResultado r3 = {0};
    VERIFICAR(dce_eliminar(&ir3, &r3) == 0);
    VERIFICAR(!se_asigna(&ir3, "suma") && !se_asigna(&ir3, "a") && !se_asigna(&ir3, "b"));
    VERIFICAR(r3.pasadas == 3);
    dce_liberar_resultado(&r3);
    ir_liberar(&ir3);
}

/*
 * Tipo 2: código inalcanzable (el ejemplo de la presentación y los dos
 * casos: después de un salto incondicional, y una condición constante).
 */
static void prueba_inalcanzable(void)
{
    const char *programa =
        "create_funk #declare_int# doble(x * declare_int) {\n"         /* 1 */
        "    give x gauss x;\n"                                        /* 2 */
        "    y * declare_int : 5;\n"                                   /* 3: después del give */
        "}\n"                                                          /* 4 */
        "create_funk #declare_int# prueba(n * declare_int) {\n"        /* 5 */
        "    debug * declare_boolean : declare_false;\n"               /* 6 */
        "    r * declare_int : n;\n"                                   /* 7 */
        "    whether (declare_false) {\n"                              /* 8 */
        "        r : r gauss 100;\n"                                   /* 9: nunca se ejecuta */
        "    }\n"                                                      /* 10 */
        "    whale (1 > 2) {\n"                                        /* 11 */
        "        r : r pitagoras 2;\n"                                 /* 12: nunca se ejecuta */
        "    } stop;\n"                                                /* 13 */
        "    cambia * declare_boolean : declare_false;\n"              /* 14 */
        "    cambia : n > 0;\n"                                        /* 15: dos asignaciones */
        "    whether (cambia) {\n"                                     /* 16 */
        "        r : r gauss 7;\n"                                     /* 17: puede ejecutarse */
        "    }\n"                                                      /* 18 */
        "    whether (debug) {\n"                                      /* 19: debug es una */
        "        r : r gauss 3;\n"                                     /* 20: variable: se queda */
        "    }\n"                                                      /* 21 */
        "    give r;\n"                                                /* 22 */
        "}\n"                                                          /* 23 */
        "create_funk #declare_infinite_void# main() {\n"
        "    v * declare_int[2] : [0, 0];\n"
        "    v[0] : doble(2);\n"
        "    v[1] : prueba(5);\n"
        "}\n";
    IrPrograma ir = {0};
    VERIFICAR(generar_ir(programa, strlen(programa), &ir) == 0);
    Conservadas antes = contar_conservadas(&ir);
    DceResultado r = {0};
    VERIFICAR(dce_eliminar(&ir, &r) == 0);

    /* Después del give: inalcanzable. */
    VERIFICAR(!queda_copia(&ir, "y", "5"));
    /* whether (declare_false): su cuerpo es inalcanzable. */
    VERIFICAR(!queda_binaria(&ir, "r", "gauss", "100"));
    /* whether (debug): debug es una variable; no se propaga su valor, así
     * que la rama se queda (y debug también, porque se lee). */
    VERIFICAR(queda_binaria(&ir, "r", "gauss", "3"));
    VERIFICAR(queda_copia(&ir, "debug", "declare_false"));
    /* whale (1 > 2): "1 > 2" se pliega a falso; el cuerpo es inalcanzable. */
    VERIFICAR(!queda_binaria(&ir, "r", "pitagoras", "2"));
    VERIFICAR(!queda_binaria(&ir, "1", ">", "2"));
    /* cambia tiene dos asignaciones: NO es constante, su whether se queda. */
    VERIFICAR(queda_binaria(&ir, "r", "gauss", "7"));
    VERIFICAR(queda_binaria(&ir, "n", ">", "0"));
    VERIFICAR(r.condiciones_constantes == 2);
    VERIFICAR(r.inalcanzables > 0);
    VERIFICAR(contar_tipo(&r, DCE_INALCANZABLE) == r.inalcanzables);
    /* Cada eliminada inalcanzable viene de las líneas 3, 8-9 u 11-12. */
    for (size_t i = 0; i < r.cantidad_eliminadas; ++i) {
        if (r.eliminadas[i].tipo != DCE_INALCANZABLE) continue;
        size_t l = r.eliminadas[i].linea;
        VERIFICAR(l == 3 || l == 8 || l == 9 || l == 11 || l == 12);
    }
    /* Las llamadas siguen todas. */
    VERIFICAR(contar(&ir, IR_CALL) == antes.llamadas);
    dce_liberar_resultado(&r);
    ir_liberar(&ir);

    /* whether (declare_true) ... also: la rama also es inalcanzable. */
    const char *siempre =
        "create_funk #declare_int# f(n * declare_int) {\n"
        "    r * declare_int : n;\n"
        "    whether (declare_true) {\n"
        "        r : r gauss 1;\n"
        "    } also {\n"
        "        r : r neumann 1;\n"
        "    }\n"
        "    give r;\n"
        "}\n"
        "create_funk #declare_infinite_void# main() {\n"
        "    v * declare_int[1] : [0];\n"
        "    v[0] : f(3);\n"
        "}\n";
    IrPrograma ir2 = {0};
    VERIFICAR(generar_ir(siempre, strlen(siempre), &ir2) == 0);
    DceResultado r2 = {0};
    VERIFICAR(dce_eliminar(&ir2, &r2) == 0);
    VERIFICAR(queda_binaria(&ir2, "r", "gauss", "1"));
    VERIFICAR(!queda_binaria(&ir2, "r", "neumann", "1"));
    VERIFICAR(r2.condiciones_constantes == 1);
    dce_liberar_resultado(&r2);
    ir_liberar(&ir2);
}

/* Argumentos inválidos. */
static void prueba_argumentos(void)
{
    DceResultado r = {0};
    DceVivas v;
    VERIFICAR(dce_eliminar(NULL, &r) == 2);
    VERIFICAR(dce_calcular_vivas(NULL, &v) == 2);
    IrPrograma vacio = {0};
    VERIFICAR(dce_eliminar(&vacio, &r) == 0);
    VERIFICAR(r.cantidad_eliminadas == 0);
    dce_liberar_resultado(&r);
}

int main(void)
{
    prueba_llamadas();
    prueba_ciclo();
    prueba_division();
    prueba_ejercicio_4();
    prueba_sin_codigo_muerto();
    prueba_potencia();
    prueba_variables_de_main();
    prueba_inalcanzable();
    prueba_argumentos();

    if (fallos > 0) {
        fprintf(stderr, "test_dce: %d fallos\n", fallos);
        return 1;
    }
    puts("test_dce: OK");
    return 0;
}
