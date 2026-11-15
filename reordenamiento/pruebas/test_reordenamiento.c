/* Pruebas del reordenamiento seguro.
 *
 * 1. Casos dirigidos: WAR, WAW, cadena RAW, control (etiquetas y saltos),
 *    llamadas (efectos), memoria y latencia 1.
 * 2. Pruebas aleatorias: se generan 1000 programas con pocas variables
 *    (muchos WAR/WAW), saltos condicionales y llamadas. Cada uno se ejecuta
 *    con un interprete antes y despues de reordenar: las variables finales y
 *    la traza de llamadas deben ser identicas, y los ciclos estimados nunca
 *    pueden empeorar. */
#include "reordenamiento.h"
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
static void empezar(IrPrograma *ir) { *ir = (IrPrograma){0}; I(ir, IR_FUNC, "main", NULL, NULL, NULL); }
static void terminar(IrPrograma *ir) {
    I(ir, IR_END_FUNC, "main", NULL, NULL, NULL);
    assert(ir_bloques_basicos(ir) == 0);
}

/* ------------------------------------------------------------------ */
/* Interprete (aritmetica sin signo: nunca hay desborde indefinido)    */
/* ------------------------------------------------------------------ */

typedef struct { char nombre[24]; unsigned long long v; } Var;
typedef struct {
    Var vars[256]; size_t nv;
    unsigned long long args[16], mem[8]; size_t nargs;
    char traza[8192]; size_t largo;
} Estado;

static unsigned long long *ref(Estado *e, const char *n) {
    for (size_t i = 0; i < e->nv; ++i) if (!strcmp(e->vars[i].nombre, n)) return &e->vars[i].v;
    assert(e->nv < 256 && strlen(n) < 24);
    strcpy(e->vars[e->nv].nombre, n);
    e->vars[e->nv].v = 0;
    return &e->vars[e->nv++].v;
}
static unsigned long long val(Estado *e, const char *s) {
    char *fin;
    unsigned long long x = strtoull(s, &fin, 10);
    if (*s && !*fin) return x;
    return *ref(e, s);
}
static unsigned long long bin(const char *o, unsigned long long a, unsigned long long b) {
    if (!strcmp(o, "gauss")) return a + b;
    if (!strcmp(o, "neumann")) return a - b;
    if (!strcmp(o, "pitagoras")) return a * b;
    if (!strcmp(o, "<")) return a < b;
    if (!strcmp(o, "==")) return a == b;
    assert(!"operador desconocido");
    return 0;
}
static size_t etiqueta(const IrPrograma *ir, const char *e) {
    for (size_t k = 0; k < ir->cantidad; ++k)
        if (ir->codigo[k].op == IR_LABEL && !strcmp(ir->codigo[k].dst, e)) return k;
    assert(!"etiqueta inexistente");
    return 0;
}
static void anotar(Estado *e, const char *fmt, unsigned long long v) {
    e->largo += (size_t)snprintf(e->traza + e->largo, sizeof e->traza - e->largo, fmt, v);
    assert(e->largo < sizeof e->traza - 64);
}
/* Llamadas externas: devuelven suma(args)+1 y quedan en la traza.
 * "store r, v" escribe mem[r % 8]; "x = load r" lee mem[r % 8]. */
