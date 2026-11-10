/* Interpreta las 34 operaciones ISA del procesador VLIW del Proyecto I.
 * Extrae campos y valida su codificación sin simular su ejecución. */
#include "sim_decode.h"

/* Correspondencia inversa de isa_defs.sv y codificacion_binaria.c. No puede
 * reutilizarse su tabla privada sin modificar el codificador aprobado.
 * Cada fila fija tipo de 7 bits, ID de 4 bits, slot y formato de operandos. */
typedef struct {
    uint8_t tipo, id, slot;
    SimOperacion operacion;
    SimFormato formato;
    const char *nombre;
} Especificacion;

static const Especificacion instrucciones[] = {
    {0x6a, 8, 0, SIM_OP_SUMA, SIM_FMT_REG, "suma"},
    {0x6a, 9, 0, SIM_OP_RESTA, SIM_FMT_REG, "resta"},
    {0x6a, 10, 0, SIM_OP_CADER, SIM_FMT_REG, "cader"},
    {0x6a, 11, 0, SIM_OP_CIZQ, SIM_FMT_REG, "cizq"},
    {0x6a, 12, 0, SIM_OP_AND, SIM_FMT_REG, "and"},
    {0x6a, 13, 0, SIM_OP_OR, SIM_FMT_REG, "or"},
    {0x6a, 14, 0, SIM_OP_XOR, SIM_FMT_REG, "xor"},
    {0x6a, 15, 0, SIM_OP_CDER, SIM_FMT_REG, "cder"},
    {0x6a, 5, 0, SIM_OP_MRQ, SIM_FMT_REG, "mrq"},
    {0x6a, 6, 0, SIM_OP_MYQ, SIM_FMT_REG, "myq"},
    {0x40, 0, 0, SIM_OP_SUMAI, SIM_FMT_IMM, "sumai"},
    {0x40, 1, 0, SIM_OP_RESTAI, SIM_FMT_IMM, "restai"},
    {0x40, 2, 0, SIM_OP_CIZQI, SIM_FMT_IMM, "cizqi"},
    {0x40, 3, 0, SIM_OP_CDERI, SIM_FMT_IMM, "cderi"},
    {0x40, 4, 0, SIM_OP_CADERI, SIM_FMT_IMM, "caderi"},
    {0x40, 5, 0, SIM_OP_XORI, SIM_FMT_IMM, "xori"},
    {0x40, 6, 0, SIM_OP_ANDI, SIM_FMT_IMM, "andi"},
    {0x40, 7, 0, SIM_OP_ORI, SIM_FMT_IMM, "ori"},
    {0x49, 0, 1, SIM_OP_GUARDAP, SIM_FMT_LSU, "guardap"},
    {0x49, 1, 1, SIM_OP_GUARDAB, SIM_FMT_LSU, "guardab"},
    {0x49, 8, 1, SIM_OP_CARGAI, SIM_FMT_LSU, "cargai"},
    {0x49, 9, 1, SIM_OP_CARGABAI, SIM_FMT_LSU, "cargabai"},
    {0x41, 1, 2, SIM_OP_IGUALSI, SIM_FMT_COND, "igualsi"},
    {0x41, 2, 2, SIM_OP_IGUALNO, SIM_FMT_COND, "igualno"},
    {0x41, 4, 2, SIM_OP_MENORA, SIM_FMT_COND, "menora"},
    {0x41, 8, 2, SIM_OP_MAYOROIGUAL, SIM_FMT_COND, "mayoroigual"},
    {0x4b, 0, 2, SIM_OP_SYE, SIM_FMT_SYE, "sye"},
    {0x4b, 1, 2, SIM_OP_SYER, SIM_FMT_SYER, "syer"},
    {0x02, 0, 3, SIM_OP_FSL, SIM_FMT_FEISTEL, "fsl"},
    {0x02, 1, 3, SIM_OP_FSLI, SIM_FMT_FEISTEL, "fsli"},
    {0x02, 2, 3, SIM_OP_ELL, SIM_FMT_ELL, "ell"},
    {0x02, 3, 3, SIM_OP_VCR, SIM_FMT_VCR, "vcr"},
    {0x02, 4, 3, SIM_OP_CAMCON, SIM_FMT_CAMCON, "camcon"},
    {0x02, 5, 3, SIM_OP_SETPWD, SIM_FMT_SETPWD, "setpwd"}
};

