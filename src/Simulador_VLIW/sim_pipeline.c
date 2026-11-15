/* Avance simultáneo del pipeline VLIW con el conjunto ALU/LSU/BRU/CRIPTO/NOP.
 * Las lecturas de ID preceden a la publicación de escrituras WB del flanco. */
#include "sim_pipeline.h"
#include "sim_alu.h"
#include <string.h>

/* Los errores detienen el intento antes del flanco. Se conserva todo estado
 * arquitectónico previo y se publica únicamente la causa de la detención. */
static SimPipelineEstado detener(SimPipeline *sim, SimPipelineEstado estado,
                                 uint32_t pc, unsigned slot, uint32_t palabra,
                                 SimDecodeEstado dec, const char *causa)
{
    sim->estado = estado;
    // El diagnóstico identifica el intento siguiente, con su PC y slot si existe instrucción. No
    // publica un nuevo SimCiclo ni cambia GPR, DMEM o etapas: es una detención administrativa
    // anterior al flanco.
    sim->error = (SimErrorPipeline){.ciclo = sim->ciclos + 1, .pc = pc,
        .palabra = palabra, .slot = slot, .tiene_instruccion = slot < 4,
        .indice_bundle = slot < 4 ? pc / SIM_CARGADOR_BYTES_BUNDLE : SIZE_MAX,
        .decodificacion = dec, .causa = causa};
    return estado;
}

/* Inicializa o reemplaza el estado. Exige imagen no vacía dentro de IMEM y
 * entrada cero, como S1/RTL. Registros, etapas y contadores quedan en reset;
 * no se inicializan SP/GP. Copia DMEM desde imagen, incluso al reemplazar una
 * instancia anterior. En error deja estado ARGUMENTO con diagnóstico. */
SimPipelineEstado sim_pipeline_iniciar(SimPipeline *sim, const SimImagen *imagen)
{
    if (!sim) return SIM_PIPE_ARGUMENTO;
    *sim = (SimPipeline){.imagen = imagen, .estado = SIM_PIPE_ARGUMENTO};
    /* S1 establece PC=0. Solo se validan los metadatos necesarios para buscar
     * código; no se reimplementa la validación de archivos o datos de S1. */
    if (!imagen || !imagen->cantidad_bundles ||
        imagen->cantidad_bundles > SIM_CARGADOR_IMEM_BUNDLES || imagen->entrada != 0) {
        sim->error.causa = "imagen nula, extensión de código inválida o entrada distinta de cero";
        sim->error.slot = 4;
        sim->error.indice_bundle = SIZE_MAX;
        return sim->estado;
    }
    sim->estado_crypto = SIM_CRYPTO_INIT;
    sim->pc = imagen->entrada;
    sim->estado = SIM_PIPE_ACTIVO;
    sim->inicializado = true;
    memcpy(sim->memoria.bytes, imagen->dmem, sizeof sim->memoria.bytes);
    return sim->estado;
}

/* Aplica el estado del reset RTL entre pasos y conserva la imagen prestada.
 * Descarta resultados pendientes y reinicia el conteo en cero; no contabiliza
 * un ciclo de ejecución. AUTH=0, INIT=1 y bóveda borrada. Conserva DMEM
 * y reproduce stores autorizados A/B que ya estaban en
 * EX al llegar el flanco de reset: memory.sv no inhibe we por reset.
 * Una excepción ya capturada sí inhibe ambos puertos.
 * Requiere una estructura previamente inicializada. */
