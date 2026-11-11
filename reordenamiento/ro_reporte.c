/* =====================================================================
 * ro_reporte.c  --  Reporte visual del reordenamiento (balc --reorder)
 * =====================================================================
 * No cambia la IR: solo la muestra. Mismo estilo que el reporte del loop
 * unrolling (tablas, colores ANSI solo en terminal, IR lado a lado).
 * ===================================================================== */

#define _POSIX_C_SOURCE 200809L  /* isatty() y fileno() con -std=c11 */
#include "ro_reporte.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RST  "\033[0m"
#define NEG  "\033[1m"           /* Negrita */
#define TEN  "\033[2m"           /* Tenue */
#define VER  "\033[32m"          /* Verde */
#define AMA  "\033[33m"          /* Amarillo */
#define CIA  "\033[36m"          /* Cian */

#define ANCHO 80                 /* Ancho total */
#define COL   37                 /* Ancho de cada columna de la IR */

/* ---- Utilidades de texto (ancho visible UTF-8, celdas, rayas) ---- */

static int ancho(const char *s) {
    int n = 0;
    for (; *s; ++s) if (((unsigned char)*s & 0xC0) != 0x80) ++n; /* Cuenta caracteres, no bytes */
    return n;
}

/* Escribe s en `w` columnas (recorta con "…" o rellena). w <= 0: sin limite. */
static void celda(FILE *f, const char *s, int w, const char *tono, int color) {
    int largo = ancho(s);
    if (color && tono) fputs(tono, f);
    if (w <= 0 || largo <= w) {
        fputs(s, f);
    } else {
        int usados = 0;
        for (const char *p = s; *p; ++p) {
            int inicio = ((unsigned char)*p & 0xC0) != 0x80;
            if (inicio && usados == w - 1) break;
            usados += inicio;
            fputc(*p, f);
        }
        fputs("…", f);
        largo = w;
    }
    if (color && tono) fputs(RST, f);
    for (int i = largo; i < w; ++i) fputc(' ', f);
}

static void raya(FILE *f, int n) { for (int i = 0; i < n; ++i) fputs("─", f); }

static void seccion(FILE *f, const char *titulo, int color) {
    fputs("\n  ", f);
    celda(f, titulo, 0, NEG, color);
    fputs("\n  ", f);
    if (color) fputs(TEN, f);
    raya(f, ANCHO - 4);
    if (color) fputs(RST, f);
    fputc('\n', f);
}

