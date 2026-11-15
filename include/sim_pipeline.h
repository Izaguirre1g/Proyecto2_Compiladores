/* Pipeline IF–ID–EX–WB del simulador, con ejecución ALU/LSU/BRU/CRIPTO/NOP y traza por ciclo.
 * Usa las imágenes de S1 y la decodificación de S2 sin modificar sus contratos. */
#ifndef SIM_PIPELINE_H
#define SIM_PIPELINE_H

#include "sim_decode.h"
#include "sim_lsu.h"
#include "sim_bru.h"
#include "sim_crypto.h"

typedef enum {
    SIM_PIPE_ACTIVO = 0, SIM_PIPE_TERMINADO, SIM_PIPE_LIMITE,
    SIM_PIPE_ARGUMENTO, SIM_PIPE_PC, SIM_PIPE_DECODIFICACION,
    SIM_PIPE_NO_SOPORTADA, SIM_PIPE_ALU, SIM_PIPE_LSU, SIM_PIPE_BRU, SIM_PIPE_CRYPTO
} SimPipelineEstado;

typedef struct {
    bool valido;
    uint32_t pc;
    uint32_t palabras[SIM_CARGADOR_SLOTS];
} SimIfId;

typedef struct {
    /* Unidad LSU del mismo bundle que la ALU: no es una etapa adicional.
     * base/dato/offset se capturan en ID; valido distingue NOP de un store
     * cuyo opcode físico es cero. destino solo tiene significado para load. */
    bool valido;
    SimOperacion operacion;
    uint32_t palabra, base, dato;
    int32_t offset;
    uint8_t destino;
} SimLsuId;

typedef struct {
    /* Solo los loads conservan un resultado pendiente hasta WB. Los stores
     * ya publicaron memoria en EX y no generan escrituras al banco. */
    bool valida;
    uint8_t destino;
    uint32_t valor;
} SimCargaPendiente;

/* Política del controlador de pruebas, no una modificación de la ISA.
 * FINITA conserva el drenado S3/S4. FISICA busca addr[13:4], incluidos NOP
 * fuera del archivo y aliasing de direcciones altas; requiere presupuesto. */
typedef enum { SIM_BUSQUEDA_FINITA = 0, SIM_BUSQUEDA_FISICA } SimModoBusqueda;

typedef struct {
    /* PC vive en el registro ID/EX común. a/b son capturas ID, nunca lecturas
     * nuevas en EX. destino solo se utiliza para sye/syer y su enlace WB. */
    bool valido;
    SimOperacion operacion;
    uint32_t palabra, a, b;
    int32_t offset;
    uint8_t destino;
} SimBruId;

typedef struct {
    /* Paralelo al registro interno link_* de bru.sv; comparte PC y validez
     * del bundle EX/WB. El flush no descarta el enlace del propio salto. */
    bool valida;
    uint8_t destino;
    uint32_t valor;
} SimEnlacePendiente;

typedef struct {
    /* valido representa el bundle, incluso si operacion es NOP. Los valores
     * a/b se capturaron en ID; EX nunca vuelve a leer el banco de registros. */
    bool valido;
    uint32_t pc, palabra;
    SimOperacion operacion;
    uint8_t destino;
    uint32_t a, b;
    SimLsuId lsu;
    SimBruId bru;
    SimCryptoId crypto;
} SimIdEx;

