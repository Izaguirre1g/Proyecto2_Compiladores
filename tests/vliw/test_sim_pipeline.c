/* Pruebas del avance de pipeline y sonda JSON de estados antes/después.
 * Las instrucciones construidas aquí son estímulos binarios de prueba. */
#include "sim_pipeline.h"
#include <stdlib.h>
#include <string.h>

static size_t comprobaciones;
#define COMPROBAR(c) do { ++comprobaciones; if (!(c)) { \
    fprintf(stderr, "FAIL pipeline línea %d: %s\n", __LINE__, #c); exit(1); } } while (0)

/* Formatos ya usados por los TB del Proyecto I. No forman un ensamblador:
 * solo construyen suma/sumai para inyectar dependencias deliberadamente malas. */
static uint32_t sumai(unsigned rd, unsigned rs1, int inmediato)
{
    return (0x40u << 25) | (rs1 << 16) | (rd << 11) | ((uint32_t)inmediato & 2047u);
}
/* Construye la palabra SUMA utilizada para probar capturas de operandos y publicación WB. */ static uint32_t suma(unsigned rd, unsigned rs1, unsigned rs2)
{
    return (0x6au << 25) | (8u << 21) | (rd << 16) | (rs1 << 11) | (rs2 << 6);
}
/* Exige el estado arquitectónico inicial y las etapas vacías tras reiniciar el pipeline. */
static void comprobar_reset(const SimPipeline *s)
{
    COMPROBAR(s->pc == 0 && s->ciclos == 0 && s->estado == SIM_PIPE_ACTIVO);
    COMPROBAR(!s->if_id.valido && !s->id_ex.valido && !s->ex_wb.valido);
    COMPROBAR(!s->ultima_escritura.valida && !s->fin_busqueda);
    COMPROBAR(s->bundles_admitidos == 0 && s->bundles_completados == 0);
    for (unsigned r = 0; r < 32; ++r) COMPROBAR(s->registros[r] == 0);
}
/* Separa productor y consumidor por distintas distancias y comprueba el valor realmente capturado sin forwarding. */
/* El consumidor lee en ID antes de publicar el WB del mismo flanco; los casos distinguen explícitamente N+1, N+2 y N+3. */ static void temporizacion_n_mas_3(void)
{
    /* B0 produce x2=7. B1 y B2 leen x2 antes de su escritura del flanco 4;
     * B3 lo lee en el 5. No hay NOP agregado, stalls ni forwarding. */
    SimImagen imagen = {.cantidad_bundles = 4};
    imagen.imem[0][0] = sumai(2, 0, 7);
    imagen.imem[1][0] = suma(5, 2, 0);
    imagen.imem[2][0] = suma(6, 2, 0);
    imagen.imem[3][0] = suma(7, 2, 0);
    SimPipeline s, otro;
    COMPROBAR(sim_pipeline_iniciar(&s, &imagen) == SIM_PIPE_ACTIVO);
    comprobar_reset(&s);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(s.ciclos == 1 && s.pc == 16 && s.if_id.valido && s.if_id.pc == 0);
    COMPROBAR(s.ultimo_ciclo.if_.valido && !s.ultimo_ciclo.id.valido);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(s.ultimo_ciclo.id.pc == 0 && s.id_ex.a == 0 && s.id_ex.b == 7);
    COMPROBAR(!s.ultimo_ciclo.ex.valido && s.registros[2] == 0);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(s.ultimo_ciclo.id.pc == 16 && s.ultimo_ciclo.id_a == 0);
    COMPROBAR(s.ultimo_ciclo.ex.pc == 0 && s.ex_wb.resultado == 7);
    COMPROBAR(s.registros[2] == 0 && !s.ultima_escritura.valida);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(s.ultimo_ciclo.id.pc == 32 && s.ultimo_ciclo.id_a == 0);
    COMPROBAR(s.ultimo_ciclo.wb.pc == 0 && s.ultimo_ciclo.wb_escribe);
    COMPROBAR(s.ultimo_ciclo.wb_valor == 7 && s.registros[2] == 7);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(s.ultimo_ciclo.id.pc == 48 && s.ultimo_ciclo.id_a == 7);
    /* Aunque x2 ya vale 7, B2 ejecuta con el cero capturado en ID del ciclo 4. */
    COMPROBAR(s.ultimo_ciclo.ex.pc == 32 && s.ultimo_ciclo.ex_a == 0);
    COMPROBAR(!s.ultimo_ciclo.if_.valido && s.fin_busqueda);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_TERMINADO);
    COMPROBAR(s.ciclos == 7 && s.bundles_admitidos == 4 && s.bundles_completados == 4);
    COMPROBAR(s.registros[5] == 0 && s.registros[6] == 0 && s.registros[7] == 7);
    COMPROBAR(s.ultima_escritura.ciclo == 7 && s.ultima_escritura.pc == 48);
    COMPROBAR(!s.if_id.valido && !s.id_ex.valido && !s.ex_wb.valido);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_TERMINADO && s.ciclos == 7);
    COMPROBAR(sim_pipeline_iniciar(&otro, &imagen) == SIM_PIPE_ACTIVO);
    COMPROBAR(sim_avanzar_ciclo(&otro) == SIM_PIPE_ACTIVO);
    COMPROBAR(otro.registros[2] == 0 && s.registros[2] == 7);
}
/* Comprueba bundles NOP, reinicio y agotamiento del presupuesto sin confundir sus estados. */
static void nop_reset_y_limite(void)
{
    SimImagen imagen = {.cantidad_bundles = 3};
    imagen.imem[0][0] = sumai(0, 0, 99);
    imagen.imem[2][0] = sumai(31, 0, 13);
    SimPipeline s;
    sim_pipeline_iniciar(&s, &imagen);
    COMPROBAR(sim_pipeline_ejecutar(&s, 0) == SIM_PIPE_LIMITE && s.ciclos == 0);
    COMPROBAR(sim_pipeline_ejecutar(&s, 3) == SIM_PIPE_LIMITE && s.ciclos == 3);
    COMPROBAR(s.registros[31] == 0);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(s.ultimo_ciclo.wb_descarta_x0 && !s.ultimo_ciclo.wb_escribe);
    COMPROBAR(s.ultimo_ciclo.wb_valor == 99 && s.registros[0] == 0);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_ACTIVO);
    COMPROBAR(s.ultimo_ciclo.wb.valido && s.ultimo_ciclo.wb_operacion == SIM_OP_NOP);
    COMPROBAR(!s.ultimo_ciclo.wb_escribe && s.bundles_completados == 2);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_TERMINADO && s.registros[31] == 13);
    COMPROBAR(s.ultima_escritura.destino == 31 && s.ultima_escritura.ciclo == 6);
    for (unsigned r = 1; r < 31; ++r) COMPROBAR(s.registros[r] == 0);
    COMPROBAR(sim_pipeline_reiniciar(&s) == SIM_PIPE_ACTIVO);
    comprobar_reset(&s);
    /* Reset antes de WB debe eliminar un resultado pendiente real hacia x31. */
    imagen.imem[0][0] = sumai(31, 0, 13);
    sim_pipeline_ejecutar(&s, 3);
    COMPROBAR(s.ex_wb.valido && s.ex_wb.resultado == 13 && s.registros[31] == 0);
    sim_pipeline_reiniciar(&s);
    comprobar_reset(&s);
    COMPROBAR(sim_pipeline_ejecutar(&s, 6) == SIM_PIPE_TERMINADO);
    COMPROBAR(s.registros[31] == 13 && s.ciclos == 6);
}
/* Encadena operaciones y verifica el orden de escritura y la disponibilidad de sus resultados. */
static void escrituras_y_cadenas(void)
{
    /* Tres productores consecutivos escriben x4 en ciclos 4/5/6. Los lectores
     * B3/B4/B5 capturan 1/2/3 en ID de ciclos 5/6/7, respectivamente. */
    SimImagen imagen = {.cantidad_bundles = 6};
    for (unsigned b = 0; b < 3; ++b) imagen.imem[b][0] = sumai(4, 0, (int)b + 1);
    for (unsigned b = 3; b < 6; ++b) imagen.imem[b][0] = suma(b + 2, 4, 0);
    SimPipeline s;
    sim_pipeline_iniciar(&s, &imagen);
    /* Cada ciclo comprueba qué versión de x4 leyó ID antes de publicar WB. */ for (unsigned c = 1; c <= 9; ++c) {
        sim_avanzar_ciclo(&s);
        if (c >= 4 && c <= 6) COMPROBAR(s.ultimo_ciclo.wb_valor == c - 3);
        if (c >= 5 && c <= 7) COMPROBAR(s.ultimo_ciclo.id_a == c - 4);
    }
    COMPROBAR(s.registros[4] == 3 && s.registros[5] == 1 && s.registros[6] == 2 && s.registros[7] == 3);
    /* La cadena mezcla productores: B4 ve x2 antiguo; B6 ve x2 nuevo pero
     * x3 antiguo. Solo B7 ya tiene disponibles ambos resultados. */
    imagen = (SimImagen){.cantidad_bundles = 8};
    imagen.imem[0][0] = sumai(1, 0, 9);
    imagen.imem[3][0] = sumai(2, 1, 4);
    imagen.imem[4][0] = suma(3, 2, 1);
    imagen.imem[6][0] = suma(4, 2, 3);
    imagen.imem[7][0] = suma(5, 2, 3);
    sim_pipeline_iniciar(&s, &imagen);
    COMPROBAR(sim_pipeline_ejecutar(&s, 11) == SIM_PIPE_TERMINADO);
    COMPROBAR(s.registros[2] == 13 && s.registros[3] == 9 && s.registros[4] == 13 && s.registros[5] == 22);
}
/* Comprueba argumentos y estados rechazados y el drenado al terminar una imagen finita. */
static void rechazos_y_final(void)
{
    SimPipeline s;
    SimImagen imagen = {0};
    COMPROBAR(sim_pipeline_iniciar(NULL, &imagen) == SIM_PIPE_ARGUMENTO);
    COMPROBAR(sim_pipeline_iniciar(&s, NULL) == SIM_PIPE_ARGUMENTO);
    COMPROBAR(sim_pipeline_iniciar(&s, &imagen) == SIM_PIPE_ARGUMENTO);
    imagen.cantidad_bundles = 1025;
    COMPROBAR(sim_pipeline_iniciar(&s, &imagen) == SIM_PIPE_ARGUMENTO);
    imagen.cantidad_bundles = 1; imagen.entrada = 16;
    COMPROBAR(sim_pipeline_iniciar(&s, &imagen) == SIM_PIPE_ARGUMENTO);
    COMPROBAR(sim_avanzar_ciclo(NULL) == SIM_PIPE_ARGUMENTO);
    COMPROBAR(sim_pipeline_reiniciar(NULL) == SIM_PIPE_ARGUMENTO);
    COMPROBAR(sim_pipeline_ejecutar(NULL, 1) == SIM_PIPE_ARGUMENTO);
    imagen.entrada = 0;
    /* Un PC no admisible debe rechazar el intento sin sumar ciclos. */ for (unsigned pc = 3; pc <= 32; pc += 29) {
        sim_pipeline_iniciar(&s, &imagen);
        /* Inyección de PC: uno desalineado y otro por encima de la extensión
         * antes de alcanzar su final secuencial. No debe haber acceso a IMEM. */
        s.pc = pc;
        COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_PC && s.ciclos == 0);
        COMPROBAR(!s.error.tiene_instruccion && s.error.pc == pc);
    }
    const struct { unsigned slot; uint32_t word; SimPipelineEstado estado; SimDecodeEstado dec; } casos[] = {
        {0, 1, SIM_PIPE_DECODIFICACION, SIM_DECODE_RESERVADA},
        {1, 0x80042807u, SIM_PIPE_DECODIFICACION, SIM_DECODE_SLOT},
        {3, 0x04001000u, SIM_PIPE_DECODIFICACION, SIM_DECODE_CAMPOS},
        /* S6 admite CRIPTO: IDs reservados conservan estos rechazos. */
        {3, 0x04c00000u, SIM_PIPE_DECODIFICACION, SIM_DECODE_RESERVADA},
        {3, 0x04e00000u, SIM_PIPE_DECODIFICACION, SIM_DECODE_RESERVADA},
        {3, 0x05004200u, SIM_PIPE_DECODIFICACION, SIM_DECODE_RESERVADA}
    };
    for (size_t n = 0; n < sizeof casos / sizeof *casos; ++n) {
        imagen = (SimImagen){.cantidad_bundles = 1};
        imagen.imem[0][0] = sumai(1, 0, 5);
        imagen.imem[0][casos[n].slot] = casos[n].word;
        sim_pipeline_iniciar(&s, &imagen);
        sim_avanzar_ciclo(&s);
        COMPROBAR(sim_avanzar_ciclo(&s) == casos[n].estado);
        COMPROBAR(s.ciclos == 1 && s.registros[1] == 0 && !s.id_ex.valido);
        COMPROBAR(s.error.pc == 0 && s.error.indice_bundle == 0 && s.error.slot == casos[n].slot);
        COMPROBAR(s.error.palabra == casos[n].word && s.error.decodificacion == casos[n].dec);
        COMPROBAR(s.error.causa != NULL && s.error.ciclo == 2);
        COMPROBAR(sim_avanzar_ciclo(&s) == casos[n].estado && s.ciclos == 1);
    }
    /* Rechazo en ID cuando un bundle viejo está por escribir. El control de
     * depuración detiene TODO el flanco, no retira ni ejecuta partes a escondidas. */
    imagen = (SimImagen){.cantidad_bundles = 3};
    imagen.imem[0][0] = sumai(1, 0, 7);
    imagen.imem[2][0] = sumai(2, 0, 5);
    /* Un ID CRIPTO reservado mantiene el escenario administrativo de rechazo
     * en ID coincidente con un WB anterior pendiente, sin cambiar aserciones. */
    imagen.imem[2][3] = 0x04c00000u;
    sim_pipeline_iniciar(&s, &imagen);
    sim_pipeline_ejecutar(&s, 3);
    COMPROBAR(s.ex_wb.valido && s.ex_wb.resultado == 7);
    COMPROBAR(sim_avanzar_ciclo(&s) == SIM_PIPE_DECODIFICACION);
    COMPROBAR(s.ciclos == 3 && s.registros[1] == 0 && s.ex_wb.valido);
    COMPROBAR(s.error.pc == 32 && s.error.ciclo == 4 && s.ultimo_ciclo.ciclo == 3);
    /* Código completo en IMEM: al llegar al final no se hace wraparound como
     * la memoria física. Se drenan tres ciclos sin volver a admitir B0. */
    imagen = (SimImagen){.cantidad_bundles = 1024};
    sim_pipeline_iniciar(&s, &imagen);
    COMPROBAR(sim_pipeline_ejecutar(&s, 1027) == SIM_PIPE_TERMINADO);
    COMPROBAR(s.bundles_admitidos == 1024 && s.bundles_completados == 1024);
}