static void ejecutar(const IrPrograma *ir, Estado *e) {
    memset(e, 0, sizeof *e);
    size_t pc = 0, pasos = 0;
    while (pc < ir->cantidad) {
        const IrInstr *x = &ir->codigo[pc++];
        assert(++pasos < 1000000);
        switch (x->op) {
        case IR_FUNC: case IR_LABEL: break;
        case IR_END_FUNC: case IR_RETURN: return;
        case IR_COPY: *ref(e, x->dst) = val(e, x->a); break;
        case IR_BIN: *ref(e, x->dst) = bin(x->operador, val(e, x->a), val(e, x->b)); break;
        case IR_JUMP: pc = etiqueta(ir, x->dst); break;
        case IR_JFALSE: if (!val(e, x->a)) pc = etiqueta(ir, x->dst); break;
        case IR_ARG: e->args[e->nargs++] = val(e, x->a); break;
        case IR_CALL: {
            unsigned long long s = 1;
            anotar(e, "%s", 0); /* separador (el valor no se usa) */
            for (size_t i = 0; i < e->nargs; ++i) { s += e->args[i]; anotar(e, "%llu,", e->args[i]); }
            anotar(e, ";", 0);
            e->nargs = 0;
            *ref(e, x->dst) = s;
            break;
        }
        case IR_STORE: e->mem[val(e, x->dst) % 8] = val(e, x->a); break;
        case IR_LOAD: *ref(e, x->dst) = e->mem[val(e, x->a) % 8]; break;
        default: assert(!"operacion no soportada");
        }
    }
}
static void comparar(Estado *a, Estado *b) {
    for (size_t i = 0; i < a->nv; ++i) {
        unsigned long long vb = *ref(b, a->vars[i].nombre);
        if (vb != a->vars[i].v) {
            fprintf(stderr, "variable %s: antes %llu, despues %llu\n", a->vars[i].nombre, a->vars[i].v, vb);
            assert(0);
        }
    }
    assert(!memcmp(a->mem, b->mem, sizeof a->mem));
    assert(!strcmp(a->traza, b->traza));
}

/* Posicion de la primera instruccion con esa operacion y ese destino/a. */
static size_t pos(const IrPrograma *ir, IrOp op, const char *dst, const char *a) {
    for (size_t k = 0; k < ir->cantidad; ++k) {
        const IrInstr *x = &ir->codigo[k];
        if (x->op == op && (!dst || (x->dst && !strcmp(x->dst, dst))) && (!a || (x->a && !strcmp(x->a, a)))) return k;
    }
    assert(!"instruccion no encontrada");
    return 0;
}

/* Reordena `ir` y compara su ejecucion con `copia` (el mismo programa). */
static RoResumen reordenar_y_comparar(IrPrograma *ir, IrPrograma *copia, unsigned latencia) {
    RoOpciones op = {latencia};
    RoResumen r;
    assert(ro_reordenar(ir, &op, &r) == 0);
    assert(r.ciclos_despues <= r.ciclos_antes);          /* Nunca empeora */
    assert(ir->cantidad == copia->cantidad);              /* No crea ni borra */
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {   /* Control: extremos fijos */
        size_t ini = ir->bloques[b].inicio, fin = ir->bloques[b].fin;
        IrOp primero = copia->codigo[ini].op, ultimo = copia->codigo[fin - 1].op;
        if (primero == IR_LABEL || primero == IR_FUNC)        /* Etiqueta/func siguen primero */
            assert(r.origen[ini] == ini);
        if (ultimo == IR_JUMP || ultimo == IR_JFALSE || ultimo == IR_JTRUE ||
            ultimo == IR_RETURN || ultimo == IR_END_FUNC)      /* Salto/return sigue ultimo */
            assert(r.origen[fin - 1] == fin - 1);
        for (size_t k = ini; k < fin; ++k) assert(r.origen[k] >= ini && r.origen[k] < fin); /* No sale del bloque */
    }
    Estado ea, eb;
    ejecutar(copia, &ea);
    ejecutar(ir, &eb);
    comparar(&ea, &eb);
    return r;
}

/* ------------------------------------------------------------------ */
/* Casos dirigidos                                                     */
/* ------------------------------------------------------------------ */

