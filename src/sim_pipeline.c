/* Avance simultáneo del pipeline VLIW con el subconjunto ALU/NOP.
 * Las lecturas de ID preceden a la publicación de escrituras WB del flanco. */
#include "sim_pipeline.h"
#include "sim_alu.h"

/* Los errores detienen el intento antes del flanco. Se conserva todo estado
 * arquitectónico previo y se publica únicamente la causa de la detención. */
static SimPipelineEstado detener(SimPipeline *sim, SimPipelineEstado estado,
                                 uint32_t pc, unsigned slot, uint32_t palabra,
                                 SimDecodeEstado dec, const char *causa)
{
    sim->estado = estado;
    sim->error = (SimErrorPipeline){.ciclo = sim->ciclos + 1, .pc = pc,
        .palabra = palabra, .slot = slot, .tiene_instruccion = slot < 4,
        .indice_bundle = slot < 4 ? pc / SIM_CARGADOR_BYTES_BUNDLE : SIZE_MAX,
        .decodificacion = dec, .causa = causa};
    return estado;
}

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
    sim->pc = imagen->entrada;
    sim->estado = SIM_PIPE_ACTIVO;
    return sim->estado;
}

SimPipelineEstado sim_pipeline_reiniciar(SimPipeline *sim)
{
    if (!sim) return SIM_PIPE_ARGUMENTO;
    /* Se toma la referencia antes de limpiar el estado. No se libera imagen:
     * sigue perteneciendo al usuario que la cargó mediante S1. */
    const SimImagen *imagen = sim->imagen;
    return sim_pipeline_iniciar(sim, imagen);
}

/* Una lectura de x0 siempre es cero; el índice 32 de S2 significa ausencia
 * y tampoco accede al arreglo. Solo ID utiliza esta función. */
static uint32_t leer_registro(const SimPipeline *sim, uint8_t indice)
{
    return indice == 0 || indice == SIM_REG_AUSENTE ? 0 : sim->registros[indice];
}

