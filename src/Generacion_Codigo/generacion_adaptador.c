#include "generacion_adaptador.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    const SimTabla *tabla;
    const SimAmbito *ambito;
    GfResolverDestino destino;
    void *contexto_destinos;
    size_t bytes_marco;
} GdContexto;

static const SimAmbito *ambito_funcion(const SimTabla *tabla, const char *nombre)
{
    const SimAmbito *encontrado = NULL;
    for (const SimAmbito *a = tabla->global->siguiente_creacion;
         a; a = a->siguiente_creacion) {
        if (a->padre == tabla->global && strcmp(a->nombre, nombre) == 0) {
            if (encontrado) return NULL;
            encontrado = a;
        }
    }
    return encontrado;
}

/* La pertenencia se decide con enlaces del AST, no con nombres bloque_N
 * ni con el orden global de creacion de ambitos. */
static int pertenece(const SimAmbito *a, const SimAmbito *funcion)
{
    for (; a; a = a->padre) if (a == funcion) return 1;
    return 0;
}

static const SimAmbito *ambito_del_uso(const GdContexto *c, const AstNodo *id)
{
    if (!c->ambito->bloque) return c->ambito; /* Tablas manuales antiguas. */
    for (const AstNodo *n = id; n; n = n->padre) {
        if (n->tipo != AST_BLOQUE) continue;
        for (const SimAmbito *a = c->tabla->global; a; a = a->siguiente_creacion)
            if (a->bloque == n && pertenece(a, c->ambito)) return a;
        return NULL; /* No adivinar el ambito de un bloque no vinculado. */
    }
    return NULL;
}

/* Semantica registra antes de analizar el inicializador. Recorremos en
 * preorden hasta el uso para respetar ese mismo punto de visibilidad,
 * incluso con declaraciones en la misma linea o dentro de un grupo. */
static int visible(const SimSimbolo *s, const AstNodo *uso)
{
    if (!s->ambito->bloque || s->es_parametro || s->clase == SIM_PARAMETRO)
        return 1;
    if (!s->declaracion) return 0;
    /* La variable de cycle se declara en la cabecera, fuera del cuerpo,
     * y es visible en todo el cuerpo. */
    if (s->declaracion->padre && s->declaracion->padre->tipo == AST_RECORRIDO &&
        s->declaracion->padre->primer_hijo == s->declaracion) return 1;
    const AstNodo *raiz = s->ambito->bloque;
    for (const AstNodo *n = raiz; n;) {
        if (n == s->declaracion) return 1;
        if (n == uso) return 0;
        if (n->primer_hijo) n = n->primer_hijo;
        else {
            while (n && n != raiz && !n->siguiente) n = n->padre;
            if (n == raiz || !n) break;
            n = n->siguiente;
        }
    }
    return 0;
}

/* Simbolo visible en el punto de uso, o NULL. */
static const SimSimbolo *buscar(GdContexto *c, const AstNodo *id)
{
    if (!id || id->tipo != AST_IDENTIFICADOR || !id->texto) return NULL;
    const SimAmbito *uso = ambito_del_uso(c, id);
    for (const SimAmbito *a = uso; a; a = a->padre)
        for (const SimSimbolo *s = a->simbolos; s; s = s->hh.next)
            if (!strcmp(s->nombre, id->texto) && visible(s, id)) return s;
    return NULL;
}

static int referencia(void *datos, const AstNodo *id, GbReferencia *r)
{
    GdContexto *c = datos;
    if (!r) return 0;
    const SimSimbolo *encontrado = buscar(c, id);
    if (!encontrado || encontrado->tipo != SIM_TIPO_INT ||
        encontrado->dimensiones || encontrado->bytes != 4 ||
        (encontrado->clase != SIM_VARIABLE &&
         encontrado->clase != SIM_PARAMETRO &&
         encontrado->clase != SIM_CONSTANTE)) return 0;
    unsigned base;
    if (pertenece(encontrado->ambito, c->ambito) &&
        (encontrado->almacen == SIM_ALMACEN_LOCAL ||
         encontrado->almacen == SIM_ALMACEN_PARAMETRO)) {
        base = 2;
        if (encontrado->desplazamiento < 4 ||
            (unsigned long)encontrado->desplazamiento > c->bytes_marco - 4)
            return 0;
    } else if (encontrado->ambito == c->tabla->global &&
               encontrado->almacen == SIM_ALMACEN_GLOBAL) {
        base = 3;
    } else return 0;
    if (encontrado->desplazamiento < -1024 ||
        encontrado->desplazamiento > 1023 ||
        encontrado->desplazamiento % 4 != 0) return 0;
    *r = (GbReferencia){GB_ENTERO, base, (int)encontrado->desplazamiento,
                       encontrado->clase != SIM_CONSTANTE};
    return 1;
}

