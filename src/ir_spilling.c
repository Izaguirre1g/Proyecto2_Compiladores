#include "ir_spilling.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Cada temporal tiene una ranura logica estable durante toda la funcion. */
typedef struct {
    char *nombre;
    char *ranura;
} Temporal;

/* Un registro conserva el temporal asociado, si existe, y su estado. */
typedef struct {
    size_t temporal;
    size_t ultimo_uso;
    int sucio;
} Registro;

typedef struct {
    const IrPrograma *origen;
    IrPrograma *salida;
    Temporal *temporales;
    size_t cantidad_temporales;
    Registro *registros;
    size_t cantidad_registros;
    size_t reloj;
    IrSpillingMetricas *metricas;
} Contexto;

/* Duplica cadenas para no compartir memoria con el programa de entrada. */
static char *duplicar(const char *cadena) {
    if (!cadena) return NULL;
    size_t longitud = strlen(cadena);
    if (longitud == SIZE_MAX) return NULL;
    char *copia = malloc(longitud + 1);
    if (copia) memcpy(copia, cadena, longitud + 1);
    return copia;
}

/* Identifica exclusivamente los temporales que genera el frontend ($tN).
 * Esto evita tratar variables del usuario y etiquetas como registros virtuales. */
static int es_temporal(const char *nombre) {
    if (!nombre || nombre[0] != '$' || nombre[1] != 't' || !nombre[2]) return 0;
    for (const char *p = nombre + 2; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
    }
    return 1;
}

/* Registra un temporal y le asigna una ranura logica numerada. */
static int registrar_temporal(Contexto *ctx, const char *nombre) {
    if (!es_temporal(nombre)) return 0;
    for (size_t i = 0; i < ctx->cantidad_temporales; ++i) {
        if (strcmp(ctx->temporales[i].nombre, nombre) == 0) return 0;
    }
    if (ctx->cantidad_temporales == SIZE_MAX / sizeof(Temporal)) return 2;
    size_t indice = ctx->cantidad_temporales;
    Temporal *nuevos = realloc(ctx->temporales, (indice + 1) * sizeof(Temporal));
    if (!nuevos) return 2;
    ctx->temporales = nuevos;
    ctx->temporales[indice] = (Temporal){0};
    char ranura[48];
    snprintf(ranura, sizeof ranura, "$spill%zu", indice);
    ctx->temporales[indice].nombre = duplicar(nombre);
    ctx->temporales[indice].ranura = duplicar(ranura);
    ctx->cantidad_temporales++;
    if (!ctx->temporales[indice].nombre || !ctx->temporales[indice].ranura) return 2;
    return 0;
}

/* Obtiene el indice de un temporal que ya se registro en el inventario. */
static size_t buscar_temporal(const Contexto *ctx, const char *nombre) {
    for (size_t i = 0; i < ctx->cantidad_temporales; ++i) {
        if (strcmp(ctx->temporales[i].nombre, nombre) == 0) return i;
    }
    return SIZE_MAX;
}

/* Agrega una instruccion de salida con cadenas independientes. */
static int agregar(IrPrograma *ir, IrOp op, const char *dst, const char *a,
                   const char *b, const char *operador, size_t linea) {
    if (ir->cantidad == ir->capacidad) {
        if (ir->capacidad > SIZE_MAX / 2 / sizeof(IrInstr)) return 2;
        size_t capacidad = ir->capacidad ? ir->capacidad * 2 : 32;
        IrInstr *nuevo = realloc(ir->codigo, capacidad * sizeof(IrInstr));
        if (!nuevo) return 2;
        ir->codigo = nuevo;
        ir->capacidad = capacidad;
    }
    IrInstr inst = {op, duplicar(dst), duplicar(a), duplicar(b),
                    duplicar(operador), linea};
    if ((dst && !inst.dst) || (a && !inst.a) || (b && !inst.b) ||
        (operador && !inst.operador)) {
        free(inst.dst); free(inst.a); free(inst.b); free(inst.operador);
        return 2;
    }
    ir->codigo[ir->cantidad++] = inst;
    return 0;
}

/* Describe los campos de la IR que se usan como valores y como resultados.
 * Algunos campos como el destino de goto o el nombre de una funcion no son
 * valores en registros y se deben conservar sin ninguna transformacion. */
static void campos(const IrInstr *inst, int *leer_dst, int *leer_a,
                   int *leer_b, int *escribir_dst) {
    *leer_dst = *leer_a = *leer_b = *escribir_dst = 0;
    switch (inst->op) {
    case IR_COPY: case IR_UNARY: case IR_LOAD: case IR_SIZE:
        *leer_a = 1; *escribir_dst = 1; break;
    case IR_BIN: case IR_INDEX_REF:
        *leer_a = 1; *leer_b = 1; *escribir_dst = 1; break;
    case IR_JFALSE: case IR_JTRUE: case IR_ARG: case IR_RETURN:
        *leer_a = 1; break;
    case IR_CALL:
        *escribir_dst = 1; break;
    case IR_STORE: case IR_LIST_ADD: case IR_LIST_REMOVE:
        *leer_dst = 1; *leer_a = 1; break;
    case IR_ARRAY_DIM:
        *leer_b = 1; break;
    case IR_SPILL_LOAD: case IR_SPILL_STORE:
        /* No se permite aplicar nuevamente la pasada sobre una IR reescrita. */
        break;
    default:
        break;
    }
}