SimPipelineEstado sim_avanzar_ciclo(SimPipeline *sim)
{
    if (!sim) return SIM_PIPE_ARGUMENTO;
    if (sim->estado != SIM_PIPE_ACTIVO && sim->estado != SIM_PIPE_LIMITE) return sim->estado;

    /* siguiente es una copia pequeña del estado, no de las memorias. Todos
     * los cálculos leen sim; ninguna escritura de siguiente es visible a ID
     * hasta la asignación final. Esto reproduce las asignaciones <= del RTL. */
    SimPipeline siguiente = *sim;
    SimCiclo ciclo = {.ciclo = sim->ciclos + 1, .id_rs1 = SIM_REG_AUSENTE,
        .id_rs2 = SIM_REG_AUSENTE, .wb_destino = SIM_REG_AUSENTE};
    siguiente.if_id = (SimIfId){0};
    siguiente.id_ex = (SimIdEx){0};
    siguiente.ex_wb = (SimExWb){0};

    /* IF: el PC es una dirección de bytes, no un índice ni un número de ciclo.
     * El final exacto no es un error: activa el drenado. Un salto por encima
     * de ese final antes de drenarse o un PC no alineado sí son inválidos. */
    size_t fin = sim->imagen->cantidad_bundles * SIM_CARGADOR_BYTES_BUNDLE;
    if (sim->pc % SIM_CARGADOR_BYTES_BUNDLE || (!sim->fin_busqueda && sim->pc > fin))
        return detener(sim, SIM_PIPE_PC, sim->pc, 4, 0, SIM_DECODE_ARGUMENTO,
                       "PC no alineado o fuera del código cargado antes del final secuencial");
    ciclo.if_.pc = sim->pc;
    if (!sim->fin_busqueda && sim->pc == fin) siguiente.fin_busqueda = true;
    if (!siguiente.fin_busqueda) {
        size_t indice = sim->pc / SIM_CARGADOR_BYTES_BUNDLE;
        siguiente.if_id.valido = true;
        siguiente.if_id.pc = sim->pc;
        for (unsigned s = 0; s < SIM_CARGADOR_SLOTS; ++s)
            siguiente.if_id.palabras[s] = sim->imagen->imem[indice][s];
        ciclo.if_.valido = true;
        ++siguiente.bundles_admitidos;
    }

    /* ID consume exclusivamente el registro IF/ID anterior, nunca el bundle
     * que se acaba de buscar. Antes de capturar ALU se revisan los cuatro slots
     * para impedir ejecutar parcialmente un bundle fuera del alcance de S3. */
    ciclo.id = (SimEtapa){sim->if_id.valido, sim->if_id.pc};
    if (sim->if_id.valido) {
        SimInstruccion instrucciones[SIM_CARGADOR_SLOTS];
        sim_decode_bundle(sim->if_id.palabras, instrucciones);
        for (unsigned s = 0; s < SIM_CARGADOR_SLOTS; ++s) {
            SimInstruccion *i = &instrucciones[s];
            if (i->estado != SIM_DECODE_OK && i->estado != SIM_DECODE_NOP)
                return detener(sim, SIM_PIPE_DECODIFICACION, sim->if_id.pc, s,
                    i->palabra, i->estado, "codificación rechazada por S2");
            if (i->estado == SIM_DECODE_OK && (s != 0 || i->familia != SIM_FAMILIA_ALU))
                return detener(sim, SIM_PIPE_NO_SOPORTADA, sim->if_id.pc, s,
                    i->palabra, i->estado, "operación LSU, BRU o CRIPTO todavía no soportada en S3");
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
    }

    /* EX usa los operandos de ID/EX anterior. El resultado solo entra en el
     * nuevo EX/WB: ni ID ni el banco lo observan en este mismo flanco. */
    ciclo.ex = (SimEtapa){sim->id_ex.valido, sim->id_ex.pc};
    if (sim->id_ex.valido) {
        const SimIdEx *ex = &sim->id_ex;
        uint32_t resultado = 0;
        if (ex->operacion != SIM_OP_NOP && !sim_alu_ejecutar(ex->operacion, ex->a, ex->b, &resultado))
            return detener(sim, SIM_PIPE_ALU, ex->pc, 0, ex->palabra,
                           SIM_DECODE_OK, "operación ALU inconsistente en ID/EX");
        siguiente.ex_wb = (SimExWb){.valido = true, .pc = ex->pc,
            .operacion = ex->operacion, .destino = ex->destino, .resultado = resultado};
        ciclo.ex_operacion = ex->operacion;
        ciclo.ex_a = ex->a; ciclo.ex_b = ex->b; ciclo.ex_resultado = resultado;
    }

    /* WB consume EX/WB anterior. Un NOP es bundle válido y se completa, pero
     * no solicita escritura. Una operación ALU hacia x0 calcula su resultado
     * y se completa, aunque se descarta la escritura como hace wb.sv. */
    ciclo.wb = (SimEtapa){sim->ex_wb.valido, sim->ex_wb.pc};
    if (sim->ex_wb.valido) {
        const SimExWb *wb = &sim->ex_wb;
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
    }
    siguiente.registros[0] = 0;
    siguiente.pc = sim->pc + SIM_CARGADOR_BYTES_BUNDLE;
    siguiente.ciclos = ciclo.ciclo;
    siguiente.ultimo_ciclo = ciclo;
    siguiente.estado = siguiente.fin_busqueda && !siguiente.if_id.valido &&
        !siguiente.id_ex.valido && !siguiente.ex_wb.valido ? SIM_PIPE_TERMINADO : SIM_PIPE_ACTIVO;
    *sim = siguiente;
    return sim->estado;
}

SimPipelineEstado sim_pipeline_ejecutar(SimPipeline *sim, uint64_t max_ciclos)
{
    if (!sim) return SIM_PIPE_ARGUMENTO;
    for (uint64_t n = 0; n < max_ciclos; ++n) {
        SimPipelineEstado estado = sim_avanzar_ciclo(sim);
        if (estado != SIM_PIPE_ACTIVO) return estado;
    }
    /* Agotar el presupuesto pausa la simulación sin avanzar tiempo ni retirar
     * más bundles. Errores y finalización previos conservan su estado. */
    if (sim->estado == SIM_PIPE_ACTIVO) sim->estado = SIM_PIPE_LIMITE;
    return sim->estado;
}