/* WAR: "a = 20" no puede subir antes de "$t1 = a pitagoras 2". */
static void p_war(IrPrograma *ir) {
    empezar(ir);
    I(ir, IR_COPY, "a", "3", NULL, NULL);
    I(ir, IR_COPY, "x", "4", NULL, NULL);
    I(ir, IR_BIN, "$t1", "a", "2", "pitagoras");
    I(ir, IR_COPY, "b", "$t1", NULL, NULL);
    I(ir, IR_COPY, "a", "20", NULL, NULL);
    I(ir, IR_BIN, "$t2", "x", "3", "pitagoras");
    I(ir, IR_COPY, "c", "$t2", NULL, NULL);
    I(ir, IR_BIN, "$t3", "a", "b", "gauss");
    I(ir, IR_COPY, "d", "$t3", NULL, NULL);
    terminar(ir);
}
/* WAW: "r = $t1" y "r = $t2" conservan su orden. */
static void p_waw(IrPrograma *ir) {
    empezar(ir);
    I(ir, IR_COPY, "p", "5", NULL, NULL);
    I(ir, IR_BIN, "$t1", "p", "1", "gauss");
    I(ir, IR_COPY, "r", "$t1", NULL, NULL);
    I(ir, IR_BIN, "$t2", "p", "7", "gauss");
    I(ir, IR_COPY, "r", "$t2", NULL, NULL);
    I(ir, IR_BIN, "$t3", "p", "p", "pitagoras");
    I(ir, IR_COPY, "q", "$t3", NULL, NULL);
    I(ir, IR_BIN, "$t4", "r", "3", "gauss");
    I(ir, IR_COPY, "s", "$t4", NULL, NULL);
    terminar(ir);
}
/* Cadena RAW pura: cada instruccion usa la anterior, no hay nada que mover. */
static void p_cadena(IrPrograma *ir) {
    empezar(ir);
    I(ir, IR_COPY, "a", "1", NULL, NULL);
    I(ir, IR_BIN, "a", "a", "2", "gauss");
    I(ir, IR_BIN, "a", "a", "3", "pitagoras");
    I(ir, IR_BIN, "a", "a", "4", "gauss");
    terminar(ir);
}
/* Control: un bloque que termina en ifFalse y otro que empieza con etiqueta. */
static void p_control(IrPrograma *ir) {
    empezar(ir);
    I(ir, IR_BIN, "$t1", "4", "6", "gauss");
    I(ir, IR_COPY, "x", "$t1", NULL, NULL);
    I(ir, IR_BIN, "$t2", "2", "5", "gauss");
    I(ir, IR_COPY, "y", "$t2", NULL, NULL);
    I(ir, IR_BIN, "$t3", "y", "x", "<");
    I(ir, IR_JFALSE, "$L1", "$t3", NULL, NULL);
    I(ir, IR_COPY, "m", "1", NULL, NULL);
    I(ir, IR_LABEL, "$L1", NULL, NULL, NULL);
    I(ir, IR_BIN, "$t4", "x", "1", "gauss");
    I(ir, IR_COPY, "z", "$t4", NULL, NULL);
    I(ir, IR_BIN, "$t5", "y", "2", "pitagoras");
    I(ir, IR_COPY, "w", "$t5", NULL, NULL);
    terminar(ir);
}
/* Llamadas: arg/call no se mueven y nada las cruza. */
static void p_llamadas(IrPrograma *ir) {
    empezar(ir);
    I(ir, IR_BIN, "$t1", "1", "1", "gauss");
    I(ir, IR_COPY, "x", "$t1", NULL, NULL);
    I(ir, IR_BIN, "$t2", "x", "3", "pitagoras");
    I(ir, IR_COPY, "y", "$t2", NULL, NULL);
    I(ir, IR_ARG, NULL, "x", NULL, NULL);
    I(ir, IR_CALL, "$t3", "f", "1", NULL);
    I(ir, IR_COPY, "r1", "$t3", NULL, NULL);
    I(ir, IR_BIN, "$t4", "y", "1", "gauss");
    I(ir, IR_COPY, "w", "$t4", NULL, NULL);
    I(ir, IR_BIN, "$t5", "2", "2", "pitagoras");
    I(ir, IR_COPY, "z", "$t5", NULL, NULL);
    I(ir, IR_ARG, NULL, "w", NULL, NULL);
    I(ir, IR_CALL, "$t6", "f", "1", NULL);
    I(ir, IR_COPY, "r2", "$t6", NULL, NULL);
    terminar(ir);
}
/* Memoria: el load debe quedar despues del store que escribe ese lugar. */
static void p_memoria(IrPrograma *ir) {
    empezar(ir);
    I(ir, IR_COPY, "r", "3", NULL, NULL);
    I(ir, IR_STORE, "r", "42", NULL, NULL);
    I(ir, IR_BIN, "$t1", "5", "5", "pitagoras");
    I(ir, IR_COPY, "q", "$t1", NULL, NULL);
    I(ir, IR_LOAD, "v", "r", NULL, NULL);
    I(ir, IR_BIN, "$t2", "v", "1", "gauss");
    I(ir, IR_STORE, "r", "$t2", NULL, NULL);
    I(ir, IR_LOAD, "u", "r", NULL, NULL);
    terminar(ir);
}

