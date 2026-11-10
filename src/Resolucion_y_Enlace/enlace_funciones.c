#include "enlace_funciones.h"
#include "referencias_integracion.h"
#include "generacion_basica.h"

#include <stdlib.h>
#include <string.h>

#define EF_MAX_VUELTAS 64

static int fallo(FILE *diagnosticos, const char *mensaje, const char *detalle)
{
    if (diagnosticos)
        fprintf(diagnosticos, "enlace de funciones: %s%s%s\n", mensaje,
                detalle ? ": " : "", detalle ? detalle : "");
    return 0;
}

static const char *nombre(const AstNodo *funcion)
{
    return funcion->primer_hijo ? funcion->primer_hijo->texto : NULL;
}

/* Marco = fin del ultimo slot del ambito de la funcion (incluidos sus
 * bloques), con minimo 4 para ra. Igual que mem_asignar_marco. */
static size_t marco_de(const SimTabla *tabla, const char *funcion)
{
    const SimAmbito *ambito = NULL;
    for (const SimAmbito *a = tabla->global->siguiente_creacion; a;
         a = a->siguiente_creacion)
        if (a->padre == tabla->global && !strcmp(a->nombre, funcion)) ambito = a;
    if (!ambito) return 0;
    size_t marco = 4;
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden) {
        const SimAmbito *a = s->ambito;
        while (a && a != ambito) a = a->padre;
        if (!a || s->desplazamiento < 4) continue;
        size_t fin = (size_t)s->desplazamiento + s->bytes;
        if (fin > marco) marco = fin;
    }
    return (marco + 3) / 4 * 4;
}

static char *leer_todo(FILE *f)
{
    if (fflush(f) != 0) return NULL;
    long largo = ftell(f);
    if (largo < 0 || fseek(f, 0, SEEK_SET) != 0) return NULL;
    char *texto = malloc((size_t)largo + 1);
    if (!texto) return NULL;
    if (fread(texto, 1, (size_t)largo, f) != (size_t)largo) {
        free(texto);
        return NULL;
    }
    texto[largo] = '\0';
    return texto;
}

/* Mismo esquema que la generacion de llamadas: sumai si cabe en el
 * inmediato; si no, cuatro bytes con sumai, cizqi y ori. */
static int constante(FILE *f, unsigned reg, uint32_t v)
{
    if (v <= 1023)
        return fprintf(f, "sumai x%u, x0, %u\n", reg, (unsigned)v) >= 0;
    if (fprintf(f, "sumai x%u, x0, %u\n", reg, (unsigned)(v >> 24)) < 0) return 0;
    for (int turno = 16; turno >= 0; turno -= 8)
        if (fprintf(f, "cizqi x%u, x%u, 8\n", reg, reg) < 0 ||
            fprintf(f, "ori x%u, x%u, %u\n", reg, reg,
                    (unsigned)((v >> turno) & 255)) < 0) return 0;
    return 1;
}

static const SimSimbolo *global(const SimTabla *tabla, const char *nombre)
{
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden)
        if (s->ambito == tabla->global && !strcmp(s->nombre, nombre)) return s;
    return NULL;
}

/* Las constantes globales solo pueden usar otras constantes globales. */
static int referencia_global(void *datos, const AstNodo *id, GbReferencia *r)
{
    const SimSimbolo *s = id && id->texto ? global(datos, id->texto) : NULL;
    if (!s || s->clase != SIM_CONSTANTE || s->tipo != SIM_TIPO_INT ||
        s->dimensiones || s->bytes != 4 || s->almacen != SIM_ALMACEN_GLOBAL ||
        s->desplazamiento < 0 || s->desplazamiento > 1023 || s->desplazamiento % 4)
        return 0;
    *r = (GbReferencia){GB_ENTERO, 3, (int)s->desplazamiento, 0};
    return 1;
}

static int arranque_lineal(const Ast *ast, const SimTabla *tabla,
                           const EfArranque *arranque, const RrTabla *destinos,
                           FILE *f, FILE *diagnosticos)
{
    uint32_t main_pc = 0;
    if (rr_buscar(destinos, "main", RR_FUNCION, &main_pc) != RR_OK)
        return fallo(diagnosticos, "el arranque necesita una funcion main", NULL);
    if (fputs("__arranque:\n", f) < 0 || !constante(f, 3, arranque->gp) ||
        !constante(f, 2, arranque->sp)) return 0;
    for (const AstNodo *c = ast->raiz->primer_hijo; c; c = c->siguiente) {
        if (c->tipo != AST_CONSTANTE) continue;
        /* hijos: identificador, tipo, valor */
        const AstNodo *id = c->primer_hijo;
        const AstNodo *valor = id && id->siguiente ? id->siguiente->siguiente : NULL;
        GbReferencia r;
        if (!valor || !referencia_global((void *)tabla, id, &r))
            return fallo(diagnosticos, "constante global no soportada",
                         id ? id->texto : NULL);
        if (gb_generar(valor, referencia_global, (void *)tabla, f, diagnosticos) != 0 ||
            fprintf(f, "guardap x3, x4, %d\n", r.desplazamiento) < 0)
            return fallo(diagnosticos, "no se pudo inicializar la constante", id->texto);
    }
    return constante(f, 13, main_pc) &&
           fputs("syer x1, x13, 0\n"
                 "__arranque_fin:\n"
                 "sye x0, __arranque_fin\n", f) >= 0;
}

