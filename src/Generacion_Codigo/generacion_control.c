#include "generacion_interna.h"

#include <limits.h>
#include <string.h>

/* Recorrido estructural y etiquetas simbolicas. Los PC se fijan despues
 * de empaquetar las instrucciones e insertar las separaciones necesarias. */
static int condicional(GfContexto *c, const AstNodo *n, int es_void, int *retorna)
{
    const AstNodo *cond = n->primer_hijo;
    const AstNodo *si = cond ? cond->siguiente : NULL;
    const AstNodo *no = si ? si->siguiente : NULL;
    if (!cond || !si || si->tipo != AST_BLOQUE ||
        (no && (no->siguiente ||
                (no->tipo != AST_BLOQUE && no->tipo != AST_SI))))
        return gf_fallo(c, n, "estructura de whether/alif invalida");
    if (c->bloques >= 256 || c->etiquetas == UINT_MAX)
        return gf_fallo(c, n, "demasiados condicionales anidados");
    unsigned etiqueta = c->etiquetas++;
    if (!gf_condicion(c, cond)) return 0;
    if (fprintf(c->salida, "igualsi x4, x0, __gf_%s_else_%u\n",
                c->funcion, etiqueta) < 0) return 0;
    int retorna_si = 0, retorna_no = 0;
    ++c->bloques;
    int ok = gc_bloque(c, si, es_void, &retorna_si);
    if (ok && no && !retorna_si)
        ok = fprintf(c->salida, "sye x0, __gf_%s_fin_%u\n",
                     c->funcion, etiqueta) >= 0;
    if (ok) ok = fprintf(c->salida, "__gf_%s_else_%u:\n",
                         c->funcion, etiqueta) >= 0;
    if (ok && no) {
        if (no->tipo == AST_SI)
            ok = condicional(c, no, es_void, &retorna_no);
        else
            ok = gc_bloque(c, no, es_void, &retorna_no);
    }
    if (ok && no && !retorna_si)
        ok = fprintf(c->salida, "__gf_%s_fin_%u:\n",
                     c->funcion, etiqueta) >= 0;
    --c->bloques;
    *retorna = retorna_si && retorna_no;
    return ok;
}

static int mientras(GfContexto *c, const AstNodo *n, int es_void)
{
    const AstNodo *cond = n->primer_hijo;
    const AstNodo *cuerpo = cond ? cond->siguiente : NULL;
    if (!cond || !cuerpo || cuerpo->tipo != AST_BLOQUE || cuerpo->siguiente)
        return gf_fallo(c, n, "estructura de whale invalida");
    if (c->bloques >= 256 || c->etiquetas == UINT_MAX)
        return gf_fallo(c, n, "demasiadas estructuras de control anidadas");
    unsigned etiqueta = c->etiquetas++;
    if (fprintf(c->salida, "__gf_%s_while_cond_%u:\n", c->funcion, etiqueta) < 0 ||
        !gf_condicion(c, cond) ||
        fprintf(c->salida, "igualsi x4, x0, __gf_%s_while_fin_%u\n",
                c->funcion, etiqueta) < 0) return 0;
    int retorna = 0;
    ++c->bloques;
    int ok = gc_bloque(c, cuerpo, es_void, &retorna);
    --c->bloques;
    if (ok && !retorna)
        ok = fprintf(c->salida, "sye x0, __gf_%s_while_cond_%u\n",
                     c->funcion, etiqueta) >= 0;
    if (ok) ok = fprintf(c->salida, "__gf_%s_while_fin_%u:\n",
                         c->funcion, etiqueta) >= 0;
    return ok;
}

/* Signo del paso si es constante: 1, -1, o 0 si se decide en ejecucion. */
static int sentido_paso(const AstNodo *paso)
{
    if (paso->tipo == AST_ENTERO) return 1;
    if (paso->tipo == AST_UNARIO && paso->texto &&
        !strcmp(paso->texto, "neumann") && paso->primer_hijo &&
        paso->primer_hijo->tipo == AST_ENTERO && !paso->primer_hijo->siguiente)
        return -1;
    return 0;
}

static int es_cero(const AstNodo *n)
{
    if (!n->texto || !*n->texto) return 0;
    for (const char *p = n->texto; *p; ++p) if (*p != '0') return 0;
    return 1;
}

/* cycle i let a until b step p { ... } endgame;
 *   i : a; cond: i < b (paso > 0) o i > b (paso < 0); cuerpo; i : i gauss p.
 * El limite es exclusivo y, como en whale, se evalua en cada vuelta.
 * i vive en el ambito cycle_N, ligado al cuerpo: la referencia se resuelve
 * con una copia de su identificador colgada del cuerpo, sin tocar el AST. */
