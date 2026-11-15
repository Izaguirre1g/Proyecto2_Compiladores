/* Pruebas del loop unrolling sobre IR construida a mano.
 *
 * Cada caso construye el mismo programa dos veces, desenrolla una copia y
 * ejecuta ambas con un interprete pequeno de la IR. La optimizacion es
 * correcta si las variables del programa terminan con los mismos valores y
 * las llamadas (efectos) ocurren en el mismo orden con los mismos argumentos.
 * Tambien se reporta cuantas instrucciones ejecuta cada version. */
#include "loop_unrolling.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Construccion de IR                                                  */
/* ------------------------------------------------------------------ */

static char *dup(const char *s) {
    if (!s) return NULL;
    char *p = malloc(strlen(s) + 1);
    assert(p);
    return strcpy(p, s);
}
static void I(IrPrograma *ir, IrOp op, const char *d, const char *a, const char *b, const char *o) {
    if (ir->cantidad == ir->capacidad) {
        ir->capacidad = ir->capacidad ? ir->capacidad * 2 : 32;
        ir->codigo = realloc(ir->codigo, ir->capacidad * sizeof *ir->codigo);
        assert(ir->codigo);
    }
    ir->codigo[ir->cantidad] = (IrInstr){op, dup(d), dup(a), dup(b), dup(o), ir->cantidad + 1};
    ir->cantidad++;
}
/* Ciclo `cycle iter let ini until lim step paso { cuerpo }` tal como lo
 * genera src/Representacion_Intermedia/ir.c. `cuerpo` recibe los nombres que debe usar. */
typedef void (*Cuerpo)(IrPrograma *, int);
static int sig_t = 1, sig_l = 1;
static char *T(void) { static char b[8][16]; static int r; snprintf(b[r & 7], 16, "$t%d", sig_t++); return b[r++ & 7]; }
static char *L(void) { static char b[8][16]; static int r; snprintf(b[r & 7], 16, "$L%d", sig_l++); return b[r++ & 7]; }
static void ciclo(IrPrograma *ir, const char *iter, const char *ini, const char *rel,
                  const char *lim, const char *paso, Cuerpo cuerpo, int arg) {
    char li[16], lf[16], tc[16], tp[16];
    strcpy(li, L()); strcpy(lf, L()); strcpy(tc, T());
    I(ir, IR_COPY, iter, ini, NULL, NULL);
    I(ir, IR_LABEL, li, NULL, NULL, NULL);
    I(ir, IR_BIN, tc, iter, lim, rel);
    I(ir, IR_JFALSE, lf, tc, NULL, NULL);
    cuerpo(ir, arg);
    strcpy(tp, T());
    I(ir, IR_BIN, tp, iter, paso, "gauss");
    I(ir, IR_COPY, iter, tp, NULL, NULL);
    I(ir, IR_JUMP, li, NULL, NULL, NULL);
    I(ir, IR_LABEL, lf, NULL, NULL, NULL);
}
static void terminar(IrPrograma *ir) {
    I(ir, IR_END_FUNC, "main", NULL, NULL, NULL);
    ir->temporales = (unsigned)sig_t;
    ir->etiquetas = (unsigned)sig_l;
    assert(ir_bloques_basicos(ir) == 0);
}
static void empezar(IrPrograma *ir) {
    *ir = (IrPrograma){0};
    sig_t = sig_l = 1;
    I(ir, IR_FUNC, "main", NULL, NULL, NULL);
}

/* ------------------------------------------------------------------ */
/* Interprete de la IR (subconjunto escalar)                           */
/* ------------------------------------------------------------------ */

typedef struct { char nombre[24]; long long v; } Var;
typedef struct {
    Var vars[512]; size_t nv;
    long long args[16]; size_t nargs;
    char traza[4096]; size_t largo_traza;
    size_t pasos;
} Estado;

