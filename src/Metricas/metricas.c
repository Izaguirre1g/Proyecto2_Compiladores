// MAPA IDE: BAL del editor -> ensamblador/BALB tradicional -> M2 -> MetMedicion.
// Los niveles O0/O1/O2 reutilizan met_simular(), pero O2 usa un productor distinto.

/*
 * Métricas del código del editor (ver include/metricas.h).
 * El tamaño estático procede de los bundles emitidos; no del simulador.
 *
 * Pasos de met_medir:
 *   1. Generar el ensamblador y el .bin con el botón Ensamblador
 *      (ide_ensamblador_generar): es el mismo binario que hace balc.
 *      Se compila MET_COMPILACIONES veces para medir el tiempo de
 *      compilación (la mediana: ver medir_compilacion).
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

/* Cada cuántos ciclos se revisa el PC en el paso 4. Es PRIMO: el ciclo
 * final dura pocos ciclos (3 en -O0, 5 en -O2), y si el tramo fuera su
 * múltiplo, cada revisión vería el PC siempre en el mismo punto del ciclo. */
#define MET_TRAMO 997u

/* Cuántos ciclos seguidos tiene que quedarse el PC en el ciclo final para
 * aceptar que main terminó (ver sigue_en_el_fin). */
#define MET_SOSTENIDO 40u

/* Copia una cadena (NULL si falta memoria). */
// IDE: reserva una copia de texto para avisos y diagnosticos.
// La ventana puede conservar la información después de liberar los temporales.
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
// IDE: localiza en el ensamblador el salto final que señala el retorno de main.
// Devuelve una dirección de bundle en bytes, no un índice de instruccion.
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
            // El recorrido del ASM busca el salto final del arranque sin ejecutar el programa.
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
 * ¿Está el PC dentro del ciclo final? Cuando main termina, el procesador
 * repite para siempre el ciclo final del arranque:
 *   -O0: busca fin, fin + 16 y fin + 32, y el salto lo regresa a fin;
 *   -O2: busca fin ... fin + 64 (5 bundles) y vuelve a fin.
 * Ojo: el PC también puede pasar UNA vez por esas direcciones sin que main
 * haya terminado (en -O2, al arrancar, el procesador busca el bundle de
 * fin antes de que la llamada a main se ejecute). Por eso no basta con
 * verlo una vez: ver sigue_en_el_fin.
 */
// IDE: comprueba si el PC pertenece a la region de salida esperada.
// Evita aceptar como final cualquier salto transitorio a una dirección parecida.
static int llego_al_fin(uint32_t pc, uint32_t fin, uint32_t ancho)
{
    return pc >= fin && pc <= fin + ancho;
}

/* Explicación de un error del pipeline (sim_pipeline.h). */
// IDE: convierte un estado de error de S6 en un mensaje legible.
// Permite informar si fallo PC, decodificacion, ALU, LSU, BRU o CRIPTO.
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
// IDE: comprueba el estado completo de la sesion M2 después de avanzar.
// Distingue pausa normal, finalización y errores o observaciones pendientes.
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
 * ¿Se queda el PC en el ciclo final durante MET_SOSTENIDO ciclos? Avanza
 * ciclo por ciclo. 1 si se quedó (main terminó de verdad), 0 si salió (era
 * un paso de una sola vez) o -1 si el simulador falló.
 */
// IDE: confirma que el PC permanece en la zona de salida por varios ciclos.
// Evita declarar que main termino al observar una visita pasajera.
static int sigue_en_el_fin(SimEjecucion *e, uint32_t fin, uint32_t ancho, SimPipelineEstado *pipeline)
{
    SimMetricasResumen r;
    for (unsigned i = 0; i < MET_SOSTENIDO; ++i) {
        sim_ejecucion_avanzar(e);
        if (revisar(e, pipeline) != 0 || sim_ejecucion_resultados(e, &r) != SIM_METRICAS_OK) return -1;
        // La terminación por agotamiento de imagen se maneja por separado del bucle de salida.
        if (*pipeline == SIM_PIPE_TERMINADO) return 1;
        // Si el PC abandona la zona final, el candidato de terminación se descarta.
        if (!llego_al_fin(r.pc, fin, ancho)) return 0;
    }
    return 1;
}

/*
 * Pasos 4 y 5. Corre "ruta" y deja en m->sim las métricas hasta el primer
 * ciclo en que el PC llega al salto final (o hasta el presupuesto).
 * Si "memoria" no es NULL, al terminar main copia ahí la memoria de datos
 * (MET_BYTES_MEMORIA bytes): ahí quedan las variables de main.
 */
