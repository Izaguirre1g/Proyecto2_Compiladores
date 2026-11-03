/* Pipeline IF–ID–EX–WB del simulador, con ejecución ALU/NOP y traza por ciclo.
 * Usa las imágenes de S1 y la decodificación de S2 sin modificar sus contratos. */
#ifndef SIM_PIPELINE_H
#define SIM_PIPELINE_H

#include "sim_decode.h"

typedef enum {
    SIM_PIPE_ACTIVO = 0, SIM_PIPE_TERMINADO, SIM_PIPE_LIMITE,
    SIM_PIPE_ARGUMENTO, SIM_PIPE_PC, SIM_PIPE_DECODIFICACION,
    SIM_PIPE_NO_SOPORTADA, SIM_PIPE_ALU
} SimPipelineEstado;

typedef struct {
    bool valido;
    uint32_t pc;
    uint32_t palabras[SIM_CARGADOR_SLOTS];
} SimIfId;

typedef struct {
    /* valido representa el bundle, incluso si operacion es NOP. Los valores
     * a/b se capturaron en ID; EX nunca vuelve a leer el banco de registros. */
    bool valido;
    uint32_t pc, palabra;
    SimOperacion operacion;
    uint8_t destino;
    uint32_t a, b;
} SimIdEx;

typedef struct {
    /* Un resultado pendiente pertenece al PC indicado y solo puede escribir
     * en el siguiente flanco. NOP conserva validez de bundle, sin destino. */
    bool valido;
    uint32_t pc;
    SimOperacion operacion;
    uint8_t destino;
    uint32_t resultado;
} SimExWb;

typedef struct { bool valido; uint32_t pc; } SimEtapa;

typedef struct {
    /* Ocupación y señales ANTES del flanco ciclo. La estructura SimPipeline
     * que contiene esta traza ya refleja el estado DESPUÉS de ese flanco. */
    uint64_t ciclo;
    SimEtapa if_, id, ex, wb;
    SimOperacion id_operacion, ex_operacion, wb_operacion;
    uint8_t id_rs1, id_rs2, wb_destino;
    /* ID: a es la lectura rs1; b es rs2 o el inmediato extendido. La marca
     * rs2=32 distingue el inmediato de una lectura real de x0. */
    uint32_t id_a, id_b, ex_a, ex_b, ex_resultado, wb_valor;
    bool wb_escribe, wb_descarta_x0;
} SimCiclo;

typedef struct {
    bool valida;
    uint64_t ciclo;
    uint32_t pc, valor;
    uint8_t destino;
} SimEscritura;

typedef struct {
    /* Describe el ciclo que se intentó, sin que ocurriera su flanco. slot=4
     * e indice_bundle=SIZE_MAX indican un error sin instrucción asociada.
     * decodificacion solo tiene significado cuando hay una instrucción. */
    uint64_t ciclo;
    uint32_t pc, palabra;
    size_t indice_bundle;
    unsigned slot;
    bool tiene_instruccion;
    SimDecodeEstado decodificacion;
    const char *causa;
} SimErrorPipeline;

typedef struct {
    /* Referencia prestada e inmutable: el llamador debe mantener la imagen
     * viva durante toda la simulación. S3 no copia ni modifica IMEM/DMEM. */
    const SimImagen *imagen;
    uint32_t pc;
    uint64_t ciclos;
    uint32_t registros[32];
    SimIfId if_id;
    SimIdEx id_ex;
    SimExWb ex_wb;
    /* Una vez alcanzada la extensión cargada, IF deja de admitir bundles;
     * el PC sigue avanzando 16 por flanco mientras se vacían las otras etapas. */
    bool fin_busqueda;
    uint64_t bundles_admitidos, bundles_completados;
    SimPipelineEstado estado;
    SimCiclo ultimo_ciclo;
    SimEscritura ultima_escritura;
    SimErrorPipeline error;
} SimPipeline;

/* Inicializa o reemplaza el estado. Exige imagen no vacía dentro de IMEM y
 * entrada cero, como S1/RTL. Registros, etapas y contadores quedan en reset;
 * no se inicializan SP/GP. En error deja estado ARGUMENTO con diagnóstico. */
SimPipelineEstado sim_pipeline_iniciar(SimPipeline *sim, const SimImagen *imagen);

/* Aplica el estado del reset RTL entre pasos y conserva la imagen prestada.
 * Descarta resultados pendientes y reinicia el conteo en cero; no contabiliza
 * un ciclo de ejecución. Requiere una estructura previamente inicializada. */
SimPipelineEstado sim_pipeline_reiniciar(SimPipeline *sim);

/* Realiza un flanco: calcula con el estado anterior y publica el siguiente
 * conjuntamente. Un rechazo congela PC, etapas, registros y contadores antes
 * del flanco y deja un diagnóstico; no es una excepción de hardware.
 * TERMINADO/errores son estables hasta reset; LIMITE admite reanudación. */
SimPipelineEstado sim_avanzar_ciclo(SimPipeline *sim);

/* Ejecuta hasta max_ciclos adicionales mediante la API de un ciclo. Si agota
 * el presupuesto con trabajo pendiente informa LIMITE, sin inventar un flanco.
 * Se puede reanudar después; no hay HALT, saltos ni límite global implícito. */
SimPipelineEstado sim_pipeline_ejecutar(SimPipeline *sim, uint64_t max_ciclos);

#endif
