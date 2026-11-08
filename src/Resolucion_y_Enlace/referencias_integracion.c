#include "referencias_integracion.h"

#include <stdint.h>

int ri_generar_funcion(const AstNodo *funcion, const SimTabla *simbolos,
                       size_t bytes_marco, const RrTabla *destinos,
                       FILE *salida, FILE *diagnosticos)
{
    if (!funcion || funcion->tipo != AST_FUNCION || !funcion->primer_hijo ||
        funcion->primer_hijo->tipo != AST_IDENTIFICADOR ||
        !funcion->primer_hijo->texto || !destinos || !simbolos || !salida)
        return 1;

    /* Exige que incluso la propia funcion tenga direccion asignada.
     * Rechaza registros vacios antes de generar cualquier instruccion. */
    uint32_t inicio = 0;
    if (rr_buscar(destinos, funcion->primer_hijo->texto, RR_FUNCION,
                  &inicio) != RR_OK)
        return 1;

    return gd_generar_funcion(funcion, simbolos, bytes_marco,
                             rr_resolver_funcion, (void *)destinos,
                             salida, diagnosticos);
}