/* La sonda muestra la ocupación previa y los registros posteriores al flanco.
 * Python usa estos datos para compararlos con las sondas de top sin alterar RTL. */
static void mostrar(const SimPipeline *s)
{
    const SimCiclo *c = &s->ultimo_ciclo;
    printf("{\"tipo\":\"ciclo\",\"ciclo\":%llu,\"pc\":%u,"
        "\"if\":%u,\"if_pc\":%u,\"id\":%u,\"id_pc\":%u,\"ex\":%u,\"ex_pc\":%u,\"wb\":%u,\"wb_pc\":%u,"
        "\"id_rs1\":%u,\"id_rs2\":%u,\"id_a\":%u,\"id_b\":%u,"
        "\"ex_op\":\"%s\",\"ex_a\":%u,\"ex_b\":%u,\"ex_resultado\":%u,"
        "\"wb_rd\":%u,\"wb_valor\":%u,\"wb_escribe\":%u,\"wb_x0\":%u,\"registros\":[",
        (unsigned long long)c->ciclo, (unsigned)s->pc,
        c->if_.valido, c->if_.pc, c->id.valido, c->id.pc, c->ex.valido, c->ex.pc, c->wb.valido, c->wb.pc,
        c->id_rs1, c->id_rs2, c->id_a, c->id_b, sim_operacion_nombre(c->ex_operacion),
        c->ex_a, c->ex_b, c->ex_resultado, c->wb_destino, c->wb_valor, c->wb_escribe, c->wb_descarta_x0);
    for (unsigned r = 0; r < 32; ++r) printf("%s%u", r ? "," : "", s->registros[r]);
    printf("]}\n");
}
/* Selecciona el escenario autoverificable o el modo de sonda solicitado; sus códigos y salidas son comprobados por la suite Python. */
int main(int argc, char **argv)
{
    if (argc == 1) {
        temporizacion_n_mas_3(); nop_reset_y_limite(); escrituras_y_cadenas(); rechazos_y_final();
        printf("OK: %zu comprobaciones de pipeline\n", comprobaciones);
        return 0;
    }
    if (argc != 4 && argc != 5) return 2;
    SimImagen *imagen = NULL;
    SimCargaEstado carga = strcmp(argv[1], "mem") == 0
        ? sim_cargar_mem(argv[2], &imagen, stderr) : sim_cargar_balb(argv[2], &imagen, stderr);
    if (carga != SIM_CARGA_OK) return 1;
    SimPipeline s;
    sim_pipeline_iniciar(&s, imagen);
    uint64_t limite = strtoull(argv[3], NULL, 10);
    uint64_t reset_despues = argc == 5 ? strtoull(argv[4], NULL, 10) : 0;
    /* Solo imprimimos flancos aceptados; TERMINADO puede incluir el último. */ for (uint64_t n = 0; n < limite && s.estado == SIM_PIPE_ACTIVO; ++n) {
        /* El reset de prueba ocurre entre pasos; descarta el resultado que
         * habría escrito el próximo flanco, igual que reset prioritario RTL. */
        if (reset_despues && n == reset_despues) sim_pipeline_reiniciar(&s);
        sim_avanzar_ciclo(&s);
        if (s.estado == SIM_PIPE_ACTIVO || s.estado == SIM_PIPE_TERMINADO) mostrar(&s);
    }
    sim_pipeline_ejecutar(&s, 0);
    if (s.estado != SIM_PIPE_TERMINADO) {
        fprintf(stderr, "S3 estado=%u PC=%u bundle=%zu slot=%u palabra=%08x S2=%u causa=%s\n",
            (unsigned)s.estado, s.error.pc, s.error.indice_bundle, s.error.slot,
            s.error.palabra, (unsigned)s.error.decodificacion, s.error.causa ? s.error.causa : "límite de ciclos");
    }
    sim_liberar_imagen(&imagen);
    return s.estado == SIM_PIPE_TERMINADO ? 0 : 1;
}
