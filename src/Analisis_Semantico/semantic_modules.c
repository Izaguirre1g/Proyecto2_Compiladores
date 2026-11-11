/* Etapa 2N-C: carga y validacion recursiva de modulos del mismo proyecto.
 * No resuelve expresiones modulo.miembro (etapa 2N-D).
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
        else {
            r = visitante(contexto, &principal->arbol, &principal->tabla,
                          c.diagnosticos);
            if (r != 0 && r != 1 && r != 2) r = 2;
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
