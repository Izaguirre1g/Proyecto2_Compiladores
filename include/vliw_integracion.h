/* Valida descripciones físicas para S7/S8-A, sin construirlas ni calendarizar.
 * Reutiliza sus estructuras prestadas y CB/S2 para reconocer ISA real. Un éxito
 * local no certifica la semántica BAL ni la completitud del productor externo. */
#ifndef VLIW_INTEGRACION_H
#define VLIW_INTEGRACION_H
#include "vliw_emisor.h"

typedef enum {
    VLIW_INT_OK = 0, VLIW_INT_ARGUMENTO, VLIW_INT_ESTRUCTURA,
    VLIW_INT_REGISTRO, VLIW_INT_SLOT, VLIW_INT_EFECTOS, VLIW_INT_CFG,
    VLIW_INT_DEPENDENCIA, VLIW_INT_DESTINO, VLIW_INT_NO_RESUELTO,
    VLIW_INT_NO_ACREDITADOS, VLIW_INT_ISA, VLIW_INT_METADATOS,
    VLIW_INT_CAPACIDAD, VLIW_INT_MEMORIA, VLIW_INT_IO
} VliwIntegracionEstado;

typedef struct {
    VliwIntegracionEstado estado;
    size_t instruccion, dependencia, bloque, destino;
    const char *mensaje; /* Literal prestado; VLIW_VACIO indica índice no aplicable. */
} VliwIntegracionDiagnostico;

/* Comprueba estructura, ISA local, máscaras, efectos, DAG y destinos recibidos.
 * entrada y sus arreglos/textos son prestados, accesibles e inmutables durante
 * la llamada; destinos=NULL equivale a tabla vacía. El diagnóstico es opcional,
 * separado de la entrada y propio del cliente; no requiere liberación.
 * No hay salida asignada ni plan: todos los recursos auxiliares se liberan
 * incluso si fallan CB, el flujo privado de diagnóstico o una reserva.
 * Exige metadatos_completos del productor para una entrada no vacía y nunca
 * modifica esa declaración. OK no autentica efectos desconocidos, ABI, BAL,
 * reubicaciones ni capacidad del plan futuro. S7/S8-A siguen siendo obligatorios.
 * Vacío sin bloques/aristas/destinos es válido. No prueba punteros arbitrarios;
 * cantidades deben describir almacenamiento real. Ver contrato-integracion.md. */
VliwIntegracionEstado vliw_integracion_validar(const VliwEntrada *entrada,
    const VliwDestinos *destinos, VliwIntegracionDiagnostico *diagnostico);
#endif
