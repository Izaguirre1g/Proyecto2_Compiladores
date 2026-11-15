#ifndef BAL_IR_LATENCIAS_EFECTOS_H
#define BAL_IR_LATENCIAS_EFECTOS_H

#include <stdbool.h>
#include "ir_defuse.h"

/* Contrato simbolico del issue #3: no es el DAG fisico ni certifica aliasing.
 * El unico consumidor autorizado a afirmar el calendario final es VLIW.
 * Las consultas son sin reservas y dejan su salida a cero en error. */
typedef enum {
    IR_AMBITO_LOCAL = 0,
    IR_AMBITO_GLOBAL = 1,
    IR_AMBITO_DESCONOCIDO = 2
} IrAmbitoNombre;

typedef IrAmbitoNombre (*IrResolverAmbito)(const char *nombre, void *contexto);

typedef struct {
    IrResolverAmbito resolver; /* NULL: conservador con nombres BAL */
    void *contexto;
} IrContextoEfectos;

typedef struct {
    IrDefUse defuse; /* Referencias prestadas de la instruccion original. */
    bool lee_memoria, escribe_memoria;
    bool puede_leer_global, puede_escribir_global;
    bool ambito_incierto;
    bool barrera, control;
} IrEfectos;

/* Propaga efectos de memoria abstracta y de variables globales. El callback
 * recibe solo nombres BAL, nunca temporales $tN, $rN ni $spillN. Si no hay
 * resolucion, un nombre BAL se trata como POSIBLE global (no confirmado).
 * IR_PARAM_DECL introduce un parametro local y no se clasifica como global.
 * IR_CALL se trata siempre como lector/escritor de memoria desconocida. */
int ir_efectos_consultar(const IrInstr *instruccion,
    const IrContextoEfectos *contexto, IrEfectos *resultado);

typedef enum {
    IR_DEP_NINGUNA = 0,
    IR_DEP_RAW = 1u << 0,
    IR_DEP_WAR = 1u << 1,
    IR_DEP_WAW = 1u << 2,
    IR_DEP_MEMORIA = 1u << 3,
    IR_DEP_BARRERA = 1u << 4,
    IR_DEP_CONTROL = 1u << 5
} IrTiposDependencia;

typedef struct {
    unsigned tipos;                /* Union de IrTiposDependencia. */
    unsigned distancia_min_bundles; /* Cota conservadora; 0: independientes. */
} IrDependenciaSimbolica;

/* Consulta un par ordenado del MISMO bloque basico. RAW->N+3; WAR/WAW y
 * memoria->al menos 1; llamadas->N+3 como barreras. IR_ARG es pseudo-IR:
 * fija orden, pero no presupone una instruccion fisica ni latencia 3.
 * Una barrera/control de limite de funcion no autoriza cruzar funciones.
 * No resuelve CFG, aliasing ni la semantica de instrucciones ISA expandidas.
 * El DAG fisico debe reconstruir/verificar las dependencias reales. */
int ir_dependencia_consultar(const IrInstr *anterior, const IrInstr *posterior,
    const IrContextoEfectos *contexto, IrDependenciaSimbolica *resultado);

#endif