/* Arreglo local o global de enteros con todas sus dimensiones conocidas.
 * Los parametros de arreglo (por referencia) y las dimensiones dinamicas
 * quedan fuera. Ocupa bytes = 4 * producto de las longitudes. */
static int arreglo(void *datos, const AstNodo *id, GbArreglo *r)
{
    GdContexto *c = datos;
    const SimSimbolo *s = buscar(c, id);
    if (!s || !r || s->tipo != SIM_TIPO_INT || s->clase != SIM_VARIABLE ||
        !s->dimensiones || s->dimensiones > GB_MAX_DIMENSIONES || !s->longitudes)
        return 0;
    size_t elementos = 1;
    for (unsigned k = 0; k < s->dimensiones; ++k) {
        if (!s->longitudes[k] || s->longitudes[k] > 1024 ||
            elementos > SIZE_MAX / s->longitudes[k]) return 0;
        elementos *= s->longitudes[k];
    }
    if (elementos > 256 || s->bytes != 4 * elementos) return 0;
    unsigned base;
    if (pertenece(s->ambito, c->ambito) && s->almacen == SIM_ALMACEN_LOCAL) {
        base = 2;
        if (s->desplazamiento < 4 ||
            (size_t)s->desplazamiento + s->bytes > c->bytes_marco) return 0;
    } else if (s->ambito == c->tabla->global && s->almacen == SIM_ALMACEN_GLOBAL) {
        base = 3;
    } else return 0;
    if (s->desplazamiento < -1024 || s->desplazamiento > 1023 ||
        s->desplazamiento % 4) return 0;
    *r = (GbArreglo){base, (int)s->desplazamiento, s->dimensiones, {0}};
    for (unsigned k = 0; k < s->dimensiones; ++k)
        r->longitudes[k] = (uint32_t)s->longitudes[k];
    return 1;
}

static int resolver_destino(void *datos, const AstNodo *id, unsigned *direccion)
{
    GdContexto *c = datos;
    if (!c->destino || !id || id->tipo != AST_IDENTIFICADOR ||
        !id->texto || !direccion) return 0;
    const SimSimbolo *f = NULL;
    for (const SimSimbolo *s = c->tabla->primero; s; s = s->siguiente_orden)
        if (s->ambito == c->tabla->global && s->clase == SIM_FUNCION &&
            strcmp(s->nombre, id->texto) == 0) { f = s; break; }
    if (!f) return 0;
    return c->destino(c->contexto_destinos, id, direccion) &&
           (*direccion & 15u) == 0;
}

int gd_generar_funcion(const AstNodo *funcion, const SimTabla *tabla,
                       size_t bytes_marco, GfResolverDestino destino,
                       void *contexto_destinos, FILE *salida,
                       FILE *diagnosticos)
{
    if (!funcion || funcion->tipo != AST_FUNCION || !tabla || !tabla->global ||
        !salida || bytes_marco < 4 || bytes_marco % 4 ||
        bytes_marco > (size_t)INT_MAX) return 1;
    const AstNodo *id = funcion->primer_hijo;
    if (!id || id->tipo != AST_IDENTIFICADOR || !id->texto) return 1;
    const SimAmbito *ambito = ambito_funcion(tabla, id->texto);
    int tiene_funcion = 0;
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden)
        if (s->ambito == tabla->global && s->clase == SIM_FUNCION &&
            strcmp(s->nombre, id->texto) == 0) { tiene_funcion = 1; break; }
    if (!ambito || !tiene_funcion) return 1;
    /* El generador base solo tiene instrucciones de palabra; no traducir
     * silenciosamente booleanos o caracteres ubicados en un byte. */
    for (const SimSimbolo *s = tabla->primero; s; s = s->siguiente_orden)
        if (pertenece(s->ambito, ambito) &&
            s->almacen != SIM_ALMACEN_PARAMETRO &&
            s->almacen != SIM_ALMACEN_LOCAL) return 1;
    GdContexto c = {tabla, ambito, destino, contexto_destinos, bytes_marco};
    return gf_generar_funcion_arreglos(funcion, bytes_marco, referencia, arreglo,
                                      resolver_destino, &c, salida, diagnosticos);
}
