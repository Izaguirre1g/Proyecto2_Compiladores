/* Cargador de programas para el simulador VLIW.
 * Valida archivos MEM y BALB y organiza instrucciones y datos en IMEM/DMEM.
 * La carga no decodifica ni ejecuta las palabras recibidas. */
#include "sim_cargador.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Contrato de fb_escribir, en formato_binario.c. Su cabecera pública incluye
 * AST y símbolos; este lector conserva independencia de esas fases. */
enum { BALB_CABECERA = 48, BALB_MAX_DATOS = 16384 };

/* Emite un diagnóstico con argumentos variables y devuelve el estado recibido.
 * Una salida NULL omite el mensaje, pero conserva el mismo código de error. */
static SimCargaEstado fallo(FILE *salida, SimCargaEstado estado,
                            const char *formato, ...)
{
    if (salida) {
        /* formato describe el mensaje; args permite consumir los valores que
         * lo acompañan. va_end termina el recorrido iniciado por va_start. */
        va_list args;
        va_start(args, formato);
        fputs("cargador: ", salida);
        vfprintf(salida, formato, args);
        fputc('\n', salida);
        va_end(args);
    }
    return estado;
}

/* Reconstruye una palabra de 32 bits desde cuatro bytes little-endian:
 * p[0] contiene los bits menos significativos y p[3] los más altos.
 * El llamador debe proporcionar cuatro bytes legibles. */
