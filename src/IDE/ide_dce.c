/*
 * Eliminación de código muerto para el IDE (ver include/ide_dce.h).
 *
 * Pasos de ide_dce_analizar:
 *   1. Guardar el texto del editor en el archivo temporal oculto (el mismo
 *      que usa la revisión de errores, ide_analisis.c).
 *   2. Correr el análisis semántico; si el programa es válido, el compilador
 *      llama a "visitante" con su AST (lo mismo que hace balc --dce).
 *   3. En el visitante: AST -> IR, variables vivas, DCE. Se guardan los
 *      textos y cuántas instrucciones de la IR tenía cada línea del código.
 *   4. Agrupar lo eliminado por línea, para resaltar el editor.
 *   5. Armar el CÓDIGO NUEVO sin el código muerto (ide_dce_codigo.c),
 *      revisar que siga siendo válido, y generar el ensamblador del código
 *      original y del nuevo (ide_ensamblador.c) para comparar. El backend
 *      genera el ensamblador desde el código fuente, así que compilando el
 *      código nuevo el ensamblador y el binario salen sin el código muerto.
 */
#include "ide_dce.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ide_analisis.h"
#include "ide_ensamblador.h"
#include "ir.h"
#include "semantic_modules.h"

/* ------------------------------------------------------------------ */
/* Ayudas                                                              */
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

/* Copia una cadena (NULL si falta memoria). */
static char *copiar(const char *s)
{
    size_t largo = strlen(s) + 1;
    char *c = malloc(largo);
    if (c != NULL) memcpy(c, s, largo);
    return c;
}

/* ------------------------------------------------------------------ */
/* El visitante: IR, variables vivas y DCE                             */
/* ------------------------------------------------------------------ */

/* Lo que el visitante necesita y lo que deja. */
typedef struct {
    IdeDce *salida;
    int llego;               /* 1 si el semántico aceptó el programa y llamó al visitante. */
    int sin_memoria;
    size_t *por_linea;       /* Cuántas instrucciones de la IR tiene cada línea BAL. */
    size_t lineas;           /* Tamaño de por_linea (la línea más alta + 1). */
    IdeDceSentencia *sentencias;  /* Declaraciones y asignaciones (del AST). */
    size_t cantidad_sentencias, capacidad_sentencias;
} Contexto;

/* El hijo número "i" de un nodo del AST (o NULL). */
static const AstNodo *hijo(const AstNodo *n, unsigned i)
{
    const AstNodo *h = n ? n->primer_hijo : NULL;
    while (h != NULL && i-- > 0) h = h->siguiente;
    return h;
}

/*
 * Recorre el AST y guarda las declaraciones y asignaciones de una variable
 * simple, para poder reescribirlas en el código nuevo. El AST solo existe
 * mientras corre el visitante, así que se copia lo necesario.
 * Devuelve 0 bien o -1 sin memoria.
 */
/* Agrega una sentencia a la lista. 0 bien, -1 sin memoria. */
static int agregar_sentencia(Contexto *c, IdeDceSentencia sen)
{
    if (c->cantidad_sentencias == c->capacidad_sentencias) {
        size_t nueva = c->capacidad_sentencias ? c->capacidad_sentencias * 2 : 32;
        IdeDceSentencia *mas = realloc(c->sentencias, nueva * sizeof *mas);
        if (mas == NULL) return -1;
        c->sentencias = mas;
        c->capacidad_sentencias = nueva;
    }
    c->sentencias[c->cantidad_sentencias++] = sen;
    return 0;
}

/*
 * Recorre el AST y guarda las sentencias, para poder reescribirlas en el
 * código nuevo. El AST solo existe mientras corre el visitante, así que se
 * copia lo necesario:
 *   - declaraciones y asignaciones de una variable simple (con su nombre);
 *   - cualquier otra sentencia de un bloque (whether, whale, cycle, give,
 *     una llamada, una escritura en un arreglo...): esas solo se borran
 *     enteras, si todo lo de adentro es código muerto (por ejemplo, un
 *     whether con la condición siempre falsa, o lo que va después de give).
 * "padre" es el tipo del nodo que contiene a "n".
 * Devuelve 0 bien o -1 sin memoria.
 */
