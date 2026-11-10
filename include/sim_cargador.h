/* Interfaz del cargador VLIW: imágenes de instrucciones y datos, estados
 * de carga y liberación de recursos. No ofrece ejecución de instrucciones. */
#ifndef SIM_CARGADOR_H
#define SIM_CARGADOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Organización de rtl/isa_defs.sv y límites del escritor BALB del Proyecto I.
 * Se expresan aquí porque los límites de IMEM/DMEM no tienen una cabecera C
 * compartida; el cargador no depende del parser ni de la tabla de símbolos. */
enum {
    SIM_CARGADOR_SLOTS = 4,
    SIM_CARGADOR_BYTES_BUNDLE = 16,
    SIM_CARGADOR_IMEM_BUNDLES = 1024,
    SIM_CARGADOR_DMEM_BYTES = 65536
};

/* El retorno distingue argumentos inválidos, errores del sistema de archivos,
 * archivos incompatibles y falta de memoria. Solo OK entrega una imagen. */
typedef enum {
    SIM_CARGA_OK = 0,
    SIM_CARGA_ARGUMENTO,
    SIM_CARGA_IO,
    SIM_CARGA_FORMATO,
    SIM_CARGA_MEMORIA
} SimCargaEstado;

typedef struct {
    /* Palabras numéricas: slots 0 ALU, 1 LSU, 2 BRU y 3 CRIPTO.
     * No se utiliza CbBundle: su texto y línea pertenecen al ensamblador.
     * Solo los primeros cantidad_bundles proceden del archivo. */
    uint32_t imem[SIM_CARGADOR_IMEM_BUNDLES][SIM_CARGADOR_SLOTS];
    /* Direcciones de byte de DMEM: la sección de datos se copia en base_datos;
     * el resto queda en cero y no representa datos aportados por el archivo. */
    uint8_t dmem[SIM_CARGADOR_DMEM_BYTES];
    /* Extensiones cargadas: bundles para código y bytes para datos. No son
     * contadores de instrucciones ejecutadas ni de accesos a memoria. */
    size_t cantidad_bundles;
    size_t cantidad_datos;
    /* BALB v1 comienza en PC=0; base_datos localiza datos en DMEM. El bit 0
     * de flags informa si el arranque también los inicializa al ejecutarse. */
    uint32_t entrada;
    uint32_t base_datos;
    uint32_t flags;
} SimImagen;

/* imagen debe apuntar a un puntero inicializado en NULL. La carga es
 * transaccional: solo publica la imagen tras validar y cerrar el archivo.
 * Un error conserva el puntero recibido y libera toda reserva interna.
 * El cargador abre y cierra el archivo; diagnosticos es prestado, nunca se
 * cierra y puede ser NULL. En otro caso recibe la causa del error.
 * Las posiciones no cargadas de ambas memorias quedan en cero.
 * .mem no aporta datos ni metadatos: entrada, base y flags quedan en cero. */
SimCargaEstado sim_cargar_mem(const char *ruta, SimImagen **imagen,
                             FILE *diagnosticos);
/* Aplica la misma propiedad de recursos a BALB: valida encabezado/secciones,
 * coloca ALU en slot 0 y copia datos a su base. No acepta binario crudo. */
SimCargaEstado sim_cargar_balb(const char *ruta, SimImagen **imagen,
                              FILE *diagnosticos);

/* Libera la imagen y deja el puntero en NULL. Admite NULL y llamadas repetidas. */
void sim_liberar_imagen(SimImagen **imagen);

#endif