SimPipelineEstado sim_pipeline_reiniciar(SimPipeline *sim)
{
    if (!sim) return SIM_PIPE_ARGUMENTO;
    if (!sim->inicializado || !sim->imagen) return SIM_PIPE_ARGUMENTO;
    /* Reset del procesador no borra DMEM. Un store en EX sigue habilitando
     * memory.we al llegar reset, aunque ID/EX se limpie por NBA en ese flanco.
     * Se prepara primero para no modificar memoria ante un estado inválido. */
    SimEfectoCrypto e = {0};
    SimAccesoLsu t = {0};
    if (sim->id_ex.valido &&
        !sim_crypto_preparar(&sim->boveda, &sim->memoria, &sim->id_ex.crypto, &e))
        return detener(sim, SIM_PIPE_CRYPTO, sim->id_ex.pc, 3, 0,
                       SIM_DECODE_OK, "estado CRIPTO inválido durante reset");
    if (sim->id_ex.valido && sim->id_ex.lsu.valido && !e.excepcion) {
        const SimLsuId *l = &sim->id_ex.lsu;
        if (!sim_lsu_preparar(&sim->memoria, l->operacion, l->base, l->dato, l->offset, &t))
            return detener(sim, SIM_PIPE_LSU, sim->id_ex.pc, 1, l->palabra,
                           SIM_DECODE_OK, "estado LSU inválido durante reset");
    }
    /* memory.sv carece de reset: camcon autorizado puede escribir en el
     * flanco que borra la bóveda. Una falta ya capturada sí inhibe A y B.
     * Ambas lecturas preceden estas escrituras; B gana incluso sobre guardab. */
    if (t.valido) sim_lsu_aplicar(&sim->memoria, &t);
    if (e.memoria.valido) sim_lsu_aplicar(&sim->memoria, &e.memoria);
    const SimImagen *imagen = sim->imagen;
    /* La política de pruebas es configuración, no un registro del procesador:
     * reset limpia el control BRU y sus enlaces, pero conserva este modo. */
    SimModoBusqueda modo = sim->modo_busqueda;
    /* memoria es el último miembro: limpiar el prefijo conserva sus 64 KiB.
     * Este mismo límite se usa al publicar un paso, sin copiar DMEM por ciclo. */
    memset(sim, 0, offsetof(SimPipeline, memoria));
    sim->imagen = imagen;
    sim->modo_busqueda = modo;
    sim->estado_crypto = SIM_CRYPTO_INIT;
    sim->inicializado = true;
    sim->estado = SIM_PIPE_ACTIVO;
    return sim->estado;
}

/* Recarga exclusivamente DMEM desde la imagen, sin reloj ni cambios al
 * procesador. Para repetir una prueba desde su estado original: reiniciar
 * primero (puede publicar el store de EX) y luego restaurar esta memoria. */
SimPipelineEstado sim_pipeline_restaurar_memoria(SimPipeline *sim)
{
    if (!sim || !sim->inicializado || !sim->imagen) return SIM_PIPE_ARGUMENTO;
    /* Es una recarga explícita, no un flanco ni un reset del banco. La imagen
     * prestada es la referencia original y permanece sin modificaciones. */
    memcpy(sim->memoria.bytes, sim->imagen->dmem, sizeof sim->memoria.bytes);
    return sim->estado;
}

/* Selecciona la política antes del primer paso de una instancia activa, o
 * después de reset. Conserva memoria y estado. Un modo inválido o un cambio
 * con ejecución iniciada devuelve ARGUMENTO sin modificar el procesador.
 * Reset conserva esta configuración administrativa; iniciar vuelve a FINITA. */
SimPipelineEstado sim_pipeline_configurar_busqueda(SimPipeline *sim, SimModoBusqueda modo)
{
    if (!sim || !sim->inicializado || sim->estado != SIM_PIPE_ACTIVO || sim->ciclos ||
        (modo != SIM_BUSQUEDA_FINITA && modo != SIM_BUSQUEDA_FISICA))
        return SIM_PIPE_ARGUMENTO;
    sim->modo_busqueda = modo;
    return sim->estado;
}

/* Una lectura de x0 siempre es cero; el índice 32 de S2 significa ausencia
 * y tampoco accede al arreglo. Solo ID utiliza esta función. */
static uint32_t leer_registro(const SimPipeline *sim, uint8_t indice)
{
    return indice == 0 || indice == SIM_REG_AUSENTE ? 0 : sim->registros[indice];
}

/* Realiza un flanco: calcula con el estado anterior y publica el siguiente
 * conjuntamente. Un rechazo congela PC, etapas, registros y contadores antes
 * del flanco y deja un diagnóstico; no es una excepción de hardware.
 * TERMINADO/errores son estables hasta reset; LIMITE admite reanudación. */
