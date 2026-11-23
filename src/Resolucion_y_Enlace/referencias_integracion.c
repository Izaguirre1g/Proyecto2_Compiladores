#include "referencias_integracion.h"

#include <stdint.h>

static int generar_funcion(const AstNodo *funcion, const SimTabla *simbolos,
                       size_t bytes_marco, const RrTabla *destinos,
                       FILE *salida, FILE *diagnosticos, int referencias)
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

    return (referencias?gd_generar_funcion_referencias:gd_generar_funcion)(funcion, simbolos, bytes_marco,
                             rr_resolver_funcion, (void *)destinos,
                             salida, diagnosticos);
}

/* Ambos caminos reutilizan la misma tabla estructurada de destinos. */
int ri_generar_funcion(const AstNodo *f,const SimTabla *t,size_t m,const RrTabla *d,FILE *o,FILE *e)
{ return generar_funcion(f,t,m,d,o,e,0); }
int ri_generar_funcion_referencias(const AstNodo *f,const SimTabla *t,size_t m,const RrTabla *d,FILE *o,FILE *e)
{ return generar_funcion(f,t,m,d,o,e,1); }
