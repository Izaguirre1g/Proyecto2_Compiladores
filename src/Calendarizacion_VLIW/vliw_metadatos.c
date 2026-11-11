/* Resume operandos y efectos mínimos de instrucciones físicas ya codificadas.
 * Utiliza exclusivamente S2 para reconocer ISA; devuelve datos parciales por
 * valor, sin parser, generación de código ni integración con el backend BAL.
 * No reserva recursos: la salida del cliente cambia solo al terminar en éxito. */
#include "vliw_metadatos.h"

/* Publica diagnóstico opcional por valor y devuelve el mismo estado. Los
 * mensajes son literales; no adquiere memoria ni conserva datos del cliente. */
static SimDecodeEstado informar(VliwMetadatosDiagnostico *d, SimDecodeEstado estado,
    uint32_t palabra, unsigned slot, const char *mensaje)
{
    if (d) *d=(VliwMetadatosDiagnostico){estado,palabra,slot,mensaje};
    return estado;
}

/* Agrega un operando S2 a una máscara local. AUSENTE es32, no x0: primero se
 * distingue esa ausencia, luego se valida el índice y finalmente se ignora x0,
 * cuyo valor no depende de escrituras. Rechaza un índice incoherente sin hacer
 * un desplazamiento fuera de rango; no toca la salida pública del cliente. */
static bool agregar_registro(uint8_t registro, uint64_t *mascara)
{
    if (registro==SIM_REG_AUSENTE) return true;
    if (registro>=32) return false;
    if (registro!=0) *mascara|=UINT64_C(1)<<registro;
    return true;
}

/* Devuelve solo efectos deducibles del opcode, con la clasificación actual de
 * I1-A1/S8-A. Este pequeño mapa es necesario porque S2 expone operandos y
 * requisitos, pero no VliwEfecto; no se copia el validador ni se cambia su API.
 * Cero significa mínimos propios vacíos, no ausencia acreditada de efectos
 * externos. No consulta AUTH/INIT, memoria o bóveda ni ejecuta la instrucción. */
static unsigned efectos_minimos(SimOperacion op)
{
    switch (op) {
    case SIM_OP_CARGAI: case SIM_OP_CARGABAI:
        return VLIW_EF_LEE_MEMORIA;
    case SIM_OP_GUARDAP: case SIM_OP_GUARDAB:
        return VLIW_EF_ESCRIBE_MEMORIA;
    case SIM_OP_FSL: case SIM_OP_FSLI:
        return VLIW_EF_EXCEPCION;
    case SIM_OP_ELL:
        return VLIW_EF_BOVEDA|VLIW_EF_EXCEPCION;
    case SIM_OP_SETPWD:
        return VLIW_EF_BOVEDA|VLIW_EF_ESTADO|VLIW_EF_EXCEPCION;
    case SIM_OP_VCR:
        return VLIW_EF_LEE_MEMORIA|VLIW_EF_ESTADO;
    case SIM_OP_CAMCON:
        return VLIW_EF_LEE_MEMORIA|VLIW_EF_ESCRIBE_MEMORIA|VLIW_EF_BOVEDA|VLIW_EF_EXCEPCION;
    default:
        return VLIW_EF_NINGUNO;
    }
}

/* Coordina validación, decodificación y publicación de metadatos parciales.
 * Usa almacenamiento local hasta comprobar operandos; cualquier rechazo,
 * incluido NOP, conserva la salida previa. No necesita limpieza ni liberación. */
SimDecodeEstado vliw_metadatos_extraer(uint32_t palabra, unsigned slot,
    VliwMetadatosParciales *salida, VliwMetadatosDiagnostico *d)
{
    if (!salida || slot>=VLIW_SLOTS)
        return informar(d,SIM_DECODE_ARGUMENTO,palabra,slot,"salida requerida y slot físico0..3");
    SimInstruccion real;
    SimDecodeEstado estado=sim_decode_palabra(palabra,slot,&real);
    // El retorno S2 decide si se pueden consumir sus operandos. No se aplica
    // el fallback ADD del hardware a identificadores reservados ni se convierte
    // una palabra inválida en NOP. NOP tampoco es una operación funcional35.
    if (estado!=SIM_DECODE_OK)
        return informar(d,estado,palabra,slot,"S2 no reconoció una operación no-NOP válida en este slot");
    VliwMetadatosParciales parcial={0};
    parcial.operacion=real.operacion; parcial.familia=real.familia;
    parcial.slot=(VliwSlot)slot;
    // Solo se usan fuentes/destinos expuestos por S2. En un store rs2 es dato
    // leído; en un load rd es destino. Feistel incluye rs2 y rd2 del par: no se
    // deducen operaciones extra a partir de sus dos escrituras.
    if (!agregar_registro(real.rs1,&parcial.lee) || !agregar_registro(real.rs2,&parcial.lee) ||
        !agregar_registro(real.rd,&parcial.escribe) || !agregar_registro(real.rd2,&parcial.escribe))
        return informar(d,SIM_DECODE_CAMPOS,palabra,slot,"operandos S2 fuera del contrato de GPR");
    parcial.efectos_minimos=efectos_minimos(real.operacion);
    parcial.salto=real.salto;
    parcial.requiere_auth=real.requiere_auth; parcial.requiere_init=real.requiere_init;
    // Incluso una ALU no acredita símbolos, clobbers, reubicaciones ni efectos
    // del programa. Una BRU añade obligaciones de clasificación ABI y destino:
    // escribir x1 no prueba una llamada y leerlo no prueba un retorno.
    parcial.pendientes=VLIW_META_PEND_PRODUCTOR|VLIW_META_PEND_CFG_DAG;
    if (real.salto!=SIM_SALTO_NINGUNO)
        parcial.pendientes|=VLIW_META_PEND_CONTROL_ABI|VLIW_META_PEND_DESTINOS;
    *salida=parcial;
    return informar(d,SIM_DECODE_OK,palabra,slot,"metadatos locales extraídos; información del productor pendiente");
}
