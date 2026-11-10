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
 */
#include "ide_dce.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ide_analisis.h"
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
} Contexto;

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

    /* Cuántas instrucciones tiene cada línea ANTES del DCE: así se sabe
     * después si una línea se eliminó completa o solo en parte. */
    size_t mayor = 0;
    for (size_t i = 0; i < ir.cantidad; ++i) {
        if (ir.codigo[i].linea > mayor) mayor = ir.codigo[i].linea;
    }
    c->lineas = mayor + 1;
    c->por_linea = calloc(c->lineas, sizeof *c->por_linea);
    if (c->por_linea == NULL) estado = 2;
    for (size_t i = 0; estado == 0 && i < ir.cantidad; ++i) c->por_linea[ir.codigo[i].linea]++;

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
        e->completa = eliminadas[l] == c->por_linea[l];

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
    Contexto c = {s, 0, 0, NULL, 0};
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
    memset(s, 0, sizeof *s);
}