static uint32_t leer32(const uint8_t *p)
{
    /* Cada byte se convierte antes de desplazarlo para operar en uint32_t.
     * Los desplazamientos colocan sus bits y el OR reúne las cuatro partes,
     * sin depender del orden de bytes ni del padding del equipo anfitrión. */
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Traduce un carácter hexadecimal a un valor entre 0 y 15. El -1 distingue
 * cualquier carácter ajeno a esa representación, incluso los espacios. */
static int hexadecimal(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Lee bundles hexadecimales de un archivo ya abierto y llena la imagen privada.
 * Cada línea no vacía aporta un bundle. Si hay un formato inválido o un error
 * de lectura, cargar() descartará la imagen parcial al recibir el estado. */
static SimCargaEstado leer_mem(FILE *f, SimImagen *imagen, FILE *diag)
{
    /* Acumuladores del bundle actual; solo se copian a IMEM al completar
     * una línea válida. digitos cuenta los caracteres hexadecimales leídos
     * y linea permite señalar dónde se encontró una entrada incorrecta. */
    uint32_t slots[SIM_CARGADOR_SLOTS] = {0};
    size_t digitos = 0, linea = 1;
    /* c es int para distinguir todos los bytes de EOF. fin_palabra recuerda
     * que hubo espacio después de un dígito: desde allí solo puede haber
     * más espacio o fin de línea, nunca una segunda secuencia hexadecimal. */
    int c, fin_palabra = 0;
    for (;;) {
        c = fgetc(f);
        if (c == '\n' || c == EOF) {
            /* EOF también permite procesar la última línea sin salto final.
             * ferror separa ese final normal de una falla real de lectura. */
            if (c == EOF && ferror(f))
                return fallo(diag, SIM_CARGA_IO, "error de lectura en .mem");
            /* Un bundle tiene cuatro palabras de ocho dígitos cada una: deben
             * llegar los 32 dígitos completos. Una palabra NOP usa ocho ceros;
             * un bundle con cuatro NOP usa 32. Una línea vacía no agrega nada. */
            if (digitos) {
                if (digitos != 32)
                    return fallo(diag, SIM_CARGA_FORMATO,
                                 ".mem, línea %zu: se requieren 32 dígitos hexadecimales", linea);
                /* Se comprueba la capacidad antes de usar el índice de IMEM.
                 * El incremento registra el bundle que acaba de copiarse. */
                if (imagen->cantidad_bundles == SIM_CARGADOR_IMEM_BUNDLES)
                    return fallo(diag, SIM_CARGA_FORMATO, ".mem excede los 1024 bundles de IMEM");
                memcpy(imagen->imem[imagen->cantidad_bundles++], slots, sizeof slots);
            }
            if (c == EOF) break;
            /* El bundle siguiente debe comenzar sin restos de la línea previa,
             * tanto en las palabras como en el control de espacios y longitud. */
            memset(slots, 0, sizeof slots);
            digitos = 0;
            fin_palabra = 0;
            ++linea;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            /* Se admiten márgenes y CR de CRLF. Antes del primer dígito no se
             * cierra la secuencia; después de él, fin_palabra impide cortarla. */
            if (digitos) fin_palabra = 1;
        } else {
            int h = hexadecimal(c);
            /* Se rechaza un carácter no hexadecimal, un dígito tras espacio
             * interno o un dígito adicional cuando el bundle ya está completo.
             * Así el cálculo de slot solo recibe índices entre 0 y 31. */
            if (h < 0 || fin_palabra || digitos == 32)
                return fallo(diag, SIM_CARGA_FORMATO,
                             ".mem, línea %zu: bundle hexadecimal inválido", linea);
            /* Ocho dígitos forman cada palabra: digitos / 8 identifica la
             * palabra textual. Restar de 3 invierte CRIPTO→ALU del archivo
             * para almacenarlas en el orden interno ALU=0 hasta CRIPTO=3. */
            size_t slot = 3 - digitos / 8;
            /* Desplazar cuatro bits abre espacio para el nuevo dígito; el OR
             * lo incorpora. El contador avanza para ubicar el próximo dígito. */
            slots[slot] = (slots[slot] << 4) | (uint32_t)h;
            ++digitos;
        }
    }
    /* Incluso un archivo bien formado en sus líneas debe aportar código;
     * vacío o compuesto solo por espacios no es una imagen ejecutable. */
    if (!imagen->cantidad_bundles)
        return fallo(diag, SIM_CARGA_FORMATO, ".mem no contiene bundles");
    return SIM_CARGA_OK;
}

/* Copia exactamente n bytes al destino proporcionado por el llamador.
 * Una lectura corta es IO si falló el flujo, o FORMATO si terminó el archivo
 * antes de completar la parte que el encabezado o el formato exigían. */
static SimCargaEstado leer_exacto(FILE *f, uint8_t *p, size_t n, FILE *diag)
{
    if (fread(p, 1, n, f) == n) return SIM_CARGA_OK;
    return fallo(diag, ferror(f) ? SIM_CARGA_IO : SIM_CARGA_FORMATO,
                 ferror(f) ? "error de lectura en BALB" : "archivo BALB truncado");
}

/* Interpreta BALB v1 desde el inicio del archivo y llena la imagen privada.
 * Primero valida todos los metadatos; luego lee código y datos en ese orden.
 * Un rechazo devuelve su causa sin publicar la imagen al usuario de la API. */
static SimCargaEstado leer_balb(FILE *f, SimImagen *imagen, FILE *diag)
{
    /* Los primeros 48 bytes son metadatos: cuatro de firma y once campos de
     * cuatro bytes. No se pueden tratar como el primer bundle de código. */
    uint8_t cabecera[BALB_CABECERA];
    SimCargaEstado estado = leer_exacto(f, cabecera, sizeof cabecera, diag);
    if (estado != SIM_CARGA_OK) return estado;
    /* La firma identifica el contenedor y permite rechazar binarios crudos
     * u otros archivos antes de interpretar sus bytes como campos BALB. */
    if (memcmp(cabecera, "BALB", 4))
        return fallo(diag, SIM_CARGA_FORMATO, "firma BALB inválida");

    uint32_t campos[11];
    /* La firma ocupa los primeros cuatro bytes. Cada iteración avanza otros
     * cuatro y reconstruye un campo little-endian, sin alinear punteros C. */
    for (size_t i = 0; i < 11; ++i) campos[i] = leer32(cabecera + 4 + 4 * i);
    /* Los tamaños cuentan bytes; los offsets localizan secciones dentro del
     * archivo. entrada y las bases son direcciones del procesador: pertenecen
     * a IMEM/DMEM y no incluyen el encabezado del contenedor. */
    uint32_t version = campos[0], encabezado = campos[1], total = campos[2];
    uint32_t entrada = campos[3], offset_codigo = campos[4], codigo = campos[5];
    uint32_t base_codigo = campos[6], offset_datos = campos[7], datos = campos[8];
    uint32_t base_datos = campos[9], flags = campos[10];

    /* Solo se conoce esta versión y su disposición fija. Otro encabezado
     * podría cambiar el significado o la posición de los campos restantes. */
    if (version != 1 || encabezado != BALB_CABECERA)
        return fallo(diag, SIM_CARGA_FORMATO, "versión o tamaño de encabezado BALB incompatible");
    /* Se exige al menos un bundle, sin palabras parciales al final, y una
     * longitud que quepa en los 1024 bundles de IMEM. */
    if (!codigo || codigo % SIM_CARGADOR_BYTES_BUNDLE ||
        codigo > SIM_CARGADOR_IMEM_BUNDLES * SIM_CARGADOR_BYTES_BUNDLE)
        return fallo(diag, SIM_CARGA_FORMATO, "tamaño de código inválido o fuera de IMEM");
    /* BALB v1 carga en IMEM[0] y comienza en PC=0. El offset 48 es una
     * posición del archivo, no una dirección ni una exigencia de alineación. */
    if (entrada != 0 || base_codigo != 0)
        return fallo(diag, SIM_CARGA_FORMATO, "entrada y dirección de código BALB v1 deben ser cero");
    /* Código empieza tras el encabezado; datos, tras el código; total incluye
     * las tres partes. Estas igualdades excluyen huecos y solapamientos.
     * Convertir antes de sumar impide que un tamaño corrupto de 32 bits
     * desborde la operación y aparente satisfacer las igualdades. */
    if (offset_codigo != BALB_CABECERA ||
        (uint64_t)offset_datos != (uint64_t)BALB_CABECERA + codigo ||
        (uint64_t)total != (uint64_t)BALB_CABECERA + codigo + datos)
        return fallo(diag, SIM_CARGA_FORMATO, "tamaños o desplazamientos BALB incompatibles");
    /* DMEM se direcciona por byte. No se exige alineación de los datos que
     * fb_escribir no exige; incluso la sección vacía puede empezar en 65536. */
    /* Se limita el tamaño al admitido por BALB y la base a DMEM. Solo después
     * de validar la base se resta: el cortocircuito evita un desbordamiento
     * sin signo y permite comprobar si los datos caben desde esa dirección. */
    if (datos > BALB_MAX_DATOS || base_datos > SIM_CARGADOR_DMEM_BYTES ||
        datos > SIM_CARGADOR_DMEM_BYTES - base_datos)
        return fallo(diag, SIM_CARGA_FORMATO, "sección de datos fuera de los límites de BALB o DMEM");
    /* Solo está definido el bit 0: indica que el arranque también inicializa
     * datos. El cargador lo conserva, pero no ejecuta esa inicialización. */
    if (flags > 1)
        return fallo(diag, SIM_CARGA_FORMATO, "flags BALB no reconocidos");

    /* La longitud validada se convierte de bytes a bundles. Estos metadatos
     * todavía pertenecen a una imagen privada: una lectura posterior que
     * falle hará que cargar() la libere completa. */
    imagen->cantidad_bundles = codigo / SIM_CARGADOR_BYTES_BUNDLE;
    imagen->cantidad_datos = datos;
    imagen->entrada = entrada;
    imagen->base_datos = base_datos;
    imagen->flags = flags;
    for (size_t b = 0; b < imagen->cantidad_bundles; ++b) {
        /* Cada grupo de 16 bytes contiene ALU, LSU, BRU y CRIPTO en ese orden.
         * leer32 reconstruye cada palabra sin invertir sus bytes ni los slots. */
        uint8_t bytes[SIM_CARGADOR_BYTES_BUNDLE];
        estado = leer_exacto(f, bytes, sizeof bytes, diag);
        if (estado != SIM_CARGA_OK) return estado;
        for (size_t s = 0; s < SIM_CARGADOR_SLOTS; ++s)
            imagen->imem[b][s] = leer32(bytes + 4 * s);
    }
    if (datos) {
        /* La base es una dirección de DMEM, no el offset del archivo. El flujo
         * ya quedó al inicio de datos después de consumir todo el código. */
        estado = leer_exacto(f, imagen->dmem + base_datos, datos, diag);
        if (estado != SIM_CARGA_OK) return estado;
    }
    /* Consumidas las longitudes declaradas, el siguiente intento debe llegar
     * al fin real. Un byte extra contradice total; un error del flujo sigue
     * siendo IO, aunque ya se hayan leído instrucciones y datos. */
    if (fgetc(f) != EOF)
        return fallo(diag, SIM_CARGA_FORMATO, "BALB contiene bytes adicionales al tamaño declarado");
    if (ferror(f)) return fallo(diag, SIM_CARGA_IO, "error al comprobar el final de BALB");
    return SIM_CARGA_OK;
}

/* Ambos formatos reciben el mismo flujo, imagen privada y diagnóstico.
 * Este puntero permite compartir la apertura, la reserva y la limpieza. */
typedef SimCargaEstado (*Lector)(FILE *, SimImagen *, FILE *);

/* Administra la carga completa y sus recursos para cualquiera de los lectores.
 * Devuelve la imagen mediante el doble puntero solo si lectura y cierre
 * terminan bien; ante cualquier fallo devuelve el estado y descarta la reserva. */
static SimCargaEstado cargar(const char *ruta, SimImagen **imagen, FILE *diag,
                             Lector lector)
{
    /* La comprobación precede a abrir o reservar recursos. El cortocircuito
     * evita desreferenciar punteros NULL; exigir *imagen == NULL impide
     * sobrescribir una imagen viva y perder la referencia que permite liberarla. */
    if (!ruta || !*ruta || !imagen || *imagen)
        return fallo(diag, SIM_CARGA_ARGUMENTO,
                     "ruta y salida requeridas; inicialice la imagen en NULL");
    /* El modo binario conserva cada byte de BALB y deja que el lector MEM
     * gestione explícitamente LF/CRLF, sin traducciones del entorno. */
    FILE *f = fopen(ruta, "rb");
    if (!f) return fallo(diag, SIM_CARGA_IO, "no se pudo abrir '%s': %s", ruta, strerror(errno));
    /* calloc reserva la imagen completa e inicializa sus bytes en cero.
     * Las zonas de IMEM/DMEM que el archivo no carga quedan así definidas. */
    SimImagen *nueva = calloc(1, sizeof *nueva);
    if (!nueva) {
        /* Todavía no hay imagen para liberar, pero el archivo sí está abierto. */
        fclose(f);
        return fallo(diag, SIM_CARGA_MEMORIA, "no se pudo reservar la imagen");
    }
    /* lector selecciona leer_mem o leer_balb. El archivo se cierra siempre,
     * incluso cuando el lector detectó un error después de una carga parcial. */
    SimCargaEstado estado = lector(f, nueva, diag);
    int cierre = fclose(f);
    /* Un cierre fallido invalida una lectura correcta. Si ya había un error
     * de lectura o formato, se conserva esa primera causa. */
    if (estado == SIM_CARGA_OK && cierre != 0)
        estado = fallo(diag, SIM_CARGA_IO, "error al cerrar '%s'", ruta);
    /* La reserva privada se descarta al fallar. Solo la asignación de la rama
     * de éxito entrega su propiedad al llamador: nunca ve una imagen parcial. */
    if (estado != SIM_CARGA_OK) free(nueva);
    else *imagen = nueva;
    return estado;
}

/* Entrada pública para la imagen hexadecimal; comparte las garantías de cargar. */
SimCargaEstado sim_cargar_mem(const char *ruta, SimImagen **imagen, FILE *diag)
{
    return cargar(ruta, imagen, diag, leer_mem);
}

/* Entrada pública para el contenedor; nunca interpreta su encabezado como código. */
SimCargaEstado sim_cargar_balb(const char *ruta, SimImagen **imagen, FILE *diag)
{
    return cargar(ruta, imagen, diag, leer_balb);
}

/* Devuelve al sistema la reserva entregada por una carga correcta. Al poner
 * el puntero del llamador en NULL se puede volver a cargar o liberar sin
 * repetir la liberación de esa reserva. No modifica otros alias del puntero. */
void sim_liberar_imagen(SimImagen **imagen)
{
    if (imagen) {
        free(*imagen);
        *imagen = NULL;
    }
}
