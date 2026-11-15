#include "funciones_marco.h"

#include <limits.h>

/* El ISA usa inmediatos con signo de 11 bits. Para mantener un margen
 * estable se descompone cualquier ajuste de sp en pasos de 1020 bytes. */
static int ajustar_sp(FILE *f, size_t bytes, int reservar)
{
    while (bytes != 0) {
        unsigned paso = (unsigned)(bytes > 1020 ? 1020 : bytes);
        if (fprintf(f, "%s x2, x2, %u\n",
                    reservar ? "restai" : "sumai", paso) < 0) return 0;
        bytes -= paso;
    }
    return 1;
}

static FmResultado validar(size_t bytes, size_t parametros)
{
    /* Las direcciones efectivas de los slots deben caber en el ISA. */
    if (bytes < 4 || bytes % 4 != 0 || bytes > 65536u || bytes > (size_t)LONG_MAX)
        return FM_ARGUMENTO_INVALIDO;
    if (parametros > 255 || parametros > (bytes - 4) / 4)
        return FM_MARCO_NO_SOPORTADO;
    return FM_OK;
}

static FmResultado publicar(FILE *salida, FILE *temporal)
{
    if (fflush(temporal) != 0 || fseek(temporal, 0, SEEK_SET) != 0)
        return FM_ERROR_ESCRITURA;
    char buffer[1024];
    size_t leidos;
    while ((leidos = fread(buffer, 1, sizeof(buffer), temporal)) != 0)
        if (fwrite(buffer, 1, leidos, salida) != leidos)
            return FM_ERROR_ESCRITURA;
    if (ferror(temporal) || fflush(salida) != 0)
        return FM_ERROR_ESCRITURA;
    return FM_OK;
}

FmResultado fm_emitir_prologo(FILE *salida, size_t bytes_marco,
                             size_t numero_parametros)
{
    if (!salida) return FM_ARGUMENTO_INVALIDO;
    FmResultado r = validar(bytes_marco, numero_parametros);
    if (r != FM_OK) return r;
    /* El home slot del ultimo parametro debe ser direccionable. */
    if (numero_parametros > 0 && numero_parametros - 1 > 254)
        return FM_MARCO_NO_SOPORTADO;
    FILE *f = tmpfile();
    if (!f) return FM_ERROR_ESCRITURA;
    int ok = ajustar_sp(f, bytes_marco, 1) &&
             fprintf(f, "guardap x2, x1, 0\n") >= 0;
    for (size_t i = 0; ok && i < numero_parametros && i < 4; ++i)
        ok = fprintf(f, "guardap x2, x%u, %zu\n", (unsigned)(8 + i), 4 * (i + 1)) >= 0;
    for (size_t i = 4; ok && i < numero_parametros; ++i) {
        /* Extra recibido: [sp + bytes_marco + 4*(i-4)].
         * Como la LSU no cubre offsets grandes, se materializa la direccion
         * por sumas con x12, registro temporal caller-saved. */
        size_t offset = bytes_marco + (i - 4) * 4;
        if (offset > (size_t)LONG_MAX) { ok = 0; break; }
        ok = fprintf(f, "sumai x12, x2, 0\n") >= 0;
        while (ok && offset) {
            unsigned paso = (unsigned)(offset > 1020 ? 1020 : offset);
            ok = fprintf(f, "sumai x12, x12, %u\n", paso) >= 0;
            offset -= paso;
        }
        if (ok) ok = fprintf(f, "cargai x12, x12, 0\n") >= 0 &&
                     fprintf(f, "guardap x2, x12, %zu\n", 4 * (i + 1)) >= 0;
    }
    if (!ok) r = FM_ERROR_ESCRITURA;
    else r = publicar(salida, f);
    fclose(f);
    return r;
}

FmResultado fm_emitir_epilogo(FILE *salida, size_t bytes_marco)
{
    if (!salida) return FM_ARGUMENTO_INVALIDO;
    FmResultado r = validar(bytes_marco, 0);
    if (r != FM_OK) return r;
    FILE *f = tmpfile();
    if (!f) return FM_ERROR_ESCRITURA;
    int ok = fprintf(f, "cargai x1, x2, 0\n") >= 0 &&
             ajustar_sp(f, bytes_marco, 0) &&
             fprintf(f, "syer x0, x1, 0\n") >= 0;
    r = ok ? publicar(salida, f) : FM_ERROR_ESCRITURA;
    fclose(f);
    return r;
}