typedef void (*Programa)(IrPrograma *);
static RoResumen caso(const char *nombre, Programa p, unsigned latencia, IrPrograma *ir) {
    IrPrograma copia;
    p(ir); p(&copia);
    RoResumen r = reordenar_y_comparar(ir, &copia, latencia);
    printf("  %-34s movidas %2zu | ciclos %3zu -> %3zu | RAW %zu WAR %zu WAW %zu mem %zu efec %zu ctrl %zu\n",
           nombre, r.movidas, r.ciclos_antes, r.ciclos_despues, r.raw, r.war, r.waw, r.memoria, r.efecto, r.control);
    ir_liberar(&copia);
    return r;
}

/* ------------------------------------------------------------------ */
/* Programas aleatorios                                                */
/* ------------------------------------------------------------------ */

static unsigned long semilla;
static unsigned azar(unsigned n) { semilla = semilla * 1103515245UL + 12345UL; return (unsigned)((semilla >> 16) % n); }
static const char *VARS[] = {"a", "b", "c", "d", "e"};          /* Pocas: muchos WAR/WAW */
static const char *OPS[] = {"gauss", "neumann", "pitagoras", "<", "=="};
static unsigned tmp;

static const char *operando(char *buf) {
    if (azar(4) == 0) { snprintf(buf, 16, "%u", azar(10)); return buf; } /* Constante */
    return VARS[azar(5)];
}
/* Bloque aleatorio: asignaciones con temporales, a veces llamadas o memoria. */
static void bloque_azar(IrPrograma *ir) {
    unsigned n = 2 + azar(8);
    for (unsigned i = 0; i < n; ++i) {
        char t[16], x[16], y[16];
        unsigned tipo = azar(10);
        if (tipo == 0) {                                     /* Llamada con 1 o 2 argumentos */
            unsigned na = 1 + azar(2);
            for (unsigned k = 0; k < na; ++k) I(ir, IR_ARG, NULL, operando(x), NULL, NULL);
            snprintf(t, sizeof t, "$t%u", ++tmp);
            I(ir, IR_CALL, t, "f", na == 1 ? "1" : "2", NULL);
            I(ir, IR_COPY, VARS[azar(5)], t, NULL, NULL);
        } else if (tipo == 1) {
            I(ir, IR_STORE, VARS[azar(5)], operando(x), NULL, NULL);
        } else if (tipo == 2) {
            I(ir, IR_LOAD, VARS[azar(5)], VARS[azar(5)], NULL, NULL);
        } else if (tipo == 3) {
            I(ir, IR_COPY, VARS[azar(5)], operando(x), NULL, NULL);
        } else {                                             /* $t = x op y ; var = $t */
            snprintf(t, sizeof t, "$t%u", ++tmp);
            I(ir, IR_BIN, t, operando(x), operando(y), OPS[azar(5)]);
            I(ir, IR_COPY, VARS[azar(5)], t, NULL, NULL);
        }
    }
}
/* A ; if (cond) { B } else { C } ; D */
static void programa_azar(IrPrograma *ir, unsigned long s) {
    semilla = s; tmp = 0;
    empezar(ir);
    for (unsigned i = 0; i < 5; ++i) {                       /* Valores iniciales distintos */
        char v[16]; snprintf(v, sizeof v, "%u", 1 + azar(9));
        I(ir, IR_COPY, VARS[i], v, NULL, NULL);
    }
    bloque_azar(ir);
    I(ir, IR_BIN, "$tc", VARS[azar(5)], VARS[azar(5)], "<");
    I(ir, IR_JFALSE, "$L1", "$tc", NULL, NULL);
    bloque_azar(ir);
    I(ir, IR_JUMP, "$L2", NULL, NULL, NULL);
    I(ir, IR_LABEL, "$L1", NULL, NULL, NULL);
    bloque_azar(ir);
    I(ir, IR_LABEL, "$L2", NULL, NULL, NULL);
    bloque_azar(ir);
    terminar(ir);
}