/* Texto de una instruccion (como ir_imprimir, sin "; linea N"). */
void ro_texto_instr(const IrInstr *x, char *b, size_t n) {
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

int ro_usar_color(FILE *f) { return isatty(fileno(f)) && !getenv("NO_COLOR"); }

/* Fila de metricas: nombre, antes, despues y diferencia. */
static void fila(FILE *f, const char *nombre, size_t antes, size_t despues, int color) {
    char c[24];
    long long dif = (long long)despues - (long long)antes;
    if (dif) snprintf(c, sizeof c, "%+lld", dif); else snprintf(c, sizeof c, "=");
    fputs("  ", f);
    celda(f, nombre, 26, NULL, color);
    fprintf(f, "%8zu  %8zu  ", antes, despues);
    celda(f, c, 0, dif < 0 ? VER : dif ? NEG : TEN, color);
    fputc('\n', f);
}

void ro_reporte(FILE *f, const char *archivo, const IrPrograma *ir,
                const RoResumen *res, unsigned latencia, int color) {
    char texto[256], instr[200];
    const char *nombre = strrchr(archivo, '/');
    nombre = nombre ? nombre + 1 : archivo;

    /* ---- Encabezado ---- */
    fputs("\n╭", f); raya(f, ANCHO - 2); fputs("╮\n│ ", f);
    snprintf(texto, sizeof texto, "REORDENAMIENTO SEGURO  ·  %s  ·  latencia %u", nombre, latencia);
    celda(f, texto, ANCHO - 4, NEG, color);
    fputs(" │\n╰", f); raya(f, ANCHO - 2); fputs("╯\n", f);

    /* ---- Bloques que cambiaron ---- */
    seccion(f, "BLOQUES REORDENADOS", color);
    if (!res->bloques_cambiados) {
        fputs("  (ningún bloque necesita menos ciclos con otro orden)\n", f);
    } else {
        fputs("  ", f);
        celda(f, "Bloque", 8, TEN, color); celda(f, "Instr.", 8, TEN, color);
        celda(f, "Movidas", 9, TEN, color); celda(f, "Ciclos", 16, TEN, color);
        celda(f, "RAW WAR WAW mem efec ctrl", 0, TEN, color);
        fputc('\n', f);
    }
    for (size_t b = 0; b < res->cantidad_detalle; ++b) {
        const RoBloque *d = &res->detalle[b];
        if (!d->movidas) continue;                         /* Solo los que cambiaron */
        char bl[32], in[32], mv[32], ci[64], dep[160];
        snprintf(bl, sizeof bl, "B%zu", b);
        snprintf(in, sizeof in, "%zu", d->fin - d->inicio);
        snprintf(mv, sizeof mv, "%zu", d->movidas);
        snprintf(ci, sizeof ci, "%zu → %zu", d->ciclos_antes, d->ciclos_despues);
        snprintf(dep, sizeof dep, "%3zu %3zu %3zu %3zu %4zu %4zu",
                 d->raw, d->war, d->waw, d->memoria, d->efecto, d->control);
        fputs("  ", f);
        celda(f, bl, 8, CIA, color); celda(f, in, 8, NULL, color);
        celda(f, mv, 9, AMA, color); celda(f, ci, 16, VER, color);
        celda(f, dep, 0, NULL, color);
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
    fila(f, "Ciclos estimados", res->ciclos_antes, res->ciclos_despues, color);
    fila(f, "Instrucciones IR", res->instrucciones, res->instrucciones, color);
    snprintf(texto, sizeof texto, "  Instrucciones movidas: %zu de %zu  ·  bloques reordenados: %zu de %zu\n",
             res->movidas, res->instrucciones, res->bloques_cambiados, res->bloques);
    fputs(texto, f);
    snprintf(texto, sizeof texto, "  Dependencias respetadas: RAW %zu · WAR %zu · WAW %zu · memoria %zu · efectos %zu · control %zu\n",
             res->raw, res->war, res->waw, res->memoria, res->efecto, res->control);
    fputs(texto, f);

    /* ---- IR lado a lado ---- */
    size_t n = ir->cantidad;
    size_t *donde = malloc((n + 1) * sizeof *donde);       /* donde[original] = posicion actual */
    if (!donde) return;
    for (size_t i = 0; i < n; ++i) donde[res->origen[i]] = i;
    fputs("\n  ", f);
    celda(f, "IR ANTES", COL, NEG, color);
    fputs(" │ ", f);
    celda(f, "IR DESPUÉS", 0, NEG, color);
    fputs("\n  ", f);
    if (color) fputs(TEN, f);
    raya(f, COL); fputs("─┼─", f); raya(f, ANCHO - COL - 5);
    if (color) fputs(RST, f);
    fputc('\n', f);
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        const IrBloque *bq = &ir->bloques[b];
        const RoBloque *d = &res->detalle[b];
        if (b) {                                             /* Linea vacia entre bloques */
            fputs("  ", f);
            celda(f, "", COL, NULL, color);
            fputs(" │\n", f);
        }
        int k = snprintf(texto, sizeof texto, "[B%zu]", b);  /* Encabezado del bloque */
        if (bq->cantidad_sucesores) {
            k += snprintf(texto + k, sizeof texto - (size_t)k, " →");
            for (size_t s = 0; s < bq->cantidad_sucesores; ++s)
                k += snprintf(texto + k, sizeof texto - (size_t)k, " B%zu", bq->sucesores[s]);
        }
        char der[300];
        if (d->movidas) snprintf(der, sizeof der, "%s  (%zu→%zu ciclos)", texto, d->ciclos_antes, d->ciclos_despues);
        else snprintf(der, sizeof der, "%s", texto);
        fputs("  ", f);
        celda(f, texto, COL, CIA, color);
        fputs(" │ ", f);
        celda(f, der, 0, CIA, color);
        fputc('\n', f);
        for (size_t i = bq->inicio; i < bq->fin; ++i) {
            /* Izquierda: la instruccion que ORIGINALMENTE estaba en i. */
            const IrInstr *orig = &ir->codigo[donde[i]];
            ro_texto_instr(orig, instr, sizeof instr);
            int movida_izq = donde[i] != i;
            snprintf(texto, sizeof texto, "%c %04zu  %s", movida_izq ? '*' : ' ', i, instr);
            fputs("  ", f);
            celda(f, texto, COL, movida_izq ? AMA : NULL, color);
            /* Derecha: la instruccion que AHORA esta en i, con su numero original. */
            ro_texto_instr(&ir->codigo[i], instr, sizeof instr);
            int movida_der = res->origen[i] != i;
            snprintf(der, sizeof der, "%c %04zu  %s", movida_der ? '*' : ' ', res->origen[i], instr);
            fputs(" │ ", f);
            celda(f, der, ANCHO - COL - 5, movida_der ? AMA : NULL, color);
            fputc('\n', f);
        }
    }
    free(donde);
    fputs("\n  ", f);
    celda(f, "* = cambió de lugar (cada instrucción conserva su número original)", 0, TEN, color);
    fputs("\n\n", f);
}
