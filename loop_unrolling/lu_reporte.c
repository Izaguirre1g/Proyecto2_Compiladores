/* =====================================================================
 * lu_reporte.c  --  Reporte visual del loop unrolling (balc --unroll)
 * =====================================================================
 * No transforma nada: solo muestra el resultado de loop_unrolling.c en la
 * terminal con tablas, la IR lado a lado y colores. Todo el texto pasa por
 * celda(), que mide el ancho visible (UTF-8 y colores) para alinear.
 * ===================================================================== */

#define _POSIX_C_SOURCE 200809L  /* Para isatty() y fileno() con -std=c11 */
#include "lu_reporte.h"
#include <stdlib.h>              /* malloc, realloc, free, getenv, strtoul */
#include <string.h>              /* strlen, strrchr, memcpy */
#include <unistd.h>              /* isatty */

/* Codigos ANSI de color (solo se usan si color != 0). */
#define RST  "\033[0m"
#define NEG  "\033[1m"           /* Negrita */
#define TEN  "\033[2m"           /* Tenue (gris) */
#define VER  "\033[32m"          /* Verde */
#define AMA  "\033[33m"          /* Amarillo */
#define ROJ  "\033[31m"          /* Rojo */
#define CIA  "\033[36m"          /* Cian */

#define ANCHO 80                 /* Ancho total del reporte */
#define COL   37                 /* Ancho de cada columna de la IR lado a lado */

/* ===================================================================== */
/* Utilidades de texto                                                    */
/* ===================================================================== */

/* Ancho visible de s: cuenta caracteres UTF-8 (no bytes). Los bytes de
 * continuacion (10xxxxxx) no ocupan espacio propio. */
static int ancho(const char *s) {
    int n = 0;
    for (; *s; ++s) if (((unsigned char)*s & 0xC0) != 0x80) ++n; /* Inicio de caracter */
    return n;
}

/* Escribe s en una celda de `w` columnas: recorta con "…" si no cabe y
 * rellena con espacios si sobra. `tono` es un color ANSI (o NULL). */
static void celda(FILE *f, const char *s, int w, const char *tono, int color) {
    int largo = ancho(s);                              /* Columnas que ocupa s */
    if (color && tono) fputs(tono, f);                 /* Abre el color */
    if (w <= 0 || largo <= w) {                        /* w <= 0: celda sin limite */
        fputs(s, f);                                   /* Cabe completo */
    } else {
        int usados = 0;                                /* Caracteres escritos */
        for (const char *p = s; *p; ++p) {
            int inicio = ((unsigned char)*p & 0xC0) != 0x80; /* Primer byte de un caracter */
            if (inicio && usados == w - 1) break;      /* Deja lugar para "…" */
            usados += inicio;
            fputc(*p, f);
        }
        fputs("…", f);
        largo = w;                                     /* Ya ocupa todo el ancho */
    }
    if (color && tono) fputs(RST, f);                  /* Cierra el color */
    for (int i = largo; i < w; ++i) fputc(' ', f);     /* Relleno */
}

/* Linea horizontal de `n` caracteres "─". */
static void raya(FILE *f, int n) { for (int i = 0; i < n; ++i) fputs("─", f); }

/* Titulo de seccion: "  TITULO" en negrita y una raya debajo. */
static void seccion(FILE *f, const char *titulo, int color) {
    fputs("\n  ", f);
    celda(f, titulo, 0, NEG, color);                   /* w = 0: sin relleno */
    fputs("\n  ", f);
    if (color) fputs(TEN, f);
    raya(f, ANCHO - 4);
    if (color) fputs(RST, f);
    fputc('\n', f);
}

/* ===================================================================== */
/* Vista (foto) de la IR                                                  */
/* ===================================================================== */

/* Escribe en b el texto de una instruccion, igual que ir_imprimir()
 * pero sin el "; linea N" (para que quepa en una columna). */