static int recorrido(GfContexto *c, const AstNodo *n, int es_void)
{
    const AstNodo *variable = n->primer_hijo;
    const AstNodo *inicio = variable ? variable->siguiente : NULL;
    const AstNodo *limite = inicio ? inicio->siguiente : NULL;
    const AstNodo *paso = limite ? limite->siguiente : NULL;
    const AstNodo *cuerpo = paso ? paso->siguiente : NULL;
    if (!cuerpo || cuerpo->tipo != AST_BLOQUE || cuerpo->siguiente ||
        variable->tipo != AST_IDENTIFICADOR || !variable->texto)
        return gf_fallo(c, n, "estructura de cycle invalida");
    if (c->bloques >= 256 || c->etiquetas == UINT_MAX)
        return gf_fallo(c, n, "demasiadas estructuras de control anidadas");
    int sentido = sentido_paso(paso);
    if ((sentido == 1 && es_cero(paso)) ||
        (sentido == -1 && es_cero(paso->primer_hijo)))
        return gf_fallo(c, paso, "el paso de cycle no puede ser cero");

    AstNodo indice = *variable;
    indice.padre = (AstNodo *)cuerpo; /* Solo se consulta, nunca se modifica. */
    indice.siguiente = indice.primer_hijo = indice.ultimo_hijo = NULL;
    GbReferencia ref;
    if (!c->resolver || !c->resolver(c->datos, &indice, &ref) ||
        ref.tipo != GB_ENTERO || (ref.base != 2 && ref.base != 3) ||
        ref.desplazamiento < -1024 || ref.desplazamiento > 1023 ||
        ref.desplazamiento % 4 || !ref.escribible)
        return gf_fallo(c, variable, "variable de cycle sin referencia escalar");

    unsigned etiqueta = c->etiquetas++;
    if (!gf_expresion(c, inicio, 0) ||
        fprintf(c->salida, "guardap x%u, x4, %d\n"
                           "__gf_%s_cycle_cond_%u:\n",
                ref.base, ref.desplazamiento, c->funcion, etiqueta) < 0)
        return 0;
    if (sentido) {
        /* x4 = i < limite (ascendente) o i > limite (descendente). */
        if (!gf_expresion(c, limite, 0) ||
            fprintf(c->salida, "cargai x5, x%u, %d\n%s x4, x5, x4\n",
                    ref.base, ref.desplazamiento,
                    sentido > 0 ? "mrq" : "myq") < 0) return 0;
    } else {
        /* El limite queda en la pila mientras se evalua el paso. */
        if (!gf_expresion(c, limite, 0) ||
            fprintf(c->salida, "restai x2, x2, 4\nguardap x2, x4, 0\n") < 0 ||
            !gf_expresion(c, paso, 4) ||
            fprintf(c->salida, "cargai x5, x2, 0\nsumai x2, x2, 4\n"
                               "cargai x6, x%u, %d\n"
                               "myq x7, x4, x0\n"
                               "mrq x4, x6, x5\n"
                               "myq x5, x6, x5\n"
                               "and x4, x4, x7\n"
                               "xori x7, x7, 1\n"
                               "and x5, x5, x7\n"
                               "or x4, x4, x5\n",
                    ref.base, ref.desplazamiento) < 0) return 0;
    }
    if (fprintf(c->salida, "igualsi x4, x0, __gf_%s_cycle_fin_%u\n",
                c->funcion, etiqueta) < 0) return 0;
    int retorna = 0;
    ++c->bloques;
    int ok = gc_bloque(c, cuerpo, es_void, &retorna);
    --c->bloques;
    if (ok && !retorna)
        ok = gf_expresion(c, paso, 0) &&
             fprintf(c->salida, "cargai x5, x%u, %d\n"
                                "suma x4, x5, x4\n"
                                "guardap x%u, x4, %d\n"
                                "sye x0, __gf_%s_cycle_cond_%u\n",
                     ref.base, ref.desplazamiento, ref.base,
                     ref.desplazamiento, c->funcion, etiqueta) >= 0;
    if (ok) ok = fprintf(c->salida, "__gf_%s_cycle_fin_%u:\n",
                         c->funcion, etiqueta) >= 0;
    return ok;
}

int gc_bloque(GfContexto *c, const AstNodo *bloque, int es_void, int *retorna)
{
    if (!bloque || bloque->tipo != AST_BLOQUE)
        return gf_fallo(c, bloque, "bloque ausente");
    int retorno = 0;
    for (const AstNodo *n = bloque->primer_hijo; n; n = n->siguiente) {
        if (retorno) return gf_fallo(c, n, "sentencias despues del retorno final");
        if (n->tipo == AST_BLOQUE) {
            if (c->bloques >= 256)
                return gf_fallo(c, n, "demasiados bloques anidados");
            ++c->bloques;
            int ok = gc_bloque(c, n, es_void, &retorno);
            --c->bloques;
            if (!ok) return 0;
        } else if (n->tipo == AST_SI) {
            if (!condicional(c, n, es_void, &retorno)) return 0;
        } else if (n->tipo == AST_MIENTRAS) {
            if (!mientras(c, n, es_void)) return 0;
            /* Puede ejecutarse cero veces: give en el cuerpo no garantiza
             * el retorno de toda la funcion. */
            retorno = 0;
        } else if (n->tipo == AST_RECORRIDO) {
            if (!recorrido(c, n, es_void)) return 0;
            retorno = 0; /* Igual que whale: puede no ejecutarse. */
        } else if (!gf_sentencia(c, n, es_void, &retorno)) return 0;
    }
    *retorna = retorno;
    return 1;
}