SimPipelineEstado sim_avanzar_ciclo(SimPipeline *sim)
{
    if (!sim || !sim->inicializado || !sim->imagen) return SIM_PIPE_ARGUMENTO;
    if (sim->estado != SIM_PIPE_ACTIVO && sim->estado != SIM_PIPE_LIMITE) return sim->estado;

    /* Solo se copia el prefijo de control: memoria es el último miembro y
     * nunca se lee desde siguiente. Los 64 KiB permanecen en sim; un store se
     * prepara como transacción y solo se aplica al aceptar todo el flanco.
     * ID sigue leyendo el banco anterior, como las asignaciones <= del RTL. */
    SimPipeline siguiente;
    memcpy(&siguiente, sim, offsetof(SimPipeline, memoria));
    SimCiclo ciclo = {.ciclo = sim->ciclos + 1, .id_rs1 = SIM_REG_AUSENTE,
        .id_rs2 = SIM_REG_AUSENTE, .wb_destino = SIM_REG_AUSENTE};
    siguiente.if_id = (SimIfId){0};
    siguiente.id_ex = (SimIdEx){0};
    siguiente.ex_wb = (SimExWb){0};

    /* EX se calcula antes de aceptar IF/ID. Esto no es forwarding: todos
     * los operandos vienen del estado anterior. Prioriza el redirect sobre
     * errores administrativos de un camino que el hardware va a descartar. */
    ciclo.ex_bru.destino = sim->id_ex.pc + 16u;
    ciclo.ex_bru.valor_enlace = sim->id_ex.pc + 16u;
    /* EX usa los operandos de ID/EX anterior. El resultado solo entra en el
     * nuevo EX/WB: ni ID ni el banco lo observan en este mismo flanco. */
    ciclo.ex = (SimEtapa){sim->id_ex.valido, sim->id_ex.pc};
    ciclo.estado_pre = sim->estado_crypto;
    if (sim->id_ex.valido &&
        !sim_crypto_preparar(&sim->boveda, &sim->memoria, &sim->id_ex.crypto, &ciclo.ex_crypto))
        return detener(sim, SIM_PIPE_CRYPTO, sim->id_ex.pc, 3,
            sim->id_ex.crypto.instruccion.palabra, SIM_DECODE_OK, "estado CRIPTO inconsistente en EX");
    ciclo.excepcion = ciclo.ex_crypto.excepcion;
    if (ciclo.excepcion) {
        /* Excepción arquitectónica en EX: se suprime todo su bundle, pero WB
         * anterior sigue vigente. La marca provino de ID, no del estado nuevo.
         * trap_count envuelve a 32 bits como el registro de top.sv. */
        ciclo.flush = true;
        siguiente.trap_pc = sim->id_ex.pc;
        siguiente.trap_count = sim->trap_count + UINT32_C(1);
        ++siguiente.bundles_anulados;
    }
    if (sim->id_ex.valido && !ciclo.excepcion) {
        const SimIdEx *ex = &sim->id_ex;
        uint32_t resultado = 0;
        if (ex->operacion != SIM_OP_NOP && !sim_alu_ejecutar(ex->operacion, ex->a, ex->b, &resultado))
            return detener(sim, SIM_PIPE_ALU, ex->pc, 0, ex->palabra,
                           SIM_DECODE_OK, "operación ALU inconsistente en ID/EX");
        siguiente.ex_wb = (SimExWb){.valido = true, .pc = ex->pc,
            .operacion = ex->operacion, .destino = ex->destino, .resultado = resultado};
        siguiente.ex_wb.crypto = ciclo.ex_crypto.wb;
        ciclo.ex_operacion = ex->operacion;
        ciclo.ex_a = ex->a; ciclo.ex_b = ex->b; ciclo.ex_resultado = resultado;
        if (ex->bru.valido) {
            const SimBruId *b = &ex->bru;
            ciclo.bru_ex_valido = true;
            if (!sim_bru_ejecutar(b->operacion, ex->pc, b->a, b->b, b->offset, &ciclo.ex_bru))
                return detener(sim, SIM_PIPE_BRU, ex->pc, 2, b->palabra,
                               SIM_DECODE_OK, "operación BRU inconsistente en ID/EX");
            ciclo.flush = ciclo.ex_bru.tomado;
            if (ciclo.ex_bru.enlace) {
                if (b->destino >= 32)
                    return detener(sim, SIM_PIPE_ARGUMENTO, ex->pc, 2, b->palabra,
                                   SIM_DECODE_OK, "destino BRU fuera del banco");
                siguiente.ex_wb.bru = (SimEnlacePendiente){.valida = true,
                    .destino = b->destino, .valor = ciclo.ex_bru.valor_enlace};
            }
        }
        if (ex->lsu.valido) {
            const SimLsuId *l = &ex->lsu;
            /* La lectura es combinacional y ve la memoria anterior al flanco.
             * El store no toca bytes aquí: queda en ciclo.ex_lsu hasta commit. */
            if (!sim_lsu_preparar(&sim->memoria, l->operacion, l->base, l->dato,
                                 l->offset, &ciclo.ex_lsu))
                return detener(sim, SIM_PIPE_LSU, ex->pc, 1, l->palabra,
                               SIM_DECODE_OK, "operación LSU inconsistente en ID/EX");
            if (ciclo.ex_lsu.carga) {
                if (l->destino >= 32)
                    return detener(sim, SIM_PIPE_ARGUMENTO, ex->pc, 1, l->palabra,
                                   SIM_DECODE_OK, "destino LSU fuera del banco");
                siguiente.ex_wb.lsu = (SimCargaPendiente){.valida = true,
                    .destino = l->destino, .valor = ciclo.ex_lsu.resultado};
            }
        }
    }

    ciclo.redirect_pc = ciclo.excepcion ? SIM_TRAP_VECTOR : ciclo.ex_bru.destino;

    /* IF representa la búsqueda especulativa previa al flanco. Con redirect
     * no se accede a IMEM ni se valida ese PC: IF/ID e ID/EX quedarán vacíos.
     * El destino se valida al intentar admitir su propia búsqueda posterior. */
    size_t fin = sim->imagen->cantidad_bundles * SIM_CARGADOR_BYTES_BUNDLE;
    bool fisica = sim->modo_busqueda == SIM_BUSQUEDA_FISICA;
    ciclo.if_.pc = sim->pc;
    ciclo.id = (SimEtapa){sim->if_id.valido, sim->if_id.pc};
    if (ciclo.flush) {
        ciclo.if_.valido = fisica || (!sim->fin_busqueda && sim->pc < fin && !(sim->pc & 15u));
        ciclo.if_descartado = ciclo.if_.valido;
        ciclo.id_descartado = sim->if_id.valido;
        siguiente.bundles_anulados += ciclo.id_descartado;
        /* Incluye saltos hacia atrás resueltos cuando IF ya había alcanzado
         * el final secuencial. El latch fin_busqueda no es un HALT. */
        siguiente.fin_busqueda = false;
    } else {
        if (!fisica && ((sim->pc & 15u) || (!sim->fin_busqueda && sim->pc > fin)))
            return detener(sim, SIM_PIPE_PC, sim->pc, 4, 0, SIM_DECODE_ARGUMENTO,
                           "PC no alineado o fuera del código del modo finito");
        if (!fisica && !sim->fin_busqueda && sim->pc == fin) siguiente.fin_busqueda = true;
        if (fisica || !siguiente.fin_busqueda) {
            size_t indice = fisica ? (sim->pc >> 4) & 1023u : sim->pc / 16u;
            siguiente.if_id.valido = true;
            siguiente.if_id.pc = sim->pc;
            /* El RTL inicializa a NOP fuera del código. No se leen palabras
             * no cargadas de una imagen construida manualmente por el cliente. */
            if (indice < sim->imagen->cantidad_bundles)
                for (unsigned slot = 0; slot < SIM_CARGADOR_SLOTS; ++slot)
                    siguiente.if_id.palabras[slot] = sim->imagen->imem[indice][slot];
            ciclo.if_.valido = true;
            ++siguiente.bundles_admitidos;
        }
    }

    /* ID consume exclusivamente el registro IF/ID anterior, nunca el bundle
     * que se acaba de buscar. Antes de capturar ALU se revisan los cuatro slots
     * para impedir ejecutar parcialmente un bundle con una codificación inválida. */
    if (sim->if_id.valido && !ciclo.flush) {
        SimInstruccion instrucciones[SIM_CARGADOR_SLOTS];
        sim_decode_bundle(sim->if_id.palabras, instrucciones);
        for (unsigned s = 0; s < SIM_CARGADOR_SLOTS; ++s) {
            SimInstruccion *i = &instrucciones[s];
            if (i->estado != SIM_DECODE_OK && i->estado != SIM_DECODE_NOP)
                return detener(sim, SIM_PIPE_DECODIFICACION, sim->if_id.pc, s,
                    i->palabra, i->estado, "codificación rechazada por S2");
            if (i->estado == SIM_DECODE_OK && !((s == 0 && i->familia == SIM_FAMILIA_ALU) ||
                                                (s == 1 && i->familia == SIM_FAMILIA_LSU) ||
                                                (s == 2 && i->familia == SIM_FAMILIA_BRU) ||
                                                (s == 3 && i->familia == SIM_FAMILIA_CRIPTO)))
                return detener(sim, SIM_PIPE_NO_SOPORTADA, sim->if_id.pc, s,
                    i->palabra, i->estado, "familia inconsistente con el slot");
        }
        SimInstruccion *i = &instrucciones[0];
        uint32_t a = leer_registro(sim, i->rs1);
        /* Convertir un inmediato negativo a uint32_t conserva su patrón módulo
         * 2^32. Los inmediatos lógicos también llegaron extendidos con signo. */
        uint32_t b = i->ancho_inmediato ? (uint32_t)i->inmediato : leer_registro(sim, i->rs2);
        siguiente.id_ex = (SimIdEx){.valido = true, .pc = sim->if_id.pc,
            .palabra = i->palabra, .operacion = i->operacion, .destino = i->rd, .a = a, .b = b};
        ciclo.id_operacion = i->operacion;
        ciclo.id_rs1 = i->rs1; ciclo.id_rs2 = i->rs2;
        ciclo.id_a = a; ciclo.id_b = b;
        SimInstruccion *l = &instrucciones[1];
        if (l->estado == SIM_DECODE_OK) {
            /* S2 conserva rs2 ausente para cargas. El decoder físico lee su
             * campo rd también por el segundo puerto; guardamos ese valor
             * inutilizado para comparar mem_wdata sin alterar la carga. */
            siguiente.id_ex.lsu = (SimLsuId){.valido = true, .operacion = l->operacion,
                .palabra = l->palabra, .destino = l->rd, .offset = l->inmediato,
                .base = leer_registro(sim, l->rs1),
                .dato = leer_registro(sim, l->rs2 == SIM_REG_AUSENTE ? l->rd : l->rs2)};
            ciclo.id_lsu = siguiente.id_ex.lsu;
        }
        /* BRU observa el mismo banco anterior que ALU y LSU. El PC original
         * ya está en siguiente.id_ex.pc; se guardan valores, no índices que
         * pudieran volver a leerse después de un WB del mismo flanco. */
        SimInstruccion *br = &instrucciones[2];
        if (br->estado == SIM_DECODE_OK) {
            siguiente.id_ex.bru = (SimBruId){.valido = true, .operacion = br->operacion,
                .palabra = br->palabra, .destino = br->rd, .offset = br->inmediato,
                .a = leer_registro(sim, br->rs1), .b = leer_registro(sim, br->rs2)};
            ciclo.id_bru = siguiente.id_ex.bru;
        }
        SimInstruccion *cr = &instrucciones[3];
        if (cr->estado == SIM_DECODE_OK) {
            /* Se captura el par fuente antes de WB. La autorización consulta
             * ESTADO anterior: no hay forwarding de vcr/setpwd en EX o WB. */
            if (!sim_crypto_capturar(cr, leer_registro(sim, cr->rs1),
                    leer_registro(sim, cr->rs2), sim->estado_crypto, &siguiente.id_ex.crypto))
                return detener(sim, SIM_PIPE_CRYPTO, sim->if_id.pc, 3, cr->palabra,
                               cr->estado, "captura CRIPTO inconsistente");
            ciclo.id_crypto = siguiente.id_ex.crypto;
        }
    }

    /* WB consume EX/WB anterior. Un NOP es bundle válido y se completa, pero
     * no solicita escritura. Una operación ALU hacia x0 calcula su resultado
     * y se completa, aunque se descarta la escritura como hace wb.sv. */
    ciclo.wb = (SimEtapa){sim->ex_wb.valido, sim->ex_wb.pc};
    if (sim->ex_wb.valido) {
        const SimExWb *wb = &sim->ex_wb;
        if ((wb->operacion != SIM_OP_NOP && wb->destino >= 32) ||
            (wb->lsu.valida && wb->lsu.destino >= 32) ||
            (wb->bru.valida && wb->bru.destino >= 32) ||
            (wb->crypto.par && (wb->crypto.rd >= 32 || wb->crypto.rd2 >= 32)))
            return detener(sim, SIM_PIPE_ARGUMENTO, wb->pc, 4, 0,
                           SIM_DECODE_ARGUMENTO, "destino WB fuera del banco");
        ciclo.wb_operacion = wb->operacion;
        ciclo.wb_destino = wb->destino;
        ciclo.wb_valor = wb->resultado;
        ciclo.wb_descarta_x0 = wb->operacion != SIM_OP_NOP && wb->destino == 0;
        ciclo.wb_escribe = wb->operacion != SIM_OP_NOP && wb->destino != 0;
        ++siguiente.bundles_completados;
        if (ciclo.wb_escribe) {
            siguiente.registros[wb->destino] = wb->resultado;
            siguiente.ultima_escritura = (SimEscritura){.valida = true,
                .ciclo = ciclo.ciclo, .pc = wb->pc, .valor = wb->resultado, .destino = wb->destino};
        }
        ciclo.wb_lsu = wb->lsu;
        ciclo.lsu_wb_escribe = wb->lsu.valida && wb->lsu.destino != 0;
        ciclo.lsu_wb_descarta_x0 = wb->lsu.valida && wb->lsu.destino == 0;
        ciclo.conflicto_wb = ciclo.wb_escribe && ciclo.lsu_wb_escribe &&
                            wb->destino == wb->lsu.destino;
        if (ciclo.lsu_wb_escribe) {
            /* Puerto 1 después del 0: LSU gana un conflicto, como regfile.sv.
             * Ambos puertos siguen observables; ultima_escritura conserva la
             * última del orden físico, sin cancelar ni reordenar operaciones. */
            siguiente.registros[wb->lsu.destino] = wb->lsu.valor;
            siguiente.ultima_escritura = (SimEscritura){.valida = true,
                .ciclo = ciclo.ciclo, .pc = wb->pc, .valor = wb->lsu.valor,
                .destino = wb->lsu.destino};
        }
        ciclo.wb_bru = wb->bru;
        ciclo.bru_wb_escribe = wb->bru.valida && wb->bru.destino != 0;
        ciclo.bru_wb_descarta_x0 = wb->bru.valida && wb->bru.destino == 0;
        ciclo.conflicto_wb |= ciclo.bru_wb_escribe &&
            ((ciclo.wb_escribe && wb->destino == wb->bru.destino) ||
             (ciclo.lsu_wb_escribe && wb->lsu.destino == wb->bru.destino));
        if (ciclo.bru_wb_escribe) {
            /* Puerto 2 es posterior a LSU/ALU: gana el enlace ante conflicto.
             * Los tres puertos permanecen visibles; x0 nunca los habilita. */
            siguiente.registros[wb->bru.destino] = wb->bru.valor;
            siguiente.ultima_escritura = (SimEscritura){.valida = true,
                .ciclo = ciclo.ciclo, .pc = wb->pc, .valor = wb->bru.valor,
                .destino = wb->bru.destino};
        }
        // La escritura de AUTH y el borrado de INIT pertenecen al WB anterior. ID ya capturó el
        // estado previo en este flanco: publicar estas marcas ahora no concede autorización
        // retroactiva a otra instrucción.
        ciclo.wb_crypto = wb->crypto;
        if (wb->crypto.auth_we)
            siguiente.estado_crypto = (siguiente.estado_crypto & ~UINT32_C(1)) | wb->crypto.auth;
        if (wb->crypto.init_clr) siguiente.estado_crypto &= ~UINT32_C(2);
        /* Puertos 3 y 4 después de ALU/LSU/BRU. Se mantienen todas las
         * solicitudes aunque colisionen; el último índice gana sin stalls. */
        const uint8_t destinos[2] = {wb->crypto.rd, wb->crypto.rd2};
        const uint32_t valores[2] = {wb->crypto.l, wb->crypto.r};
        for (unsigned p = 0; p < 2; ++p) {
            uint8_t rd = destinos[p];
            ciclo.crypto_wb_escribe[p] = wb->crypto.par && rd != 0;
            if (!ciclo.crypto_wb_escribe[p]) continue;
            ciclo.conflicto_wb |= (ciclo.wb_escribe && wb->destino == rd) ||
                (ciclo.lsu_wb_escribe && wb->lsu.destino == rd) ||
                (ciclo.bru_wb_escribe && wb->bru.destino == rd) ||
                (p == 1 && ciclo.crypto_wb_escribe[0] && destinos[0] == rd);
            siguiente.registros[rd] = valores[p];
            siguiente.ultima_escritura = (SimEscritura){.valida = true,
                .ciclo = ciclo.ciclo, .pc = wb->pc, .valor = valores[p], .destino = rd};
        }
    }
    ciclo.estado_post = siguiente.estado_crypto;
    ciclo.conflicto_memoria = ciclo.ex_lsu.escritura && ciclo.ex_crypto.memoria.escritura &&
        ciclo.ex_lsu.palabra_fisica == ciclo.ex_crypto.memoria.palabra_fisica;
    siguiente.registros[0] = 0;
    /* Redirect EX gana al incremento de IF. Solo se vaciaron IF/ID e ID/EX;
     * EX/WB conserva el salto y excluye el culpable de excepción; WB anterior
     * ya pudo completarse. */
    siguiente.pc = ciclo.flush ? ciclo.redirect_pc : sim->pc + SIM_CARGADOR_BYTES_BUNDLE;
    siguiente.ciclos = ciclo.ciclo;
    siguiente.ultimo_ciclo = ciclo;
    /* Solo el modo finito vacía por extensión. Si EX volvió al programa,
     * limpió fin_busqueda antes de esta decisión y no puede terminar aquí. */
    // El modo finito termina solo cuando ya no habrá búsquedas y las tres etapas quedaron
    // inválidas. Una etapa NOP válida aún debe retirarse: quitarla aquí alteraría el número de
    // bundles completados y ciclos.
    siguiente.estado = siguiente.fin_busqueda && !siguiente.if_id.valido &&
        !siguiente.id_ex.valido && !siguiente.ex_wb.valido ? SIM_PIPE_TERMINADO : SIM_PIPE_ACTIVO;
    /* Todas las validaciones ya pasaron. Publicar memoria ahora reproduce
     * sus NBA sin permitir efectos de un ciclo rechazado. La transacción fue
     * construida por la LSU; aun así aplicar valida antes del primer byte. */
    if (ciclo.ex_lsu.valido && !sim_lsu_aplicar(&sim->memoria, &ciclo.ex_lsu))
        return detener(sim, SIM_PIPE_LSU, sim->id_ex.pc, 1, sim->id_ex.lsu.palabra,
                       SIM_DECODE_OK, "transacción de memoria inválida");
    /* Los efectos de B provienen de preparar y sus límites ya están validados.
     * A se publica primero y B después, sin releer la palabra modificada por A.
     * La bóveda se cambia en el prefijo siguiente solo al confirmar el flanco. */
    if (ciclo.ex_crypto.memoria.valido)
        sim_lsu_aplicar(&sim->memoria, &ciclo.ex_crypto.memoria);
    sim_crypto_aplicar(&siguiente.boveda, &ciclo.ex_crypto);
    memcpy(sim, &siguiente, offsetof(SimPipeline, memoria));
    return sim->estado;
}

/* Ejecuta hasta max_ciclos adicionales mediante la API de un ciclo. Si agota
 * el presupuesto con trabajo pendiente informa LIMITE, sin inventar un flanco.
 * Se puede reanudar después; no hay HALT ni límite global implícito. */
SimPipelineEstado sim_pipeline_ejecutar(SimPipeline *sim, uint64_t max_ciclos)
{
    if (!sim || !sim->inicializado || !sim->imagen) return SIM_PIPE_ARGUMENTO;
    for (uint64_t n = 0; n < max_ciclos; ++n) {
        SimPipelineEstado estado = sim_avanzar_ciclo(sim);
        if (estado != SIM_PIPE_ACTIVO) return estado;
    }
    /* Agotar el presupuesto pausa la simulación sin avanzar tiempo ni retirar
     * más bundles. Errores y finalización previos conservan su estado. */
    if (sim->estado == SIM_PIPE_ACTIVO) sim->estado = SIM_PIPE_LIMITE;
    return sim->estado;
}