/* Construye una salida segura antes de reconocer la operación. Conserva la
 * palabra para diagnóstico, pero no inventa x0 como fuente o destino ausente. */
static SimInstruccion vacia(uint32_t palabra, unsigned slot)
{
    SimInstruccion i = {0};
    i.palabra = palabra;
    i.slot = slot;
    /* Al bajar los bits altos y limitar a 7/4 bits se separan tipo e ID;
     * los operandos restantes no pueden contaminar estas comparaciones. */
    i.tipo = (uint8_t)((palabra >> 25) & 0x7fu);
    i.id = (uint8_t)((palabra >> 21) & 0x0fu);
    i.estado = SIM_DECODE_RESERVADA;
    i.rd = i.rd2 = i.rs1 = i.rs2 = SIM_REG_AUSENTE;
    i.lk = i.rk = i.off_boveda = i.corrimiento = SIM_CAMPO_AUSENTE;
    return i;
}

/* bit es la posición menos significativa del campo de registro. Desplazar
 * hasta ella y aplicar 0x1f conserva exactamente cinco bits: índices 0..31. */
static uint8_t registro(uint32_t palabra, unsigned bit)
{
    return (uint8_t)((palabra >> bit) & 0x1fu);
}

/* Obtiene el inmediato bajo de 5, 11 o 16 bits. La máscara elimina los campos
 * de registro que están encima. Con signo se resta el peso del bit alto y se
 * suman los bits bajos: evita convertir un uint32_t fuera del rango de int32_t. */
static void inmediato(SimInstruccion *i, unsigned ancho, bool con_signo)
{
    uint32_t signo = 1u << (ancho - 1);
    uint32_t bruto = i->palabra & ((1u << ancho) - 1u);
    i->inmediato_bruto = bruto;
    i->ancho_inmediato = (uint8_t)ancho;
    i->inmediato_con_signo = con_signo;
    i->inmediato = con_signo ? (int32_t)(bruto & (signo - 1u)) - (int32_t)(bruto & signo)
                             : (int32_t)bruto;
}

/* Extrae operandos de una operación ya reconocida. Devuelve false cuando
 * infringe restricciones estáticas del ensamblador, que RTL no siempre revisa.
 * No comprueba valores del banco, destinos PC ni privilegios dinámicos. */
