/* Etapa 2N-C: carga y validacion recursiva de modulos del mismo proyecto,
 * y enlace: sem_visitar_proyecto une el programa y sus modulos en un solo AST.
 * Las expresiones modulo.miembro las valida semantic.c (etapa 2N-D).
 * Compilar junto con parser, lexer, ast, semantic y symbol_table.
 */
#include "semantic_modules.h"
#include "semantic.h"
#include "sintactico.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { MOD_CARGANDO, MOD_VALIDO, MOD_INVALIDO } EstadoModulo;

typedef struct Modulo {
    char *ruta;
    Ast arbol;
    SimTabla tabla;
    EstadoModulo estado;
    struct Modulo *siguiente;
} Modulo;

typedef struct {
    Modulo *modulos;
    FILE *diagnosticos;
    size_t profundidad;
} Cargador;

static const AstNodo *hijo(const AstNodo *n, unsigned i)
{
    const AstNodo *p = n ? n->primer_hijo : NULL;
    while (p && i--) p = p->siguiente;
    return p;
}

static char *duplicar(const char *s)
{
    size_t n = strlen(s);
    if (n == SIZE_MAX) return NULL;
    char *r = malloc(n + 1);
    if (r) memcpy(r, s, n + 1);
    return r;
}

/* Normalizacion lexica de rutas: elimina //, /./ y resuelve /../.
 * No consulta el sistema de archivos ni sigue enlaces simbolicos.
 * Devuelve una cadena reservada con malloc (liberar con free).
 */
static char *normalizar_ruta(const char *ruta)
{
    if (!ruta || !*ruta) return NULL;
    size_t n = strlen(ruta);
    if (n == SIZE_MAX) return NULL;
    char *salida = malloc(n + 2);
    if (!salida) return NULL;

    int absoluta = ruta[0] == '/';
    size_t longitud = 0;
    if (absoluta) salida[longitud++] = '/';

    for (size_t i = 0; i < n;) {
        while (i < n && ruta[i] == '/') ++i;
        size_t inicio = i;
        while (i < n && ruta[i] != '/') ++i;
        size_t largo = i - inicio;
        if (largo == 0 || (largo == 1 && ruta[inicio] == '.'))
            continue;

        if (largo == 2 && ruta[inicio] == '.' && ruta[inicio + 1] == '.') {
            size_t segmento = longitud;
            while (segmento > 0 && salida[segmento - 1] != '/')
                --segmento;
            int anterior_es_padre = longitud >= segmento + 2 &&
                longitud - segmento == 2 &&
                salida[segmento] == '.' && salida[segmento + 1] == '.';
            if (longitud > (size_t)absoluta && !anterior_es_padre) {
                longitud = segmento;
                if (longitud > (size_t)absoluta && salida[longitud - 1] == '/')
                    --longitud;
                continue;
            }
            if (absoluta) continue;
        }

        if (longitud && salida[longitud - 1] != '/')
            salida[longitud++] = '/';
        memcpy(salida + longitud, ruta + inicio, largo);
        longitud += largo;
    }
    if (!longitud) salida[longitud++] = '.';
    salida[longitud] = '\0';
    return salida;
}

/* El AST no requiere que el buffer fuente sea terminado con NUL. */
static int leer(const char *ruta, char **contenido, size_t *tamano)
{
    *contenido = NULL;
    *tamano = 0;
    FILE *f = fopen(ruta, "rb");
    if (!f) return 1;
    size_t cap = 4096, n = 0;
    char *datos = malloc(cap);
    if (!datos) { fclose(f); return 2; }
    for (;;) {
        if (n == cap) {
            if (cap > SIZE_MAX / 2) { free(datos); fclose(f); return 2; }
            size_t nueva = cap * 2;
            char *p = realloc(datos, nueva);
            if (!p) { free(datos); fclose(f); return 2; }
            datos = p;
            cap = nueva;
        }
        size_t leidos = fread(datos + n, 1, cap - n, f);
        n += leidos;
        if (leidos == 0) {
            if (ferror(f)) { free(datos); fclose(f); return 1; }
            break;
        }
    }
    fclose(f);
    *contenido = datos;
    *tamano = n;
    return 0;
}

static Modulo *buscar(Cargador *c, const char *ruta)
{
    for (Modulo *m = c->modulos; m; m = m->siguiente)
        if (strcmp(m->ruta, ruta) == 0) return m;
    return NULL;
}

/* Solo permite resolver alias declarados directamente por el importador. */
static int resolver_importacion(void *contexto, const char *archivo,
                               const char *alias, const Ast **arbol,
                               const SimTabla **tabla)
{
    Cargador *c = contexto;
    Modulo *origen = buscar(c, archivo);
    if (!origen || !origen->arbol.raiz) return 0;
    for (const AstNodo *n = origen->arbol.raiz->primer_hijo;
         n; n = n->siguiente) {
        if (n->tipo != AST_IMPORTACION) continue;
        const AstNodo *nombre = hijo(n, 0);
        const AstNodo *renombre = hijo(n, 1);
        const char *visible = renombre ? renombre->texto :
                              nombre ? nombre->texto : NULL;
        if (!visible || strcmp(visible, alias) != 0 ||
            !nombre || !nombre->texto) continue;
        char *ruta = sem_ruta_modulo(archivo, nombre->texto);
        if (!ruta) return 0;
        char *normalizada = normalizar_ruta(ruta);
        free(ruta);
        if (!normalizada) return 0;
        Modulo *remoto = buscar(c, normalizada);
        free(normalizada);
        if (!remoto || remoto->estado != MOD_VALIDO) return 0;
        *arbol = &remoto->arbol;
        *tabla = &remoto->tabla;
        return 1;
    }
    return 0;
}

static void liberar(Cargador *c)
{
    Modulo *m = c->modulos;
    while (m) {
        Modulo *prox = m->siguiente;
        sim_liberar(&m->tabla);
        ast_liberar(&m->arbol);
        free(m->ruta);
        free(m);
        m = prox;
    }
}

static int cargar(Cargador *c, const char *ruta, SemModo modo,
                  const char *importador, const AstNodo *importacion);