static void formatear(const IrInstr *x, char *b, size_t n) {
    switch (x->op) {
    case IR_FUNC:       snprintf(b, n, "func %s", x->dst); break;
    case IR_END_FUNC:   snprintf(b, n, "endfunc %s", x->dst); break;
    case IR_PARAM_DECL: snprintf(b, n, "param %s", x->dst); break;
    case IR_COPY:       snprintf(b, n, "%s = %s", x->dst, x->a); break;
    case IR_BIN:        snprintf(b, n, "%s = %s %s %s", x->dst, x->a, x->operador, x->b); break;
    case IR_UNARY:      snprintf(b, n, "%s = %s %s", x->dst, x->operador, x->a); break;
    case IR_LABEL:      snprintf(b, n, "%s:", x->dst); break;
    case IR_JUMP:       snprintf(b, n, "goto %s", x->dst); break;
    case IR_JFALSE:     snprintf(b, n, "ifFalse %s goto %s", x->a, x->dst); break;
    case IR_JTRUE:      snprintf(b, n, "ifTrue %s goto %s", x->a, x->dst); break;
    case IR_ARG:        snprintf(b, n, "arg %s", x->a); break;
    case IR_CALL:       snprintf(b, n, "%s = call %s, %s", x->dst, x->a, x->b); break;
    case IR_RETURN:     if (x->a) snprintf(b, n, "return %s", x->a); else snprintf(b, n, "return"); break;
    case IR_ARRAY_DECL: snprintf(b, n, "array %s, dims %s", x->dst, x->a); break;
    case IR_ARRAY_DIM:  snprintf(b, n, "dim %s[%s] = %s", x->dst, x->a, x->b); break;
    case IR_INDEX_REF:  snprintf(b, n, "%s = ref %s[%s]", x->dst, x->a, x->b); break;
    case IR_LOAD:       snprintf(b, n, "%s = load %s", x->dst, x->a); break;
    case IR_STORE:      snprintf(b, n, "store %s, %s", x->dst, x->a); break;
    case IR_LIST_DECL:  snprintf(b, n, "list %s", x->dst); break;
    case IR_LIST_ADD:   snprintf(b, n, "add %s, %s", x->dst, x->a); break;
    case IR_LIST_REMOVE:snprintf(b, n, "remove %s, %s", x->dst, x->a); break;
    case IR_SIZE:       snprintf(b, n, "%s = size %s", x->dst, x->a); break;
    }
}

/* 1 si `s` es un $tN o $LN con numero mayor al que tenia `base` (o sea,
 * un nombre que invento el loop unrolling). */
static int nombre_nuevo(const char *s, const LuVista *base) {
    if (!base || !s || s[0] != '$' || (s[1] != 't' && s[1] != 'L')) return 0;
    char *fin;
    unsigned long k = strtoul(s + 2, &fin, 10);       /* Numero despues de "$t" / "$L" */
    if (fin == s + 2 || *fin) return 0;               /* No es un nombre interno valido */
    return s[1] == 't' ? k > base->contador_t : k > base->contador_l;
}

/* Agrega una linea a la vista (copia el texto). Devuelve 0 si falta memoria. */
static int agregar(LuVista *v, const char *texto, char tipo) {
    LuLinea *p = realloc(v->lineas, (v->cantidad + 1) * sizeof *p);
    if (!p) return 0;
    v->lineas = p;
    size_t n = strlen(texto) + 1;
    char *copia = malloc(n);
    if (!copia) return 0;
    memcpy(copia, texto, n);
    v->lineas[v->cantidad++] = (LuLinea){copia, tipo};
    return 1;
}

