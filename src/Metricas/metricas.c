/*
 * Métricas del código del editor (ver include/metricas.h).
 * El tamaño estático procede de los bundles emitidos; no del simulador.
 *
 * Pasos de met_medir:
 *   1. Generar el ensamblador y el .bin con el botón Ensamblador
 *      (ide_ensamblador_generar): es el mismo binario que hace balc.
 *   2. Buscar en el ensamblador la dirección del salto final del arranque
 *      (sye x0,0: un salto a sí mismo).
 *   3. Escribir el .bin en un archivo temporal oculto, porque el simulador
 *      carga el programa desde un archivo.
 *   4. Correr el simulador por tramos hasta que el PC llega al salto final.
 *   5. Volver a correrlo desde cero y avanzar ciclo por ciclo en el último
 *      tramo, para tener el ciclo EXACTO en que main terminó.
 *
 * Por qué por tramos (pasos 4 y 5): para saber el PC hay que pedir las
 * métricas (sim_ejecucion_resultados), y cada vez el recolector calcula un
 * resumen de los 64 KB de memoria. Pedirlas en cada uno de millones de
 * ciclos sería muy lento; cada MET_TRAMO ciclos es rápido.
 */
#include "metricas.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ide_analisis.h"
#include "ide_ensamblador.h"

/* Cada cuántos ciclos se revisa el PC en el paso 4. Conviene que no sea
 * múltiplo de 3 (ver llego_al_fin). */
#define MET_TRAMO 1000u

/* Copia una cadena (NULL si falta memoria). */
static char *copiar(const char *s)
{
    char *c = malloc(strlen(s) + 1);
    if (c != NULL) strcpy(c, s);
    return c;
}

/*
 * Paso 2: la dirección del primer bundle con "sye x0,0" (salto a sí mismo).
 * En el .asm cada línea que no es comentario es un bundle, en orden desde
 * la dirección 0:
 *     sumai x3,x0,0 | nop | nop | nop      <- bundle 0, dirección 0x000
 *     nop                                  <- bundle 1, dirección 0x010
 *     # __arranque_fin:                    <- comentario: no cuenta
 *     nop | nop | sye x0,0 | nop  # -> __arranque_fin
 * Devuelve 0 si lo encontró, -1 si no.
 */
static int buscar_fin(const char *ensamblador, uint32_t *direccion)
{
    const char *buscado = "sye x0,0";
    size_t largo_buscado = strlen(buscado);
    uint32_t bundle = 0;

    for (const char *linea = ensamblador; *linea != '\0';) {
        size_t largo = strcspn(linea, "\n");
        /* Se salta la sangría para ver si la línea es un comentario o está vacía. */
        size_t inicio = strspn(linea, " \t");
        int es_bundle = inicio < largo && linea[inicio] != '#';

        if (es_bundle) {
            /* Solo la parte antes del comentario "#". */
            size_t codigo = strcspn(linea, "#\n");
            for (size_t i = 0; i + largo_buscado <= codigo; ++i) {
                if (strncmp(linea + i, buscado, largo_buscado) != 0) continue;
                /* "sye x0,0" exacto: no "sye x0,0x10" ni "sye x0,016". */
                char siguiente = linea[i + largo_buscado];
                if (siguiente == ' ' || siguiente == '|' || siguiente == '\t' ||
                    siguiente == '#' || siguiente == '\n' || siguiente == '\0') {
                    *direccion = bundle * MET_BYTES_BUNDLE;
                    return 0;
                }
            }
            bundle++;
        }
        linea += largo;
        if (*linea == '\n') linea++;
    }
    return -1;
}

/*
 * El PC ya pasó por el salto final. Cuando main termina, el procesador
 * repite para siempre tres ciclos:
 *     busca el salto final       -> después el PC queda en fin + 16
 *     busca el bundle siguiente  -> después el PC queda en fin + 32
 *     el salto se ejecuta        -> después el PC vuelve a fin
 * fin + 32 no se usa como señal, porque ahí puede empezar main (el código
 * que se ejecuta ANTES de terminar). Como MET_TRAMO no es múltiplo de 3,
 * en dos revisiones seguidas el PC no cae las dos veces en fin + 32.
 */
