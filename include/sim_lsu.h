/* Memoria privada y transacciones LSU: reproduce el puerto A de memory.sv
 * sin controlar el reloj, el banco de registros ni la decodificación. */
#ifndef SIM_LSU_H
#define SIM_LSU_H
#include "sim_decode.h"

/* Cada instancia posee sus bytes. No hay reserva dinámica ni referencias
 * compartidas; la imagen de S1 solo se copia durante carga/restauración. */
typedef struct { uint8_t bytes[SIM_CARGADOR_DMEM_BYTES]; } SimMemoria;

typedef struct {
    /* Señales combinacionales antes del flanco. direccion conserva los 32
     * bits efectivos; palabra_fisica descarta altos y los dos bits bajos. */
    bool valido, escritura, carga;
    uint32_t direccion, palabra_fisica;
    uint8_t byte_enable;
    uint32_t dato_escritura, lectura, resultado;
} SimAccesoLsu;

/* Lee la palabra seleccionada por addr[15:2], incluso para direcciones altas
 * o no alineadas. Devuelve false ante punteros nulos, sin alterar salida. */
bool sim_memoria_leer_palabra(const SimMemoria *memoria, uint32_t direccion,
                              uint32_t *resultado);

/* Prepara carga/store con los operandos capturados en ID y offset signed de
 * S2 (-1024..1023). No modifica memoria. En error conserva la salida recibida.
 * Un load entrega el dato que EX debe capturar para WB; un store entrega BE
 * y dato desplazado que el pipeline aplicará solo al aceptar el flanco. */
bool sim_lsu_preparar(const SimMemoria *memoria, SimOperacion operacion,
                      uint32_t base, uint32_t dato, int32_t offset,
                      SimAccesoLsu *acceso);

/* Publica únicamente los bytes habilitados de una transacción válida.
 * Valida todos sus límites antes de modificar un byte; false no tiene efectos.
 * Para loads es una operación válida sin escritura. No actualiza registros. */
bool sim_lsu_aplicar(SimMemoria *memoria, const SimAccesoLsu *acceso);
#endif
