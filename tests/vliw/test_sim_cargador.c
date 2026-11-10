/* Pruebas del cargador VLIW: imágenes válidas, archivos corruptos y recursos.
 * Verifica datos y estados de retorno, sin ejecutar instrucciones. */
#define _POSIX_C_SOURCE 200809L
#include "sim_cargador.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Cuenta condiciones comprobadas, incluidas las posiciones de memoria;
 * el total no equivale al número de escenarios de entrada diferentes. */
static size_t comprobaciones;
static char ruta[] = "/tmp/balb-cargador-XXXXXX";

/* Ante la primera discrepancia se informa la condición y se termina con 1.
 * La suite Python usa ese estado para detectar el fallo del ejecutable C. */
#define COMPROBAR(condicion) do { \
    ++comprobaciones; \
    if (!(condicion)) { \
        fprintf(stderr, "FAIL línea %d: %s\n", __LINE__, #condicion); \
        exit(1); \
    } \
} while (0)

/* La variante con --wrap fuerza fallos sin agregar controles de prueba al
 * cargador. fclose cierra realmente el archivo antes de simular el error. */
#ifdef SIM_PRUEBA_FALLOS
static int sin_memoria, error_cierre;
void *__real_calloc(size_t n, size_t bytes);
int __real_fclose(FILE *f);
void *__wrap_calloc(size_t n, size_t bytes)
{
    /* sin_memoria fuerza NULL solo durante el escenario de reserva fallida;
     * en el resto de pruebas se utiliza la reserva real del sistema. */
    return sin_memoria ? NULL : __real_calloc(n, bytes);
}
int __wrap_fclose(FILE *f)
{
    /* Se libera el descriptor incluso al devolver EOF deliberadamente.
     * Así se prueba la respuesta al error sin introducir una fuga en el test. */
    int estado = __real_fclose(f);
    return error_cierre ? EOF : estado;
}
#endif

/* Reemplaza el contenido del archivo temporal por exactamente n bytes.
 * Permite construir entradas válidas y truncadas sin alterar fixtures. */
static void escribir(const void *bytes, size_t n)
{
    FILE *f = fopen(ruta, "wb");
    COMPROBAR(f != NULL);
    COMPROBAR(fwrite(bytes, 1, n, f) == n);
    COMPROBAR(fclose(f) == 0);
}

/* Serializa un campo little-endian del encabezado de prueba. Cada turno
 * extrae el siguiente byte de v y lo coloca desde menor a mayor dirección. */
static void poner32(unsigned char *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(v >> (8 * i));
}

/* Construye los 48 bytes del encabezado para las longitudes y base indicadas.
 * El llamador prepara el payload aparte y puede alterar campos para probar
 * rechazos; esta función no valida los valores que recibe. */
static void cabecera(unsigned char *bytes, uint32_t codigo, uint32_t datos,
                     uint32_t base, uint32_t flags)
{
    /* Se construye el contrato publicado de BALB v1: no se serializa una
     * estructura C ni se usa el lector para construir sus propios oráculos. */
    uint32_t campos[] = {1, 48, 48 + codigo + datos, 0, 48, codigo, 0,
                         48 + codigo, datos, base, flags};
    memcpy(bytes, "BALB", 4);
    for (size_t i = 0; i < 11; ++i) poner32(bytes + 4 + 4 * i, campos[i]);
}

/* Escribe una entrada inválida y selecciona MEM o BALB con el parámetro balb.
 * Espera FORMATO, salida NULL y un diagnóstico que contenga mensaje: fallar
 * por una causa distinta tampoco debe hacer pasar el escenario. */
static void rechazo(int balb, const void *bytes, size_t n, const char *mensaje)
{
    escribir(bytes, n);
    SimImagen *imagen = NULL;
    FILE *diag = tmpfile();
    COMPROBAR(diag != NULL);
    SimCargaEstado estado = balb ? sim_cargar_balb(ruta, &imagen, diag)
                                 : sim_cargar_mem(ruta, &imagen, diag);
    COMPROBAR(estado == SIM_CARGA_FORMATO);
    COMPROBAR(imagen == NULL);
    char texto[256] = {0};
    /* El diagnóstico quedó escrito al final del flujo. Se vuelve al inicio
     * para leerlo; el cero reservado al final permite buscar como cadena C. */
    rewind(diag);
    COMPROBAR(fread(texto, 1, sizeof texto - 1, diag) > 0);
    COMPROBAR(strstr(texto, mensaje) != NULL);
    COMPROBAR(fclose(diag) == 0);
}