static int llego_al_fin(uint32_t pc, uint32_t fin)
{
    return pc == fin || pc == fin + MET_BYTES_BUNDLE;
}

/* Explicación de un error del pipeline (sim_pipeline.h). */
static const char *explicar_pipeline(SimPipelineEstado e)
{
    switch (e) {
        case SIM_PIPE_PC:             return "el PC salió de la memoria de instrucciones";
        case SIM_PIPE_DECODIFICACION: return "una instrucción no se pudo decodificar";
        case SIM_PIPE_NO_SOPORTADA:   return "una instrucción no está soportada por el simulador";
        case SIM_PIPE_ALU:            return "error en la ALU";
        case SIM_PIPE_LSU:            return "acceso a memoria inválido (LSU)";
        case SIM_PIPE_BRU:            return "salto inválido (BRU)";
        case SIM_PIPE_CRYPTO:         return "error en la unidad CRIPTO";
        default:                      return "el simulador se detuvo con un error";
    }
}

/* Lo que pasó al correr el simulador (pasos 4 y 5). */
typedef enum { CORRIDA_FIN, CORRIDA_PRESUPUESTO, CORRIDA_ERROR, CORRIDA_SIN_MEMORIA } Corrida;

/* Revisa cómo quedó el simulador después de avanzar. Devuelve 0 si puede
 * seguir (o terminó), o CORRIDA_ERROR / CORRIDA_SIN_MEMORIA. */
static int revisar(const SimEjecucion *e, SimPipelineEstado *pipeline)
{
    SimEjecucionEstado st;
    if (sim_ejecucion_estado(e, &st) != SIM_EJECUCION_OK) return CORRIDA_ERROR;
    *pipeline = st.pipeline;
    if (st.pendiente || st.codigo == SIM_EJECUCION_MEMORIA) return CORRIDA_SIN_MEMORIA;
    if (st.pipeline != SIM_PIPE_ACTIVO && st.pipeline != SIM_PIPE_LIMITE &&
        st.pipeline != SIM_PIPE_TERMINADO) return CORRIDA_ERROR;
    return 0;
}

/*
 * Pasos 4 y 5. Corre "ruta" y deja en m->sim las métricas hasta el primer
 * ciclo en que el PC llega al salto final (o hasta el presupuesto).
 */
