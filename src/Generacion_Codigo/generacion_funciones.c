#include "generacion_interna.h"
#include "funciones_llamada.h"
#include "funciones_marco.h"

#include <stdint.h>
#include <limits.h>
#include <string.h>

typedef struct {
    GbResolver resolver;
    GbResolverArreglo arreglos;
    void *datos;
    size_t reserva;
} Referencias;

int gf_fallo(GfContexto *c, const AstNodo *n, const char *mensaje)
{
    if (c->diagnosticos)
        fprintf(c->diagnosticos, "%zu:%zu: generacion funciones: %s\n",
                n ? n->ubicacion.first_line : 0,
                n ? n->ubicacion.first_column : 0, mensaje);
    return 0;
}

static int referencia_desplazada(void *datos, const AstNodo *n, GbReferencia *r)
{
    Referencias *ref = datos;
    if (!ref->resolver || !ref->resolver(ref->datos, n, r)) return 0;
    if (r->base == 2) {
        if (ref->reserva > 1020 || r->desplazamiento > 1023 - (int)ref->reserva)
            return 0;
        r->desplazamiento += (int)ref->reserva;
    }
    return 1;
}

/* Igual que referencia_desplazada, para arreglos relativos a sp. */
static int arreglo_desplazado(void *datos, const AstNodo *n, GbArreglo *a)
{
    Referencias *ref = datos;
    if (!ref->arreglos || !ref->arreglos(ref->datos, n, a)) return 0;
    if (a->base == 2) {
        if (ref->reserva > 1020 || a->desplazamiento > 1023 - (int)ref->reserva)
            return 0;
        a->desplazamiento += (int)ref->reserva;
    }
    return 1;
}

static int emitir_constante(FILE *f, unsigned reg, uint32_t v)
{
    if (v <= 1023)
        return fprintf(f, "sumai x%u, x0, %u\n", reg, (unsigned)v) >= 0;
    if (fprintf(f, "sumai x%u, x0, %u\n", reg, (unsigned)(v >> 24)) < 0)
        return 0;
    for (int turno = 16; turno >= 0; turno -= 8) {
        if (fprintf(f, "cizqi x%u, x%u, 8\n", reg, reg) < 0 ||
            fprintf(f, "ori x%u, x%u, %u\n", reg, reg,
                    (unsigned)((v >> turno) & 255)) < 0)
            return 0;
    }
    return 1;
}

static int expresion_basica(GfContexto *c, const AstNodo *n, size_t reserva)
{
    Referencias ref = {c->resolver, c->arreglos, c->datos, reserva};
    return gb_generar_arreglos(n, referencia_desplazada,
                               c->arreglos ? arreglo_desplazado : NULL, &ref,
                               c->salida, c->diagnosticos) == 0;
}

/* Las expresiones con llamadas se procesan usando la pila para valores vivos. */
static int expresion_ext(GfContexto *c, const AstNodo *n, size_t reserva);

static int contiene_llamada(const AstNodo *n)
{
    if (!n) return 0;
    if (n->tipo == AST_LLAMADA) return 1;
    for (const AstNodo *h = n->primer_hijo; h; h = h->siguiente)
        if (contiene_llamada(h)) return 1;
    return 0;
}

static int llamada(GfContexto *c, const AstNodo *n, size_t reserva)
{
    if (!n || n->tipo != AST_LLAMADA || !n->primer_hijo)
        return gf_fallo(c, n, "llamada invalida");
    const AstNodo *id = n->primer_hijo;
    const AstNodo *args = id->siguiente;
    if (id->tipo != AST_IDENTIFICADOR || !args ||
        args->tipo != AST_ARGUMENTOS || args->siguiente)
        return gf_fallo(c, n, "solo llamadas directas del ABI escalar");
    unsigned direccion;
    if (!c->destino || !c->destino(c->datos, id, &direccion) ||
        (direccion & 15u) != 0)
        return gf_fallo(c, id, "funcion sin direccion alineada");
    size_t cantidad = 0;
    for (const AstNodo *a = args->primer_hijo; a; a = a->siguiente) {
        if (++cantidad > 4)
            return gf_fallo(c, n, "maximo cuatro argumentos en integracion AST inicial");
    }
    size_t i = 0;
    for (const AstNodo *a = args->primer_hijo; a; a = a->siguiente, ++i) {
        /* Cada argumento se evalua ANTES de crear su slot. Los accesos sp
         * posteriores compensan los slots temporales ya reservados. */
        if (reserva > SIZE_MAX - 4 * i ||
            !expresion_ext(c, a, reserva + 4 * i))
            return gf_fallo(c, a, "argumento fuera del subconjunto escalar");
        if (fprintf(c->salida, "restai x2, x2, 4\n"
                               "guardap x2, x4, 0\n") < 0) return 0;
    }
    unsigned fuentes[4];
    for (size_t j = 0; j < cantidad; ++j) {
        fuentes[j] = (unsigned)(4 + j);
        if (fprintf(c->salida, "cargai x%u, x2, %zu\n", fuentes[j],
                    4 * (cantidad - 1 - j)) < 0) return 0;
    }
    if (!emitir_constante(c->salida, 13, direccion)) return 0;
    if (fl_emitir_llamada(c->salida, fuentes, cantidad) != FL_OK) return 0;
    if (cantidad && fprintf(c->salida, "sumai x2, x2, %zu\n", 4 * cantidad) < 0)
        return 0;
    return 1;
}