static long long *ref(Estado *e, const char *n) {
    for (size_t i = 0; i < e->nv; ++i) if (!strcmp(e->vars[i].nombre, n)) return &e->vars[i].v;
    assert(e->nv < 512 && strlen(n) < 24);
    strcpy(e->vars[e->nv].nombre, n);
    e->vars[e->nv].v = 0;
    return &e->vars[e->nv++].v;
}
static long long val(Estado *e, const char *s) {
    char *fin;
    long long x = strtoll(s, &fin, 10);
    if (*s && !*fin) return x;
    return *ref(e, s);
}
static long long binaria(const char *o, long long a, long long b) {
    if (!strcmp(o, "gauss")) return a + b;
    if (!strcmp(o, "neumann")) return a - b;
    if (!strcmp(o, "pitagoras")) return a * b;
    if (!strcmp(o, "euclides")) return b ? a / b : 0;
    if (!strcmp(o, "euler")) return b ? a % b : 0;
    if (!strcmp(o, "<")) return a < b;
    if (!strcmp(o, "<=")) return a <= b;
    if (!strcmp(o, ">")) return a > b;
    if (!strcmp(o, ">=")) return a >= b;
    if (!strcmp(o, "==")) return a == b;
    if (!strcmp(o, "=/=")) return a != b;
    if (!strcmp(o, "&&")) return a && b;
    if (!strcmp(o, "||")) return a || b;
    assert(!"operador desconocido");
    return 0;
}
static size_t buscar_etiqueta(const IrPrograma *ir, const char *e) {
    for (size_t k = 0; k < ir->cantidad; ++k)
        if (ir->codigo[k].op == IR_LABEL && !strcmp(ir->codigo[k].dst, e)) return k;
    assert(!"etiqueta inexistente");
    return 0;
}
/* Las llamadas son externas: devuelven suma(args) + 1 y quedan en la traza. */
static void ejecutar(const IrPrograma *ir, Estado *e) {
    memset(e, 0, sizeof *e);
    size_t pc = 0;
    while (pc < ir->cantidad) {
        const IrInstr *x = &ir->codigo[pc++];
        e->pasos++;
        assert(e->pasos < 10000000);
        switch (x->op) {
        case IR_FUNC: case IR_LABEL: case IR_PARAM_DECL: break;
        case IR_END_FUNC: return;
        case IR_COPY: *ref(e, x->dst) = val(e, x->a); break;
        case IR_BIN: *ref(e, x->dst) = binaria(x->operador, val(e, x->a), val(e, x->b)); break;
        case IR_UNARY: {
            long long a = val(e, x->a);
            *ref(e, x->dst) = !strcmp(x->operador, "neumann") ? -a : !strcmp(x->operador, "~") ? !a : a != 0;
            break;
        }
        case IR_JUMP: pc = buscar_etiqueta(ir, x->dst); break;
        case IR_JFALSE: if (!val(e, x->a)) pc = buscar_etiqueta(ir, x->dst); break;
        case IR_JTRUE: if (val(e, x->a)) pc = buscar_etiqueta(ir, x->dst); break;
        case IR_ARG: assert(e->nargs < 16); e->args[e->nargs++] = val(e, x->a); break;
        case IR_CALL: {
            long long s = 1;
            int n = snprintf(e->traza + e->largo_traza, sizeof e->traza - e->largo_traza, "%s(", x->a);
            e->largo_traza += (size_t)n;
            for (size_t i = 0; i < e->nargs; ++i) {
                s += e->args[i];
                n = snprintf(e->traza + e->largo_traza, sizeof e->traza - e->largo_traza, "%lld,", e->args[i]);
                e->largo_traza += (size_t)n;
            }
            n = snprintf(e->traza + e->largo_traza, sizeof e->traza - e->largo_traza, ") ");
            e->largo_traza += (size_t)n;
            assert(e->largo_traza < sizeof e->traza - 64);
            e->nargs = 0;
            *ref(e, x->dst) = s;
            break;
        }
        case IR_RETURN: return;
        default: assert(!"operacion no soportada por el interprete");
        }
    }
}
/* Las variables del programa (no temporales) deben coincidir. */
static void comparar(Estado *a, Estado *b) {
    for (size_t i = 0; i < a->nv; ++i) {
        if (a->vars[i].nombre[0] == '$') continue;
        long long vb = *ref(b, a->vars[i].nombre);
        if (vb != a->vars[i].v) {
            fprintf(stderr, "variable %s: antes %lld, despues %lld\n", a->vars[i].nombre, a->vars[i].v, vb);
            assert(0);
        }
    }
    assert(!strcmp(a->traza, b->traza));
}
static size_t contar(const IrPrograma *ir, IrOp op) {
    size_t n = 0;
    for (size_t k = 0; k < ir->cantidad; ++k) n += ir->codigo[k].op == op;
    return n;
}
/* Ninguna etiqueta se define dos veces despues de copiar cuerpos. */
static void etiquetas_unicas(const IrPrograma *ir) {
    for (size_t i = 0; i < ir->cantidad; ++i) if (ir->codigo[i].op == IR_LABEL)
        for (size_t j = i + 1; j < ir->cantidad; ++j)
            assert(ir->codigo[j].op != IR_LABEL || strcmp(ir->codigo[i].dst, ir->codigo[j].dst));
}