// IDE: ejecuta el programa por tramos y después afina el ciclo de terminación.
// El primer recorrido busca la zona final; el segundo localiza el corte exacto.
static Corrida correr(const char *ruta, MetMedicion *m, SimPipelineEstado *pipeline,
                     uint8_t *memoria)
{
    const uint32_t fin = m->direccion_fin;
    const uint32_t ancho = m->ancho_fin != 0 ? m->ancho_fin : MET_ANCHO_FIN_O0;
    SimEjecucion *e = NULL;
    // El IDE utiliza una sesion M2 real, cargada desde la imagen BALB.
    if (sim_ejecucion_crear(ruta, SIM_EJECUCION_BALB, SIM_BUSQUEDA_FINITA, &e, NULL) !=
        SIM_EJECUCION_OK) return CORRIDA_ERROR;

    /* Paso 4: por tramos, hasta ver el PC en el ciclo final y que se quede ahí. */
    SimMetricasResumen r;
    uint64_t visto = 0;   /* Ciclo de la revisión que vio el fin. */
    int estado = 0;
    for (;;) {
        // Se avanza por tramos para evitar calcular la huella de 64 KiB en cada flanco.
        sim_ejecucion_ejecutar(e, MET_TRAMO);
        if ((estado = revisar(e, pipeline)) != 0) break;
        if (sim_ejecucion_resultados(e, &r) != SIM_METRICAS_OK) { estado = CORRIDA_ERROR; break; }
        if (*pipeline == SIM_PIPE_TERMINADO) { visto = r.ciclos; break; }
        // Detectar un PC en la salida no basta: se espera estabilidad antes de aceptar el corte.
        if (llego_al_fin(r.pc, fin, ancho)) {
            int queda = sigue_en_el_fin(e, fin, ancho, pipeline);
            if (queda < 0) { estado = CORRIDA_ERROR; break; }
            if (queda == 1) { visto = r.ciclos; break; }
        }
        // Al agotar el presupuesto reportamos SIN TERMINAR en vez de inventar resultado final.
        if (r.ciclos >= m->presupuesto) {
            /* No terminó: se quedan las métricas hasta aquí. */
            m->sim = r;
            // Las sesiones temporales se liberan cuando se alcanza el corte o aparece un error.
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
        if (memoria != NULL && sim_ejecucion_memoria(e, memoria, MET_BYTES_MEMORIA) != SIM_EJECUCION_OK) {
            sim_ejecucion_liberar(&e);
            return CORRIDA_ERROR;
        }
        sim_ejecucion_liberar(&e);
        return CORRIDA_FIN;
    }

    /* Paso 5: desde cero otra vez. Como cada revisión mira todo el ciclo
     * final, main terminó en el último tramo antes de "visto" (se dejan dos
     * de margen): se corre de un golpe hasta ahí y después ciclo por ciclo,
     * buscando el PRIMER ciclo con el PC en fin a partir del cual el PC ya
     * no sale del ciclo final. */
    sim_ejecucion_liberar(&e);
    if (sim_ejecucion_crear(ruta, SIM_EJECUCION_BALB, SIM_BUSQUEDA_FINITA, &e, NULL) !=
        SIM_EJECUCION_OK) return CORRIDA_ERROR;
    // La segunda corrida salta al tramo cercano al final para ahorrar consultas frecuentes.
    uint64_t desde = visto > 2u * MET_TRAMO + 3u ? visto - 2u * MET_TRAMO - 3u : 0;
    // Segunda corrida: se aproxima al punto final y luego afina ciclo por ciclo.
    if (desde > 0) sim_ejecucion_ejecutar(e, desde);

    Corrida resultado = CORRIDA_ERROR;
    int hay_candidato = 0;            /* Ya se vio el PC en fin y sigue en el ciclo final. */
    SimMetricasResumen candidato;     /* Las métricas de ese primer ciclo. */
    uint64_t ciclo_candidato = 0;
    for (uint64_t ciclo = desde; ciclo <= visto + MET_SOSTENIDO; ++ciclo) {
        if (sim_ejecucion_resultados(e, &r) != SIM_METRICAS_OK) break;
        if (!llego_al_fin(r.pc, fin, ancho)) {
            hay_candidato = 0;        /* Salió: era un paso de una sola vez. */
        } else if (!hay_candidato && r.pc == fin) {
            hay_candidato = 1;        /* Primer ciclo con el PC en el salto final. */
            candidato = r;
            ciclo_candidato = ciclo;
        }
        // Un PC estable durante suficientes ciclos acredita el primer corte de main.
        if (hay_candidato && ciclo - ciclo_candidato >= MET_SOSTENIDO) {
            /* Se quedó: main ya había regresado en el ciclo candidato. En el
             * ciclo final no se escribe memoria, así que es la misma de ese ciclo. */
            m->sim = candidato;
            resultado = CORRIDA_FIN;
            if (memoria != NULL && sim_ejecucion_memoria(e, memoria, MET_BYTES_MEMORIA) != SIM_EJECUCION_OK)
                resultado = CORRIDA_ERROR;
            break;
        }
        sim_ejecucion_avanzar(e);
        if (revisar(e, pipeline) != 0) break;
    }
    sim_ejecucion_liberar(&e);
    return resultado;
}

/* Orden de qsort: de la compilación más rápida a la más lenta (por el total). */
// IDE: comparador para ordenar duraciones de compilacion.
// Se utiliza para tomar una mediana en vez de confiar en una medicion aislada.
static int comparar_tiempos(const void *a, const void *b)
{
    double x = ((const IdeAsmTiempos *)a)->total, y = ((const IdeAsmTiempos *)b)->total;
    return (x > y) - (x < y);
}

/*
 * Tiempo de compilación: "primera" es la compilación que ya se hizo; se
 * compila MET_COMPILACIONES - 1 veces más (solo para medir) y se toma la
 * corrida MEDIANA por tiempo total, con sus fases. Así las fases suman lo
 * mismo que ese total. Devuelve 0 bien, -1 si una compilación falló.
 */
// IDE: repite compilaciones y toma la mediana del tiempo total.
// Las mediciones describen compilacion, no ciclos de simulación.
static int medir_compilacion(const char *texto, size_t largo, const char *ruta,
                             const IdeAsmTiempos *primera, IdeAsmTiempos *mediana)
{
    IdeAsmTiempos corridas[MET_COMPILACIONES];
    corridas[0] = *primera;
    for (int i = 1; i < MET_COMPILACIONES; ++i) {
        IdeEnsamblador otra;
        // Se obtiene ensamblador y BALB reales antes de invocar el simulador.
        if (ide_ensamblador_generar(texto, largo, ruta, &otra) != 0) return -1;
        corridas[i] = otra.tiempos;
        ide_ensamblador_liberar(&otra);
    }
    // Se ordenan las duraciones y se elige la central para reducir ruido.
    qsort(corridas, MET_COMPILACIONES, sizeof corridas[0], comparar_tiempos);
    *mediana = corridas[MET_COMPILACIONES / 2];
    return 0;
}

// API IDE: expone la localizacion de la dirección de finalización.
// La deteccion consulta el ensamblador generado por la ruta correspondiente.
int met_buscar_fin(const char *ensamblador, uint32_t *direccion)
{
    if (ensamblador == NULL || direccion == NULL) return -1;
    return buscar_fin(ensamblador, direccion);
}

// API IDE: ejecuta una imagen BALB real con el simulador M2.
// Deja ciclos, IPC y operaciones en MetMedicion; puede copiar DMEM final.
// Esta API se reutiliza también en la comparación de niveles O0/O1/O2.
int met_simular(const unsigned char *balb, size_t largo_balb, const char *ruta,
                uint8_t *memoria, MetMedicion *m)
{
    if (balb == NULL || m == NULL) return -1;
    if (m->presupuesto == 0) m->presupuesto = MET_PRESUPUESTO_DEFECTO;

    /* Paso 3: junto al archivo, oculto: ".ide_<nombre>.bal.bin". */
    char *temporal = ide_ruta_temporal(ruta);
    char *bin = temporal != NULL ? malloc(strlen(temporal) + 5) : NULL;
    int estado = -1;
    if (bin != NULL) {
        strcpy(bin, temporal);
        strcat(bin, ".bin");
        if (ide_escribir_archivo(bin, (const char *)balb, largo_balb) == 0) {
            /* Pasos 4 y 5. */
            SimPipelineEstado pipeline = SIM_PIPE_ACTIVO;
            char texto_motivo[160];
            // El resultado distingue termino, presupuesto agotado y errores del simulador.
            switch (correr(bin, m, &pipeline, memoria)) {
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
        // El archivo BALB temporal se elimina independientemente del resultado.
        remove(bin);
    }
    free(bin);
    free(temporal);
    return estado;
}

// FUNCIÓN CENTRAL DE METRICAS IDE: compila el código del editor y lo simula.
// Usa el backend tradicional; el boton de niveles usa otra ruta para O2.
// La ruta del boton Metricas es la compilacion tradicional del editor, NO el backend O2.
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
    /* El tiempo de compilación (solo si el programa es válido: con errores
     * la compilación se detiene en el semántico y no hay nada que medir). */
    if (medir_compilacion(texto, largo, ruta, &a.tiempos, &m->compilacion) != 0) {
        ide_ensamblador_liberar(&a);
        return -1;
    }
    if (a.estado == IDE_ASM_SIN_BACKEND) {
        m->estado = MET_SIN_BACKEND;
        m->motivo = copiar(a.motivo != NULL ? a.motivo : "no se pudo generar el binario");
        ide_ensamblador_liberar(&a);
        return m->motivo != NULL ? 0 : -1;
    }
    // Cantidad de bundles ESTATICOS de la imagen, independiente de los ciclos dinamicos.
    m->bundles = a.bundles;

    /* Paso 2. */
    // El PC final se deriva del ensamblador, no de adivinar cuando acaba main.
    if (buscar_fin(a.ensamblador, &m->direccion_fin) != 0) {
        m->estado = MET_SIN_BACKEND;
        m->motivo = copiar("el ensamblador no tiene el salto final del arranque (sye x0,0)");
        ide_ensamblador_liberar(&a);
        return m->motivo != NULL ? 0 : -1;
    }

    /* Pasos 3 a 5. */
    int estado = met_simular(a.balb, a.largo_balb, ruta, NULL, m);
    ide_ensamblador_liberar(&a);
    if (estado != 0) met_liberar(m);
    return estado;
}

/* La conversión utiliza el conteo emitido, nunca bundles retirados o ciclos.
 * Comprobamos antes de multiplicar para poder reutilizarla fuera de IMEM actual. */
// IDE: calcula la huella estatica de los bundles del programa.
// Cada bundle VLIW ocupa 16 bytes, independientemente de cuantos slots sean NOP.
int met_calcular_bytes_codigo(size_t bundles, size_t *bytes)
{
    if (bytes == NULL || bundles > SIZE_MAX / MET_BYTES_BUNDLE) return -1;
    *bytes = bundles * MET_BYTES_BUNDLE;
    return 0;
}

/* Una ejecución cortada conserva su imagen completa. En cambio, un fallo de
 * compilación no acredita una imagen, aunque el llamador dejara un conteo viejo. */
// IDE: obtiene el tamaño estático de una medicion terminada o limitada.
// No cuenta ciclos ni bytes efectivamente leidos de memoria.
int met_tamano_codigo(const MetMedicion *m, size_t *bytes)
{
    if (m == NULL || (m->estado != MET_LISTO && m->estado != MET_SIN_TERMINAR)) return -1;
    // La huella estatica se calcula multiplicando por 16; los NOP también ocupan bits.
    return met_calcular_bytes_codigo(m->bundles, bytes);
}

/* Usamos una magnitud y un signo separado: dos tamaños size_t pueden diferir
 * más que PTRDIFF_MAX. El porcentaje divide en coma flotante después de la
 * resta exacta, sin multiplicar enteros por 100 ni dividir por cero. */
// IDE: compara la huella de código de dos compilaciones.
// La diferencia de bytes no demuestra por si sola mejor rendimiento.
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

// IDE: convierte parte/total a porcentaje para las gráficas.
// Un denominador cero produce cero y evita division invalida.
double met_porcentaje(uint64_t parte, uint64_t total)
{
    return total == 0 ? 0.0 : 100.0 * (double)parte / (double)total;
}

// IDE: traduce el índice de slot al nombre ALU, LSU, BRU o CRIPTO.
// El orden coincide con los cuatro campos del bundle del procesador.
const char *met_nombre_slot(int slot)
{
    static const char *const nombres[4] = {"ALU", "LSU", "BRU", "CRIPTO"};
    return slot >= 0 && slot < 4 ? nombres[slot] : "?";
}

// IDE: obtiene del recolector M1 las diferencias de rendimiento.
// Solo compara mediciones válidas: no confunde una pausa con terminación.
int met_comparar(const MetMedicion *antes, const MetMedicion *despues,
                 SimMetricasComparacion *comparacion)
{
    if (antes == NULL || despues == NULL || antes->estado != MET_LISTO ||
        despues->estado != MET_LISTO) return -1;
    return sim_metricas_comparar(&antes->sim, &despues->sim, comparacion) == SIM_METRICAS_OK
               ? 0 : -1;
}

// IDE: libera textos y restablece una MetMedicion.
// No libera el simulador: la sesion ya fue cerrada al medir.
void met_liberar(MetMedicion *m)
{
    if (m == NULL) return;
    free(m->motivo);
    memset(m, 0, sizeof *m);
}