static int juntar_sentencias(Contexto *c, const AstNodo *n, AstTipo padre)
{
    for (; n != NULL; n = n->siguiente) {
        const AstNodo *id = hijo(n, 0);
        int declaracion = n->tipo == AST_DECLARACION && hijo(n, 1) != NULL &&
                          hijo(n, 1)->tipo == AST_TIPO;
        int asignacion = n->tipo == AST_ASIGNACION;
        if ((declaracion || asignacion) && id != NULL && id->tipo == AST_IDENTIFICADOR &&
            id->texto != NULL) {
            const AstNodo *valor = hijo(n, declaracion ? 2 : 1);
            IdeDceSentencia sen = {0, declaracion, n->ubicacion.first_line, n->ubicacion.first_column,
                                   copiar(id->texto), valor != NULL,
                                   valor != NULL && valor->tipo == AST_LLAMADA};
            if (sen.nombre == NULL || agregar_sentencia(c, sen) != 0) {
                free(sen.nombre);
                return -1;
            }
        } else if (padre == AST_BLOQUE) {
            IdeDceSentencia sen = {1, 0, n->ubicacion.first_line, n->ubicacion.first_column,
                                   NULL, 0, 0};
            if (agregar_sentencia(c, sen) != 0) return -1;
        }
        if (juntar_sentencias(c, n->primer_hijo, n->tipo) != 0) return -1;
    }
    return 0;
}

/* Escribe en un tmpfile con "imprimir" y devuelve el texto. */
static char *capturar_ir(const IrPrograma *ir)
{
    FILE *f = tmpfile();
    if (f == NULL) return NULL;
    char *texto = (ir_imprimir(ir, f) == 0) ? leer_todo(f) : NULL;
    fclose(f);
    return texto;
}

static char *capturar_vivas(const DceVivas *v)
{
    FILE *f = tmpfile();
    if (f == NULL) return NULL;
    char *texto = (dce_imprimir_vivas(v, f) == 0) ? leer_todo(f) : NULL;
    fclose(f);
    return texto;
}

/*
 * Lo llama el análisis semántico cuando el programa es válido.
 * Devuelve 0 si todo salió bien; 1 si la IR no soporta algo (el mensaje
 * "IR:linea:columna: ..." queda en "diag"); 2 si faltó memoria.
 */
static int visitante(void *datos, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    (void)tabla;
    Contexto *c = datos;
    IdeDce *s = c->salida;
    c->llego = 1;

    IrPrograma ir = {0};
    int estado = ir_desde_ast(arbol, &ir, diag);
    if (estado != 0) return estado;

    /* Las sentencias del AST, para armar después el código nuevo. */
    if (juntar_sentencias(c, arbol->raiz, AST_PROGRAMA) != 0) {
        ir_liberar(&ir);
        c->sin_memoria = 1;
        return 2;
    }

    /* Cuántas instrucciones tiene cada línea ANTES del DCE: así se sabe
     * después si una línea se eliminó completa o solo en parte. */
    size_t mayor = 0;
    for (size_t i = 0; i < ir.cantidad; ++i) {
        if (ir.codigo[i].linea > mayor) mayor = ir.codigo[i].linea;
    }
    c->lineas = mayor + 1;
    c->por_linea = calloc(c->lineas, sizeof *c->por_linea);
    if (c->por_linea == NULL) estado = 2;
    /* No cuentan las etiquetas ni el inicio y fin de cada función: no son
     * código que escribió la persona y nunca se eliminan como tal. */
    for (size_t i = 0; estado == 0 && i < ir.cantidad; ++i) {
        IrOp op = ir.codigo[i].op;
        if (op == IR_LABEL || op == IR_FUNC || op == IR_END_FUNC || op == IR_PARAM_DECL) continue;
        c->por_linea[ir.codigo[i].linea]++;
    }

    /* La IR antes, las variables vivas, el DCE y la IR después. */
    if (estado == 0 && (s->ir_antes = capturar_ir(&ir)) == NULL) estado = 2;
    if (estado == 0) {
        DceVivas vivas;
        if (dce_calcular_vivas(&ir, &vivas) != 0) estado = 2;
        else {
            if ((s->vivas = capturar_vivas(&vivas)) == NULL) estado = 2;
            dce_liberar_vivas(&vivas);
        }
    }
    if (estado == 0 && dce_eliminar(&ir, &s->resultado) != 0) estado = 2;
    if (estado == 0 && (s->ir_despues = capturar_ir(&ir)) == NULL) estado = 2;

    ir_liberar(&ir);
    if (estado == 2) c->sin_memoria = 1;
    return estado;
}