typedef void (*Programa)(IrPrograma *, int);
/* Construye, desenrolla, ejecuta y compara. Devuelve el resumen. */
static LuResumen probar(const char *nombre, Programa prog, int arg, unsigned factor, IrPrograma *salida) {
    IrPrograma antes, despues;
    prog(&antes, arg);
    prog(&despues, arg);
    LuOpciones op;
    lu_opciones_defecto(&op);
    if (factor) op.factor = factor;
    LuResumen r;
    assert(lu_desenrollar(&despues, &op, &r, NULL) == 0);
    assert(despues.cantidad_bloques > 0);
    etiquetas_unicas(&despues);
    Estado ea, eb;
    ejecutar(&antes, &ea);
    ejecutar(&despues, &eb);
    comparar(&ea, &eb);
    printf("  %-38s arg=%-5d IR %3zu -> %3zu | ejecutadas %6zu -> %6zu | total %zu parcial %zu sin cambio %zu\n",
           nombre, arg, r.instr_antes, r.instr_despues, ea.pasos, eb.pasos, r.totales, r.parciales, r.rechazados);
    assert(r.cantidad_detalle == r.ciclos);   /* Un detalle por ciclo */
    lu_resumen_liberar(&r);                   /* Los contadores siguen validos */
    ir_liberar(&antes);
    if (salida) *salida = despues; else ir_liberar(&despues);
    return r;
}

/* ------------------------------------------------------------------ */
/* Programas de prueba                                                 */
/* ------------------------------------------------------------------ */

/* suma : suma gauss i */
static void cuerpo_suma(IrPrograma *ir, int x) {
    (void)x;
    char t[16]; strcpy(t, T());
    I(ir, IR_BIN, t, "suma", "i", "gauss");
    I(ir, IR_COPY, "suma", t, NULL, NULL);
}
/* 1. N constante: cycle i let 0 until 5 */
static void p_constante(IrPrograma *ir, int x) {
    (void)x; empezar(ir);
    I(ir, IR_COPY, "suma", "0", NULL, NULL);
    ciclo(ir, "i", "0", "<", "5", "1", cuerpo_suma, 0);
    terminar(ir);
}
/* 2/3. N variable desconocido: n viene de una llamada (leer(arg) = arg + 1). */
static void p_variable(IrPrograma *ir, int n) {
    char num[16]; snprintf(num, sizeof num, "%d", n - 1);
    empezar(ir);
    I(ir, IR_ARG, NULL, num, NULL, NULL);
    I(ir, IR_CALL, "$t90", "leer", "1", NULL);
    I(ir, IR_COPY, "n", "$t90", NULL, NULL);
    I(ir, IR_COPY, "suma", "0", NULL, NULL);
    ciclo(ir, "i", "0", "<", "n", "1", cuerpo_suma, 0);
    terminar(ir);
}
/* Variable con valor conocido al entrar: n = arg (constante propagada). */
static void p_variable_conocida(IrPrograma *ir, int n) {
    char num[16]; snprintf(num, sizeof num, "%d", n);
    empezar(ir);
    I(ir, IR_COPY, "n", num, NULL, NULL);
    I(ir, IR_COPY, "suma", "0", NULL, NULL);
    ciclo(ir, "i", "0", "<", "n", "1", cuerpo_suma, 0);
    terminar(ir);
}
/* Paso 3 con <=: cycle i let 2 until/<= 40 step 3 */
static void p_paso_menor_igual(IrPrograma *ir, int lim) {
    char num[16]; snprintf(num, sizeof num, "%d", lim);
    empezar(ir);
    I(ir, IR_COPY, "suma", "0", NULL, NULL);
    ciclo(ir, "i", "2", "<=", num, "3", cuerpo_suma, 0);
    terminar(ir);
}
/* whether (i euler 2 == 0) pares++ ; total : total gauss f(i) */
static void cuerpo_if_llamada(IrPrograma *ir, int x) {
    (void)x;
    char t1[16], t2[16], t3[16], t4[16], t5[16], ls[16], lf[16];
    strcpy(t1, T()); strcpy(t2, T()); strcpy(t3, T()); strcpy(t4, T()); strcpy(t5, T());
    strcpy(ls, L()); strcpy(lf, L());
    I(ir, IR_BIN, t1, "i", "2", "euler");
    I(ir, IR_BIN, t2, t1, "0", "==");
    I(ir, IR_JFALSE, ls, t2, NULL, NULL);
    I(ir, IR_BIN, t3, "pares", "1", "gauss");
    I(ir, IR_COPY, "pares", t3, NULL, NULL);
    I(ir, IR_JUMP, lf, NULL, NULL, NULL);
    I(ir, IR_LABEL, ls, NULL, NULL, NULL);
    I(ir, IR_LABEL, lf, NULL, NULL, NULL);
    I(ir, IR_ARG, NULL, "i", NULL, NULL);
    I(ir, IR_CALL, t4, "f", "1", NULL);
    I(ir, IR_BIN, t5, "total", t4, "gauss");
    I(ir, IR_COPY, "total", t5, NULL, NULL);
}
static void p_if_llamada(IrPrograma *ir, int n) {
    char num[16]; snprintf(num, sizeof num, "%d", n);
    empezar(ir);
    I(ir, IR_COPY, "pares", "0", NULL, NULL);
    I(ir, IR_COPY, "total", "0", NULL, NULL);
    ciclo(ir, "i", "0", "<", num, "1", cuerpo_if_llamada, 0);
    terminar(ir);
}
/* Ciclos anidados: el interno (3 vueltas) se desenrolla primero y luego el externo. */
static void cuerpo_interno(IrPrograma *ir, int lim) {
    char num[16]; snprintf(num, sizeof num, "%d", lim);
    ciclo(ir, "j", "0", "<", num, "1", cuerpo_suma, 0);
}
static void p_anidado(IrPrograma *ir, int externo) {
    char num[16]; snprintf(num, sizeof num, "%d", externo);
    empezar(ir);
    I(ir, IR_COPY, "suma", "0", NULL, NULL);
    ciclo(ir, "i", "0", "<", num, "1", cuerpo_interno, 3);
    terminar(ir);
}
/* Rechazos: el cuerpo modifica el iterador, el limite o contiene return. */
static void cuerpo_mod_iter(IrPrograma *ir, int x) { (void)x; I(ir, IR_COPY, "i", "100", NULL, NULL); }
static void cuerpo_mod_lim(IrPrograma *ir, int x) {
    (void)x; char t[16]; strcpy(t, T());
    I(ir, IR_BIN, t, "n", "1", "neumann"); I(ir, IR_COPY, "n", t, NULL, NULL);
}
static void p_rechazo(IrPrograma *ir, int caso) {
    empezar(ir);
    I(ir, IR_ARG, NULL, "9", NULL, NULL);
    I(ir, IR_CALL, "$t90", "leer", "1", NULL);
    I(ir, IR_COPY, "n", "$t90", NULL, NULL);
    ciclo(ir, "i", "0", "<", "n", "1", caso == 0 ? cuerpo_mod_iter : cuerpo_mod_lim, 0);
    terminar(ir);
}

