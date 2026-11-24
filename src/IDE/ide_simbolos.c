/*
 * Tabla de símbolos del código del editor (ver include/ide_simbolos.h).
 *
 * La tabla la construye el análisis semántico con las funciones de
 * src/Analisis_Semantico/symbol_table.c. Este archivo no la modifica: solo
 * la LEE con esas mismas funciones y copia lo que la ventana va a mostrar.
 * No escribe ningún archivo (ni .simbolos.md ni .asm).
 */
#include "ide_simbolos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "enlace_funciones.h"
#include "ide_analisis.h"
#include "memory.h"
#include "resolucion_referencias.h"
#include "semantic_modules.h"
#include "symbol_table.h"

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
    IdeSimbolos *salida;
    int llego;          /* 1 si el semántico aceptó el programa y llamó al visitante. */
    int sin_memoria;
} Contexto;

/* Dónde vive el símbolo, con los mismos nombres que "balc -m" (almacen_sim, cli.c). */
static const char *nombre_almacen(SimAlmacen a)
{
    switch (a) {
        case SIM_ALMACEN_SIN_ASIGNAR: return "sin_asignar";
        case SIM_ALMACEN_GLOBAL: return "global(gp)";
        case SIM_ALMACEN_LOCAL: return "local(sp)";
        case SIM_ALMACEN_PARAMETRO: return "parametro(sp)";
        case SIM_ALMACEN_CODIGO: return "codigo";
    }
    return "desconocido";
}

/* Las dimensiones como texto: "[2][3]" para una matriz 2x3. Un arreglo
 * cuyo tamaño no se conoce al compilar se muestra como "[?]". */
static void escribir_dimensiones(const SimSimbolo *s, char *texto, size_t largo)
{
    texto[0] = '\0';
    size_t usado = 0;
    for (unsigned d = 0; d < s->dimensiones && usado < largo; ++d) {
        int n = (s->longitudes != NULL && s->longitudes[d] > 0)
                    ? snprintf(texto + usado, largo - usado, "[%zu]", s->longitudes[d])
                    : snprintf(texto + usado, largo - usado, "[?]");
        if (n < 0) break;
        usado += (size_t)n;
    }
}

/* Copia un símbolo de la SimTabla a una fila propia. "funciones" son las
 * direcciones que dio el enlazador (NULL si no se generó el código). */
static void copiar_simbolo(const SimTabla *tabla, const SimSimbolo *s,
                           const RrTabla *funciones, IdeSimbolo *fila)
{
    memset(fila, 0, sizeof *fila);
    snprintf(fila->nombre, sizeof fila->nombre, "%s", s->nombre ? s->nombre : "?");
    fila->clase = sim_nombre_clase(s->clase);   /* symbol_table.c */
    fila->tipo = sim_nombre_tipo(s->tipo);      /* symbol_table.c */
    const SimAmbito *a = s->ambito;
    snprintf(fila->ambito, sizeof fila->ambito, "%s", a && a->nombre ? a->nombre : "?");
    fila->nivel = a ? a->nivel : 0;
    escribir_dimensiones(s, fila->dimensiones, sizeof fila->dimensiones);
    fila->parametros = s->cantidad_parametros;
    /* Dirección de código: solo las funciones del ámbito global, y solo si el
     * enlazador la encontró (la misma regla que "balc -m"). */
    uint32_t direccion = 0;
    fila->con_direccion = funciones != NULL && s->clase == SIM_FUNCION && a == tabla->global &&
                          s->nombre != NULL &&
                          rr_buscar(funciones, s->nombre, RR_FUNCION, &direccion) == RR_OK;
    fila->direccion = direccion;
    /* Una función con dirección vive en el código. */
    fila->almacen = fila->con_direccion ? "codigo" : nombre_almacen(s->almacen);
    fila->bytes = s->bytes;
    fila->con_desplazamiento = s->almacen == SIM_ALMACEN_GLOBAL ||
                               s->almacen == SIM_ALMACEN_LOCAL ||
                               s->almacen == SIM_ALMACEN_PARAMETRO;
    fila->desplazamiento = s->desplazamiento;
    fila->linea = s->linea;
    fila->columna = s->columna;
}

/* Copia los símbolos (en el orden en que se declararon) y los ámbitos (en
 * el orden en que se crearon). 0 bien, -1 sin memoria. */