static bool operandos(SimInstruccion *i)
{
    switch (i->formato) {
    case SIM_FMT_REG:
        /* REG: rd[20:16], rs1[15:11], rs2[10:6]. Los seis bits libres
         * inferiores se ignoran, tal como hace decoder_alu. */
        i->rd = registro(i->palabra, 16);
        i->rs1 = registro(i->palabra, 11);
        i->rs2 = registro(i->palabra, 6);
        break;
    case SIM_FMT_IMM:
        /* IMM intercambia las posiciones frente a REG: fuente [20:16],
         * destino [15:11]. top extiende a signo los 11 bits en todos los casos. */
        i->rs1 = registro(i->palabra, 16);
        i->rd = registro(i->palabra, 11);
        inmediato(i, 11, true);
        if (i->operacion == SIM_OP_CIZQI || i->operacion == SIM_OP_CDERI ||
            i->operacion == SIM_OP_CADERI) {
            /* La ALU utiliza solo operand_b[4:0] para desplazar. Se conserva
             * también el inmediato completo: 2047 se extiende a -1, pero su
             * cantidad efectiva de corrimiento sigue siendo 31. */
            i->corrimiento = (uint8_t)(i->inmediato_bruto & 0x1fu);
        }
        break;
    case SIM_FMT_LSU:
        i->rs1 = registro(i->palabra, 16);
        /* [15:11] es destino en loads o fuente de datos en stores, nunca ambos.
         * Aunque RTL cablea las dos salidas, solo una se consume por opcode. */
        if (i->operacion == SIM_OP_CARGAI || i->operacion == SIM_OP_CARGABAI)
            i->rd = registro(i->palabra, 11);
        else i->rs2 = registro(i->palabra, 11);
        inmediato(i, 11, true);
        break;
    case SIM_FMT_COND:
        i->rs1 = registro(i->palabra, 16);
        i->rs2 = registro(i->palabra, 11);
        inmediato(i, 11, true);
        i->salto = SIM_SALTO_COND_PC;
        break;
    case SIM_FMT_SYE:
        i->rd = registro(i->palabra, 16);
        inmediato(i, 16, true);
        i->salto = SIM_SALTO_PC;
        break;
    case SIM_FMT_SYER:
        i->rd = registro(i->palabra, 16);
        i->rs1 = registro(i->palabra, 11);
        inmediato(i, 11, true);
        /* La base se leerá del registro durante ejecución; aquí no se calcula
         * su destino ni se exige alineación del offset de syer. */
        i->salto = SIM_SALTO_REGISTRO;
        break;
    case SIM_FMT_FEISTEL:
    case SIM_FMT_ELL:
        /* Los campos compartidos cambian de significado: [16:12] es rd
         * en Feistel y rs1 en ell; [11:7] es inicio del par o segunda fuente. */
        i->lk = (uint8_t)((i->palabra >> 19) & 3u);
        i->requiere_auth = true;
        if (i->formato == SIM_FMT_FEISTEL) {
            i->rk = (uint8_t)((i->palabra >> 17) & 3u);
            i->rd = registro(i->palabra, 12);
            i->rs1 = registro(i->palabra, 7);
            /* Pares desde x2 hasta x30: se excluyen x0 y los índices impares.
             * Solo después se derivan los segundos índices para evitar x32. */
            if (i->rd < 2 || i->rs1 < 2 || (i->rd & 1u) || (i->rs1 & 1u)) return false;
            i->rd2 = (uint8_t)(i->rd + 1);
            i->rs2 = (uint8_t)(i->rs1 + 1);
        } else {
            i->off_boveda = (uint8_t)((i->palabra >> 17) & 3u);
            i->rs1 = registro(i->palabra, 12);
            i->rs2 = registro(i->palabra, 7);
            /* ell permite registros pares, incluido x0, y solo offsets 0 o 2
             * para escribir dos palabras de la llave sin salir de su mitad. */
            if ((i->off_boveda != 0 && i->off_boveda != 2) ||
                (i->rs1 & 1u) || (i->rs2 & 1u)) return false;
        }
        break;
    case SIM_FMT_VCR:
    case SIM_FMT_CAMCON:
        /* Dirección absoluta [20:5], sin signo. No se confunde con registros
         * ni con offsets LSU, y no se consulta DMEM durante la decodificación. */
        i->direccion = (uint16_t)((i->palabra >> 5) & 0xffffu);
        i->tiene_direccion = true;
        if (i->formato == SIM_FMT_CAMCON) {
            inmediato(i, 5, false);
            i->requiere_auth = true;
        }
        break;
    case SIM_FMT_SETPWD:
        i->rs1 = registro(i->palabra, 0);
        i->requiere_init = true;
        break;
    default:
        return false;
    }
    /* Los saltos directos expresan offsets en bytes múltiplos de 16 según el
     * ensamblador. Se valida ese contrato sin sumar PC ni decidir si se toman. */
    if ((i->salto == SIM_SALTO_PC || i->salto == SIM_SALTO_COND_PC) &&
        i->inmediato % SIM_CARGADOR_BYTES_BUNDLE != 0) return false;
    return true;
}