int lu_vista_crear(const IrPrograma *ir, const LuVista *base, LuVista *v) {
    *v = (LuVista){0};
    v->instr = ir->cantidad;                                   /* Metrica: instrucciones */
    v->bloques = ir->cantidad_bloques;                         /* Metrica: bloques */
    v->contador_t = ir->temporales;                            /* Para marcar lo nuevo despues */
    v->contador_l = ir->etiquetas;
    for (size_t k = 0; k < ir->cantidad; ++k) {                /* Metricas: saltos y etiquetas */
        IrOp op = ir->codigo[k].op;
        v->saltos += op == IR_JUMP || op == IR_JFALSE || op == IR_JTRUE;
        v->etiquetas += op == IR_LABEL;
    }
    char texto[256], instr[200];
    for (size_t j = 0; j < ir->cantidad_bloques; ++j) {        /* Un encabezado por bloque */
        const IrBloque *b = &ir->bloques[j];
        if (j && !agregar(v, "", ' ')) return 2;               /* Linea en blanco entre bloques */
        int n = snprintf(texto, sizeof texto, "[B%zu]", j);
        if (b->cantidad_sucesores) {                           /* "[B1] → B3 B2" */
            n += snprintf(texto + n, sizeof texto - (size_t)n, " →");
            for (size_t s = 0; s < b->cantidad_sucesores; ++s)
                n += snprintf(texto + n, sizeof texto - (size_t)n, " B%zu", b->sucesores[s]);
        }
        if (!agregar(v, texto, 'B')) return 2;
        for (size_t i = b->inicio; i < b->fin; ++i) {          /* Instrucciones del bloque */
            const IrInstr *x = &ir->codigo[i];
            formatear(x, instr, sizeof instr);
            int es_nueva = nombre_nuevo(x->dst, base) || nombre_nuevo(x->a, base) || nombre_nuevo(x->b, base);
            snprintf(texto, sizeof texto, "%c %04zu  %s", es_nueva ? '+' : ' ', i, instr);
            if (!agregar(v, texto, es_nueva ? 'N' : 'I')) return 2;
        }
    }
    return 0;
}

void lu_vista_liberar(LuVista *v) {
    for (size_t i = 0; i < v->cantidad; ++i) free(v->lineas[i].texto);
    free(v->lineas);
    *v = (LuVista){0};
}

/* ===================================================================== */
/* Reporte                                                                */
/* ===================================================================== */

int lu_usar_color(FILE *f) {
    return isatty(fileno(f)) && !getenv("NO_COLOR");   /* Terminal real y sin NO_COLOR */
}

/* Color de una linea de la IR segun su tipo. */
static const char *tono_linea(char tipo) {
    return tipo == 'B' ? CIA : tipo == 'N' ? VER : NULL;
}

/* Fila de la tabla de metricas: nombre, antes, despues y la diferencia. */
static void fila_metrica(FILE *f, const char *nombre, size_t antes, size_t despues, int color) {
    char a[24], d[24], c[24];
    long long dif = (long long)despues - (long long)antes;
    snprintf(a, sizeof a, "%zu", antes);
    snprintf(d, sizeof d, "%zu", despues);
    if (dif) snprintf(c, sizeof c, "%+lld", dif);       /* "+11" o "-3" */
    else snprintf(c, sizeof c, "=");                    /* Sin cambio */
    fputs("  ", f);
    celda(f, nombre, 26, NULL, color);
    fprintf(f, "%8s  %8s  ", a, d);
    celda(f, c, 8, dif ? NEG : TEN, color);
    fputc('\n', f);
}

