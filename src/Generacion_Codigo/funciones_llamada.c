#include "funciones_llamada.h"

static FlResultado publicar(FILE *salida, FILE *temporal)
{
    if (fflush(temporal) != 0 || fseek(temporal, 0, SEEK_SET) != 0)
        return FL_ERROR_ESCRITURA;
    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), temporal)) != 0)
        if (fwrite(buffer, 1, n, salida) != n)
            return FL_ERROR_ESCRITURA;
    if (ferror(temporal) || fflush(salida) != 0)
        return FL_ERROR_ESCRITURA;
    return FL_OK;
}

FlResultado fl_emitir_llamada(FILE *salida,
                             const unsigned *registros_fuente,
                             size_t numero_argumentos)
{
    if (!salida || (numero_argumentos && !registros_fuente))
        return FL_ARGUMENTO_INVALIDO;
    if (numero_argumentos > 255)
        return FL_DEMASIADOS_ARGUMENTOS;
    for (size_t i = 0; i < numero_argumentos; ++i)
        if (registros_fuente[i] > 31 ||
            (registros_fuente[i] >= 1 && registros_fuente[i] <= 3))
            return FL_ARGUMENTO_INVALIDO;

    FILE *f = tmpfile();
    if (!f) return FL_ERROR_ESCRITURA;
    int ok = 1;
    const size_t bytes = numero_argumentos * 4;
    /* Reservar los slots de TODOS los valores asegura permutaciones seguras:
     * ej. fuentes x9,x8 -> destino x8,x9 sin perder ningun valor. */
    if (bytes)
        ok = fprintf(f, "restai x2, x2, %zu\n", bytes) >= 0;
    for (size_t i = 0; ok && i < numero_argumentos; ++i)
        ok = fprintf(f, "guardap x2, x%u, %zu\n",
                     registros_fuente[i], 4 * i) >= 0;
    for (size_t i = 0; ok && i < numero_argumentos && i < 4; ++i)
        ok = fprintf(f, "cargai x%u, x2, %zu\n",
                     (unsigned)(8 + i), 4 * i) >= 0;
    /* En el punto de llamada, el quinto argumento debe estar en [sp+0],
     * el sexto en [sp+4], etc. Copiar de menor a mayor indice es seguro:
     * se lee siempre desde una direccion mayor que la escritura. */
    for (size_t i = 4; ok && i < numero_argumentos; ++i)
        ok = fprintf(f, "cargai x12, x2, %zu\n", 4 * i) >= 0 &&
             fprintf(f, "guardap x2, x12, %zu\n", 4 * (i - 4)) >= 0;
    if (ok) ok = fprintf(f, "syer x1, x13, 0\n") >= 0;
    if (ok && bytes)
        ok = fprintf(f, "sumai x2, x2, %zu\n", bytes) >= 0;
    FlResultado r = ok ? publicar(salida, f) : FL_ERROR_ESCRITURA;
    fclose(f);
    return r;
}