static Corrida correr(const char *ruta, MetMedicion *m, SimPipelineEstado *pipeline)
{
    const uint32_t fin = m->direccion_fin;
    SimEjecucion *e = NULL;
    if (sim_ejecucion_crear(ruta, SIM_EJECUCION_BALB, SIM_BUSQUEDA_FINITA, &e, NULL) !=
        SIM_EJECUCION_OK) return CORRIDA_ERROR;

    /* Paso 4: por tramos, hasta ver el PC en el salto final. */
    SimMetricasResumen r;
    uint64_t visto = 0;   /* Ciclo de la revisión que vio el fin. */
    int estado = 0;
    for (;;) {
        sim_ejecucion_ejecutar(e, MET_TRAMO);
        if ((estado = revisar(e, pipeline)) != 0) break;
        if (sim_ejecucion_resultados(e, &r) != SIM_METRICAS_OK) { estado = CORRIDA_ERROR; break; }
        if (llego_al_fin(r.pc, fin) || *pipeline == SIM_PIPE_TERMINADO) { visto = r.ciclos; break; }
        if (r.ciclos >= m->presupuesto) {
            /* No terminó: se quedan las métricas hasta aquí. */
            m->sim = r;
            sim_ejecucion_liberar(&e);
            return CORRIDA_PRESUPUESTO;
        }
    }
    if (estado != 0) {
        /* Las métricas hasta el error también sirven para mostrar algo. */
        if (sim_ejecucion_resultados(e, &r) == SIM_METRICAS_OK) m->sim = r;
        sim_ejecucion_liberar(&e);
        return (Corrida)estado;
    }
    if (*pipeline == SIM_PIPE_TERMINADO) {
        /* El código se acabó sin llegar al salto final: son las métricas finales. */
        m->sim = r;
        sim_ejecucion_liberar(&e);
        return CORRIDA_FIN;
    }

    /* Paso 5: desde cero otra vez. El fin está después de la revisión que
     * NO lo vio (dos tramos atrás, por si una revisión cayó en fin + 32):
     * se corre de un golpe hasta ahí y después ciclo por ciclo. */
    sim_ejecucion_liberar(&e);
    if (sim_ejecucion_crear(ruta, SIM_EJECUCION_BALB, SIM_BUSQUEDA_FINITA, &e, NULL) !=
        SIM_EJECUCION_OK) return CORRIDA_ERROR;
    uint64_t desde = visto > 2u * MET_TRAMO + 3u ? visto - 2u * MET_TRAMO - 3u : 0;
    if (desde > 0) sim_ejecucion_ejecutar(e, desde);

    Corrida resultado = CORRIDA_ERROR;
    for (uint64_t ciclo = desde; ciclo <= visto; ++ciclo) {
        if (sim_ejecucion_resultados(e, &r) != SIM_METRICAS_OK) break;
        if (r.pc == fin) {
            /* Primer ciclo con el PC en el salto final: main ya regresó. */
            m->sim = r;
            resultado = CORRIDA_FIN;
            break;
        }
        sim_ejecucion_avanzar(e);
        if (revisar(e, pipeline) != 0) break;
    }
    sim_ejecucion_liberar(&e);
    return resultado;
}

int met_medir(const char *texto, size_t largo, const char *ruta, uint64_t presupuesto,
              MetMedicion *m)
{
    memset(m, 0, sizeof *m);
    m->presupuesto = presupuesto != 0 ? presupuesto : MET_PRESUPUESTO_DEFECTO;

    /* Paso 1: ensamblador y .bin, como el botón Ensamblador. */
    IdeEnsamblador a;
    if (ide_ensamblador_generar(texto, largo, ruta, &a) != 0) return -1;
    if (a.estado == IDE_ASM_CON_ERRORES) {
        m->estado = MET_CON_ERRORES;
        ide_ensamblador_liberar(&a);
        return 0;
    }
    if (a.estado == IDE_ASM_SIN_BACKEND) {
        m->estado = MET_SIN_BACKEND;
        m->motivo = copiar(a.motivo != NULL ? a.motivo : "no se pudo generar el binario");
        ide_ensamblador_liberar(&a);
        return m->motivo != NULL ? 0 : -1;
    }
    m->bundles = a.bundles;

    /* Paso 2. */
    if (buscar_fin(a.ensamblador, &m->direccion_fin) != 0) {
        m->estado = MET_SIN_BACKEND;
        m->motivo = copiar("el ensamblador no tiene el salto final del arranque (sye x0,0)");
        ide_ensamblador_liberar(&a);
        return m->motivo != NULL ? 0 : -1;
    }

    /* Paso 3: junto al archivo, oculto: ".ide_<nombre>.bal.bin". */
    char *temporal = ide_ruta_temporal(ruta);
    char *bin = temporal != NULL ? malloc(strlen(temporal) + 5) : NULL;
    int estado = -1;
    if (bin != NULL) {
        strcpy(bin, temporal);
        strcat(bin, ".bin");
        if (ide_escribir_archivo(bin, (const char *)a.balb, a.largo_balb) == 0) {
            /* Pasos 4 y 5. */
            SimPipelineEstado pipeline = SIM_PIPE_ACTIVO;
            char texto_motivo[160];
            switch (correr(bin, m, &pipeline)) {
                case CORRIDA_FIN:
                    m->estado = MET_LISTO;
                    estado = 0;
                    break;
                case CORRIDA_PRESUPUESTO:
                    m->estado = MET_SIN_TERMINAR;
                    snprintf(texto_motivo, sizeof texto_motivo,
                             "el programa no terminó en %llu ciclos (¿un ciclo infinito?)",
                             (unsigned long long)m->presupuesto);
                    m->motivo = copiar(texto_motivo);
                    estado = m->motivo != NULL ? 0 : -1;
                    break;
                case CORRIDA_ERROR:
                    m->estado = MET_FALLO_SIMULADOR;
                    m->motivo = copiar(explicar_pipeline(pipeline));
                    estado = m->motivo != NULL ? 0 : -1;
                    break;
                case CORRIDA_SIN_MEMORIA:
                default:
                    break;
            }
        }
        remove(bin);
    }
    free(bin);
    free(temporal);
    ide_ensamblador_liberar(&a);
    if (estado != 0) met_liberar(m);
    return estado;
}