/* Produce el nombre del registro fisico logico reservado por la pasada. */
static void nombre_registro(size_t indice, char *destino, size_t capacidad) {
    snprintf(destino, capacidad, "$r%zu", indice);
}

/* Conserva en pila el valor de un registro antes de reutilizarlo.
 * La escritura solo es necesaria si el temporal cambio desde su carga. */
static int guardar(Contexto *ctx, size_t reg, size_t linea) {
    Registro *r = &ctx->registros[reg];
    if (r->temporal == SIZE_MAX || !r->sucio) return 0;
    char nombre[48];
    nombre_registro(reg, nombre, sizeof nombre);
    int rc = agregar(ctx->salida, IR_SPILL_STORE,
                     ctx->temporales[r->temporal].ranura, nombre, NULL, NULL, linea);
    if (!rc) {
        ctx->metricas->almacenes++;
        r->sucio = 0;
    }
    return rc;
}

/* Elige un registro libre o el menos recientemente usado que no este
 * protegido por los operandos de la instruccion actual. */
static size_t elegir_registro(const Contexto *ctx, const unsigned char *protegido) {
    size_t elegido = SIZE_MAX;
    for (size_t i = 0; i < ctx->cantidad_registros; ++i) {
        if (protegido[i]) continue;
        if (ctx->registros[i].temporal == SIZE_MAX) return i;
        if (elegido == SIZE_MAX ||
            ctx->registros[i].ultimo_uso < ctx->registros[elegido].ultimo_uso) {
            elegido = i;
        }
    }
    return elegido;
}

/* Consigue un registro para leer o escribir un temporal.
 * En una lectura se emite spill_load solo cuando el valor no esta cacheado. */
static int adquirir(Contexto *ctx, const char *nombre, int lectura,
                    unsigned char *protegido, size_t linea, size_t *resultado) {
    size_t temp = buscar_temporal(ctx, nombre);
    if (temp == SIZE_MAX) return 2;
    for (size_t i = 0; i < ctx->cantidad_registros; ++i) {
        if (ctx->registros[i].temporal == temp) {
            ctx->registros[i].ultimo_uso = ++ctx->reloj;
            protegido[i] = 1;
            *resultado = i;
            return 0;
        }
    }
    size_t reg = elegir_registro(ctx, protegido);
    if (reg == SIZE_MAX) return 2;
    if (ctx->registros[reg].temporal != SIZE_MAX) ctx->metricas->desalojos++;
    if (guardar(ctx, reg, linea)) return 2;
    Registro *r = &ctx->registros[reg];
    r->temporal = temp;
    r->sucio = 0;
    r->ultimo_uso = ++ctx->reloj;
    protegido[reg] = 1;
    if (lectura) {
        char nombre_reg[48];
        nombre_registro(reg, nombre_reg, sizeof nombre_reg);
        if (agregar(ctx->salida, IR_SPILL_LOAD, nombre_reg,
                    ctx->temporales[temp].ranura, NULL, NULL, linea)) return 2;
        ctx->metricas->cargas++;
    }
    *resultado = reg;
    return 0;
}

/* Traduce un operando temporal a un registro disponible. Los valores
 * inmediatos, las variables escalares y los nombres se conservan intactos. */
static int traducir_lectura(Contexto *ctx, const char *original,
                           unsigned char *protegido, size_t linea,
                           char *registro, size_t capacidad, const char **salida) {
    *salida = original;
    if (!es_temporal(original)) return 0;
    size_t indice;
    if (adquirir(ctx, original, 1, protegido, linea, &indice)) return 2;
    nombre_registro(indice, registro, capacidad);
    *salida = registro;
    return 0;
}

/* Vuelca todos los temporales modificados al terminar un bloque basico.
 * De esta forma un salto hacia otro bloque no depende del estado de cache. */
static int vaciar(Contexto *ctx, size_t linea) {
    for (size_t i = 0; i < ctx->cantidad_registros; ++i) {
        if (guardar(ctx, i, linea)) return 2;
    }
    return 0;
}

/* Invalida el cache porque al entrar en un bloque no sabemos desde que
 * predecesor llego la ejecucion. Las ranuras logicas son la fuente estable. */
static void invalidar(Contexto *ctx) {
    for (size_t i = 0; i < ctx->cantidad_registros; ++i) {
        ctx->registros[i].temporal = SIZE_MAX;
        ctx->registros[i].sucio = 0;
        ctx->registros[i].ultimo_uso = 0;
    }
}

/* Reescribe una instruccion, generando cargas y almacenes de ser necesarios.
 * Se conservan los operandos de entrada mientras se reserva el destino. */
