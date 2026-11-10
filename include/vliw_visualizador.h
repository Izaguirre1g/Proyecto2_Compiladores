/* Presenta planes S7 verificados y sus métricas estáticas, sin cambiar ISA,
 * instrucciones, dependencias ni posiciones. No representa tiempo dinámico. */
#ifndef VLIW_VISUALIZADOR_H
#define VLIW_VISUALIZADOR_H

#include "vliw_scheduler.h"
#include "vliw_emisor.h"

enum { VLIW_VIS_TIPOS_DEPENDENCIA = VLIW_DEP_BARRERA + 1 };
typedef enum {
    VLIW_VIS_TABLA = 0, VLIW_VIS_CSV, VLIW_VIS_JSON
} VliwVisualFormato;
typedef struct {
    /* Solo afecta la tabla: 0 ajusta por columna hasta 96 bytes; un ancho
     * explícito entre 8 y 160 permite truncamiento marcado con puntos. */
    size_t ancho_slot;
    bool mostrar_dependencias, mostrar_estadisticas;
} VliwVisualOpciones;

typedef struct {
    size_t instrucciones_utiles, bundles_totales, bundles_vacios;
    size_t bundles_con_operaciones, bundles_proteccion;
    size_t operaciones_por_slot[VLIW_SLOTS];
    size_t slots_disponibles, slots_utilizados, slots_vacios;
    double utilizacion_porcentual, operaciones_por_bundle;
    size_t bytes_imem, bloques, textos_ausentes;
    size_t dependencias_por_tipo[VLIW_VIS_TIPOS_DEPENDENCIA];
} VliwEstadisticas;
typedef enum {
    VLIW_VIS_OK = 0, VLIW_VIS_ARGUMENTO, VLIW_VIS_PLAN,
    VLIW_VIS_DESTINO, VLIW_VIS_TEXTO, VLIW_VIS_MEMORIA, VLIW_VIS_GENERACION
} VliwVisualEstado;
typedef struct {
    VliwVisualEstado estado;
    VliwEstado estado_plan;
    size_t instruccion, dependencia; /* VLIW_VACIO para errores globales. */
    const char *mensaje;             /* Literal estático; no se libera. */
} VliwVisualDiagnostico;

/* Arreglos y textos prestados, accesibles e inmutables durante la llamada.
 * Se valida el plan con S7. resultado solo cambia en éxito. No se interpretan
 * textos para calcular estadísticas; NULL o "" se cuentan como ausentes.
 * Un programa vacío tiene todos los valores y cocientes en cero. */
VliwVisualEstado vliw_estadisticas(const VliwEntrada *entrada,
    const VliwPlan *plan, VliwEstadisticas *resultado,
    VliwVisualDiagnostico *diagnostico);

/* salida debe apuntar inicialmente a NULL: recibe una cadena propia solo en
 * éxito. destinos puede ser NULL o contener anotaciones parciales por índice
 * original; no se infieren ni reescriben operandos. opciones=NULL usa 48,true,true.
 * CSV/JSON conservan textos completos UTF-8 válidos, incluidos controles
 * escapados. NULL/"" son texto ausente, distinto de un slot NOP. La tabla
 * escapa bytes no imprimibles/no ASCII y marca el truncamiento con "...".
 * Las opciones de secciones solo afectan la tabla; JSON siempre es completo.
 * No requiere enlazar el emisor, el codificador ni el simulador. */
VliwVisualEstado vliw_visualizar(const VliwEntrada *entrada, const VliwPlan *plan,
    const VliwDestinos *destinos, VliwVisualFormato formato,
    const VliwVisualOpciones *opciones, char **salida,
    VliwVisualDiagnostico *diagnostico);
void vliw_visual_liberar(char **salida);
#endif