/* La conversión utiliza el conteo emitido, nunca bundles retirados o ciclos.
 * Comprobamos antes de multiplicar para poder reutilizarla fuera de IMEM actual. */
int met_calcular_bytes_codigo(size_t bundles, size_t *bytes)
{
    if (bytes == NULL || bundles > SIZE_MAX / MET_BYTES_BUNDLE) return -1;
    *bytes = bundles * MET_BYTES_BUNDLE;
    return 0;
}

/* Una ejecución cortada conserva su imagen completa. En cambio, un fallo de
 * compilación no acredita una imagen, aunque el llamador dejara un conteo viejo. */
int met_tamano_codigo(const MetMedicion *m, size_t *bytes)
{
    if (m == NULL || (m->estado != MET_LISTO && m->estado != MET_SIN_TERMINAR)) return -1;
    return met_calcular_bytes_codigo(m->bundles, bytes);
}

/* Usamos una magnitud y un signo separado: dos tamaños size_t pueden diferir
 * más que PTRDIFF_MAX. El porcentaje divide en coma flotante después de la
 * resta exacta, sin multiplicar enteros por 100 ni dividir por cero. */
int met_comparar_tamano_codigo(const MetMedicion *antes, const MetMedicion *despues,
                              MetComparacionTamano *comparacion)
{
    MetComparacionTamano c = {0};
    if (comparacion == NULL || met_tamano_codigo(antes, &c.original_bytes) != 0 ||
        met_tamano_codigo(despues, &c.optimizado_bytes) != 0) return -1;
    c.reduccion = c.optimizado_bytes < c.original_bytes;
    c.diferencia_bytes = c.reduccion ? c.original_bytes - c.optimizado_bytes
                                    : c.optimizado_bytes - c.original_bytes;
    c.porcentaje_definido = c.original_bytes != 0;
    if (c.porcentaje_definido) {
        c.variacion_porcentual = 100.0 * ((double)c.diferencia_bytes / (double)c.original_bytes);
        if (c.reduccion) c.variacion_porcentual = -c.variacion_porcentual;
    }
    *comparacion = c;
    return 0;
}

double met_porcentaje(uint64_t parte, uint64_t total)
{
    return total == 0 ? 0.0 : 100.0 * (double)parte / (double)total;
}

const char *met_nombre_slot(int slot)
{
    static const char *const nombres[4] = {"ALU", "LSU", "BRU", "CRIPTO"};
    return slot >= 0 && slot < 4 ? nombres[slot] : "?";
}

int met_comparar(const MetMedicion *antes, const MetMedicion *despues,
                 SimMetricasComparacion *comparacion)
{
    if (antes == NULL || despues == NULL || antes->estado != MET_LISTO ||
        despues->estado != MET_LISTO) return -1;
    return sim_metricas_comparar(&antes->sim, &despues->sim, comparacion) == SIM_METRICAS_OK
               ? 0 : -1;
}

void met_liberar(MetMedicion *m)
{
    if (m == NULL) return;
    free(m->motivo);
    memset(m, 0, sizeof *m);
}