static int cargar_normalizado(Cargador *c, const char *ruta, SemModo modo,
                             const char *importador, const AstNodo *importacion)
{
    Modulo *existente = buscar(c, ruta);
    if (existente) {
        if (existente->estado == MOD_CARGANDO) {
            fprintf(c->diagnosticos, "%s:%zu:%zu: error semantico: "
                    "importacion circular detectada hacia '%s'\n",
                    importador ? importador : ruta,
                    importacion ? importacion->ubicacion.first_line : 0,
                    importacion ? importacion->ubicacion.first_column : 0, ruta);
            return 1;
        }
        return existente->estado == MOD_VALIDO ? 0 : 1;
    }
    if (c->profundidad >= 128) {
        fprintf(c->diagnosticos, "%s: error semantico: demasiados niveles de importacion\n", ruta);
        return 1;
    }
    Modulo *m = calloc(1, sizeof(*m));
    if (!m) return 2;
    m->ruta = duplicar(ruta);
    if (!m->ruta) { free(m); return 2; }
    m->estado = MOD_CARGANDO;
    m->siguiente = c->modulos;
    c->modulos = m;
    ++c->profundidad;

    int resultado = 0;
    char *fuente = NULL;
    size_t longitud = 0;
    int r = leer(ruta, &fuente, &longitud);
    if (r != 0) {
        if (r == 1) {
            if (importacion) {
                fprintf(c->diagnosticos, "%s:%zu:%zu: error semantico: "
                        "no se pudo abrir modulo '%s'\n",
                        importador ? importador : ruta,
                        importacion->ubicacion.first_line,
                        importacion->ubicacion.first_column, ruta);
            } else {
                fprintf(c->diagnosticos, "%s: error: "
                        "no se pudo abrir el archivo principal\n", ruta);
            }
        }
        resultado = r;
        goto finalizar;
    }
    resultado = construir_ast(fuente, longitud, ruta, c->diagnosticos, &m->arbol);
    free(fuente);
    if (resultado != 0) goto finalizar;

    /* Registrar antes de visitar hijos permite detectar A -> B -> A. */
    for (const AstNodo *n = m->arbol.raiz->primer_hijo; n; n = n->siguiente) {
        if (n->tipo != AST_IMPORTACION) continue;
        const AstNodo *nombre = hijo(n, 0);
        if (!nombre || !nombre->texto) { resultado = 1; continue; }
        char *subruta = sem_ruta_modulo(ruta, nombre->texto);
        if (!subruta) { resultado = 2; break; }
        int sub = cargar(c, subruta, SEM_MODULO_IMPORTADO, ruta, n);
        free(subruta);
        if (sub == 2) { resultado = 2; break; }
        if (sub != 0) resultado = 1;
    }
    if (resultado == 2) goto finalizar;
    if (sim_inicializar(&m->tabla) != SIM_OK) { resultado = 2; goto finalizar; }
    r = analizar_semantica_con_modulos(&m->arbol, &m->tabla, ruta,
                                        c->diagnosticos, modo,
                                        resolver_importacion, c);
    if (r == 2) resultado = 2;
    else if (r != 0) resultado = 1;
    if (!sim_verificar_pre_memoria(&m->tabla, c->diagnosticos)) resultado = 2;
finalizar:
    --c->profundidad;
    m->estado = resultado == 0 ? MOD_VALIDO : MOD_INVALIDO;
    return resultado;
}

/* Canoniza tambien el archivo principal, antes de entrar en la cache. */
static int cargar(Cargador *c, const char *ruta, SemModo modo,
                  const char *importador, const AstNodo *importacion)
{
    char *normalizada = normalizar_ruta(ruta);
    if (!normalizada) return 2;
    int resultado = cargar_normalizado(c, normalizada, modo,
                                      importador, importacion);
    free(normalizada);
    return resultado;
}

/* =====================================================================
 * Enlace entre archivos: fusion de modulos en un solo AST
 * =====================================================================
 * El backend (IR, ensamblador, O2, IDE) trabaja sobre UN programa. Para
 * que `bring` genere codigo, despues de validar todos los modulos se arma
 * un AST nuevo que contiene:
 *   1. las funciones y constantes de cada modulo importado (directa o
 *      indirectamente), con su nombre prefijado: `sumar` de
 *      matematicas.bal pasa a ser `matematicas__sumar`;
 *   2. el programa principal sin sus `bring`.
 * Las referencias se reescriben: `math.sumar(2, 3)` -> `matematicas__sumar(2, 3)`
 * y, dentro del modulo, `sumar(...)` -> `matematicas__sumar(...)`. Las
 * variables locales y parametros que tapan una constante del modulo no se
 * renombran. Las constantes enteras o booleanas con valor literal se
 * reemplazan por su valor (la IR aun no admite declaraciones globales).
 * El AST fusionado vuelve a pasar por la semantica para construir su
 * tabla de simbolos y se entrega al visitante como un programa normal.
 * Un programa sin `bring` no cambia: se entrega su AST original. */

typedef struct {
    Modulo **v;
    size_t n, cap;
} ListaModulos;

typedef struct {
    Cargador *c;
    Ast *destino;
    const Modulo *mod;          /* Modulo que se esta copiando */
    int es_principal;           /* El principal conserva sus nombres */
    const char **locales;       /* Nombres locales visibles (pila) */
    size_t nloc, caploc;
    int error;                  /* 1: falta de memoria */
} Copia;

/* Modulo que `origen` importa con el nombre `nombre` (o NULL). */
static Modulo *modulo_importado(Cargador *c, const Modulo *origen, const char *nombre)
{
    char *ruta = sem_ruta_modulo(origen->ruta, nombre);
    if (!ruta) return NULL;
    char *normalizada = normalizar_ruta(ruta);
    free(ruta);
    if (!normalizada) return NULL;
    Modulo *m = buscar(c, normalizada);
    free(normalizada);
    return m;
}

/* Recorrido en profundidad: cada modulo queda despues de los que importa,
 * y una sola vez aunque se importe desde varios lugares o con varios alias. */