/* Busca tipo/ID en la tabla y valida la unidad antes de extraer operandos.
 * La salida se reemplaza en cada llamada, también si la entrada es inválida. */
SimDecodeEstado sim_decode_palabra(uint32_t palabra, unsigned slot,
                                   SimInstruccion *salida)
{
    if (!salida) return SIM_DECODE_ARGUMENTO;
    *salida = vacia(palabra, slot);
    if (slot >= SIM_CARGADOR_SLOTS) return salida->estado = SIM_DECODE_ARGUMENTO;
    /* Solo la palabra cero es el NOP canónico del ensamblador. Un opcode
     * desconocido que el hardware deja inactivo no se convierte aquí en NOP. */
    if (!palabra) {
        salida->estado = SIM_DECODE_NOP;
        salida->familia = SIM_FAMILIA_NOP;
        salida->operacion = SIM_OP_NOP;
        return salida->estado;
    }
    for (size_t n = 0; n < sizeof instrucciones / sizeof *instrucciones; ++n) {
        const Especificacion *e = &instrucciones[n];
        if (e->tipo != salida->tipo || e->id != salida->id) continue;
        /* Se reconoce primero tipo/ID: una operación conocida en otro slot
         * produce SLOT, mientras que un ID no definido conserva RESERVADA. */
        if (e->slot != slot) return salida->estado = SIM_DECODE_SLOT;
        SimInstruccion i = *salida;
        i.operacion = e->operacion;
        i.formato = e->formato;
        /* Las familias ALU, LSU, BRU y CRIPTO son consecutivas y conservan
         * el mismo orden de los cuatro slots publicado por SimImagen. */
        i.familia = (SimFamilia)(SIM_FAMILIA_ALU + slot);
        if (!operandos(&i)) return salida->estado = SIM_DECODE_CAMPOS;
        /* Los operandos se publican juntos después de validar. En un rechazo
         * no quedan registros parciales que S3 pueda consumir por accidente. */
        i.estado = SIM_DECODE_OK;
        *salida = i;
        return salida->estado;
    }
    return salida->estado;
}

/* Decodifica cada posición en su propia unidad, sin copiar ni modificar IMEM.
 * El resultado agregado no reemplaza los cuatro estados individuales. */
SimDecodeEstado sim_decode_bundle(const uint32_t palabras[SIM_CARGADOR_SLOTS],
                                  SimInstruccion salida[SIM_CARGADOR_SLOTS])
{
    if (!palabras || !salida) return SIM_DECODE_ARGUMENTO;
    SimDecodeEstado primero = SIM_DECODE_OK;
    bool todos_nop = true;
    for (unsigned s = 0; s < SIM_CARGADOR_SLOTS; ++s) {
        SimDecodeEstado estado = sim_decode_palabra(palabras[s], s, &salida[s]);
        if (estado != SIM_DECODE_NOP) todos_nop = false;
        /* Se conserva el primer error, pero se sigue con los demás slots para
         * que el llamador pueda inspeccionar todo el bundle sin estado antiguo. */
        if (estado != SIM_DECODE_OK && estado != SIM_DECODE_NOP && primero == SIM_DECODE_OK)
            primero = estado;
    }
    if (primero != SIM_DECODE_OK) return primero;
    return todos_nop ? SIM_DECODE_NOP : SIM_DECODE_OK;
}

/* Permite nombrar una operación sin reconstruir opcodes en el consumidor.
 * Un valor fuera del catálogo no se interpreta y se informa como inválido. */
const char *sim_operacion_nombre(SimOperacion operacion)
{
    if (operacion == SIM_OP_NOP) return "nop";
    for (size_t n = 0; n < sizeof instrucciones / sizeof *instrucciones; ++n)
        if (instrucciones[n].operacion == operacion) return instrucciones[n].nombre;
    return "invalida";
}