static int copiar_tabla(const SimTabla *tabla, const RrTabla *funciones, IdeSimbolos *s)
{
    /* Símbolos: la lista global tabla->primero -> siguiente_orden. */
    size_t n = 0;
    for (const SimSimbolo *x = tabla->primero; x != NULL; x = x->siguiente_orden) n++;
    s->simbolos = calloc(n ? n : 1, sizeof *s->simbolos);
    if (s->simbolos == NULL) return -1;
    for (const SimSimbolo *x = tabla->primero; x != NULL; x = x->siguiente_orden)
        copiar_simbolo(tabla, x, funciones, &s->simbolos[s->cantidad++]);

    /* Ámbitos: desde el global, por siguiente_creacion. Cada uno tiene su
     * propia tabla hash de uthash; HASH_COUNT dice cuántos símbolos tiene. */
    size_t m = 0;
    for (const SimAmbito *a = tabla->global; a != NULL; a = a->siguiente_creacion) m++;
    s->ambitos = calloc(m ? m : 1, sizeof *s->ambitos);
    if (s->ambitos == NULL) return -1;
    for (const SimAmbito *a = tabla->global; a != NULL; a = a->siguiente_creacion) {
        IdeAmbito *e = &s->ambitos[s->cantidad_ambitos++];
        snprintf(e->nombre, sizeof e->nombre, "%s", a->nombre ? a->nombre : "?");
        e->nivel = a->nivel;
        snprintf(e->padre, sizeof e->padre, "%s",
                 a->padre && a->padre->nombre ? a->padre->nombre : "");
        e->simbolos = HASH_COUNT(a->simbolos);
    }
    return 0;
}

/*
 * Lo llama el análisis semántico cuando el programa es válido, con la tabla
 * de símbolos ya terminada. Devuelve 0 (o 2 si faltó memoria).
 */
static int visitante(void *datos, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    Contexto *c = datos;
    IdeSimbolos *s = c->salida;
    c->llego = 1;

    /* 1. ¿La tabla es consistente? (symbol_table.c). Se revisa ANTES de
     *    asignar la memoria, como hace el propio semántico. */
    s->consistente = sim_verificar_pre_memoria(tabla, diag);

    /* 2. La memoria, igual que "balc -m": cada símbolo recibe su almacén y
     *    desplazamiento. Si no se puede (por ejemplo, un parámetro que el
     *    backend no soporta), la tabla se muestra igual, con "pendiente". */
    MemProyectoMapa mapa = {0};
    if (mem_asignar_proyecto_referencias(tabla, CLI_GP_BASE, CLI_FIN_GLOBALES,
                                         CLI_CAPACIDAD_MARCO, &mapa) == MEM_PROYECTO_OK) {
        s->con_memoria = 1;
        s->funciones = mapa.funciones;
        s->marco_maximo = mapa.marco_maximo_bytes;
        s->gp = mapa.globales.gp_base;
        s->bytes_globales = mapa.globales.bytes_globales;
    }
    s->sp = CLI_TOPE_PILA;

    /* 3. Las direcciones de código: se genera y se enlaza el programa igual
     *    que "balc -m" (compilar_proyecto, cli.c). El enlazador deja en
     *    "funciones" la dirección final de cada función. El ensamblador no
     *    se usa aquí (va a un archivo en memoria y se descarta). */
    RrTabla funciones;
    rr_inicializar(&funciones);
    if (s->con_memoria) {
        const EfArranque arranque = {(uint32_t)mapa.globales.gp_base, CLI_TOPE_PILA};
        EfResumen resumen = {0};
        FILE *temporal = tmpfile();
        if (temporal != NULL) {
            s->con_codigo = ef_enlazar_referencias(arbol, tabla, 0, &arranque, &funciones,
                                                   temporal, diag, &resumen) == 0;
            fclose(temporal);
        }
    }

    /* 4. Copiar símbolos y ámbitos (la tabla se libera al volver). */
    int copiado = copiar_tabla(tabla, s->con_codigo ? &funciones : NULL, s);
    rr_liberar(&funciones);
    if (copiado != 0) {
        c->sin_memoria = 1;
        return 2;
    }

    /* 5. El listado de texto de symbol_table.c (sim_imprimir). */
    FILE *f = tmpfile();
    if (f != NULL) {
        sim_imprimir(tabla, f);
        s->listado = leer_todo(f);
        fclose(f);
    }
    if (s->listado == NULL) {
        c->sin_memoria = 1;
        return 2;
    }
    return 0;
}

int ide_simbolos_generar(const char *texto, size_t largo, const char *ruta, IdeSimbolos *s)
{
    memset(s, 0, sizeof *s);

    /* Paso 1: el texto del editor, en el archivo temporal. */
    char *temporal = ide_ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (ide_escribir_archivo(temporal, texto, largo) != 0) {
        free(temporal);
        return -1;
    }

    /* Pasos 2 y 3: análisis semántico; si es válido, el visitante. Los
     * mensajes del compilador no se muestran aquí (ya los muestra la
     * pestaña Errores), así que van a un archivo en memoria. */
    int estado = -1;
    FILE *diag = tmpfile();
    if (diag != NULL) {
        Contexto c = {s, 0, 0};
        int resultado = sem_visitar_proyecto(temporal, diag, visitante, &c);
        if (c.sin_memoria) {
            estado = -1;
        } else if (resultado == 0 && c.llego) {
            s->estado = IDE_SIM_LISTO;
            estado = 0;
        } else {
            s->estado = IDE_SIM_CON_ERRORES;
            estado = 0;
        }
        fclose(diag);
    }

    remove(temporal);
    free(temporal);
    if (estado != 0) ide_simbolos_liberar(s);
    return estado;
}

void ide_simbolos_liberar(IdeSimbolos *s)
{
    if (s == NULL) return;
    free(s->simbolos);
    free(s->ambitos);
    free(s->listado);
    memset(s, 0, sizeof *s);
}
