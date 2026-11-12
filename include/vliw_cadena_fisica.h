/* Coordina la cadena consumidora desde ISA física acreditada hasta CB/S8-C.
 * Reutiliza metadatos, CFG, DAG, proyección, I1-A1, S7/verificador y S8-A.
 * No transforma IR/BAL ni resuelve ABI, spilling o direcciones materializadas. */
#ifndef VLIW_CADENA_FISICA_H
#define VLIW_CADENA_FISICA_H
#include "vliw_proyeccion_fisica.h"
#include "vliw_visualizador.h"
#include "codificacion_binaria.h"

typedef struct {
    const VliwCfgEntrada *flujo;
    const unsigned *efectos_adicionales;
    size_t cantidad_efectos;
    const char *const *textos;
    size_t cantidad_textos;
    uint64_t generacion;
    bool efectos_acreditados, abi_acreditado;
    VliwGarantiasFisicas garantias;
} VliwCadenaEntrada;
typedef enum {
    VLIW_CAD_INICIO=0,VLIW_CAD_META,VLIW_CAD_CFG,VLIW_CAD_DAG,
    VLIW_CAD_PROYECCION,VLIW_CAD_VALIDACION,VLIW_CAD_PLAN,
    VLIW_CAD_VERIFICACION,VLIW_CAD_EMISION,VLIW_CAD_CODIFICACION,
    VLIW_CAD_CONTRASTE,VLIW_CAD_ESTADISTICAS,VLIW_CAD_VISUALIZACION
} VliwCadenaFase;
typedef enum {
    VLIW_CAD_OK=0,VLIW_CAD_ARGUMENTO,VLIW_CAD_INCOMPLETO,VLIW_CAD_ABI,
    VLIW_CAD_REUBICACION,VLIW_CAD_ISA,VLIW_CAD_METADATOS,
    VLIW_CAD_CONTROL,VLIW_CAD_DEPENDENCIAS,VLIW_CAD_CAPACIDAD,
    VLIW_CAD_PLAN_INVALIDO,VLIW_CAD_EMISOR,VLIW_CAD_CODIGO,
    VLIW_CAD_MEMORIA,VLIW_CAD_IO
} VliwCadenaEstado;
typedef struct {
    VliwCadenaEstado estado;
    VliwCadenaFase fase;
    int estado_original;
    int estado_interno; /* Causa anidada de proyección/emisor, cuando aplica. */
    VliwProyeccionFase fase_proyeccion; /* Identifica el dominio de la causa anidada. */
    uint64_t generacion;
    size_t instruccion, dependencia, bloque, destino;
    char mensaje[512];
} VliwCadenaDiagnostico;
/* Todos los miembros son propios. Proyección posee las vistas S7/destinos;
 * CFG/DAG y metadatos se retienen como evidencia independiente, sin apuntar a
 * las entradas. El plan indexa la proyección y no los IDs externos. No alterar
 * punteros/cantidades antes de liberar. codigo vacío no es un binario ejecutable. */
typedef struct {
    uint64_t generacion;
    VliwMetadatosParciales *metadatos;
    size_t cantidad_metadatos;
    VliwCfg *cfg;
    VliwDag *dag;
    VliwProyeccion *proyeccion;
    VliwPlan *plan;
    char *asm_texto, *visualizacion_json;
    CbPrograma codigo;
    bool codificado;
    VliwEstadisticas estadisticas;
} VliwCadena;
/* Préstamos accesibles/inmutables durante la llamada, diagnóstico separado y
 * salida inicialmente NULL. Solo OK publica; cualquier fallo conserva entradas
 * y salida y libera intermediarios. El vacío devuelve plan/ASM/estadísticas
 * vacíos: CB no acepta fuente vacía y por eso codificado permanece false.
 * Las acreditaciones externas se exigen, no se autentican ni se deducen desde
 * x1, x13, generación o éxito de los consumidores. No es un backend BAL. */
VliwCadenaEstado vliw_cadena_construir(const VliwCadenaEntrada *entrada,
    VliwCadena **salida,VliwCadenaDiagnostico *diagnostico);
/* Libera en orden inverso al de adquisición, admite NULL y deja NULL. */
void vliw_cadena_liberar(VliwCadena **cadena);
#endif