static int expresion_ext(GfContexto *c, const AstNodo *n, size_t reserva)
{
    if (!n) return gf_fallo(c, n, "expresion ausente");
    if (c->nivel >= 256) return gf_fallo(c, n, "expresion demasiado profunda");
    ++c->nivel;
    int ok = 0;
    if (!contiene_llamada(n)) {
        ok = expresion_basica(c, n, reserva);
    } else if (n->tipo == AST_LLAMADA) {
        ok = llamada(c, n, reserva);
        if (ok) ok = fprintf(c->salida, "sumai x4, x8, 0\n") >= 0;
    } else if (n->tipo == AST_UNARIO && n->texto && n->primer_hijo &&
               !n->primer_hijo->siguiente && !strcmp(n->texto, "neumann")) {
        ok = expresion_ext(c, n->primer_hijo, reserva);
        if (ok) ok = fprintf(c->salida, "resta x4, x0, x4\n") >= 0;
    } else if (n->tipo == AST_BINARIO && n->texto && n->primer_hijo &&
               n->primer_hijo->siguiente && !n->primer_hijo->siguiente->siguiente &&
               (!strcmp(n->texto, "gauss") || !strcmp(n->texto, "neumann"))) {
        const AstNodo *izq = n->primer_hijo;
        if (reserva <= SIZE_MAX - 4 && expresion_ext(c, izq, reserva) &&
            fprintf(c->salida, "restai x2, x2, 4\n"
                               "guardap x2, x4, 0\n") >= 0 &&
            expresion_ext(c, izq->siguiente, reserva + 4)) {
            ok = fprintf(c->salida, "cargai x5, x2, 0\n"
                       "%s x4, x5, x4\n"
                       "sumai x2, x2, 4\n",
                       !strcmp(n->texto, "gauss") ? "suma" : "resta") >= 0;
        }
    } else {
        ok = gf_fallo(c, n, "operador con llamadas aun no soportado");
    }
    --c->nivel;
    return ok;
}

static int asignacion_ext(GfContexto *c, const AstNodo *n)
{
    int declaracion = n->tipo == AST_DECLARACION;
    const AstNodo *id = n->primer_hijo;
    const AstNodo *valor = id ? id->siguiente : NULL;
    if (!id || !valor || id->tipo != AST_IDENTIFICADOR)
        return gf_fallo(c, n, "asignacion invalida");
    GbReferencia ref;
    if (!c->resolver || !c->resolver(c->datos, id, &ref) ||
        (ref.tipo != GB_ENTERO && ref.tipo != GB_BOOLEANO) ||
        (ref.base != 2 && ref.base != 3 && ref.base != 0) ||
        ref.desplazamiento < -1024 || ref.desplazamiento > 1023 ||
        ref.desplazamiento % 4 || !ref.escribible)
        return gf_fallo(c, id, "referencia escalar no valida");
    if (declaracion) {
        const char *esperado = ref.tipo == GB_ENTERO ? "declare_int" : "declare_boolean";
        if (valor->tipo != AST_TIPO || !valor->texto || strcmp(valor->texto, esperado))
            return gf_fallo(c, valor, "tipo declarado incompatible");
        valor = valor->siguiente;
    }
    if (!valor || valor->siguiente) return gf_fallo(c, n, "asignacion invalida");
    if (!expresion_ext(c, valor, 0)) return 0;
    return fprintf(c->salida, "guardap x%u, x4, %d\n", ref.base,
                   ref.desplazamiento) >= 0;
}

/* La semantica verifica el booleano; las llamadas en condiciones quedan
 * fuera del subconjunto inicial. El resultado se entrega en x4. */
int gf_condicion(GfContexto *c, const AstNodo *n)
{
    if (contiene_llamada(n))
        return gf_fallo(c, n, "condicion con llamadas aun no soportada");
    return expresion_basica(c, n, 0);
}

int gf_expresion(GfContexto *c, const AstNodo *n, size_t reserva)
{
    return expresion_ext(c, n, reserva);
}