/* Comprueba el análisis de líneas MEM, su orden de palabras y el límite de IMEM.
 * Se exige además que las zonas no aportadas por el archivo queden en cero. */
static void casos_mem(void)
{
    /* Los cuatro valores distintos detectan inversión de slots o bytes.
     * Hay márgenes, CRLF, una línea vacía y última línea sin salto final.
     * El segundo bundle contiene cuatro NOP y sí cuenta como bundle cargado. */
    const char *texto = " \tabcdef0189abcdef0123456780000001\r\n"
                        "\n00000000000000000000000000000000\t";
    escribir(texto, strlen(texto));
    SimImagen *imagen = NULL;
    COMPROBAR(sim_cargar_mem(ruta, &imagen, stderr) == SIM_CARGA_OK);
    COMPROBAR(imagen->cantidad_bundles == 2);
    const uint32_t esperados[] = {0x80000001u, 0x01234567u, 0x89abcdefu, 0xabcdef01u};
    COMPROBAR(memcmp(imagen->imem[0], esperados, sizeof esperados) == 0);
    /* Desde el bundle NOP hasta el final de IMEM todo debe ser cero. DMEM
     * completa y los metadatos de datos también quedan vacíos al cargar MEM. */
    for (size_t b = 1; b < SIM_CARGADOR_IMEM_BUNDLES; ++b)
        for (size_t s = 0; s < SIM_CARGADOR_SLOTS; ++s)
            COMPROBAR(imagen->imem[b][s] == 0);
    for (size_t i = 0; i < sizeof imagen->dmem; ++i) COMPROBAR(imagen->dmem[i] == 0);
    COMPROBAR(imagen->entrada == 0 && imagen->base_datos == 0 &&
              imagen->flags == 0 && imagen->cantidad_datos == 0);
    /* Una segunda carga sobre una imagen viva debe rechazar la petición,
     * conservar el propietario y permitir después una liberación idempotente. */
    SimImagen *original = imagen;
    COMPROBAR(sim_cargar_mem(ruta, &imagen, NULL) == SIM_CARGA_ARGUMENTO);
    COMPROBAR(imagen == original && imagen->cantidad_bundles == 2);
    sim_liberar_imagen(&imagen);
    COMPROBAR(imagen == NULL);
    sim_liberar_imagen(&imagen);
    sim_liberar_imagen(NULL);

    /* La misma imagen en mayúsculas y sin salto final debe producir las
     * mismas cuatro palabras; el caso de las letras no altera su valor. */
    const char *mayusculas = "ABCDEF0189ABCDEF0123456780000001";
    escribir(mayusculas, strlen(mayusculas));
    COMPROBAR(sim_cargar_mem(ruta, &imagen, NULL) == SIM_CARGA_OK);
    COMPROBAR(memcmp(imagen->imem[0], esperados, sizeof esperados) == 0);
    sim_liberar_imagen(&imagen);

    /* Se provocan archivo vacío, líneas sin código, longitudes incompletas
     * o excesivas, espacio interno, X/Z, directiva @, comentario y prefijo 0x.
     * Ninguno pertenece a las líneas que emite el escritor MEM actual. */
    const char *invalidos[] = {"", " \t\r\n", "0", "0000000000000000000000000000000",
        "000000000000000000000000000000000", "0000000000000000 0000000000000000",
        "0000000000000000000000000000000x", "0000000000000000000000000000000z",
        "@0000\n", "# comentario\n", "0x00000000000000000000000000000000"};
    for (size_t i = 0; i < sizeof invalidos / sizeof *invalidos; ++i)
        rechazo(0, invalidos[i], strlen(invalidos[i]), ".mem");

    /* IMEM completa es válida para cargar; el bundle 1025 debe rechazarse.
     * Reservar un vector de excepción o vaciado corresponde al ejecutor. */
    size_t n = (SIM_CARGADOR_IMEM_BUNDLES + 1u) * 33;
    char *grande = malloc(n);
    COMPROBAR(grande != NULL);
    /* Cada línea aporta 32 ceros y un salto: quitar 33 bytes deja exactamente
     * 1024 bundles; usar todos los bytes agrega uno y debe fallar. */
    for (size_t i = 0; i < n; ++i) grande[i] = i % 33 == 32 ? '\n' : '0';
    escribir(grande, n - 33);
    COMPROBAR(sim_cargar_mem(ruta, &imagen, NULL) == SIM_CARGA_OK);
    COMPROBAR(imagen->cantidad_bundles == SIM_CARGADOR_IMEM_BUNDLES);
    sim_liberar_imagen(&imagen);
    rechazo(0, grande, n, "1024");
    free(grande);
}