static int ordenar_modulos(Cargador *c, Modulo *m, ListaModulos *l, int es_principal)
{
    for (size_t i = 0; i < l->n; ++i)
        if (l->v[i] == m) return 0;
    for (const AstNodo *n = m->arbol.raiz->primer_hijo; n; n = n->siguiente) {
        if (n->tipo != AST_IMPORTACION) continue;
        const AstNodo *nombre = hijo(n, 0);
        Modulo *s = nombre && nombre->texto ? modulo_importado(c, m, nombre->texto) : NULL;
        if (!s || s->estado != MOD_VALIDO) return 2;
        int r = ordenar_modulos(c, s, l, 0);
        if (r) return r;
    }
    if (es_principal) return 0;
    if (l->n == l->cap) {
        size_t cap = l->cap ? l->cap * 2 : 8;
        Modulo **v = realloc(l->v, cap * sizeof *v);
        if (!v) return 2;
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = m;
    return 0;
}

/* Prefijo de un modulo: el nombre de su archivo sin ".bal". */
static int prefijo_de(const Modulo *m, char *buf, size_t tam)
{
    const char *base = strrchr(m->ruta, '/');
    base = base ? base + 1 : m->ruta;
    size_t n = strlen(base);
    if (n > 4 && strcmp(base + n - 4, ".bal") == 0) n -= 4;
    if (n == 0 || n >= tam) return 0;
    memcpy(buf, base, n);
    buf[n] = '\0';
    return 1;
}

/* Nombre declarado por un nodo del nivel superior (funcion o constante). */
static const char *nombre_global(const AstNodo *n)
{
    if (n->tipo != AST_FUNCION && n->tipo != AST_CONSTANTE && n->tipo != AST_DECLARACION)
        return NULL;
    const AstNodo *id = hijo(n, 0);
    return id && id->tipo == AST_IDENTIFICADOR ? id->texto : NULL;
}

/* Declaracion global de `nombre` en el modulo `m` (o NULL). */
static const AstNodo *global_de(const Modulo *m, const char *nombre)
{
    if (!nombre) return NULL;
    for (const AstNodo *n = m->arbol.raiz->primer_hijo; n; n = n->siguiente) {
        if (n->tipo == AST_DECLARACIONES) {
            for (const AstNodo *d = n->primer_hijo; d; d = d->siguiente) {
                const char *s = nombre_global(d);
                if (s && strcmp(s, nombre) == 0) return d;
            }
            continue;
        }
        const char *s = nombre_global(n);
        if (s && strcmp(s, nombre) == 0) return n;
    }
    return NULL;
}

/* Constante que se puede reemplazar por su valor: entera (con o sin signo)
 * o booleana, con un literal como valor. */
static int constante_en_linea(const AstNodo *k)
{
    if (!k || k->tipo != AST_CONSTANTE) return 0;
    const AstNodo *tipo = hijo(k, 1), *valor = hijo(k, 2);
    if (!tipo || !tipo->texto || !valor || hijo(k, 3)) return 0;
    if (strcmp(tipo->texto, "declare_int") == 0) {
        if (valor->tipo == AST_ENTERO) return 1;
        return valor->tipo == AST_UNARIO && valor->texto && strcmp(valor->texto, "neumann") == 0 &&
               hijo(valor, 0) && hijo(valor, 0)->tipo == AST_ENTERO && !hijo(valor, 1);
    }
    return strcmp(tipo->texto, "declare_boolean") == 0 && valor->tipo == AST_BOOLEANO;
}

static int local_agregar(Copia *x, const char *nombre)
{
    if (!nombre) return 1;
    if (x->nloc == x->caploc) {
        size_t cap = x->caploc ? x->caploc * 2 : 16;
        const char **v = realloc(x->locales, cap * sizeof *v);
        if (!v) { x->error = 1; return 0; }
        x->locales = v;
        x->caploc = cap;
    }
    x->locales[x->nloc++] = nombre;
    return 1;
}

static int sombreado(const Copia *x, const char *nombre)
{
    for (size_t i = x->nloc; i-- > 0;)
        if (strcmp(x->locales[i], nombre) == 0) return 1;
    return 0;
}

/* Modulo al que apunta `alias` en el archivo que se esta copiando. */
static const Modulo *modulo_de_alias(Copia *x, const char *alias)
{
    for (const AstNodo *n = x->mod->arbol.raiz->primer_hijo; n; n = n->siguiente) {
        if (n->tipo != AST_IMPORTACION) continue;
        const AstNodo *nombre = hijo(n, 0), *renombre = hijo(n, 1);
        const char *visible = renombre ? renombre->texto : nombre ? nombre->texto : NULL;
        if (visible && nombre && nombre->texto && strcmp(visible, alias) == 0)
            return modulo_importado(x->c, x->mod, nombre->texto);
    }
    return NULL;
}

/* Nodo nuevo con el tipo y la ubicacion indicados. Lo que viene de un
 * modulo queda en la linea 0 ("otro archivo"): las herramientas que usan la
 * linea para editar el texto del principal (el DCE de "Aplicar al codigo" y
 * -O1) no deben confundir la linea 5 del modulo con la linea 5 del principal. */
static AstNodo *nuevo(Copia *x, AstTipo tipo, const char *texto, AstUbicacion u)
{
    if (!x->es_principal) u = (AstUbicacion){0, 0, 0, 0};
    AstNodo *r = ast_crear(x->destino, tipo, texto, u);
    if (!r) x->error = 1;
    return r;
}

/* Copia exacta (sin renombrar nada). */
static AstNodo *copiar_igual(Copia *x, const AstNodo *n, AstUbicacion u)
{
    AstNodo *r = nuevo(x, n->tipo, n->texto, u);
    for (const AstNodo *h = n->primer_hijo; r && h; h = h->siguiente) {
        AstNodo *k = copiar_igual(x, h, u);
        if (!k) return NULL;
        ast_agregar_hijo(r, k);
    }
    return r;
}

/* Nombre con prefijo: matematicas__sumar. */
static AstNodo *nombre_enlazado(Copia *x, const Modulo *dueno, const char *nombre, AstUbicacion u)
{
    char prefijo[256], texto[600];
    if (!prefijo_de(dueno, prefijo, sizeof prefijo) ||
        (size_t)snprintf(texto, sizeof texto, "%s__%s", prefijo, nombre) >= sizeof texto) {
        x->error = 1;
        return NULL;
    }
    return nuevo(x, AST_IDENTIFICADOR, texto, u);
}

/* Referencia a un global del modulo `dueno`: su valor si es una constante
 * literal, o su nombre con prefijo. */
static AstNodo *referencia(Copia *x, const Modulo *dueno, const AstNodo *global,
                           const char *nombre, AstUbicacion u)
{
    if (constante_en_linea(global)) return copiar_igual(x, hijo(global, 2), u);
    return nombre_enlazado(x, dueno, nombre, u);
}

static AstNodo *copiar(Copia *x, const AstNodo *n, int en_funcion);

/* Copia los hijos de `n` desde el indice `desde`. */
static int copiar_hijos(Copia *x, AstNodo *destino, const AstNodo *n, unsigned desde, int en_funcion)
{
    unsigned i = 0;
    for (const AstNodo *h = n->primer_hijo; h; h = h->siguiente, ++i) {
        if (i < desde) continue;
        AstNodo *k = copiar(x, h, en_funcion);
        if (!k) return 0;
        ast_agregar_hijo(destino, k);
    }
    return 1;
}

/* Nombre que se declara (variable, parametro, indice): se copia tal cual
 * dentro de una funcion; en el nivel superior de un modulo lleva prefijo. */
static AstNodo *nombre_declarado(Copia *x, const AstNodo *id, int en_funcion)
{
    if (!id) return NULL;
    if (!en_funcion && !x->es_principal && id->tipo == AST_IDENTIFICADOR)
        return nombre_enlazado(x, x->mod, id->texto, id->ubicacion);
    return copiar_igual(x, id, id->ubicacion);
}

static AstNodo *copiar(Copia *x, const AstNodo *n, int en_funcion)
{
    if (!n || x->error) return NULL;
    size_t marca = x->nloc;
    AstNodo *r = NULL;
    switch (n->tipo) {
    case AST_ACCESO_MIEMBRO: {             /* alias.miembro */
        const AstNodo *a = hijo(n, 0), *m = hijo(n, 1);
        if (a && m && !hijo(n, 2) && a->tipo == AST_IDENTIFICADOR && m->tipo == AST_IDENTIFICADOR) {
            const Modulo *dueno = modulo_de_alias(x, a->texto);
            const AstNodo *g = dueno ? global_de(dueno, m->texto) : NULL;
            if (g) return referencia(x, dueno, g, m->texto, n->ubicacion);
        }
        break;
    }
    case AST_IDENTIFICADOR:                /* Uso de un nombre */
        if (!x->es_principal && !sombreado(x, n->texto)) {
            const AstNodo *g = global_de(x->mod, n->texto);
            if (g) return referencia(x, x->mod, g, n->texto, n->ubicacion);
        }
        break;
    case AST_LLAMADA: {                    /* f(...) dentro de un modulo */
        const AstNodo *f = hijo(n, 0);
        if (!x->es_principal && f && f->tipo == AST_IDENTIFICADOR) {
            const AstNodo *g = global_de(x->mod, f->texto);
            if (g && g->tipo == AST_FUNCION) {
                r = nuevo(x, n->tipo, n->texto, n->ubicacion);
                AstNodo *k = r ? nombre_enlazado(x, x->mod, f->texto, f->ubicacion) : NULL;
                if (!k) return NULL;
                ast_agregar_hijo(r, k);
                return copiar_hijos(x, r, n, 1, en_funcion) ? r : NULL;
            }
        }
        break;
    }
    case AST_FUNCION: {
        r = nuevo(x, n->tipo, n->texto, n->ubicacion);
        AstNodo *k = r ? nombre_declarado(x, hijo(n, 0), 0) : NULL;
        if (!k) return NULL;
        ast_agregar_hijo(r, k);
        int ok = copiar_hijos(x, r, n, 1, 1);
        x->nloc = marca;                   /* Parametros y locales salen de ambito */
        return ok ? r : NULL;
    }
    case AST_DECLARACION:
    case AST_CONSTANTE:
    case AST_PARAMETRO: {
        /* El nombre se declara DESPUES de copiar el valor inicial: en
         * `x * declare_int : x` el segundo x es el de afuera. */
        r = nuevo(x, n->tipo, n->texto, n->ubicacion);
        AstNodo *k = r ? nombre_declarado(x, hijo(n, 0), en_funcion) : NULL;
        if (!k) return NULL;
        ast_agregar_hijo(r, k);
        if (!copiar_hijos(x, r, n, 1, en_funcion)) return NULL;
        if (en_funcion && hijo(n, 0)) local_agregar(x, hijo(n, 0)->texto);
        return x->error ? NULL : r;
    }
    case AST_RECORRIDO: {                  /* cycle i let a until b step c { ... } */
        const AstNodo *indice = hijo(n, 0);
        r = nuevo(x, n->tipo, n->texto, n->ubicacion);
        AstNodo *k = r && indice ? copiar_igual(x, indice, indice->ubicacion) : NULL;
        if (!k) { x->error = 1; return NULL; }
        ast_agregar_hijo(r, k);
        for (const AstNodo *h = indice->siguiente; h; h = h->siguiente) {
            if (h->tipo == AST_BLOQUE) local_agregar(x, indice->texto);
            AstNodo *c = copiar(x, h, en_funcion);
            if (!c) return NULL;
            ast_agregar_hijo(r, c);
        }
        x->nloc = marca;
        return r;
    }
    case AST_CAPTURA: {                    /* seize (Tipo e) { ... } */
        r = nuevo(x, n->tipo, n->texto, n->ubicacion);
        for (unsigned i = 0; r && i < 2 && hijo(n, i); ++i) {
            AstNodo *k = copiar_igual(x, hijo(n, i), hijo(n, i)->ubicacion);
            if (!k) return NULL;
            ast_agregar_hijo(r, k);
        }
        if (!r || (hijo(n, 1) && !local_agregar(x, hijo(n, 1)->texto))) return NULL;
        int ok = copiar_hijos(x, r, n, 2, en_funcion);
        x->nloc = marca;
        return ok ? r : NULL;
    }
    case AST_BLOQUE: {
        r = nuevo(x, n->tipo, n->texto, n->ubicacion);
        int ok = r && copiar_hijos(x, r, n, 0, en_funcion);
        x->nloc = marca;
        return ok ? r : NULL;
    }
    default:
        break;
    }
    r = nuevo(x, n->tipo, n->texto, n->ubicacion);
    if (!r || !copiar_hijos(x, r, n, 0, en_funcion)) return NULL;
    return r;
}

/* Copia el nivel superior de un archivo dentro de la raiz fusionada. */
static int copiar_archivo(Copia *x, AstNodo *raiz)
{
    for (const AstNodo *n = x->mod->arbol.raiz->primer_hijo; n; n = n->siguiente) {
        if (n->tipo == AST_IMPORTACION) continue;                /* Ya esta enlazado */
        if (!x->es_principal && constante_en_linea(n)) continue; /* Se usa su valor */
        x->nloc = 0;
        AstNodo *k = copiar(x, n, 0);
        if (!k) return 0;
        ast_agregar_hijo(raiz, k);
    }
    return 1;
}

static int tiene_importaciones(const Modulo *m)
{
    for (const AstNodo *n = m->arbol.raiz->primer_hijo; n; n = n->siguiente)
        if (n->tipo == AST_IMPORTACION) return 1;
    return 0;
}

/* Arma en `fusion` el programa completo (modulos + principal). 0 o 2. */
static int fusionar(Cargador *c, Modulo *principal, Ast *fusion)
{
    ListaModulos lista = {0};
    int r = ordenar_modulos(c, principal, &lista, 1);
    if (r) { free(lista.v); return r; }
    const AstNodo *raiz_original = principal->arbol.raiz;
    AstNodo *raiz = ast_crear(fusion, AST_PROGRAMA, raiz_original->texto, raiz_original->ubicacion);
    Copia x = {c, fusion, NULL, 0, NULL, 0, 0, raiz == NULL};
    for (size_t i = 0; !x.error && i < lista.n; ++i) {
        x.mod = lista.v[i];
        x.es_principal = 0;
        if (!copiar_archivo(&x, raiz)) x.error = 1;
    }
    if (!x.error) {
        x.mod = principal;
        x.es_principal = 1;
        if (!copiar_archivo(&x, raiz)) x.error = 1;
    }
    free(x.locales);
    free(lista.v);
    if (x.error) return 2;
    fusion->raiz = raiz;
    return 0;
}

/* =====================================================================
 * Plegado de constantes
 * =====================================================================
 * Los backends tienen soporte parcial de `declare_const`: la IR y -O1 no
 * aceptan constantes globales, el backend normal no acepta constantes
 * booleanas ni arreglos globales ni constantes locales, y nadie acepta una
 * constante como dimension (`a * declare_int[N]`). Como una constante no
 * cambia, se puede resolver ANTES del backend, sobre una copia del AST:
 *   1. Una constante declare_int o declare_boolean cuyo valor se calcula en
 *      compilacion (literales, otras constantes y operadores) se reemplaza
 *      por su valor en cada uso. Se respetan los parametros y variables
 *      locales que la tapan. Las constantes declare_int GLOBALES siguen
 *      declaradas, con su valor ya calculado: el binario las guarda en su
 *      seccion de datos (formato_binario.c) y la IR las ignora si ya nadie
 *      las usa. Las demas constantes plegadas desaparecen.
 *   2. Un arreglo constante global con valor literal se declara como arreglo
 *      normal al inicio de cada funcion que lo usa (nadie lo puede
 *      modificar: la semantica ya lo verifico).
 *   3. Una constante local que no se puede calcular (depende de variables o
 *      llamadas), o un arreglo constante local, pasa a ser una declaracion
 *      normal.
 * La aritmetica es la misma de la seccion de datos del binario
 * (fb_preparar_datos): 32 bits con desborde circular, division y residuo
 * con signo truncando hacia cero, potencia con exponente no negativo. No se
 * pliega una division por cero. El resultado vuelve a pasar por la
 * semantica; si fallara, se usa el AST sin plegar, como antes. */

typedef enum { PL_SOMBRA, PL_VALOR, PL_ARREGLO } PlClase;

typedef struct {
    const char *nombre;
    PlClase clase;
    int64_t valor;               /* PL_VALOR */
    int es_bool;                 /* PL_VALOR */
    const AstNodo *declaracion;  /* PL_ARREGLO: la constante original */
} PlEntrada;

typedef struct {
    Ast *destino;
    PlEntrada *v;                /* Pila de nombres visibles */
    size_t n, cap;
    size_t globales;             /* v[0..globales) son las constantes globales */
    int error;
} Plegado;

static AstNodo OMITIR_NODO;      /* Marca: la sentencia desaparece */
#define OMITIR (&OMITIR_NODO)

static int pl_agregar(Plegado *p, PlEntrada e)
{
    if (!e.nombre) return 1;
    if (p->n == p->cap) {
        size_t cap = p->cap ? p->cap * 2 : 32;
        PlEntrada *v = realloc(p->v, cap * sizeof *v);
        if (!v) { p->error = 1; return 0; }
        p->v = v;
        p->cap = cap;
    }
    p->v[p->n++] = e;
    return 1;
}

static int pl_sombra(Plegado *p, const char *nombre)
{
    return pl_agregar(p, (PlEntrada){nombre, PL_SOMBRA, 0, 0, NULL});
}

static const PlEntrada *pl_buscar(const Plegado *p, const char *nombre)
{
    if (!nombre) return NULL;
    for (size_t i = p->n; i-- > 0;)
        if (strcmp(p->v[i].nombre, nombre) == 0) return &p->v[i];
    return NULL;
}

/* Tipo escalar de una constante: 0 int, 1 boolean, -1 otro. */
static int pl_tipo_escalar(const AstNodo *k)
{
    const AstNodo *t = hijo(k, 1);
    if (!t || t->tipo != AST_TIPO || !t->texto) return -1;
    if (strcmp(t->texto, "declare_int") == 0) return 0;
    if (strcmp(t->texto, "declare_boolean") == 0) return 1;
    return -1;
}

/* Valor con signo de 32 bits (como int32_t) a partir de su patron de bits. */
static int64_t pl_firmado(uint32_t v)
{
    return v <= INT32_MAX ? (int64_t)v : (int64_t)v - INT64_C(4294967296);
}

static int pl_evaluar(const Plegado *p, const AstNodo *e, int64_t *v, int *es_bool, unsigned prof);

/* a[i][j] de un arreglo constante con valor literal e indices constantes. */
static int pl_elemento(const Plegado *p, const AstNodo *e, int64_t *v, int *es_bool, unsigned prof)
{
    const AstNodo *indices[8];
    unsigned cantidad = 0;
    const AstNodo *base = e;
    for (; base && base->tipo == AST_INDICE && hijo(base, 1) && !hijo(base, 2);
         base = base->primer_hijo) {
        if (cantidad == 8) return 0;
        indices[cantidad++] = hijo(base, 1);
    }
    if (!base || base->tipo != AST_IDENTIFICADOR) return 0;
    const PlEntrada *a = pl_buscar(p, base->texto);
    if (!a || a->clase != PL_ARREGLO) return 0;
    const AstNodo *lit = hijo(a->declaracion, 2);
    for (unsigned k = cantidad; k-- > 0;) {      /* Del primer indice al ultimo */
        int64_t i;
        int b;
        if (!lit || lit->tipo != AST_LITERAL_COLECCION ||
            !pl_evaluar(p, indices[k], &i, &b, prof + 1) || b || i < 0) return 0;
        lit = hijo(lit, (unsigned)i);
    }
    return lit && lit->tipo != AST_LITERAL_COLECCION && pl_evaluar(p, lit, v, es_bool, prof + 1);
}

/* Calcula `e` en compilacion. 1 si se pudo (valor en *v, *es_bool). */
static int pl_evaluar(const Plegado *p, const AstNodo *e, int64_t *v, int *es_bool, unsigned prof)
{
    if (!e || prof > 200) return 0;
    switch (e->tipo) {
    case AST_ENTERO: {                  /* Hasta 4294967295, como la seccion de datos */
        if (!e->texto || !*e->texto || strlen(e->texto) > 10) return 0;
        int64_t r = 0;
        for (const char *s = e->texto; *s; ++s) {
            if (*s < '0' || *s > '9') return 0;
            r = r * 10 + (*s - '0');
        }
        if (r > (int64_t)UINT32_MAX) return 0;
        *v = pl_firmado((uint32_t)r); *es_bool = 0;
        return 1;
    }
    case AST_BOOLEANO:
        if (!e->texto) return 0;
        if (strcmp(e->texto, "declare_true") == 0) *v = 1;
        else if (strcmp(e->texto, "declare_false") == 0) *v = 0;
        else return 0;
        *es_bool = 1;
        return 1;
    case AST_IDENTIFICADOR: {
        const PlEntrada *k = pl_buscar(p, e->texto);
        if (!k || k->clase != PL_VALOR) return 0;
        *v = k->valor; *es_bool = k->es_bool;
        return 1;
    }
    case AST_INDICE:
        return pl_elemento(p, e, v, es_bool, prof);
    case AST_UNARIO: {
        int64_t a;
        int b;
        if (!e->texto || hijo(e, 1) || !pl_evaluar(p, hijo(e, 0), &a, &b, prof + 1)) return 0;
        if (!strcmp(e->texto, "neumann") && !b) {
            *v = pl_firmado(0u - (uint32_t)a); *es_bool = 0;
            return 1;
        }
        if (!strcmp(e->texto, "~") && b) { *v = !a; *es_bool = 1; return 1; }
        return 0;
    }
    case AST_BINARIO: {
        int64_t a, c;
        int ba, bc;
        const char *op = e->texto;
        if (!op || hijo(e, 2) || !pl_evaluar(p, hijo(e, 0), &a, &ba, prof + 1) ||
            !pl_evaluar(p, hijo(e, 1), &c, &bc, prof + 1) || ba != bc) return 0;
        *es_bool = 0;
        if (!ba) {                                   /* Aritmetica y comparaciones */
            uint32_t x = (uint32_t)a, y = (uint32_t)c, r;
            if (!strcmp(op, "gauss")) r = x + y;
            else if (!strcmp(op, "neumann")) r = x - y;
            else if (!strcmp(op, "pitagoras")) r = x * y;
            else if (!strcmp(op, "euclides") || !strcmp(op, "euler")) {
                if (c == 0) return 0;              /* Division por cero: no se pliega */
                r = (uint32_t)(!strcmp(op, "euclides") ? a / c : a % c);
            } else if (!strcmp(op, "descartes")) {
                if (c < 0) return 0;
                r = 1;
                while (y) {                        /* Potencia rapida, 32 bits */
                    if (y & 1u) r *= x;
                    x *= x;
                    y >>= 1;
                }
            } else {
                *es_bool = 1;
                if (!strcmp(op, "==")) *v = a == c;
                else if (!strcmp(op, "=/=")) *v = a != c;
                else if (!strcmp(op, "<")) *v = a < c;
                else if (!strcmp(op, ">")) *v = a > c;
                else if (!strcmp(op, "<=")) *v = a <= c;
                else if (!strcmp(op, ">=")) *v = a >= c;
                else return 0;
                return 1;
            }
            *v = pl_firmado(r);
            return 1;
        }
        *es_bool = 1;                                /* Logicos */
        if (!strcmp(op, "&&")) *v = a && c;
        else if (!strcmp(op, "||")) *v = a || c;
        else if (!strcmp(op, "^")) *v = (a != 0) != (c != 0);
        else if (!strcmp(op, "==")) *v = a == c;
        else if (!strcmp(op, "=/=")) *v = a != c;
        else return 0;
        return 1;
    }
    default:
        return 0;
    }
}

/* Literal con el valor: Entero, neumann Entero o Booleano. */
static AstNodo *pl_literal(Plegado *p, int64_t v, int es_bool, AstUbicacion u)
{
    AstNodo *r;
    if (es_bool) {
        r = ast_crear(p->destino, AST_BOOLEANO, v ? "declare_true" : "declare_false", u);
    } else {
        char texto[24];
        snprintf(texto, sizeof texto, "%lld", (long long)(v < 0 ? -v : v));
        r = ast_crear(p->destino, AST_ENTERO, texto, u);
        if (r && v < 0) {
            AstNodo *menos = ast_crear(p->destino, AST_UNARIO, "neumann", u);
            if (menos) ast_agregar_hijo(menos, r);
            r = menos;
        }
    }
    if (!r) p->error = 1;
    return r;
}

/* Copia exacta. */
static AstNodo *pl_igual(Plegado *p, const AstNodo *n)
{
    AstNodo *r = ast_crear(p->destino, n->tipo, n->texto, n->ubicacion);
    if (!r) { p->error = 1; return NULL; }
    for (const AstNodo *h = n->primer_hijo; h; h = h->siguiente) {
        AstNodo *k = pl_igual(p, h);
        if (!k) return NULL;
        ast_agregar_hijo(r, k);
    }
    return r;
}

static AstNodo *pl_copiar(Plegado *p, const AstNodo *n);

/* Copia los hijos desde `desde`, saltando los que desaparecen. */
static int pl_hijos(Plegado *p, AstNodo *destino, const AstNodo *n, unsigned desde)
{
    unsigned i = 0;
    for (const AstNodo *h = n->primer_hijo; h; h = h->siguiente, ++i) {
        if (i < desde) continue;
        AstNodo *k = pl_copiar(p, h);
        if (!k) return 0;
        if (k != OMITIR) ast_agregar_hijo(destino, k);
    }
    return 1;
}

/* Nodo con el mismo tipo; el primer hijo (un nombre que se declara) tal cual. */
static AstNodo *pl_con_nombre(Plegado *p, const AstNodo *n, AstTipo tipo)
{
    AstNodo *r = ast_crear(p->destino, tipo, n->texto, n->ubicacion);
    AstNodo *k = r && hijo(n, 0) ? pl_igual(p, hijo(n, 0)) : NULL;
    if (!r || (hijo(n, 0) && !k)) { p->error = 1; return NULL; }
    if (k) ast_agregar_hijo(r, k);
    return pl_hijos(p, r, n, 1) ? r : NULL;
}

/* 1 si el subarbol declara `nombre` (parametro, variable, constante, indice
 * de cycle o variable de seize). */
static int pl_declara(const AstNodo *n, const char *nombre)
{
    for (; n; n = n->siguiente) {
        if (n->tipo == AST_PARAMETRO || n->tipo == AST_DECLARACION || n->tipo == AST_CONSTANTE ||
            n->tipo == AST_RECORRIDO) {
            const AstNodo *id = hijo(n, 0);
            if (id && id->texto && strcmp(id->texto, nombre) == 0) return 1;
        }
        if (n->tipo == AST_CAPTURA) {
            const AstNodo *id = hijo(n, 1);
            if (id && id->texto && strcmp(id->texto, nombre) == 0) return 1;
        }
        if (pl_declara(n->primer_hijo, nombre)) return 1;
    }
    return 0;
}

/* Linea 0 ("codigo generado"): igual que el codigo copiado de un modulo, el
 * DCE que reescribe el texto (-O1, "Aplicar al codigo") no lo confunde con
 * la linea donde estaba la constante global. */
static void pl_sin_linea(AstNodo *n)
{
    for (; n; n = n->siguiente) {
        n->ubicacion = (AstUbicacion){0, 0, 0, 0};
        pl_sin_linea(n->primer_hijo);
    }
}

static int pl_usa(const AstNodo *n, const char *nombre)
{
    for (; n; n = n->siguiente) {
        if (n->tipo == AST_IDENTIFICADOR && n->texto && strcmp(n->texto, nombre) == 0) return 1;
        if (pl_usa(n->primer_hijo, nombre)) return 1;
    }
    return 0;
}

static AstNodo *pl_copiar(Plegado *p, const AstNodo *n)
{
    if (!n || p->error) return NULL;
    size_t marca = p->n;
    switch (n->tipo) {
    case AST_IDENTIFICADOR: {                       /* Uso de un nombre */
        const PlEntrada *k = pl_buscar(p, n->texto);
        if (k && k->clase == PL_VALOR) return pl_literal(p, k->valor, k->es_bool, n->ubicacion);
        return pl_igual(p, n);
    }
    case AST_LLAMADA:                               /* El nombre de la funcion no se toca */
    case AST_IMPORTACION:
    case AST_ACCESO_MIEMBRO:
        if (n->tipo != AST_LLAMADA) return pl_igual(p, n);
        return pl_con_nombre(p, n, AST_LLAMADA);
    case AST_DECLARACION:
    case AST_PARAMETRO: {
        AstNodo *r = pl_con_nombre(p, n, n->tipo);
        if (r && hijo(n, 0)) pl_sombra(p, hijo(n, 0)->texto);
        return p->error ? NULL : r;
    }
    case AST_CONSTANTE: {                           /* Constante local */
        const AstNodo *id = hijo(n, 0);
        int tipo = pl_tipo_escalar(n);
        int64_t v;
        int b;
        if (tipo >= 0 && id && !hijo(n, 3) && pl_evaluar(p, hijo(n, 2), &v, &b, 0) && b == tipo) {
            pl_agregar(p, (PlEntrada){id->texto, PL_VALOR, v, b, NULL});
            return p->error ? NULL : OMITIR;
        }
        AstNodo *r = pl_con_nombre(p, n, AST_DECLARACION);
        if (r && id) pl_sombra(p, id->texto);
        return p->error ? NULL : r;
    }
    case AST_RECORRIDO: {                           /* cycle i let a until b ... */
        const AstNodo *i = hijo(n, 0);
        AstNodo *r = ast_crear(p->destino, n->tipo, n->texto, n->ubicacion);
        AstNodo *k = r && i ? pl_igual(p, i) : NULL;
        if (!k) { p->error = 1; return NULL; }
        ast_agregar_hijo(r, k);
        for (const AstNodo *h = i->siguiente; h; h = h->siguiente) {
            if (h->tipo == AST_BLOQUE) pl_sombra(p, i->texto);
            AstNodo *c = pl_copiar(p, h);
            if (!c) return NULL;
            if (c != OMITIR) ast_agregar_hijo(r, c);
        }
        p->n = marca;
        return r;
    }
    case AST_CAPTURA: {                             /* seize (Tipo e) { ... } */
        AstNodo *r = ast_crear(p->destino, n->tipo, n->texto, n->ubicacion);
        if (!r) { p->error = 1; return NULL; }
        for (unsigned i = 0; i < 2 && hijo(n, i); ++i) {
            AstNodo *k = pl_igual(p, hijo(n, i));
            if (!k) return NULL;
            ast_agregar_hijo(r, k);
        }
        if (hijo(n, 1)) pl_sombra(p, hijo(n, 1)->texto);
        int ok = pl_hijos(p, r, n, 2);
        p->n = marca;
        return ok && !p->error ? r : NULL;
    }
    case AST_BLOQUE: {
        AstNodo *r = ast_crear(p->destino, n->tipo, n->texto, n->ubicacion);
        int ok = r && pl_hijos(p, r, n, 0);
        p->n = marca;
        if (!r) p->error = 1;
        return ok ? r : NULL;
    }
    case AST_FUNCION: {
        /* nombre, tipo, parametros y bloque. Los arreglos constantes globales
         * que usa se declaran al inicio de su bloque. */
        AstNodo *r = ast_crear(p->destino, n->tipo, n->texto, n->ubicacion);
        if (!r) { p->error = 1; return NULL; }
        for (const AstNodo *h = n->primer_hijo; h; h = h->siguiente) {
            AstNodo *k;
            if (h == n->primer_hijo || h->tipo == AST_TIPO) k = pl_igual(p, h);
            else if (h->tipo == AST_BLOQUE) {
                k = ast_crear(p->destino, AST_BLOQUE, h->texto, h->ubicacion);
                if (!k) { p->error = 1; return NULL; }
                for (size_t g = 0; g < p->globales; ++g) {
                    const PlEntrada *a = &p->v[g];
                    if (a->clase != PL_ARREGLO || !pl_usa(n->primer_hijo->siguiente, a->nombre) ||
                        pl_declara(n->primer_hijo->siguiente, a->nombre)) continue;
                    /* El valor del arreglo se lee en el ambito global: un
                     * parametro llamado igual que una constante del literal
                     * (T : [N, 2] con f(N)) no debe taparla. */
                    size_t visibles = p->n;
                    p->n = p->globales;
                    AstNodo *d = pl_con_nombre(p, a->declaracion, AST_DECLARACION);
                    p->n = visibles;
                    if (!d) return NULL;
                    pl_sin_linea(d);   /* No es una linea del texto de esta funcion */
                    ast_agregar_hijo(k, d);
                }
                size_t antes = p->n;
                if (!pl_hijos(p, k, h, 0)) return NULL;
                p->n = antes;
            } else k = pl_copiar(p, h);
            if (!k) return NULL;
            if (k != OMITIR) ast_agregar_hijo(r, k);
        }
        p->n = marca;
        return p->error ? NULL : r;
    }
    default: {
        AstNodo *r = ast_crear(p->destino, n->tipo, n->texto, n->ubicacion);
        if (!r) { p->error = 1; return NULL; }
        return pl_hijos(p, r, n, 0) ? r : NULL;
    }
    }
}

static int pl_tiene_constantes(const AstNodo *n)
{
    for (; n; n = n->siguiente)
        if (n->tipo == AST_CONSTANTE || pl_tiene_constantes(n->primer_hijo)) return 1;
    return 0;
}

/* Arma en `salida` el programa con las constantes plegadas. 0 bien, 2 sin memoria. */
static int plegar_constantes(const Ast *entrada, Ast *salida)
{
    Plegado p = {salida, NULL, 0, 0, 0, 0};
    const AstNodo *raiz = entrada->raiz;
    /* 1. Constantes globales, en orden (una puede usar las anteriores). */
    for (const AstNodo *n = raiz->primer_hijo; n; n = n->siguiente) {
        if (n->tipo != AST_CONSTANTE || !hijo(n, 0)) continue;
        const AstNodo *id = hijo(n, 0), *tipo = hijo(n, 1), *valor = hijo(n, 2);
        int escalar = pl_tipo_escalar(n);
        int64_t v;
        int b;
        if (escalar >= 0 && !hijo(n, 3) && pl_evaluar(&p, valor, &v, &b, 0) && b == escalar)
            pl_agregar(&p, (PlEntrada){id->texto, PL_VALOR, v, b, NULL});
        else if (tipo && tipo->tipo == AST_TIPO_ARREGLO && valor &&
                 valor->tipo == AST_LITERAL_COLECCION && !hijo(n, 3))
            pl_agregar(&p, (PlEntrada){id->texto, PL_ARREGLO, 0, 0, n});
        else
            pl_sombra(&p, id->texto);               /* Se queda como esta */
    }
    p.globales = p.n;
    /* 2. Copia: las constantes plegadas o convertidas desaparecen. */
    AstNodo *nueva = ast_crear(salida, AST_PROGRAMA, raiz->texto, raiz->ubicacion);
    if (!nueva) p.error = 1;
    for (const AstNodo *n = raiz->primer_hijo; !p.error && n; n = n->siguiente) {
        if (n->tipo == AST_CONSTANTE && hijo(n, 0)) {
            const PlEntrada *k = NULL;
            for (size_t g = 0; g < p.globales; ++g)
                if (strcmp(p.v[g].nombre, hijo(n, 0)->texto) == 0) k = &p.v[g];
            /* Desaparecen las booleanas plegadas y los arreglos (que se
             * declaran dentro de las funciones). Las declare_int globales,
             * siguen declaradas porque van a la seccion de datos del binario:
             * las plegadas con su valor (su expresion podria nombrar un
             * arreglo que ya no es global) y las demas tal cual, con sus
             * errores de siempre. */
            if (k && (k->clase == PL_ARREGLO || (k->clase == PL_VALOR && k->es_bool))) continue;
            AstNodo *r;
            if (k && k->clase == PL_VALOR) {
                const AstNodo *id = hijo(n, 0), *tipo = hijo(n, 1), *valor = hijo(n, 2);
                AstNodo *i, *t, *l;
                r = ast_crear(salida, n->tipo, n->texto, n->ubicacion);
                if (!r || !(i = pl_igual(&p, id)) || !(t = pl_igual(&p, tipo)) ||
                    !(l = pl_literal(&p, k->valor, 0, valor->ubicacion))) { p.error = 1; break; }
                ast_agregar_hijo(r, i);
                ast_agregar_hijo(r, t);
                ast_agregar_hijo(r, l);
            } else r = pl_igual(&p, n);
            if (!r) break;
            ast_agregar_hijo(nueva, r);
            continue;
        }
        p.n = p.globales;
        AstNodo *k = pl_copiar(&p, n);
        if (!k) break;
        if (k != OMITIR) ast_agregar_hijo(nueva, k);
    }
    free(p.v);
    if (p.error) return 2;
    salida->raiz = nueva;
    return 0;
}

/* Analisis semantico de un AST transformado (fusionado o plegado), con
 * tabla nueva. Con `silencioso`, los mensajes se descartan. 0, 1 o 2. */
static int analizar_transformado(Cargador *c, const Ast *arbol, const char *ruta,
                                 SimTabla *tabla, int silencioso)
{
    FILE *diag = silencioso ? tmpfile() : c->diagnosticos;
    if (!diag) return 2;
    int r = 2;
    if (sim_inicializar(tabla) == SIM_OK) {
        r = analizar_semantica_con_modulos(arbol, tabla, ruta, diag, SEM_PROGRAMA_PRINCIPAL,
                                           resolver_importacion, c);
        if (r != 0) r = r == 2 ? 2 : 1;
        else if (!sim_verificar_pre_memoria(tabla, diag)) r = 2;
        if (r != 0) sim_liberar(tabla);
    }
    if (silencioso) fclose(diag);
    return r;
}

int sem_visitar_proyecto(const char *archivo_principal, FILE *diagnosticos,
                        SemProyectoVisitante visitante, void *contexto)
{
    if (!archivo_principal || !*archivo_principal || !visitante) return 2;
    Cargador c = {0};
    c.diagnosticos = diagnosticos ? diagnosticos : stderr;
    int r = cargar(&c, archivo_principal, SEM_PROGRAMA_PRINCIPAL, NULL, NULL);
    if (r == 0) {
        char *normalizada = normalizar_ruta(archivo_principal);
        Modulo *principal = normalizada ? buscar(&c, normalizada) : NULL;
        free(normalizada);
        if (!principal || principal->estado != MOD_VALIDO) {
            liberar(&c);
            return 2;
        }
        /* El backend recibe, en orden de preferencia: el programa plegado,
         * el programa enlazado (con bring) o el AST original. */
        const Ast *arbol = &principal->arbol;
        SimTabla *tabla = &principal->tabla;
        Ast fusion = {0}, plegado = {0};
        SimTabla tabla_fusion = {0}, tabla_plegado = {0};   /* sim_inicializar espera ceros */
        int hay_fusion = 0, hay_plegado = 0;

        if (tiene_importaciones(principal)) {
            r = fusionar(&c, principal, &fusion);
            if (r != 0) {
                fprintf(c.diagnosticos, "%s: error de enlace: no se pudieron unir los modulos\n",
                        principal->ruta);
            } else {
                r = analizar_transformado(&c, &fusion, principal->ruta, &tabla_fusion, 0);
                if (r != 0)
                    fprintf(c.diagnosticos, "%s: error de enlace: el programa con sus modulos "
                            "no paso el analisis semantico\n", principal->ruta);
                else {
                    hay_fusion = 1;
                    arbol = &fusion;
                    tabla = &tabla_fusion;
                }
            }
        }
        if (r == 0 && pl_tiene_constantes(arbol->raiz)) {
            int q = plegar_constantes(arbol, &plegado);
            if (q == 2) r = 2;
            else if (analizar_transformado(&c, &plegado, principal->ruta, &tabla_plegado, 1) == 0) {
                hay_plegado = 1;
                arbol = &plegado;
                tabla = &tabla_plegado;
            }
            /* Si el plegado no pasa la semantica se usa el AST anterior. */
        }
        if (r == 0) {
            r = visitante(contexto, arbol, tabla, c.diagnosticos);
            if (r != 0 && r != 1 && r != 2) r = 2;
        }
        if (hay_plegado) sim_liberar(&tabla_plegado);
        if (hay_fusion) sim_liberar(&tabla_fusion);
        ast_liberar(&plegado);
        ast_liberar(&fusion);
    }
    liberar(&c);
    return r;
}

int sem_analizar_proyecto(const char *archivo_principal, FILE *diagnosticos)
{
    if (!archivo_principal || !*archivo_principal) return 2;
    Cargador c = {0};
    c.diagnosticos = diagnosticos ? diagnosticos : stderr;
    int r = cargar(&c, archivo_principal, SEM_PROGRAMA_PRINCIPAL, NULL, NULL);
    liberar(&c);
    return r;
}
