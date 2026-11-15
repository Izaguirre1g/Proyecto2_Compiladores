/* =====================================================================
 * ro_json.c  --  Grafo de dependencias en JSON (balc --reorder-json)
 * =====================================================================
 * No cambia la IR: para cada bloque basico pide el analisis a
 * ro_analizar_bloque (el mismo que usa ro_reordenar) y lo escribe como
 * JSON. El visualizador reordenamiento/visualizar_grafo.html lo dibuja.
 *
 * Forma general (detalle en reordenamiento/README.md):
 *   { "formato": "bal-reordenamiento", "version": 1, "archivo", "latencia",
 *     "factor_unroll", "bloques": [ { "id", "inicio", "fin", "sucesores",
 *     "fijas", "ciclos_antes", "ciclos_despues", "aplicado", "orden",
 *     "ruta_critica", "nodos", "aristas" } ], "resumen": { ... } }
 * ===================================================================== */

#include "ro_json.h"
#include "reordenamiento.h"   /* ro_analizar_bloque, RoGrafoBloque */
#include "ro_reporte.h"       /* ro_texto_instr */

/* Cadena JSON con escapes (comillas, barra y caracteres de control). */
static void texto(FILE *f, const char *s) {
    if (!s) { fputs("null", f); return; }
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p < 0x20) fprintf(f, "\\u%04x", (unsigned)*p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

/* Lista JSON de nombres: ["a","$t1"]. */
static void nombres(FILE *f, const char *const *v, size_t n) {
    fputc('[', f);
    for (size_t i = 0; i < n; ++i) { if (i) fputc(',', f); texto(f, v[i]); }
    fputc(']', f);
}

/* Ciclo de emision o null si la instruccion no ocupa ciclo. */
static void ciclo(FILE *f, size_t c) {
    if (c == RO_SIN_CICLO) fputs("null", f); else fprintf(f, "%zu", c);
}

/* Lista de tipos de una arista: ["RAW","CONTROL"]. */
static void tipos(FILE *f, unsigned t) {
    static const struct { unsigned bit; const char *nombre; } tabla[] = {
        {RO_DEP_RAW, "RAW"}, {RO_DEP_WAR, "WAR"}, {RO_DEP_WAW, "WAW"},
        {RO_DEP_MEMORIA, "MEMORIA"}, {RO_DEP_EFECTO, "EFECTO"}, {RO_DEP_CONTROL, "CONTROL"},
    };
    int primero = 1;
    fputc('[', f);
    for (size_t i = 0; i < sizeof tabla / sizeof tabla[0]; ++i)
        if (t & tabla[i].bit) {
            if (!primero) fputc(',', f);
            texto(f, tabla[i].nombre);
            primero = 0;
        }
    fputc(']', f);
}

/* Un bloque completo. Suma sus numeros a los totales. */
static void bloque(FILE *f, const IrPrograma *ir, size_t b, const RoGrafoBloque *g,
                   size_t *movidas) {
    char instr[256];
    const IrBloque *bq = &ir->bloques[b];
    fprintf(f, "{\"id\":%zu,\"inicio\":%zu,\"fin\":%zu,\"sucesores\":[", b, g->inicio, g->fin);
    for (size_t s = 0; s < bq->cantidad_sucesores; ++s)
        fprintf(f, "%s%zu", s ? "," : "", bq->sucesores[s]);
    fputs("],\n  \"fijas\":[", f);                        /* Etiquetas/func/param del inicio */
    for (size_t k = g->inicio; k < g->primero; ++k) {
        ro_texto_instr(&ir->codigo[k], instr, sizeof instr);
        fprintf(f, "%s{\"pos\":%zu,\"texto\":", k > g->inicio ? "," : "", k);
        texto(f, instr);
        fprintf(f, ",\"linea\":%zu}", ir->codigo[k].linea);
    }
    fprintf(f, "],\n  \"ciclos_antes\":%zu,\"ciclos_despues\":%zu,\"aplicado\":%s,\n  \"orden\":[",
            g->ciclos_antes, g->ciclos_despues, g->aplicado ? "true" : "false");
    for (size_t k = 0; k < g->cantidad_nodos; ++k)
        fprintf(f, "%s%zu", k ? "," : "", g->orden[k]);
    fprintf(f, "],\n  \"ruta_critica\":{\"ciclos\":%zu,\"nodos\":[", g->longitud_ruta);
    for (size_t k = 0; k < g->largo_ruta; ++k)
        fprintf(f, "%s%zu", k ? "," : "", g->ruta[k]);
    fputs("]},\n  \"nodos\":[", f);
    for (size_t k = 0; k < g->cantidad_nodos; ++k) {
        const RoNodo *x = &g->nodos[k];
        if (x->nueva_pos != k) ++*movidas;
        ro_texto_instr(&ir->codigo[x->pos], instr, sizeof instr);
        fprintf(f, "%s\n    {\"id\":%zu,\"pos\":%zu,\"texto\":", k ? "," : "", k, x->pos);
        texto(f, instr);
        fprintf(f, ",\"linea\":%zu,\"lee\":", ir->codigo[x->pos].linea);
        nombres(f, x->lee, x->nlee);
        fputs(",\"escribe\":", f);
        nombres(f, x->escribe, x->nesc);
        fprintf(f, ",\"altura\":%zu,\"critico\":%s,\"ciclo_antes\":", x->altura,
                x->critico ? "true" : "false");
        ciclo(f, x->ciclo_antes);
        fputs(",\"ciclo_despues\":", f);
        ciclo(f, x->ciclo_despues);
        fprintf(f, ",\"nueva_pos\":%zu}", x->nueva_pos);
    }
    fputs("],\n  \"aristas\":[", f);
    for (size_t k = 0; k < g->cantidad_aristas; ++k) {
        const RoArista *e = &g->aristas[k];
        fprintf(f, "%s\n    {\"de\":%zu,\"a\":%zu,\"latencia\":%u,\"tipos\":",
                k ? "," : "", e->de, e->a, e->latencia);
        tipos(f, e->tipos);
        fputs(",\"por\":", f);
        texto(f, e->por);
        fprintf(f, ",\"critica\":%s}", e->critica ? "true" : "false");
    }
    fputs("]}", f);
}

int ro_exportar_json(FILE *f, const char *archivo, const IrPrograma *ir,
                     unsigned latencia, unsigned factor_unroll) {
    if (!f || !ir || (ir->cantidad && !ir->bloques)) return 2;
    const char *nombre = archivo ? archivo : "";
    for (const char *p = nombre; *p; ++p) if (*p == '/') nombre = p + 1; /* Solo el nombre */
    size_t cambiados = 0, movidas = 0, antes = 0, despues = 0;
    fputs("{\"formato\":\"bal-reordenamiento\",\"version\":1,\"archivo\":", f);
    texto(f, nombre);
    fprintf(f, ",\"latencia\":%u,\"factor_unroll\":", latencia);
    if (factor_unroll) fprintf(f, "%u", factor_unroll); else fputs("null", f);
    fputs(",\n\"bloques\":[", f);
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        RoGrafoBloque g;
        if (ro_analizar_bloque(ir, b, latencia, &g)) return 2;
        fputs(b ? ",\n" : "\n", f);
        bloque(f, ir, b, &g, &movidas);
        cambiados += (size_t)g.aplicado;
        antes += g.ciclos_antes; despues += g.ciclos_despues;
        ro_grafo_liberar(&g);
    }
    fprintf(f, "],\n\"resumen\":{\"bloques\":%zu,\"bloques_cambiados\":%zu,\"instrucciones\":%zu,"
               "\"movidas\":%zu,\"ciclos_antes\":%zu,\"ciclos_despues\":%zu}}\n",
            ir->cantidad_bloques, cambiados, ir->cantidad, movidas, antes, despues);
    return ferror(f) ? 2 : 0;
}
