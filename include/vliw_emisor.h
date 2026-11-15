/* Emite exactamente un plan S7 como ensamblador VLIW, con destinos lógicos
 * explícitos. Reutiliza el codificador y S2; no calendariza ni codifica ISA. */
#ifndef VLIW_EMISOR_H
#define VLIW_EMISOR_H
#include "vliw_scheduler.h"

typedef enum {
    VLIW_DEST_DIRECTO = 0, VLIW_DEST_CONDICIONAL, VLIW_DEST_INDIRECTO,
    VLIW_DEST_RETORNO, VLIW_DEST_NO_RESUELTO
} VliwDestinoTipo;
typedef struct {
    size_t instruccion; /* Posición original, no id externo. Una entrada por BRU. */
    VliwDestinoTipo tipo;
    size_t tomado;      /* Bloque explícito para sye/condicional; VLIW_VACIO si dinámico. */
    /* Condicional: bloque siguiente o VLIW_VACIO al salir del programa.
     * Llamada: continuación de retorno secuencial. Otras formas: VLIW_VACIO. */
    size_t caida;
} VliwDestino;
typedef struct {
    const VliwDestino *items;
    size_t cantidad;
} VliwDestinos;

typedef enum {
    VLIW_EM_OK = 0, VLIW_EM_ARGUMENTO, VLIW_EM_PLAN, VLIW_EM_TEXTO,
    VLIW_EM_DESTINO, VLIW_EM_NO_RESUELTO, VLIW_EM_RANGO,
    VLIW_EM_CODIFICADOR, VLIW_EM_METADATOS, VLIW_EM_MEMORIA, VLIW_EM_IO
} VliwEmisorEstado;
typedef struct {
    VliwEmisorEstado estado;
    size_t instruccion, bloque; /* VLIW_VACIO cuando el error es global. */
    VliwEstado estado_plan;
    char mensaje[512];          /* Propiedad del llamador; incluye diagnóstico CB. */
} VliwEmisorDiagnostico;

/* Entrada/plan/destinos/textos prestados e inmutables durante la llamada.
 * destinos=NULL equivale a tabla vacía, válida solo si no existen transferencias.
 * ensamblador debe apuntar a NULL; solo se publica una cadena completa validada.
 * La salida es propia y no depende de la vida posterior de las entradas.
 * Vacío -> cadena "" liberable; no se intenta codificar ese caso con CB.
 * Cada texto es una operación no-NOP, terminada en NUL, de menos de 160 bytes.
 * No acepta etiquetas, comentarios, saltos de línea ni separadores de slots.
 * Las máscaras deben coincidir con los operandos S2 (excluido x0) y los efectos
 * deben cubrir la semántica real. No se reparan metadatos ni destinos antiguos. */
VliwEmisorEstado vliw_emitir(const VliwEntrada *entrada, const VliwPlan *plan,
                             const VliwDestinos *destinos, char **ensamblador,
                             VliwEmisorDiagnostico *diagnostico);
/* No escribe archivos del llamador: salida atómica en memoria. El único flujo
 * interno es un tmpfile C11 para recuperar diagnósticos del codificador real;
 * sus fallos de creación/escritura/lectura/cierre se informan como EM_IO. */
void vliw_emisor_liberar(char **ensamblador);
#endif