/* Comprueba el contenedor completo, su encabezado y los límites de las secciones.
 * Los archivos inválidos deben rechazarse antes de entregar una imagen parcial. */
static void casos_balb(void)
{
    /* Bytes asimétricos: ALU es little-endian al principio del código.
     * Datos sin alineación de palabra son válidos en el escritor existente. */
    unsigned char bytes[68] = {0};
    /* 48 de encabezado + 16 de código + 4 de datos = 68 bytes. La base 3
     * comprueba que el lector no invente una restricción de alineación de DMEM. */
    cabecera(bytes, 16, 4, 3, 1);
    for (size_t i = 48; i < sizeof bytes; ++i) bytes[i] = (unsigned char)(i - 48 + 1);
    escribir(bytes, sizeof bytes);
    SimImagen *imagen = NULL;
    COMPROBAR(sim_cargar_balb(ruta, &imagen, stderr) == SIM_CARGA_OK);
    COMPROBAR(imagen->cantidad_bundles == 1 && imagen->entrada == 0);
    /* El payload 01,02,03,04 se reconstruye como 0x04030201 en ALU.
     * Las otras palabras verifican que cada grupo de cuatro bytes conserve
     * su slot; los últimos cuatro bytes deben aparecer en DMEM[3..6]. */
    COMPROBAR(imagen->imem[0][0] == 0x04030201u);
    COMPROBAR(imagen->imem[0][1] == 0x08070605u);
    COMPROBAR(imagen->imem[0][2] == 0x0c0b0a09u);
    COMPROBAR(imagen->imem[0][3] == 0x100f0e0du);
    COMPROBAR(imagen->cantidad_datos == 4 && imagen->base_datos == 3 && imagen->flags == 1);
    COMPROBAR(memcmp(imagen->dmem + 3, bytes + 64, 4) == 0);
    /* Las posiciones inmediatamente antes y después no deben ser sobrescritas. */
    COMPROBAR(imagen->dmem[2] == 0 && imagen->dmem[7] == 0);
    sim_liberar_imagen(&imagen);

    /* Se trunca en cada byte del archivo válido: incluye todos los campos,
     * el interior de cada slot y la sección de datos. Nada debe publicarse. */
    for (size_t n = 0; n < sizeof bytes; ++n) rechazo(1, bytes, n, "truncado");
    /* Un byte extra contradice el total declarado. Alterar solo la firma
     * conserva el tamaño, pero deja de identificar un contenedor BALB. */
    unsigned char extra[69];
    memcpy(extra, bytes, sizeof bytes); extra[68] = 0;
    rechazo(1, extra, sizeof extra, "adicionales");
    unsigned char copia[sizeof bytes];
    memcpy(copia, bytes, sizeof bytes); copia[0] = 'X';
    rechazo(1, copia, sizeof copia, "firma");

    /* Cada fila indica offset de archivo, valor corrupto y diagnóstico esperado.
     * Se cubren versión/encabezado, total, entrada, offsets, código, bases y flags;
     * UINT32_MAX comprueba que las sumas no acepten un desbordamiento. */
    static const struct { unsigned offset; uint32_t valor; const char *mensaje; } casos[] = {
        {4, 2, "versión"}, {8, 64, "encabezado"}, {12, 67, "desplazamientos"},
        {12, UINT32_MAX, "desplazamientos"}, {16, 1, "entrada"}, {16, 16, "entrada"},
        {20, 32, "desplazamientos"}, {20, UINT32_MAX, "desplazamientos"},
        {24, 0, "código"}, {24, 15, "código"}, {24, 17, "código"},
        {24, 16400, "IMEM"}, {24, UINT32_MAX, "código"},
        {28, 1, "dirección"}, {28, 16, "dirección"},
        {32, 48, "desplazamientos"}, {32, 65, "desplazamientos"},
        {32, UINT32_MAX, "desplazamientos"},
        {40, 65533, "DMEM"}, {40, 65537, "DMEM"}, {40, UINT32_MAX, "DMEM"},
        {44, 2, "flags"}, {44, UINT32_MAX, "flags"}
    };
    for (size_t i = 0; i < sizeof casos / sizeof *casos; ++i) {
        /* Partir siempre del original aísla cada defecto: no se arrastran
         * campos dañados desde la fila anterior que oculten la causa probada. */
        memcpy(copia, bytes, sizeof bytes);
        poner32(copia + casos[i].offset, casos[i].valor);
        rechazo(1, copia, sizeof copia, casos[i].mensaje);
    }
    /* Aunque DMEM tiene 64 KiB, BALB permite hasta 16 KiB de datos. El primer
     * caso rebasa ese límite; UINT32_MAX provoca además un total desbordado
     * en el encabezado construido y debe fallar la consistencia de tamaños. */
    memcpy(copia, bytes, sizeof bytes);
    cabecera(copia, 16, 16385, 0, 0);
    rechazo(1, copia, sizeof copia, "datos");
    cabecera(copia, 16, UINT32_MAX, 0, 0);
    rechazo(1, copia, sizeof copia, "desplazamientos");
    /* Un byte en 65535 termina exactamente dentro de DMEM y debe cargarse.
     * El byte esperado es el que sigue al único bundle del archivo. */
    cabecera(copia, 16, 1, 65535, 0);
    escribir(copia, 65);
    COMPROBAR(sim_cargar_balb(ruta, &imagen, NULL) == SIM_CARGA_OK);
    COMPROBAR(imagen->dmem[65535] == copia[64]);
    sim_liberar_imagen(&imagen);
    /* Base 65536 solo es válida para una sección vacía: no se accede a esa
     * posición. También se comprueba que flags=0 sea un valor aceptado. */
    cabecera(copia, 16, 0, 65536, 0);
    escribir(copia, 64);
    COMPROBAR(sim_cargar_balb(ruta, &imagen, NULL) == SIM_CARGA_OK);
    COMPROBAR(imagen->cantidad_datos == 0 && imagen->flags == 0);
    sim_liberar_imagen(&imagen);

    /* Código y datos alcanzan 16 KiB cada uno; base 49152 hace que datos
     * termine en el último byte de DMEM. La marca 0xa5 detecta si se pierde
     * ese último byte aun cuando las cantidades declaradas parezcan correctas. */
    size_t n = 48 + 16384 + 16384;
    unsigned char *maximo = calloc(n, 1);
    COMPROBAR(maximo != NULL);
    cabecera(maximo, 16384, 16384, 49152, 1);
    maximo[n - 1] = 0xa5;
    escribir(maximo, n);
    COMPROBAR(sim_cargar_balb(ruta, &imagen, NULL) == SIM_CARGA_OK);
    COMPROBAR(imagen->cantidad_bundles == 1024 && imagen->cantidad_datos == 16384);
    COMPROBAR(imagen->dmem[65535] == 0xa5);
    sim_liberar_imagen(&imagen);
    free(maximo);
}