void lu_reporte(FILE *f, const char *archivo, unsigned factor, const LuVista *antes,
                const LuVista *despues, const LuResumen *res, int color) {
    char texto[256];
    const char *nombre = strrchr(archivo, '/');            /* Solo el nombre, sin carpetas */
    nombre = nombre ? nombre + 1 : archivo;

    /* ---- Encabezado ---- */
    fputs("\n╭", f); raya(f, ANCHO - 2); fputs("╮\n│ ", f);
    snprintf(texto, sizeof texto, "LOOP UNROLLING  ·  %s  ·  factor %u", nombre, factor);
    celda(f, texto, ANCHO - 4, NEG, color);
    fputs(" │\n╰", f); raya(f, ANCHO - 2); fputs("╯\n", f);

    /* ---- Tabla de ciclos ---- */
    seccion(f, "CICLOS", color);
    if (!res->cantidad_detalle) {
        fputs("  (el programa no tiene ciclos)\n", f);
    } else {
        fputs("  ", f);
        celda(f, "Ciclo", 7, TEN, color); celda(f, "Línea", 7, TEN, color);
        celda(f, "Condición", 14, TEN, color); celda(f, "Paso", 6, TEN, color);
        celda(f, "Vueltas", 9, TEN, color); celda(f, "Decisión", 0, TEN, color);
        fputc('\n', f);
    }
    for (size_t i = 0; i < res->cantidad_detalle; ++i) {
        const LuDetalle *d = &res->detalle[i];
        char linea[24], cond[112], paso[24], vueltas[24], decision[200];
        snprintf(linea, sizeof linea, "%zu", d->linea);
        if (d->iter[0]) snprintf(cond, sizeof cond, "%s %s %s", d->iter, d->relacion, d->limite);
        else snprintf(cond, sizeof cond, "—");                      /* Ciclo no canonico */
        if (d->paso) snprintf(paso, sizeof paso, "+%lld", d->paso);
        else snprintf(paso, sizeof paso, "—");
        if (d->vueltas_conocidas) snprintf(vueltas, sizeof vueltas, "%llu", d->vueltas);
        else snprintf(vueltas, sizeof vueltas, d->decision == LU_PARCIAL_VAR ? "?" : "—");
        const char *tono = VER;
        switch (d->decision) {
        case LU_TOTAL:
            snprintf(decision, sizeof decision, "✔ TOTAL: %llu copias, sin saltos", d->vueltas); break;
        case LU_PARCIAL_CONST:
            snprintf(decision, sizeof decision, "✔ PARCIAL ×%u: %llu vueltas + %llu sueltas",
                     d->factor, d->vueltas / d->factor, d->vueltas % d->factor);
            tono = AMA; break;
        case LU_PARCIAL_VAR:
            snprintf(decision, sizeof decision, "✔ PARCIAL ×%u + ciclo residuo", d->factor);
            tono = AMA; break;
        case LU_RECHAZADO:
            snprintf(decision, sizeof decision, "✘ sin cambios: %s", d->motivo ? d->motivo : "");
            tono = ROJ; break;
        }
        fputs("  ", f);
        celda(f, d->etiqueta, 7, NULL, color);
        celda(f, linea, 7, NULL, color);
        celda(f, cond, 14, NULL, color);
        celda(f, paso, 6, NULL, color);
        celda(f, vueltas, 9, NULL, color);
        celda(f, decision, 0, tono, color);                         /* Ultima columna: sin recorte */
        fputc('\n', f);
    }

    /* ---- Metricas ---- */
    seccion(f, "MÉTRICAS", color);
    fputs("  ", f);
    celda(f, "", 26, NULL, color);
    celda(f, "   antes", 10, TEN, color);
    celda(f, " después", 10, TEN, color);
    celda(f, "cambio", 0, TEN, color);
    fputc('\n', f);
    fila_metrica(f, "Instrucciones IR", antes->instr, despues->instr, color);
    fila_metrica(f, "Bloques básicos", antes->bloques, despues->bloques, color);
    fila_metrica(f, "Saltos", antes->saltos, despues->saltos, color);
    fila_metrica(f, "Etiquetas", antes->etiquetas, despues->etiquetas, color);
    snprintf(texto, sizeof texto, "  Ciclos: %zu  ·  total %zu  ·  parcial %zu  ·  sin cambios %zu\n",
             res->ciclos, res->totales, res->parciales, res->rechazados);
    fputs(texto, f);

    /* ---- IR lado a lado ---- */
    fputs("\n  ", f);
    celda(f, "IR ANTES", COL, NEG, color);
    fputs(" │ ", f);
    celda(f, "IR DESPUÉS", 0, NEG, color);
    fputs("\n  ", f);
    if (color) fputs(TEN, f);
    raya(f, COL); fputs("─┼─", f); raya(f, ANCHO - COL - 5);
    if (color) fputs(RST, f);
    fputc('\n', f);
    size_t filas = antes->cantidad > despues->cantidad ? antes->cantidad : despues->cantidad;
    for (size_t i = 0; i < filas; ++i) {                   /* Empareja linea i con linea i */
        const LuLinea *a = i < antes->cantidad ? &antes->lineas[i] : NULL;
        const LuLinea *d = i < despues->cantidad ? &despues->lineas[i] : NULL;
        fputs("  ", f);
        celda(f, a ? a->texto : "", COL, a ? tono_linea(a->tipo) : NULL, color);
        fputs(" │ ", f);
        celda(f, d ? d->texto : "", ANCHO - COL - 5, d ? tono_linea(d->tipo) : NULL, color);
        fputc('\n', f);
    }
    fputs("\n  ", f);
    celda(f, "+ = instrucción creada por el loop unrolling", 0, TEN, color);
    fputs("\n\n", f);
}
