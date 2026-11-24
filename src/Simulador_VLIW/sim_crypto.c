// MAPA CRIPTO: ID captura permisos -> EX prepara operaciones -> WB publica resultados de registros.
// La boveda es estado privado; AUTH/INIT son estado publico.

/* Semántica criptográfica del Proyecto I, separada del control del pipeline.
 * Todo cálculo usa uint32_t para respetar la aritmética módulo 2^32. */
#include "sim_crypto.h"
#include <string.h>

/* Rota x a la izquierda con cantidad reducida a cinco bits. Devuelve
 * una palabra de 32 bits; el caso cero evita un desplazamiento indefinido. */
// CRIPTO: rota circularmente una palabra de 32 bits a la izquierda.
// Reducir la cantidad a 0..31 evita desplazamientos indefinidos del lenguaje C.
uint32_t sim_crypto_rol32(uint32_t x, unsigned cantidad)
{
    unsigned n = cantidad & 31u;
    /* En C desplazar por 32 es indefinido. El caso cero devuelve x sin
     * evaluar ese desplazamiento; los demás combinan extremos circulares. */
    return n ? (x << n) | (x >> (32u - n)) : x;
}

/* Combina x y la palabra de llave para una ronda. Devuelve el valor
 * módulo 2^32; no consulta ni modifica la bóveda. */
// CRIPTO: implementa la función combinacional de la ronda criptografica.
// Usa suma modulo 2^32, rotaciones y XOR; todavía no modifica la boveda.
uint32_t sim_crypto_f(uint32_t x, uint32_t llave)
{
    /* La suma unsigned descarta el acarreo antes del XOR, como el bus RTL. */
    return (sim_crypto_rol32(x, 5) + llave) ^ sim_crypto_rol32(x, 13);
}

/* Borra la bóveda recibida. Un puntero NULL no produce efectos; no
 * modifica registros, memoria ni el estado del pipeline. */
// CRIPTO: borra el estado privado de la boveda durante un reinicio.
// La memoria de datos y el banco de registros se administran en el pipeline.
void sim_crypto_reset(SimBoveda *boveda)
{
    if (boveda) memset(boveda, 0, sizeof *boveda);
}

/* Exige una instrucción S2 válida de CRIPTO. No convierte una palabra inválida
 * en excepción de privilegio; false indica error administrativo de contrato. */
// CRIPTO EN ID: conserva instruccion, fuentes y estado del permiso de acceso.
// El permiso se decide al decodificar, sin aprovechar autorizaciones futuras por bypass.
bool sim_crypto_capturar(const SimInstruccion *i, uint32_t a, uint32_t b,
                         uint32_t estado, SimCryptoId *salida)
{
    if (!i || !salida || i->estado != SIM_DECODE_OK ||
        i->familia != SIM_FAMILIA_CRIPTO || i->slot != 3 ||
        i->operacion < SIM_OP_FSL || i->operacion > SIM_OP_SETPWD) return false;
    // Capturamos instrucción, operandos y permiso en ID. Falta AUTH o INIT según los bits
    // requeridos: se conserva esa decisión para EX y no se vuelve a conceder permiso usando un
    // WB posterior.
    *salida = (SimCryptoId){.valido = true, .instruccion = *i, .a = a, .b = b,
        .falta_permiso = (i->requiere_auth && !(estado & SIM_CRYPTO_AUTH)) ||
                         (i->requiere_init && !(estado & SIM_CRYPTO_INIT))};
    return true;
}

/* Función combinacional: consulta la bóveda en EX sin modificarla. Una falta
 * de permiso devuelve solo la excepción y cero efectos, nunca un stop de API. */
