/*
 * Reordenamiento para el IDE (ver include/ide_reordenamiento.h).
 *
 * Pasos de ide_reordenamiento_analizar (los mismos que "balc --reorder"):
 *   1. Guardar el texto del editor en el archivo temporal oculto (el mismo
 *      que usa la revisión de errores, ide_analisis.c).
 *   2. Correr el análisis semántico; si el programa es válido, el compilador
 *      llama a "visitante" con su AST.
 *   3. En el visitante: AST -> IR (que ya trae sus bloques básicos), loop
 *      unrolling si se pidió, foto de la IR, grafo de dependencias de cada
 *      bloque, ro_reordenar (módulo de Meibel) y otra foto.
 * No cambia el módulo de reordenamiento: solo lo llama.
 */
#include "ide_reordenamiento.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ide_analisis.h"
#include "ir.h"
#include "loop_unrolling.h"
#include "ro_reporte.h"
#include "semantic_modules.h"

/* ------------------------------------------------------------------ */
/* Listas de líneas                                                    */
/* ------------------------------------------------------------------ */

/* Agrega una línea con formato (como printf). Devuelve 0 bien, -1 sin memoria. */
static int agregar(IdeRoTexto *t, char tipo, const char *formato, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, formato);
    vsnprintf(buffer, sizeof buffer, formato, args);
    va_end(args);

    if (t->cantidad == t->capacidad) {
        size_t nueva = t->capacidad ? t->capacidad * 2 : 32;
        IdeRoLinea *p = realloc(t->lineas, nueva * sizeof *p);
        if (p == NULL) return -1;
        t->lineas = p;
        t->capacidad = nueva;
    }
    char *copia = malloc(strlen(buffer) + 1);
    if (copia == NULL) return -1;
    strcpy(copia, buffer);
    t->lineas[t->cantidad].tipo = tipo;
    t->lineas[t->cantidad].texto = copia;
    t->cantidad++;
    return 0;
}

static void liberar_texto(IdeRoTexto *t)
{
    for (size_t i = 0; i < t->cantidad; ++i) free(t->lineas[i].texto);
    free(t->lineas);
    memset(t, 0, sizeof *t);
}

void ide_reordenamiento_tipos(unsigned tipos, char *texto, size_t largo)
{
    static const struct { unsigned bit; const char *nombre; } nombres[] = {
        {RO_DEP_RAW, "RAW"}, {RO_DEP_WAR, "WAR"}, {RO_DEP_WAW, "WAW"},
        {RO_DEP_MEMORIA, "memoria"}, {RO_DEP_EFECTO, "efecto"}, {RO_DEP_CONTROL, "control"},
    };
    size_t usado = 0;
    texto[0] = '\0';
    for (size_t i = 0; i < sizeof nombres / sizeof nombres[0]; ++i) {
        if (!(tipos & nombres[i].bit)) continue;
        int n = snprintf(texto + usado, largo - usado, "%s%s", usado ? " + " : "", nombres[i].nombre);
        if (n < 0 || (size_t)n >= largo - usado) break;
        usado += (size_t)n;
    }
}

/* ------------------------------------------------------------------ */
/* Fotos de la IR                                                      */
/* ------------------------------------------------------------------ */

/* Texto de la instrucción i de la IR (como en balc --reorder). */
static void instruccion(const IrPrograma *ir, size_t i, char *texto, size_t largo)
{
    ro_texto_instr(&ir->codigo[i], texto, largo);
}

/* La IR ANTES de reordenar. "movida[i]" dice si la instrucción i va a
 * cambiar de lugar (se sabe después de reordenar: se marca al final). */
static int foto_antes(const IrPrograma *ir, IdeRoTexto *t)
{
    char texto[160];
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        if (agregar(t, 'B', "[B%zu]", b) != 0) return -1;
        for (size_t i = ir->bloques[b].inicio; i < ir->bloques[b].fin; ++i) {
            instruccion(ir, i, texto, sizeof texto);
            if (agregar(t, 'I', "  %04zu  %s", i, texto) != 0) return -1;
        }
    }
    return 0;
}

/* La IR DESPUÉS de reordenar: cada instrucción con su número ORIGINAL
 * (res->origen) y '*' si cambió de lugar. Los bloques no cambian. */
static int foto_despues(const IrPrograma *ir, const RoResumen *res, IdeRoTexto *t)
{
    char texto[160];
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        const RoBloque *d = b < res->cantidad_detalle ? &res->detalle[b] : NULL;
        int ok = d != NULL && d->movidas > 0
                     ? agregar(t, 'B', "[B%zu]  %zu → %zu ciclos", b, d->ciclos_antes, d->ciclos_despues)
                     : agregar(t, 'B', "[B%zu]", b);
        if (ok != 0) return -1;
        for (size_t i = ir->bloques[b].inicio; i < ir->bloques[b].fin; ++i) {
            size_t original = i < res->cantidad_origen ? res->origen[i] : i;
            int movida = original != i;
            instruccion(ir, i, texto, sizeof texto);
            if (agregar(t, movida ? 'M' : 'I', "%c %04zu  %s", movida ? '*' : ' ', original, texto) != 0)
                return -1;
        }
    }
    return 0;
}