typedef struct {
    /* Un resultado pendiente pertenece al PC indicado y solo puede escribir
     * en el siguiente flanco. NOP conserva validez de bundle, sin destino. */
    bool valido;
    uint32_t pc;
    SimOperacion operacion;
    uint8_t destino;
    uint32_t resultado;
    SimCargaPendiente lsu;
    SimEnlacePendiente bru;
    SimCryptoWb crypto;
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
    /* Señales LSU antes del flanco y arbitraje WB. Los campos ALU anteriores
     * mantienen su significado; lsu_wb_escribe describe el puerto 1 incluso
     * si hay conflicto. CRIPTO gana a BRU, BRU a LSU y LSU a ALU para destinos no cero. */
    SimLsuId id_lsu;
    SimAccesoLsu ex_lsu;
    SimCargaPendiente wb_lsu;
    bool lsu_wb_escribe, lsu_wb_descarta_x0, conflicto_wb;
    /* Las etapas if_/id conservan su ocupación previa aun al descartarse.
     * Con flush, ID no se decodifica: id_bru solo describe una captura aceptada.
     * ex_bru conserva decisión/destino del bundle original en EX. */
    SimBruId id_bru;
    bool bru_ex_valido, flush, if_descartado, id_descartado;
    SimResultadoBru ex_bru;
    SimEnlacePendiente wb_bru;
    bool bru_wb_escribe, bru_wb_descarta_x0;
    /* No se copia el almacenamiento privado de la bóveda a la traza. Se
     * conservan capturas GPR, efectos de memoria, permisos y resultados ISA. */
    SimCryptoId id_crypto;
    SimEfectoCrypto ex_crypto;
    SimCryptoWb wb_crypto;
    uint32_t estado_pre, estado_post, redirect_pc;
    bool excepcion, conflicto_memoria;
    bool crypto_wb_escribe[2];
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
     * viva durante toda la simulación. S4 no modifica IMEM ni la DMEM de la
     * imagen; tiene su propia copia de datos. */
    const SimImagen *imagen;
    uint32_t pc;
    uint64_t ciclos;
    uint32_t registros[32];
    SimIfId if_id;
    SimIdEx id_ex;
    SimExWb ex_wb;
    /* En modo finito se deja de admitir al alcanzar el final; un redirect
     * limpia fin_busqueda y permite volver al código durante el drenado. */
    bool fin_busqueda;
    SimModoBusqueda modo_busqueda;
    /* Cuenta IF/ID descartados y, desde S6, el bundle culpable en EX.
     * La búsqueda IF suprimida por flush nunca se admite: aparece en la traza. */
    uint64_t bundles_anulados;
    uint64_t bundles_admitidos, bundles_completados;
    SimPipelineEstado estado;
    SimCiclo ultimo_ciclo;
    SimEscritura ultima_escritura;
    SimErrorPipeline error;
    /* Permite rechazar una estructura inicializada en cero sin imagen. No
     * vuelve seguro pasar memoria automática sin inicializar: sigue siendo
     * obligatorio iniciar antes de avanzar/reiniciar, como en S3. */
    bool inicializado;
    /* Estado interno por instancia; no hay instrucciones para leer la bóveda.
     * ESTADO solo contiene AUTH/INIT. trap_* son sondas de top.sv, de 32 bits. */
    SimBoveda boveda;
    uint32_t estado_crypto, trap_pc, trap_count;
    /* Debe permanecer al final: el avance/reset copian o limpian solo el
     * prefijo anterior a memoria. Nuevos campos de control se agregan antes. */
    SimMemoria memoria;
} SimPipeline;

/* Inicializa o reemplaza el estado. Exige imagen no vacía dentro de IMEM y
 * entrada cero, como S1/RTL. Registros, etapas y contadores quedan en reset;
 * no se inicializan SP/GP. Copia DMEM desde imagen, incluso al reemplazar una
 * instancia anterior. En error deja estado ARGUMENTO con diagnóstico. */
SimPipelineEstado sim_pipeline_iniciar(SimPipeline *sim, const SimImagen *imagen);

/* Aplica el estado del reset RTL entre pasos y conserva la imagen prestada.
 * Descarta resultados pendientes y reinicia el conteo en cero; no contabiliza
 * un ciclo de ejecución. AUTH=0, INIT=1 y bóveda borrada. Conserva DMEM
 * y reproduce stores autorizados A/B que ya estaban en
 * EX al llegar el flanco de reset: memory.sv no inhibe we por reset.
 * Una excepción ya capturada sí inhibe ambos puertos.
 * Requiere una estructura previamente inicializada. */
SimPipelineEstado sim_pipeline_reiniciar(SimPipeline *sim);

/* Recarga exclusivamente DMEM desde la imagen, sin reloj ni cambios al
 * procesador. Para repetir una prueba desde su estado original: reiniciar
 * primero (puede publicar el store de EX) y luego restaurar esta memoria. */
SimPipelineEstado sim_pipeline_restaurar_memoria(SimPipeline *sim);

/* Selecciona la política antes del primer paso de una instancia activa, o
 * después de reset. Conserva memoria y estado. Un modo inválido o un cambio
 * con ejecución iniciada devuelve ARGUMENTO sin modificar el procesador.
 * Reset conserva esta configuración administrativa; iniciar vuelve a FINITA. */
SimPipelineEstado sim_pipeline_configurar_busqueda(SimPipeline *sim, SimModoBusqueda modo);

/* Realiza un flanco: calcula con el estado anterior y publica el siguiente
 * conjuntamente. Un rechazo congela PC, etapas, registros y contadores antes
 * del flanco y deja un diagnóstico; no es una excepción de hardware.
 * TERMINADO/errores son estables hasta reset; LIMITE admite reanudación. */
SimPipelineEstado sim_avanzar_ciclo(SimPipeline *sim);

/* Ejecuta hasta max_ciclos adicionales mediante la API de un ciclo. Si agota
 * el presupuesto con trabajo pendiente informa LIMITE, sin inventar un flanco.
 * Se puede reanudar después; no hay HALT ni límite global implícito. */
SimPipelineEstado sim_pipeline_ejecutar(SimPipeline *sim, uint64_t max_ciclos);

#endif