// CRIPTO EN EX: calcula el resultado y los efectos pendientes de la instruccion.
// Si falta permiso genera una excepción arquitectonica, no un fallo de la API.
bool sim_crypto_preparar(const SimBoveda *boveda, const SimMemoria *memoria,
                         const SimCryptoId *id, SimEfectoCrypto *salida)
{
    if (!boveda || !memoria || !id || !salida) return false;
    SimEfectoCrypto e = {0};
    if (!id->valido) { *salida = e; return true; }
    const SimInstruccion *i = &id->instruccion;
    if (i->estado != SIM_DECODE_OK || i->familia != SIM_FAMILIA_CRIPTO ||
        i->operacion < SIM_OP_FSL || i->operacion > SIM_OP_SETPWD) return false;
    // La instruccion validada pasa a preparar sus efectos, sin mutar estado hasta aplicar.
    e.valido = true; e.operacion = i->operacion;
    /* La decisión ya se tomó en ID. Revalidar con un AUTH recién publicado
     * introduciría un bypass de ESTADO que el procesador no posee. */
    // Permiso denegado: se prepara excepción, sin escribir la boveda ni DMEM.
    if (id->falta_permiso) { e.excepcion = true; *salida = e; return true; }
    switch (i->operacion) {
    // FSL y FSLI escriben un par de registros; ambos resultados deben llegar a WB.
    case SIM_OP_FSL:
    case SIM_OP_FSLI: {
        if (i->lk >= 4 || i->rk >= 4 || i->rd < 2 || i->rd > 30 ||
            (i->rd & 1u) || i->rd2 != i->rd + 1u) return false;
        // La ronda Feistel lee de la boveda la subllave elegida por lk/rk.
        uint32_t k = boveda->keys[i->lk][i->rk];
        e.wb = (SimCryptoWb){.par = true, .rd = i->rd, .rd2 = i->rd2};
        /* Ambas rondas comparten F. La inversa intercambia el lado que
         * alimenta F y deshace el XOR; las fuentes aún son capturas ID. */
        // FSL y FSLI son rondas inversas: se invierte que lado participa en F.
        if (i->operacion == SIM_OP_FSL) {
            e.wb.l = id->b; e.wb.r = id->a ^ sim_crypto_f(id->b, k);
        } else {
            e.wb.l = id->b ^ sim_crypto_f(id->a, k); e.wb.r = id->a;
        }
        break;
    }
    // ELL prepara dos palabras para escribir una pareja de la llave.
    case SIM_OP_ELL:
        if (i->lk >= 4 || (i->off_boveda != 0 && i->off_boveda != 2)) return false;
        /* S2 restringe off a 0/2. RTL suma el índice módulo 4, pero esos
         * pares legales nunca envuelven ni salen de la llave seleccionada. */
        e.llave_we = true; e.lk = i->lk; e.off = i->off_boveda;
        e.a = id->a; e.b = id->b;
        break;
    // SETPWD configura la contraseña y prepara el cambio de estado publico.
    case SIM_OP_SETPWD:
        e.pwd_we = true; e.a = id->a; e.wb.init_clr = true;
        break;
    // VCR/CAMCON usan memoria de datos y la boveda: la lectura se prepara mediante LSU.
    case SIM_OP_VCR:
    case SIM_OP_CAMCON:
        if (!i->tiene_direccion || i->inmediato < 0 || i->inmediato > 31) return false;
        /* CARGAI/GUARDAP permiten reutilizar preparación y aplicación S4 sin
         * cambiar LSU. La dirección absoluta conserva sus 16 bits; addr[15:2]
         * selecciona una palabra incluso en direcciones no alineadas. */
        if (!sim_lsu_preparar(memoria, SIM_OP_CARGAI, i->direccion, 0, 0, &e.memoria)) return false;
        if (i->operacion == SIM_OP_VCR) {
            // VCR compara la palabra leída con la contraseña solo si ya fue establecida. El
            // resultado de autorización queda pendiente en WB; consultar la bóveda en EX no
            // modifica aún el registro público AUTH.
            // La autorizacion calculada espera a WB para hacerse visible en ESTADO.
            e.wb.auth_we = true;
            e.wb.auth = boveda->pwd_set && e.memoria.lectura == boveda->password;
        } else {
            // CAMCON calcula una rotacion y prepara un store por el puerto de memoria B.
            e.rotacion = (uint8_t)i->inmediato; e.pwd_rot = true;
            uint32_t dato = sim_crypto_rol32(e.memoria.lectura, e.rotacion);
            if (!sim_lsu_preparar(memoria, SIM_OP_GUARDAP, i->direccion, dato, 0, &e.memoria)) return false;
        }
        break;
    default: return false;
    }
    *salida = e;
    return true;
}

/* Publica únicamente la bóveda al final de EX. El controlador aplica memoria
 * A y después B y conserva wb para el siguiente flanco. Efecto debe provenir
 * de preparar; reset omite esta publicación y borra toda la bóveda. */
// CRIPTO EN EX: publica en la boveda privada las escrituras permitidas.
// Los efectos de registro y ESTADO continuan pendientes hasta WB.
void sim_crypto_aplicar(SimBoveda *boveda, const SimEfectoCrypto *e)
{
    if (!boveda || !e || !e->valido || e->excepcion) return;
    // Publica las escrituras a las subllaves cuando el efecto fue validado.
    if (e->llave_we) {
        boveda->keys[e->lk][e->off] = e->a;
        boveda->keys[e->lk][e->off + 1u] = e->b;
    }
    /* La bóveda se publica en EX; ESTADO permanece pendiente hasta WB.
     * setpwd tiene prioridad sobre rotación en key_vault.sv. Son operaciones
     * excluyentes cuando el efecto proviene de una instrucción legal. */
    // SETPWD reemplaza la contraseña; CAMCON solamente la rota.
    if (e->pwd_we) { boveda->password = e->a; boveda->pwd_set = true; }
    else if (e->pwd_rot) boveda->password = sim_crypto_rol32(boveda->password, e->rotacion);
}