/* Comprueba los estados de argumentos y E/S, los fallos inducidos y la limpieza
 * después de cargas repetidas. El puntero debe permanecer NULL tras cada error. */
static void casos_recursos(void)
{
    SimImagen *imagen = NULL;
    /* Ruta NULL/vacía y salida NULL deben rechazarse sin abrir el archivo
     * ni intentar reservar una imagen. */
    COMPROBAR(sim_cargar_mem(NULL, &imagen, NULL) == SIM_CARGA_ARGUMENTO);
    COMPROBAR(sim_cargar_balb("", &imagen, NULL) == SIM_CARGA_ARGUMENTO);
    COMPROBAR(sim_cargar_mem(ruta, NULL, NULL) == SIM_CARGA_ARGUMENTO);
    /* El nombre temporal ya existe: su hijo no puede existir al ser un archivo. */
    char ausente[sizeof ruta + 10];
    snprintf(ausente, sizeof ausente, "%s/hijo", ruta);
    COMPROBAR(sim_cargar_balb(ausente, &imagen, NULL) == SIM_CARGA_IO);
    COMPROBAR(imagen == NULL);
    /* Un directorio no puede leerse como programa: cubre errores de E/S,
     * además de los archivos que simplemente terminan antes de tiempo. */
    COMPROBAR(sim_cargar_mem("/tmp", &imagen, NULL) == SIM_CARGA_IO);
    COMPROBAR(sim_cargar_balb("/tmp", &imagen, NULL) == SIM_CARGA_IO);
    /* Un bundle MEM válido de cuatro NOP también sirve como BALB truncado:
     * sus 33 bytes no alcanzan los 48 que requiere el encabezado. */
    const char *nop = "00000000000000000000000000000000\n";
    escribir(nop, strlen(nop));
#ifdef SIM_PRUEBA_FALLOS
    /* La reserva fallida debe cerrar el archivo; un error de cierre tampoco
     * puede entregar una imagen aparentemente cargada con éxito. */
    sin_memoria = 1;
    COMPROBAR(sim_cargar_mem(ruta, &imagen, NULL) == SIM_CARGA_MEMORIA);
    COMPROBAR(imagen == NULL);
    sin_memoria = 0;
    error_cierre = 1;
    COMPROBAR(sim_cargar_mem(ruta, &imagen, NULL) == SIM_CARGA_IO);
    COMPROBAR(imagen == NULL);
    error_cierre = 0;
#endif
    /* Repetir éxito y rechazo permite detectar reservas y descriptores
     * perdidos mediante sanitizadores y un límite reducido de archivos. */
    for (size_t i = 0; i < 128; ++i) {
        COMPROBAR(sim_cargar_mem(ruta, &imagen, NULL) == SIM_CARGA_OK);
        sim_liberar_imagen(&imagen);
        COMPROBAR(sim_cargar_balb(ruta, &imagen, NULL) == SIM_CARGA_FORMATO);
        COMPROBAR(imagen == NULL);
    }
}