/* En la foto de antes, marca con '*' las instrucciones que se movieron:
 * la instrucción original p se movió si ya no está en la posición p. */
static void marcar_movidas(IdeRoTexto *antes, const RoResumen *res)
{
    for (size_t k = 0; k < antes->cantidad; ++k) {
        IdeRoLinea *l = &antes->lineas[k];
        if (l->tipo != 'I') continue;
        size_t p = strtoul(l->texto + 2, NULL, 10);   /* "  0004  ..." */
        if (p < res->cantidad_origen && res->origen[p] != p) {
            l->tipo = 'M';
            l->texto[0] = '*';
        }
    }
}

/* ------------------------------------------------------------------ */
/* Dependencias                                                        */
/* ------------------------------------------------------------------ */

/*
 * Grafo de dependencias de cada bloque que se va a reordenar (el MISMO
 * análisis que usa ro_reordenar, pero sin cambiar la IR). Cada arista
 * "a -> b" dice que a tiene que ir antes que b, por qué y con cuántos
 * ciclos de separación. Las aristas que solo son de control (todo va
 * antes del salto o del return) se cuentan pero no se listan: son muchas
 * y siempre dicen lo mismo.
 */
static int dependencias(const IrPrograma *ir, unsigned latencia, IdeRoTexto *t)
{
    char de[160], a[160], tipos[80];
    for (size_t b = 0; b < ir->cantidad_bloques; ++b) {
        RoGrafoBloque g;
        if (ro_analizar_bloque(ir, b, latencia, &g) != 0) return -1;
        if (!g.aplicado) {
            ro_grafo_liberar(&g);
            continue;   /* Este bloque no cambia: no se muestra. */
        }

        int estado = agregar(t, 'B', "[B%zu]  %zu → %zu ciclos  ·  ruta crítica: %zu ciclos "
                                     "(ningún orden puede bajar de ahí)",
                             b, g.ciclos_antes, g.ciclos_despues, g.longitud_ruta);
        size_t solo_control = 0;
        for (size_t k = 0; estado == 0 && k < g.cantidad_aristas; ++k) {
            const RoArista *e = &g.aristas[k];
            if (e->tipos == RO_DEP_CONTROL) {
                solo_control++;
                continue;
            }
            size_t pd = g.nodos[e->de].pos, pa = g.nodos[e->a].pos;
            instruccion(ir, pd, de, sizeof de);
            instruccion(ir, pa, a, sizeof a);
            ide_reordenamiento_tipos(e->tipos, tipos, sizeof tipos);
            estado = agregar(t, e->critica ? 'C' : 'E',
                             "  %04zu  %-22s  →  %04zu  %-22s  %s%s%s · %u %s", pd, de, pa, a, tipos,
                             e->por ? " por " : "", e->por ? e->por : "", e->latencia,
                             e->latencia == 1 ? "ciclo" : "ciclos");
        }
        if (estado == 0 && solo_control > 0) {
            estado = agregar(t, 'E', "  (y %zu de control: todo queda antes del salto o del final "
                                     "del bloque)", solo_control);
        }

        /* La ruta crítica: la cadena de dependencias más larga. */
        if (estado == 0 && g.largo_ruta > 0) {
            char ruta[512];
            size_t usado = 0;
            for (size_t k = 0; k < g.largo_ruta && usado + 12 < sizeof ruta; ++k) {
                usado += (size_t)snprintf(ruta + usado, sizeof ruta - usado, "%s%04zu",
                                          k ? " → " : "", g.nodos[g.ruta[k]].pos);
            }
            estado = agregar(t, 'R', "  Ruta crítica: %s", ruta);
        }
        ro_grafo_liberar(&g);
        if (estado != 0) return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Análisis                                                            */
/* ------------------------------------------------------------------ */

/* Lee todo lo escrito en un archivo temporal (tmpfile) como una cadena. */
static char *leer_todo(FILE *f)
{
    if (f == NULL || fflush(f) != 0 || fseek(f, 0, SEEK_END) != 0) return NULL;
    long largo = ftell(f);
    if (largo < 0 || fseek(f, 0, SEEK_SET) != 0) return NULL;
    char *texto = malloc((size_t)largo + 1);
    if (texto == NULL) return NULL;
    size_t leidos = fread(texto, 1, (size_t)largo, f);
    texto[leidos] = '\0';
    return texto;
}

/* Lo que el visitante necesita y lo que deja. */
typedef struct {
    IdeReordenamiento *salida;
    int desenrollar;
    int llego;          /* 1 si el semántico aceptó el programa y llamó al visitante. */
    int sin_memoria;
} Contexto;

/*
 * Lo llama el análisis semántico cuando el programa es válido.
 * Devuelve 0 bien; 1 si la IR no soporta algo (el mensaje queda en
 * "diag"); 2 si faltó memoria.
 */
static int visitante(void *datos, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    (void)tabla;
    Contexto *c = datos;
    IdeReordenamiento *r = c->salida;
    c->llego = 1;

    IrPrograma ir = {0};
    int estado = ir_desde_ast(arbol, &ir, diag);
    if (estado != 0) return estado;

    /* Loop unrolling primero (como balc --unroll --reorder): deja bloques
     * más largos, con más instrucciones para reordenar. */
    if (c->desenrollar) {
        LuOpciones lu;
        lu_opciones_defecto(&lu);
        estado = lu_desenrollar(&ir, &lu, NULL, NULL);
        r->desenrollado = 1;
    }

    RoOpciones opciones;
    ro_opciones_defecto(&opciones);
    if (r->latencia != 0) opciones.latencia = r->latencia;
    r->latencia = opciones.latencia;

    /* Foto antes y dependencias (sin cambiar la IR), reordenar, foto después. */
    if (estado == 0 && foto_antes(&ir, &r->antes) != 0) estado = 2;
    if (estado == 0 && dependencias(&ir, opciones.latencia, &r->dependencias) != 0) estado = 2;
    if (estado == 0) estado = ro_reordenar(&ir, &opciones, &r->resumen);
    if (estado == 0 && foto_despues(&ir, &r->resumen, &r->despues) != 0) estado = 2;
    if (estado == 0) marcar_movidas(&r->antes, &r->resumen);

    ir_liberar(&ir);
    if (estado != 0) {
        c->sin_memoria = 1;
        return 2;
    }
    return 0;
}

/*
 * Si la IR no soportó algo, el compilador escribió una línea como
 *     IR:9:5: cycle requiere paso constante positivo en esta etapa (AST Recorrido)
 * Se convierte en "línea 9: cycle requiere ...". NULL si no hay tal línea.
 */
static char *motivo_de_la_ir(const char *diagnosticos)
{
    const char *ir = diagnosticos ? strstr(diagnosticos, "IR:") : NULL;
    if (ir == NULL) return NULL;
    unsigned long linea = 0, columna = 0;
    int usados = 0;
    if (sscanf(ir, "IR:%lu:%lu: %n", &linea, &columna, &usados) != 2 || usados == 0) return NULL;
    const char *mensaje = ir + usados;
    size_t largo = strcspn(mensaje, "\n");
    char *texto = malloc(largo + 32);
    if (texto != NULL) snprintf(texto, largo + 32, "línea %lu: %.*s", linea, (int)largo, mensaje);
    return texto;
}

int ide_reordenamiento_analizar(const char *texto, size_t largo, const char *ruta,
                                unsigned latencia, int desenrollar, IdeReordenamiento *r)
{
    memset(r, 0, sizeof *r);
    r->latencia = latencia;

    /* Paso 1: el texto del editor, en el archivo temporal. */
    char *temporal = ide_ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (ide_escribir_archivo(temporal, texto, largo) != 0) {
        free(temporal);
        return -1;
    }

    /* Pasos 2 y 3. */
    FILE *diag = tmpfile();
    Contexto c = {r, desenrollar, 0, 0};
    int estado = -1;
    if (diag != NULL) {
        int resultado = sem_visitar_proyecto(temporal, diag, visitante, &c);
        char *mensajes = leer_todo(diag);
        fclose(diag);
        if (c.sin_memoria) {
            estado = -1;
        } else if (resultado == 0 && c.llego) {
            r->estado = IDE_RO_LISTO;
            estado = 0;
        } else if (c.llego) {
            r->estado = IDE_RO_SIN_IR;
            r->motivo = motivo_de_la_ir(mensajes);
            if (r->motivo == NULL) {
                const char *generico = "la IR no pudo traducir el programa";
                r->motivo = malloc(strlen(generico) + 1);
                if (r->motivo != NULL) strcpy(r->motivo, generico);
            }
            estado = r->motivo ? 0 : -1;
        } else {
            r->estado = IDE_RO_CON_ERRORES;
            estado = 0;
        }
        free(mensajes);
    }

    remove(temporal);
    free(temporal);
    if (estado != 0) ide_reordenamiento_liberar(r);
    return estado;
}

void ide_reordenamiento_liberar(IdeReordenamiento *r)
{
    if (r == NULL) return;
    free(r->motivo);
    ro_resumen_liberar(&r->resumen);
    liberar_texto(&r->antes);
    liberar_texto(&r->despues);
    liberar_texto(&r->dependencias);
    memset(r, 0, sizeof *r);
}