/* ------------------------------------------------------------------ */
/* Agrupar lo eliminado por línea                                      */
/* ------------------------------------------------------------------ */

/*
 * Arma salida->lineas: una entrada por cada línea BAL con algo eliminado,
 * con todo lo que se eliminó de ella y si fue la línea completa.
 * Devuelve 0 bien o -1 sin memoria.
 */
static int agrupar_por_linea(const Contexto *c, IdeDce *s)
{
    const DceResultado *r = &s->resultado;
    if (r->cantidad_eliminadas == 0) return 0;

    /* Cuántas se eliminaron de cada línea. */
    size_t *eliminadas = calloc(c->lineas, sizeof *eliminadas);
    if (eliminadas == NULL) return -1;
    for (size_t i = 0; i < r->cantidad_eliminadas; ++i) {
        size_t l = r->eliminadas[i].linea;
        if (l < c->lineas) eliminadas[l]++;
    }

    size_t distintas = 0;
    for (size_t l = 0; l < c->lineas; ++l) distintas += eliminadas[l] > 0;
    s->lineas = calloc(distintas, sizeof *s->lineas);
    if (s->lineas == NULL) {
        free(eliminadas);
        return -1;
    }

    /* Una entrada por línea, en orden; su detalle junta lo eliminado. */
    for (size_t l = 0; l < c->lineas; ++l) {
        if (eliminadas[l] == 0) continue;
        IdeDceLinea *e = &s->lineas[s->cantidad_lineas++];
        e->linea = l;
        e->eliminadas = eliminadas[l];
        e->total = c->por_linea[l];
        e->completa = eliminadas[l] >= c->por_linea[l];
        for (size_t i = 0; i < r->cantidad_eliminadas; ++i) {
            if (r->eliminadas[i].linea == l && r->eliminadas[i].tipo == DCE_INALCANZABLE) e->inalcanzable = 1;
        }

        size_t largo = 1;
        for (size_t i = 0; i < r->cantidad_eliminadas; ++i) {
            if (r->eliminadas[i].linea == l) largo += strlen(r->eliminadas[i].texto) + 2;
        }
        e->detalle = malloc(largo);
        if (e->detalle == NULL) {
            free(eliminadas);
            return -1;
        }
        e->detalle[0] = '\0';
        for (size_t i = 0; i < r->cantidad_eliminadas; ++i) {
            if (r->eliminadas[i].linea != l) continue;
            if (e->detalle[0] != '\0') strcat(e->detalle, "; ");
            strcat(e->detalle, r->eliminadas[i].texto);
        }
    }
    free(eliminadas);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Paso 5: el código nuevo y su ensamblador                            */
/* ------------------------------------------------------------------ */

/* ¿El texto es un programa válido? (el mismo análisis que la pestaña Errores). */
static int es_valido(const char *texto, const char *ruta)
{
    DiagLista lista = {0};
    int valido = ide_analizar_texto(texto, strlen(texto), ruta, &lista) == 0 && lista.cantidad == 0;
    diag_liberar(&lista);
    return valido;
}

/*
 * Arma el código nuevo; si no fuera válido (no debería pasar), lo descarta.
 * Después genera el ensamblador del código original y del nuevo, con el
 * mismo backend que balc, para mostrar cuántos bundles se ahorran.
 * Devuelve 0 bien o -1 sin memoria.
 */
static int codigo_y_ensamblador(const Contexto *c, const char *texto, size_t largo,
                                const char *ruta, IdeDce *s)
{
    if (ide_dce_codigo_nuevo(texto, largo, c->sentencias, c->cantidad_sentencias,
                             c->por_linea, c->lineas, &s->resultado, s) != 0) return -1;
    if (s->codigo_nuevo != NULL && !es_valido(s->codigo_nuevo, ruta)) {
        free(s->codigo_nuevo);
        s->codigo_nuevo = NULL;
        s->sentencias_borradas = s->sentencias_cambiadas = 0;
    }
    if (s->codigo_nuevo == NULL) return 0;

    IdeEnsamblador antes, despues;
    if (ide_ensamblador_generar(texto, largo, ruta, &antes) != 0) return -1;
    if (ide_ensamblador_generar(s->codigo_nuevo, strlen(s->codigo_nuevo), ruta, &despues) != 0) {
        ide_ensamblador_liberar(&antes);
        return -1;
    }
    int estado = 0;
    if (antes.estado == IDE_ASM_LISTO && despues.estado == IDE_ASM_LISTO) {
        s->hay_ensamblador = 1;
        s->bundles_antes = antes.bundles;
        s->bundles_despues = despues.bundles;
    } else if (despues.estado == IDE_ASM_LISTO) {
        /* El original no compila, pero sin el código muerto sí (por ejemplo,
         * el backend rechaza sentencias después del give final). */
        s->hay_ensamblador = 2;
        s->bundles_despues = despues.bundles;
        s->motivo_ensamblador = copiar(antes.motivo ? antes.motivo : "el backend no pudo generarlo");
        if (s->motivo_ensamblador == NULL) estado = -1;
    } else {
        const char *motivo = antes.motivo ? antes.motivo : despues.motivo;
        s->motivo_ensamblador = copiar(motivo ? motivo : "el backend no pudo generar el ensamblador");
        if (s->motivo_ensamblador == NULL) estado = -1;
    }
    ide_ensamblador_liberar(&antes);
    ide_ensamblador_liberar(&despues);
    return estado;
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
    if (sscanf(ir, "IR:%lu:%lu: %n", &linea, &columna, &usados) != 2 || usados == 0) {
        return NULL;
    }
    const char *mensaje = ir + usados;
    size_t largo = strcspn(mensaje, "\n");
    char *texto = malloc(largo + 32);
    if (texto != NULL) snprintf(texto, largo + 32, "línea %lu: %.*s", linea, (int)largo, mensaje);
    return texto;
}

/* ------------------------------------------------------------------ */
/* Entrada                                                             */
/* ------------------------------------------------------------------ */

int ide_dce_analizar(const char *texto, size_t largo, const char *ruta, IdeDce *s)
{
    memset(s, 0, sizeof *s);

    /* Paso 1: el texto del editor, en el archivo temporal. */
    char *temporal = ide_ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (ide_escribir_archivo(temporal, texto, largo) != 0) {
        free(temporal);
        return -1;
    }

    /* Paso 2 y 3: semántico y, si el programa es válido, el visitante. */
    FILE *diag = tmpfile();
    Contexto c = {s, 0, 0, NULL, 0, NULL, 0, 0};
    int estado = -1;
    if (diag != NULL) {
        int resultado = sem_visitar_proyecto(temporal, diag, visitante, &c);
        char *mensajes = leer_todo(diag);
        fclose(diag);

        if (c.sin_memoria) {
            estado = -1;
        } else if (resultado == 0 && c.llego) {
            /* Paso 4: agrupar por línea. */
            s->estado = IDE_DCE_LISTO;
            estado = agrupar_por_linea(&c, s);
            /* Paso 5: el código nuevo y su ensamblador. */
            if (estado == 0) estado = codigo_y_ensamblador(&c, texto, largo, ruta, s);
        } else if (c.llego) {
            /* El programa es válido, pero la IR no lo soporta todavía. */
            s->estado = IDE_DCE_SIN_IR;
            s->motivo = motivo_de_la_ir(mensajes);
            if (s->motivo == NULL) s->motivo = copiar("la IR no pudo traducir el programa");
            estado = s->motivo ? 0 : -1;
        } else {
            /* El semántico (o el parser) encontró errores. */
            s->estado = IDE_DCE_CON_ERRORES;
            estado = 0;
        }
        free(mensajes);
    }

    remove(temporal);
    free(temporal);
    free(c.por_linea);
    for (size_t i = 0; i < c.cantidad_sentencias; ++i) free(c.sentencias[i].nombre);
    free(c.sentencias);
    if (estado != 0) ide_dce_liberar(s);
    return estado;
}

void ide_dce_liberar(IdeDce *s)
{
    if (s == NULL) return;
    free(s->motivo);
    dce_liberar_resultado(&s->resultado);
    for (size_t i = 0; i < s->cantidad_lineas; ++i) free(s->lineas[i].detalle);
    free(s->lineas);
    free(s->ir_antes);
    free(s->vivas);
    free(s->ir_despues);
    free(s->codigo_nuevo);
    free(s->motivo_ensamblador);
    memset(s, 0, sizeof *s);
}