/* Carga el mismo programa desde ambos formatos y comprueba su equivalencia.
 * Este modo es utilizado por Python con la salida real del compilador. */
static void comparar_reales(const char *mem, const char *balb)
{
    /* Este modo recibe archivos producidos por balc o fixtures de Arquitectura.
     * Compara toda IMEM y comprueba DMEM contra los bytes reales del contenedor. */
    SimImagen *a = NULL, *b = NULL;
    COMPROBAR(sim_cargar_mem(mem, &a, stderr) == SIM_CARGA_OK);
    COMPROBAR(sim_cargar_balb(balb, &b, stderr) == SIM_CARGA_OK);
    COMPROBAR(a->cantidad_bundles == b->cantidad_bundles);
    /* Comparar toda IMEM incluye el código y los ceros del espacio restante;
     * no basta con que ambos lectores informen la misma cantidad de bundles. */
    COMPROBAR(memcmp(a->imem, b->imem, sizeof a->imem) == 0);
    FILE *f = fopen(balb, "rb");
    COMPROBAR(f != NULL);
    size_t offset = 48 + b->cantidad_bundles * 16;
    /* Se salta el encabezado y el código para contrastar cada byte de datos
     * con su dirección en DMEM. MEM no tiene sección de datos para comparar. */
    COMPROBAR(fseek(f, (long)offset, SEEK_SET) == 0);
    for (size_t i = 0; i < b->cantidad_datos; ++i)
        COMPROBAR(fgetc(f) == b->dmem[b->base_datos + i]);
    COMPROBAR(fclose(f) == 0);
    sim_liberar_imagen(&a);
    sim_liberar_imagen(&b);
}

/* Sin argumentos ejecuta escenarios sintéticos con un archivo temporal propio.
 * Con dos rutas compara MEM/BALB existentes. Retornar 0 significa que todas
 * las comprobaciones del modo elegido llegaron al final sin discrepancias. */
int main(int argc, char **argv)
{
    if (argc == 3) comparar_reales(argv[1], argv[2]);
    else {
        COMPROBAR(argc == 1);
        /* mkstemp asigna un nombre único y abre el archivo. Se cierra ese
         * descriptor porque escribir() lo abrirá por ruta; unlink lo elimina
         * tras terminar los escenarios, sin tocar fixtures del repositorio. */
        int fd = mkstemp(ruta);
        COMPROBAR(fd >= 0);
        COMPROBAR(close(fd) == 0);
        casos_mem();
        casos_balb();
        casos_recursos();
        COMPROBAR(unlink(ruta) == 0);
    }
    printf("OK: %zu comprobaciones del cargador\n", comprobaciones);
    return 0;
}