static int procesar(Contexto *ctx, const IrInstr *inst) {
    int leer_dst, leer_a, leer_b, escribir_dst;
    campos(inst, &leer_dst, &leer_a, &leer_b, &escribir_dst);
    if (inst->op == IR_SPILL_LOAD || inst->op == IR_SPILL_STORE) return 2;
    /* Una llamada puede alterar registros; los valores vivos se preservan. */
    if (inst->op == IR_CALL) {
        if (vaciar(ctx, inst->linea)) return 2;
        invalidar(ctx);
    }
    unsigned char *protegido = calloc(ctx->cantidad_registros, 1);
    if (!protegido) return 2;
    const char *dst = inst->dst, *a = inst->a, *b = inst->b;
    char reg_dst[48], reg_a[48], reg_b[48];
    int rc = 0;
    if (leer_dst) rc = traducir_lectura(ctx, dst, protegido, inst->linea,
                                       reg_dst, sizeof reg_dst, &dst);
    if (!rc && leer_a) rc = traducir_lectura(ctx, a, protegido, inst->linea,
                                            reg_a, sizeof reg_a, &a);
    if (!rc && leer_b) rc = traducir_lectura(ctx, b, protegido, inst->linea,
                                            reg_b, sizeof reg_b, &b);
    size_t indice_destino = SIZE_MAX;
    if (!rc && escribir_dst && es_temporal(inst->dst)) {
        rc = adquirir(ctx, inst->dst, 0, protegido, inst->linea, &indice_destino);
        if (!rc) {
            nombre_registro(indice_destino, reg_dst, sizeof reg_dst);
            dst = reg_dst;
        }
    }
    if (!rc) rc = agregar(ctx->salida, inst->op, dst, a, b,
                          inst->operador, inst->linea);
    if (!rc && indice_destino != SIZE_MAX) ctx->registros[indice_destino].sucio = 1;
    free(protegido);
    return rc;
}

/* Libera el inventario y el estado auxiliar, incluso si una reserva fallo. */
static void liberar_contexto(Contexto *ctx) {
    for (size_t i = 0; i < ctx->cantidad_temporales; ++i) {
        free(ctx->temporales[i].nombre);
        free(ctx->temporales[i].ranura);
    }
    free(ctx->temporales);
    free(ctx->registros);
}

/* Punto de entrada de la asignacion local conservadora de registros.
 * Cada bloque empieza con el cache vacio; los bordes se sincronizan con pila.
 * Se evita insertar almacenes despues de saltos y retornos incondicionales. */
int ir_aplicar_spilling(const IrPrograma *origen, IrPrograma *salida,
                       size_t registros, IrSpillingMetricas *metricas) {
    if (!origen || !salida || !metricas || origen == salida ||
        registros < 3 || registros > 32 || !origen->bloques ||
        salida->codigo || salida->bloques || salida->cantidad ||
        salida->cantidad_bloques) return 2;
    IrSpillingMetricas m = {0};
    m.registros = registros;
    Contexto ctx = {0};
    ctx.origen = origen;
    ctx.salida = salida;
    ctx.cantidad_registros = registros;
    ctx.metricas = &m;
    ctx.registros = calloc(registros, sizeof(Registro));
    if (!ctx.registros) return 2;
    invalidar(&ctx);
    int rc = 0;
    /* Primero inventariamos todos los temporales para fijar sus ranuras. */
    for (size_t i = 0; i < origen->cantidad && !rc; ++i) {
        const IrInstr *inst = &origen->codigo[i];
        int rd, ra, rb, wd;
        campos(inst, &rd, &ra, &rb, &wd);
        if (inst->op == IR_SPILL_LOAD || inst->op == IR_SPILL_STORE) {rc = 2;break;}
        if (rd || wd) rc = registrar_temporal(&ctx, inst->dst);
        if (!rc && ra) rc = registrar_temporal(&ctx, inst->a);
        if (!rc && rb) rc = registrar_temporal(&ctx, inst->b);
    }
    m.ranuras = ctx.cantidad_temporales;
    /* Cada bloque preserva sus temporales antes de transferir el control. */
    for (size_t j = 0; j < origen->cantidad_bloques && !rc; ++j) {
        const IrBloque *bloque = &origen->bloques[j];
        invalidar(&ctx);
        for (size_t i = bloque->inicio; i < bloque->fin && !rc; ++i) {
            const IrInstr *inst = &origen->codigo[i];
            int ultimo = i + 1 == bloque->fin;
            int terminal = inst->op == IR_JUMP || inst->op == IR_JFALSE ||
                           inst->op == IR_JTRUE || inst->op == IR_RETURN ||
                           inst->op == IR_END_FUNC;
            if (ultimo && terminal) rc = vaciar(&ctx, inst->linea);
            if (!rc) rc = procesar(&ctx, inst);
            if (ultimo && !terminal && !rc) rc = vaciar(&ctx, inst->linea);
        }
    }
    if (!rc) rc = ir_bloques_basicos(salida);
    if (rc) ir_liberar(salida);
    else *metricas = m;
    liberar_contexto(&ctx);
    return rc;
}
