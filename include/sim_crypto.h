/* Unidad criptográfica: bóveda privada, rondas Feistel y efectos preparados
 * para EX/WB. No controla PC, banco de registros ni el reloj del pipeline. */
#ifndef SIM_CRYPTO_H
#define SIM_CRYPTO_H
#include "sim_lsu.h"

enum { SIM_CRYPTO_AUTH = 1, SIM_CRYPTO_INIT = 2, SIM_TRAP_VECTOR = 0x3ff0 };

/* Estado interno por instancia, sin getters de secretos ni rutas a DMEM/GPR.
 * La representación visible permite almacenamiento sin heap; sus miembros no
 * son una API de lectura arquitectónica. Solo las sondas de prueba los inspeccionan. */
typedef struct {
    uint32_t keys[4][4]; /* LK selecciona llave; RK selecciona palabra de ronda. */
    uint32_t password;
    bool pwd_set;       /* Distingue contraseña cero de ausencia de provisionamiento. */
} SimBoveda;

typedef struct {
    /* S2 se conserva íntegro: no se vuelven a interpretar bits de la palabra.
     * a/b son valores capturados en ID; falta_permiso también queda congelada
     * allí, incluso si AUTH/INIT cambian antes de llegar a EX. */
    bool valido, falta_permiso;
    SimInstruccion instruccion;
    uint32_t a, b;
} SimCryptoId;

typedef struct {
    /* Resultados EX pendientes de WB: par GPR y cambios independientes de
     * ESTADO. ell/camcon escriben en EX y no solicitan un resultado al banco. */
    bool par, auth_we, auth, init_clr;
    uint8_t rd, rd2;
    uint32_t l, r;
} SimCryptoWb;

typedef struct {
    bool valido, excepcion;
    SimOperacion operacion;
    SimCryptoWb wb;
    /* Reutiliza la transacción S4 para el puerto B de palabra completa.
     * Ambas lecturas se preparan contra la misma memoria anterior al flanco. */
    SimAccesoLsu memoria;
    bool llave_we, pwd_we, pwd_rot;
    uint8_t lk, off, rotacion;
    uint32_t a, b;
} SimEfectoCrypto;

uint32_t sim_crypto_rol32(uint32_t x, unsigned cantidad);
uint32_t sim_crypto_f(uint32_t x, uint32_t llave);
void sim_crypto_reset(SimBoveda *boveda);
/* Exige una instrucción S2 válida de CRIPTO. No convierte una palabra inválida
 * en excepción de privilegio; false indica error administrativo de contrato. */
bool sim_crypto_capturar(const SimInstruccion *i, uint32_t a, uint32_t b,
                         uint32_t estado, SimCryptoId *salida);
/* Función combinacional: consulta la bóveda en EX sin modificarla. Una falta
 * de permiso devuelve solo la excepción y cero efectos, nunca un stop de API. */
bool sim_crypto_preparar(const SimBoveda *boveda, const SimMemoria *memoria,
                         const SimCryptoId *id, SimEfectoCrypto *salida);
/* Publica únicamente la bóveda al final de EX. El controlador aplica memoria
 * A y después B y conserva wb para el siguiente flanco. Efecto debe provenir
 * de preparar; reset omite esta publicación y borra toda la bóveda. */
void sim_crypto_aplicar(SimBoveda *boveda, const SimEfectoCrypto *efecto);
#endif
