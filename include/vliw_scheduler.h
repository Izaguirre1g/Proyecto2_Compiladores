/* Núcleo de calendarización estática VLIW: consume metadatos externos y
 * produce índices de operaciones por bundle, sin ensamblar ni reescribir ISA. */
#ifndef VLIW_SCHEDULER_H
#define VLIW_SCHEDULER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VLIW_VACIO SIZE_MAX
enum { VLIW_SLOTS = 4, VLIW_MAX_BUNDLES = 1023, VLIW_RAW_DISTANCIA = 3 };
typedef enum {
    VLIW_SLOT_ALU = 0, VLIW_SLOT_LSU, VLIW_SLOT_BRU, VLIW_SLOT_CRIPTO
} VliwSlot;

typedef enum {
    VLIW_EF_NINGUNO = 0,
    VLIW_EF_LEE_MEMORIA = 1u << 0,
    VLIW_EF_ESCRIBE_MEMORIA = 1u << 1,
    VLIW_EF_BOVEDA = 1u << 2,
    VLIW_EF_ESTADO = 1u << 3,
    VLIW_EF_EXCEPCION = 1u << 4,
    VLIW_EF_DESCONOCIDOS = 1u << 5,
    VLIW_EF_BARRERA = 1u << 6
} VliwEfecto;
typedef enum {
    VLIW_CONTROL_NINGUNO = 0, VLIW_CONTROL_SALTO,
    VLIW_CONTROL_LLAMADA, VLIW_CONTROL_RETORNO
} VliwControl;

typedef struct {
    size_t id;             /* Identidad externa única; no necesita ser consecutiva. */
    VliwSlot slot;
    const char *texto;     /* Opcional y prestado. El scheduler nunca lo interpreta. */
    /* Solo bits 0..31 son legales. El contenedor de 64 bits permite rechazar
     * x32 y superiores en vez de truncar silenciosamente metadatos incorrectos.
     * Se excluye x0 al comprobar riesgos; Feistel debe incluir ambos destinos. */
    uint64_t lee, escribe;
    unsigned efectos;
    VliwControl control;
    size_t bloque;
} VliwInstruccion;

typedef enum {
    VLIW_DEP_RAW = 0, VLIW_DEP_WAR, VLIW_DEP_WAW,
    VLIW_DEP_MEMORIA, VLIW_DEP_CONTROL, VLIW_DEP_BARRERA
} VliwTipoDependencia;
typedef struct {
    /* Son índices de entrada, no los id externos. Distancia se mide en
     * bundles: RAW >=3; las otras relaciones >=1. No se eliminan aristas. */
    size_t antes, despues;
    VliwTipoDependencia tipo;
    size_t distancia;
} VliwDependencia;

typedef struct {
    size_t inicio, fin;    /* Intervalo [inicio, fin), no vacío y contiguo. */
    const size_t *sucesores;
    size_t cantidad_sucesores;
} VliwBloque;

typedef struct {
    const VliwInstruccion *instrucciones;
    size_t cantidad_instrucciones;
    const VliwDependencia *dependencias;
    size_t cantidad_dependencias;
    const VliwBloque *bloques;
    size_t cantidad_bloques;
    /* Afirmación obligatoria para programas no vacíos: máscaras, efectos,
     * CFG y restricciones son completos. No reemplaza el verificador final.
     * Una lista sin aristas no certifica independencia por sí misma. */
    bool metadatos_completos;
} VliwEntrada;

typedef struct {
    size_t instrucciones[VLIW_SLOTS]; /* Índice original o VLIW_VACIO. */
    size_t bloque;                  /* También identifica bundles de protección. */
    unsigned operaciones;
} VliwBundle;
typedef struct {
    VliwBundle *bundles;
    size_t cantidad_bundles, cantidad_instrucciones, cantidad_vacios;
    size_t *bundle_por_instruccion;  /* Indexado por posición original, no id. */
    size_t *inicio_por_bloque;       /* Apunta al primer NOP de protección. */
    size_t cantidad_bloques;
} VliwPlan;

typedef enum {
    VLIW_OK = 0, VLIW_ARGUMENTO, VLIW_METADATOS, VLIW_BLOQUES,
    VLIW_DEPENDENCIA, VLIW_CICLO, VLIW_CAPACIDAD, VLIW_MEMORIA, VLIW_PLAN_INVALIDO
} VliwEstado;
typedef struct {
    VliwEstado estado;
    size_t instruccion, dependencia; /* VLIW_VACIO cuando no se identifica una. */
    const char *mensaje;             /* Literal estático; no se libera. */
} VliwDiagnostico;

/* Entradas/arreglos/textos prestados e inmutables durante la llamada; sus
 * cantidades deben describir almacenamiento accesible real, como en toda API C.
 * Salida debe apuntar a NULL. Solo se publica un plan completo y verificado;
 * en error conserva *salida. El plan posee sus arreglos, no referencias a texto.
 * Programa vacío (sin bloques ni aristas) produce un plan vacío liberable. */
VliwEstado vliw_planificar(const VliwEntrada *entrada, VliwPlan **salida,
                           VliwDiagnostico *diagnostico);
/* Recorre y valida la salida independientemente del algoritmo de selección.
 * No repara dependencias ni modifica el plan. Acepta también planes de prueba. */
VliwEstado vliw_verificar(const VliwEntrada *entrada, const VliwPlan *plan,
                          VliwDiagnostico *diagnostico);
void vliw_liberar(VliwPlan **plan);
#endif