/* Genera el lineal de todas las funciones con las direcciones de destinos. */
static char *generar(const Ast *ast, const SimTabla *tabla,
                     const EfArranque *arranque, const RrTabla *destinos,
                     FILE *diagnosticos)
{
    FILE *f = tmpfile();
    if (!f) return NULL;
    int ok = !arranque || arranque_lineal(ast, tabla, arranque, destinos, f, diagnosticos);
    for (const AstNodo *fn = ast->raiz->primer_hijo; ok && fn; fn = fn->siguiente) {
        if (fn->tipo != AST_FUNCION) continue;
        size_t marco = marco_de(tabla, nombre(fn));
        ok = marco && fprintf(f, "__fn_%s:\n", nombre(fn)) >= 0 &&
             ri_generar_funcion(fn, tabla, marco, destinos, f, diagnosticos) == 0;
        if (!ok) fallo(diagnosticos, "no se pudo generar la funcion", nombre(fn));
    }
    char *texto = ok ? leer_todo(f) : NULL;
    fclose(f);
    return texto;
}

int ef_enlazar(const Ast *ast, const SimTabla *tabla, uint32_t inicio,
               const EfArranque *arranque, RrTabla *funciones, FILE *salida, FILE *diagnosticos,
               EfResumen *resumen)
{
    if (!ast || !ast->raiz || !tabla || !tabla->global || !salida) {
        fallo(diagnosticos, "argumentos invalidos", NULL);
        return 1;
    }
    RrTabla destinos;
    rr_inicializar(&destinos);
    size_t cantidad = 0;
    int ok = 1;
    for (const AstNodo *fn = ast->raiz->primer_hijo; ok && fn; fn = fn->siguiente) {
        if (fn->tipo != AST_FUNCION) continue;
        if (!nombre(fn)) ok = fallo(diagnosticos, "funcion sin nombre", NULL);
        else if (rr_definir(&destinos, nombre(fn), RR_FUNCION, inicio) != RR_OK)
            ok = fallo(diagnosticos, "funcion duplicada o direccion invalida", nombre(fn));
        ++cantidad;
    }
    if (ok && !cantidad) ok = fallo(diagnosticos, "el programa no tiene funciones", NULL);

    FILE *bundles = NULL;
    RsResumen saltos = {0};
    size_t vuelta = 0;
    int estable = 0;
    while (ok && !estable) {
        if (++vuelta > EF_MAX_VUELTAS) {
            ok = fallo(diagnosticos, "las direcciones de funciones no convergen", NULL);
            break;
        }
        char *lineal = generar(ast, tabla, arranque, &destinos, diagnosticos);
        if (!lineal) { ok = 0; break; }
        RrTabla etiquetas;
        rr_inicializar(&etiquetas);
        if (bundles) fclose(bundles);
        bundles = tmpfile();
        ok = bundles && rs_resolver_etiquetas(lineal, inicio, bundles, diagnosticos,
                                              &saltos, &etiquetas) == 0;
        free(lineal);

        /* Comparar la direccion supuesta con el PC real de cada funcion. */
        RrTabla nuevos;
        rr_inicializar(&nuevos);
        estable = 1;
        for (const AstNodo *fn = ast->raiz->primer_hijo; ok && fn; fn = fn->siguiente) {
            if (fn->tipo != AST_FUNCION) continue;
            char etiqueta[160];
            uint32_t real = 0, supuesta = 0;
            snprintf(etiqueta, sizeof(etiqueta), "__fn_%s", nombre(fn));
            ok = rr_buscar(&etiquetas, etiqueta, RR_ETIQUETA, &real) == RR_OK &&
                 rr_buscar(&destinos, nombre(fn), RR_FUNCION, &supuesta) == RR_OK &&
                 rr_definir(&nuevos, nombre(fn), RR_FUNCION, real) == RR_OK;
            if (!ok) fallo(diagnosticos, "funcion sin direccion", nombre(fn));
            if (real != supuesta) estable = 0;
        }
        rr_liberar(&etiquetas);
        rr_liberar(&destinos);
        destinos = nuevos;
    }

    /* RrEntrada es opaca: se copia recorriendo las funciones por nombre. */
    if (ok && funciones)
        for (const AstNodo *fn = ast->raiz->primer_hijo; ok && fn; fn = fn->siguiente) {
            if (fn->tipo != AST_FUNCION) continue;
            uint32_t d = 0;
            ok = rr_buscar(&destinos, nombre(fn), RR_FUNCION, &d) == RR_OK &&
                 rr_definir(funciones, nombre(fn), RR_FUNCION, d) == RR_OK;
        }
    if (ok) {
        char *texto = leer_todo(bundles);
        ok = texto && fputs(texto, salida) >= 0 && fflush(salida) == 0;
        free(texto);
        if (!ok) fallo(diagnosticos, "error de escritura", NULL);
    }
    if (ok && resumen) *resumen = (EfResumen){saltos, cantidad, vuelta};
    if (bundles) fclose(bundles);
    rr_liberar(&destinos);
    return ok ? 0 : 1;
}
