#include "ir_registros_fisicos.h"
#include "ir_defuse.h"

#include <stdint.h>
#include <string.h>

int ir_fisico_configuracion_valida(size_t registros)
{
    return registros >= IR_FISICO_MIN_REGISTROS &&
           registros <= IR_FISICO_MAX_REGISTROS;
}

int ir_fisico_mapear(const char *nombre, size_t registros, unsigned *fisico)
{
    if (!fisico || !ir_fisico_configuracion_valida(registros) || !nombre ||
        nombre[0] != '$' || nombre[1] != 'r' || !nombre[2]) return 2;

    /* Comparar el indice DURANTE el parseo impide desbordamientos. */
    size_t indice = 0;
    for (const char *p = nombre + 2; *p; ++p) {
        if (*p < '0' || *p > '9') return 2;
        unsigned digito = (unsigned)(*p - '0');
        if (indice >= registros || indice > (SIZE_MAX - digito) / 10) return 2;
        indice = indice * 10 + digito;
    }
    if (indice >= registros) return 2;
    *fisico = IR_FISICO_PRIMER_REGISTRO + (unsigned)indice;
    return 0;
}

static int revisar_nombre(const char *nombre, size_t registros)
{
    if (!nombre || nombre[0] != '$') return 0;
    /* BAL usa literales de caracter $c$: '$t$' y '$r$' no son registros. */
    if (nombre[1] && nombre[2] == '$' && !nombre[3]) return 0;
    if (nombre[1] == 't') return 2; /* temporal virtual sin asignar */
    if (nombre[1] == 'r') {
        unsigned fisico;
        return ir_fisico_mapear(nombre, registros, &fisico);
    }
    return 0;
}

int ir_fisico_validar_programa(const IrPrograma *p, size_t registros,
                               size_t *error_en)
{
    if (error_en) *error_en = SIZE_MAX;
    if (!p || !p->codigo || !p->cantidad ||
        !ir_fisico_configuracion_valida(registros)) return 2;

    for (size_t i = 0; i < p->cantidad; ++i) {
        const IrInstr *x = &p->codigo[i];
        IrDefUse analisis;
        if (ir_consultar_def_usos(x, &analisis) ||
            revisar_nombre(x->dst, registros) ||
            revisar_nombre(x->a, registros) ||
            revisar_nombre(x->b, registros)) {
            if (error_en) *error_en = i;
            return 2;
        }
    }
    return 0;
}