int main(void) {
    LuResumen r;
    IrPrograma ir;

    puts("[1] N constante -> desenrollado total");
    r = probar("cycle i let 0 until 5", p_constante, 0, 0, &ir);
    assert(r.totales == 1 && r.parciales == 0);
    assert(contar(&ir, IR_JUMP) == 0 && contar(&ir, IR_LABEL) == 0 && contar(&ir, IR_JFALSE) == 0);
    ir_liberar(&ir);

    puts("[2] N variable pequeno");
    r = probar("n conocido al entrar (n = 3)", p_variable_conocida, 3, 0, NULL);
    assert(r.totales == 1);
    for (int n = 0; n <= 9; ++n) {
        r = probar("n desconocido (residuo)", p_variable, n, 0, NULL);
        assert(r.parciales == 1);
    }

    puts("[3] N variable grande");
    r = probar("n desconocido", p_variable, 1000, 0, NULL);
    assert(r.parciales == 1);
    r = probar("n conocido (1002 -> parcial + 2 sueltas)", p_variable_conocida, 1002, 0, &ir);
    assert(r.parciales == 1);
    assert(contar(&ir, IR_JUMP) == 1); /* solo el ciclo principal, sin residuo */
    ir_liberar(&ir);

    puts("[4] Factor configurable");
    for (unsigned k = 2; k <= 8; k += 2) {
        char nombre[48]; snprintf(nombre, sizeof nombre, "factor %u", k);
        r = probar(nombre, p_variable, 101, k, NULL);
        assert(r.parciales == 1);
    }

    puts("[5] Paso > 1 y condicion <=");
    for (int lim = 1; lim <= 120; lim += 17) probar("cycle i let 2 <= lim step 3", p_paso_menor_igual, lim, 0, NULL);

    puts("[6] Cuerpo con condicional y llamada (efectos)");
    r = probar("4 vueltas", p_if_llamada, 4, 0, NULL);
    assert(r.totales == 1);
    r = probar("50 vueltas", p_if_llamada, 50, 0, NULL);
    assert(r.parciales == 1);

    puts("[7] Ciclos anidados");
    r = probar("2 x 3 vueltas", p_anidado, 2, 0, NULL);
    assert(r.totales == 2);
    r = probar("40 x 3 vueltas", p_anidado, 40, 0, NULL);
    assert(r.totales == 1 && r.parciales == 1);

    puts("[8] Rechazos (la IR no cambia)");
    r = probar("el cuerpo modifica el iterador", p_rechazo, 0, 0, NULL);
    assert(r.rechazados == 1 && r.instr_antes == r.instr_despues);
    r = probar("el cuerpo modifica el limite", p_rechazo, 1, 0, NULL);
    assert(r.rechazados == 1 && r.instr_antes == r.instr_despues);

    puts("OK: pruebas de loop unrolling");
    return 0;
}
