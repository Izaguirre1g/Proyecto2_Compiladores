#include "ir_spilling_backend.h"
#include "funciones_marco.h"

/* Usa la validacion del marco original sin permitir que los parametros
 * invadan las ranuras reservadas para los temporales desalojados. */
int ir_spilling_backend_preparar(size_t bytes_existentes, size_t ranuras,
                                 size_t numero_parametros,
                                 IrSpillingBackend *backend) {
    if (!backend || bytes_existentes < 4 || bytes_existentes % 4 != 0 ||
        numero_parametros > 255 ||
        numero_parametros > (bytes_existentes - 4) / 4) return 2;

    IrSpillingMarco marco = {0};
    if (ir_spilling_calcular_marco(bytes_existentes, ranuras, &marco)) return 2;

    /* fm_emitir_prologo requiere un marco multiplo de cuatro y con espacio
     * para la direccion de retorno y todos los parametros. */
    IrSpillingBackend nuevo = {marco, numero_parametros};
    *backend = nuevo;
    return 0;
}

/* Reutiliza la implementacion del backend, incluida la reserva del stack
 * pointer y el guardado del registro de retorno. */
int ir_spilling_backend_prologo(FILE *salida, const IrSpillingBackend *backend) {
    if (!salida || !backend) return 2;
    return fm_emitir_prologo(salida, backend->marco.bytes_marco,
                            backend->numero_parametros) == FM_OK ? 0 : 2;
}

/* Es esencial liberar exactamente el mismo numero de bytes reservados
 * en el prologo; de lo contrario se corromperia la pila del llamador. */
int ir_spilling_backend_epilogo(FILE *salida, const IrSpillingBackend *backend) {
    if (!salida || !backend) return 2;
    return fm_emitir_epilogo(salida, backend->marco.bytes_marco) == FM_OK ? 0 : 2;
}

/* Mantiene la conversion de slots $spillN a accesos LSU en un solo lugar. */
int ir_spilling_backend_acceso(FILE *salida, const IrSpillingBackend *backend,
                               const IrInstr *instruccion,
                               unsigned registro_fisico) {
    if (!backend) return 2;
    return ir_spilling_emitir_acceso(salida, &backend->marco,
                                    instruccion, registro_fisico);
}
