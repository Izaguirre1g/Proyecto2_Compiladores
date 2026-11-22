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
        if (!principal || principal->estado != MOD_VALIDO)
            r = 2;
        else if (!tiene_importaciones(principal)) {
            r = visitante(contexto, &principal->arbol, &principal->tabla,
                          c.diagnosticos);
            if (r != 0 && r != 1 && r != 2) r = 2;
        } else {
            /* Con `bring`: se entrega el programa enlazado. */
            Ast fusion = {0};
            SimTabla tabla = {0};          /* sim_inicializar espera una tabla en cero */
            int hay_tabla = 0;
            r = fusionar(&c, principal, &fusion);
            if (r == 0) {
                if (sim_inicializar(&tabla) != SIM_OK) r = 2;
                else {
                    hay_tabla = 1;
                    int s = analizar_semantica_con_modulos(&fusion, &tabla, principal->ruta,
                                                           c.diagnosticos, SEM_PROGRAMA_PRINCIPAL,
                                                           resolver_importacion, &c);
                    if (s != 0) {
                        fprintf(c.diagnosticos, "%s: error de enlace: el programa con sus modulos "
                                "no paso el analisis semantico\n", principal->ruta);
                        r = s == 2 ? 2 : 1;
                    } else if (!sim_verificar_pre_memoria(&tabla, c.diagnosticos)) r = 2;
                }
            } else {
                fprintf(c.diagnosticos, "%s: error de enlace: no se pudieron unir los modulos\n",
                        principal->ruta);
            }
            if (r == 0) {
                r = visitante(contexto, &fusion, &tabla, c.diagnosticos);
                if (r != 0 && r != 1 && r != 2) r = 2;
            }
            if (hay_tabla) sim_liberar(&tabla);
            ast_liberar(&fusion);
        }
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