int gf_sentencia(GfContexto *c, const AstNodo *n, int es_void, int *retorna)
{
    int retorno = 0;
    if (n->tipo == AST_DECLARACION || n->tipo == AST_ASIGNACION) {
        if (contiene_llamada(n)) {
            if (!asignacion_ext(c, n)) return 0;
        } else if (gb_generar_arreglos(n, c->resolver, c->arreglos, c->datos,
                                       c->salida, c->diagnosticos) != 0) return 0;
    } else if (n->tipo == AST_LLAMADA) {
        if (!llamada(c, n, 0)) return 0;
    } else if (n->tipo == AST_RETORNO) {
        const AstNodo *valor = n->primer_hijo;
        if (valor && (es_void || valor->siguiente))
            return gf_fallo(c, n, "retorno incompatible con void");
        if (!valor && !es_void)
            return gf_fallo(c, n, "retorno escalar sin valor");
        if (valor) {
            if (valor->tipo == AST_LLAMADA) {
                if (!llamada(c, valor, 0)) return 0;
            } else {
                if (!expresion_ext(c, valor, 0)) return 0;
                if (fprintf(c->salida, "sumai x8, x4, 0\n") < 0) return 0;
            }
        }
        if (fm_emitir_epilogo(c->salida, c->bytes_marco) != FM_OK) return 0;
        retorno = 1;
    } else return gf_fallo(c, n, "sentencia fuera del subconjunto 3B-D1");
    *retorna = retorno;
    return 1;
}

int gf_generar_funcion(const AstNodo *funcion, size_t bytes_marco,
                       GbResolver resolver, GfResolverDestino destino,
                       void *datos, FILE *salida, FILE *diagnosticos)
{
    return gf_generar_funcion_arreglos(funcion, bytes_marco, resolver, NULL,
                                       destino, datos, salida, diagnosticos);
}

int gf_generar_funcion_arreglos(const AstNodo *funcion, size_t bytes_marco,
                                GbResolver resolver, GbResolverArreglo arreglos,
                                GfResolverDestino destino, void *datos,
                                FILE *salida, FILE *diagnosticos)
{
    if (!salida || !funcion || funcion->tipo != AST_FUNCION) return 1;
    const AstNodo *id = funcion->primer_hijo;
    const AstNodo *tipo = id ? id->siguiente : NULL;
    const AstNodo *params = tipo ? tipo->siguiente : NULL;
    const AstNodo *bloque = params ? params->siguiente : NULL;
    if (!id || id->tipo != AST_IDENTIFICADOR || !id->texto || !*id->texto || !tipo ||
        tipo->tipo != AST_TIPO || !tipo->texto || !params ||
        params->tipo != AST_PARAMETROS || !bloque || bloque->siguiente ||
        bloque->tipo != AST_BLOQUE) return 1;
    int es_void = strcmp(tipo->texto, "declare_infinite_void") == 0;
    if (!es_void && strcmp(tipo->texto, "declare_int") &&
        strcmp(tipo->texto, "declare_boolean")) return 1;
    size_t cantidad = 0;
    for (const AstNodo *p = params->primer_hijo; p; p = p->siguiente) {
        const AstNodo *pid = p->primer_hijo;
        const AstNodo *pt = pid ? pid->siguiente : NULL;
        if (p->tipo != AST_PARAMETRO || !pid || pid->tipo != AST_IDENTIFICADOR ||
            !pt || pt->tipo != AST_TIPO || !pt->texto || pt->siguiente ||
            (strcmp(pt->texto, "declare_int") && strcmp(pt->texto, "declare_boolean")))
            return 1;
        ++cantidad;
    }
    FILE *temporal = tmpfile();
    if (!temporal) return 1;
    GfContexto c = {temporal, diagnosticos, resolver, destino, datos, bytes_marco,
                  0, id->texto, 0, 0, arreglos};
    int retorna = 0;
    int ok = fm_emitir_prologo(temporal, bytes_marco, cantidad) == FM_OK &&
             gc_bloque(&c, bloque, es_void, &retorna);
    if (ok && !retorna) {
        if (!es_void) ok = gf_fallo(&c, bloque, "falta retorno escalar en algun camino");
        else ok = fm_emitir_epilogo(temporal, bytes_marco) == FM_OK;
    }
    if (ok && (fflush(temporal) != 0 || fseek(temporal, 0, SEEK_SET) != 0)) ok = 0;
    if (ok) {
        char buf[4096];
        size_t leidos;
        while ((leidos = fread(buf, 1, sizeof(buf), temporal)) != 0)
            if (fwrite(buf, 1, leidos, salida) != leidos) { ok = 0; break; }
        if (ferror(temporal) || fflush(salida) != 0) ok = 0;
    }
    fclose(temporal);
    return ok ? 0 : 1;
}
