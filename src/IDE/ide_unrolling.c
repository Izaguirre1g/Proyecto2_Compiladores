/*
 * Loop unrolling para el IDE (ver include/ide_unrolling.h).
 *
 * Pasos de ide_unrolling_analizar (los mismos que "balc --unroll"):
 *   1. Guardar el texto del editor en el archivo temporal oculto (el mismo
 *      que usa la revisión de errores, ide_analisis.c).
 *   2. Correr el análisis semántico; si el programa es válido, el compilador
 *      llama a "visitante" con su AST.
 *   3. En el visitante: AST -> IR, foto de la IR, lu_desenrollar (módulo
 *      de Meibel) y otra foto, que marca lo que creó el unrolling.
 * No cambia el módulo de loop unrolling: solo lo llama.
 */
#include "ide_unrolling.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ide_analisis.h"
#include "ir.h"
#include "semantic_modules.h"

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
    IdeUnrolling *salida;
    int llego;          /* 1 si el semántico aceptó el programa y llamó al visitante. */
    int sin_memoria;
} Contexto;

/*
 * Lo llama el análisis semántico cuando el programa es válido.
 * Devuelve 0 bien; 1 si la IR no soporta algo (el mensaje queda en
 * "diag"); 2 si faltó memoria.
 */
static int visitante(void *datos, const Ast *arbol, SimTabla *tabla, FILE *diag)
{
    (void)tabla;
    Contexto *c = datos;
    IdeUnrolling *u = c->salida;
    c->llego = 1;

    IrPrograma ir = {0};
    int estado = ir_desde_ast(arbol, &ir, diag);
    if (estado != 0) return estado;

    LuOpciones opciones;
    lu_opciones_defecto(&opciones);
    if (u->factor != 0) opciones.factor = u->factor;
    u->factor = opciones.factor;

    /* Foto antes, desenrollado, foto después (marca lo nuevo con 'N'). */
    estado = lu_vista_crear(&ir, NULL, &u->antes);
    if (estado == 0) estado = lu_desenrollar(&ir, &opciones, &u->resumen, NULL);
    if (estado == 0) estado = lu_vista_crear(&ir, &u->antes, &u->despues);

    ir_liberar(&ir);
    if (estado != 0) {
        c->sin_memoria = 1;
        return 2;
    }
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
    if (sscanf(ir, "IR:%lu:%lu: %n", &linea, &columna, &usados) != 2 || usados == 0) return NULL;
    const char *mensaje = ir + usados;
    size_t largo = strcspn(mensaje, "\n");
    char *texto = malloc(largo + 32);
    if (texto != NULL) snprintf(texto, largo + 32, "línea %lu: %.*s", linea, (int)largo, mensaje);
    return texto;
}

int ide_unrolling_analizar(const char *texto, size_t largo, const char *ruta,
                           unsigned factor, IdeUnrolling *u)
{
    memset(u, 0, sizeof *u);
    u->factor = factor;

    /* Paso 1: el texto del editor, en el archivo temporal. */
    char *temporal = ide_ruta_temporal(ruta);
    if (temporal == NULL) return -1;
    if (ide_escribir_archivo(temporal, texto, largo) != 0) {
        free(temporal);
        return -1;
    }

    /* Pasos 2 y 3. */
    FILE *diag = tmpfile();
    Contexto c = {u, 0, 0};
    int estado = -1;
    if (diag != NULL) {
        int resultado = sem_visitar_proyecto(temporal, diag, visitante, &c);
        char *mensajes = leer_todo(diag);
        fclose(diag);
        if (c.sin_memoria) {
            estado = -1;
        } else if (resultado == 0 && c.llego) {
            u->estado = IDE_LU_LISTO;
            estado = 0;
        } else if (c.llego) {
            u->estado = IDE_LU_SIN_IR;
            u->motivo = motivo_de_la_ir(mensajes);
            if (u->motivo == NULL) {
                const char *generico = "la IR no pudo traducir el programa";
                u->motivo = malloc(strlen(generico) + 1);
                if (u->motivo != NULL) strcpy(u->motivo, generico);
            }
            estado = u->motivo ? 0 : -1;
        } else {
            u->estado = IDE_LU_CON_ERRORES;
            estado = 0;
        }
        free(mensajes);
    }

    remove(temporal);
    free(temporal);
    if (estado != 0) ide_unrolling_liberar(u);
    return estado;
}

/* Las mismas palabras que el reporte de balc --unroll (lu_reporte.c). */
void ide_unrolling_decision(const LuDetalle *d, char *texto, size_t largo)
{
    switch (d->decision) {
        case LU_TOTAL:
            snprintf(texto, largo, "TOTAL: %llu copias, sin saltos", d->vueltas);
            break;
        case LU_PARCIAL_CONST:
            snprintf(texto, largo, "PARCIAL ×%u: %llu vueltas + %llu sueltas", d->factor,
                     d->factor ? d->vueltas / d->factor : 0, d->factor ? d->vueltas % d->factor : 0);
            break;
        case LU_PARCIAL_VAR:
            snprintf(texto, largo, "PARCIAL ×%u + ciclo residuo", d->factor);
            break;
        case LU_RECHAZADO:
        default:
            snprintf(texto, largo, "sin cambios: %s", d->motivo ? d->motivo : "");
            break;
    }
}

void ide_unrolling_liberar(IdeUnrolling *u)
{
    if (u == NULL) return;
    free(u->motivo);
    lu_resumen_liberar(&u->resumen);
    lu_vista_liberar(&u->antes);
    lu_vista_liberar(&u->despues);
    memset(u, 0, sizeof *u);
}