int main(void) {
    IrPrograma ir;
    RoResumen r;

    puts("[1] WAR");
    r = caso("a se lee y luego se reescribe", p_war, 3, &ir);
    assert(r.movidas > 0 && r.war >= 1);
    assert(pos(&ir, IR_COPY, "a", "20") > pos(&ir, IR_BIN, "$t1", "a"));   /* No subio antes de la lectura */
    assert(pos(&ir, IR_BIN, "$t3", "a") > pos(&ir, IR_COPY, "a", "20"));   /* La lectura nueva ve 20 */
    ro_resumen_liberar(&r); ir_liberar(&ir);

    puts("[2] WAW");
    r = caso("r se escribe dos veces", p_waw, 3, &ir);
    assert(r.movidas > 0 && r.waw >= 1);
    assert(pos(&ir, IR_COPY, "r", "$t1") < pos(&ir, IR_COPY, "r", "$t2"));  /* Orden de escrituras */
    ro_resumen_liberar(&r); ir_liberar(&ir);

    puts("[3] Cadena RAW (nada que mover)");
    r = caso("a = a op k, cuatro veces", p_cadena, 3, &ir);
    assert(r.movidas == 0 && r.ciclos_antes == r.ciclos_despues);
    ro_resumen_liberar(&r); ir_liberar(&ir);

    puts("[4] Control");
    r = caso("ifFalse y etiqueta fijos", p_control, 3, &ir);
    assert(ir.codigo[pos(&ir, IR_JFALSE, NULL, NULL) - 1].op != IR_LABEL);
    assert(pos(&ir, IR_BIN, "$t3", NULL) < pos(&ir, IR_JFALSE, NULL, NULL)); /* Condicion antes del salto */
    assert(pos(&ir, IR_LABEL, "$L1", NULL) == 8);                          /* La etiqueta no se mueve */
    ro_resumen_liberar(&r); ir_liberar(&ir);

    puts("[5] Llamadas (efectos)");
    r = caso("arg/call como barreras", p_llamadas, 3, &ir);
    assert(pos(&ir, IR_ARG, NULL, "x") == 5 && pos(&ir, IR_CALL, "$t3", NULL) == 6);
    assert(pos(&ir, IR_ARG, NULL, "w") == 12 && pos(&ir, IR_CALL, "$t6", NULL) == 13);
    assert(r.efecto > 0);
    ro_resumen_liberar(&r); ir_liberar(&ir);

    puts("[6] Memoria");
    r = caso("store / load en orden", p_memoria, 3, &ir);
    assert(pos(&ir, IR_LOAD, "v", NULL) > pos(&ir, IR_STORE, "r", "42"));
    assert(pos(&ir, IR_LOAD, "u", NULL) > pos(&ir, IR_STORE, "r", "$t2"));
    assert(r.memoria > 0);
    ro_resumen_liberar(&r); ir_liberar(&ir);

    puts("[7] Latencia 1 (no hay esperas que esconder)");
    r = caso("WAR con latencia 1", p_war, 1, &ir);
    assert(r.movidas == 0);
    ro_resumen_liberar(&r); ir_liberar(&ir);

    puts("[8] 1000 programas aleatorios (WAR/WAW, saltos, llamadas, memoria)");
    size_t movidas = 0, antes = 0, despues = 0;
    for (unsigned long s = 1; s <= 1000; ++s) {
        IrPrograma a, b;
        programa_azar(&a, s);
        programa_azar(&b, s);
        RoResumen x = reordenar_y_comparar(&a, &b, 1 + (unsigned)(s % 4));
        movidas += x.movidas; antes += x.ciclos_antes; despues += x.ciclos_despues;
        ro_resumen_liberar(&x); ir_liberar(&a); ir_liberar(&b);
    }
    printf("  1000 programas: %zu instrucciones movidas, ciclos %zu -> %zu, mismos resultados\n",
           movidas, antes, despues);
    assert(movidas > 0 && despues < antes);

    puts("OK: pruebas de reordenamiento");
    return 0;
}
