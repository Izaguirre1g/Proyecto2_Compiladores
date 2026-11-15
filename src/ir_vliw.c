#include "ir_vliw.h"
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Este productor solo acredita operaciones ALU/LSU que construye y valida
 * con el codificador real. Los registros auxiliares no se prestan al allocador. */
enum { IRV_PRIMER_REG = 16, IRV_MAX_REG = 8, IRV_SCRATCH_A = 28,
       IRV_SCRATCH_B = 29, IRV_MAX_IMM = 1023, IRV_MIN_IMM = -1024 };

typedef struct {
    const char *nombre; /* prestado de IrPrograma transformado */
    size_t id;
} NombreEtiqueta;
typedef struct {
    VliwCfgOperacion *ops;
    char (*textos)[160];
    const char **punteros;
    unsigned *efectos;
    VliwCfgSimbolo *simbolos;
    NombreEtiqueta *nombres;
    size_t cantidad_simbolos;
    VliwCfgTransferencia *transferencias;
    const char **destinos_pendientes;
    size_t cantidad_transferencias;
    VliwCfgRegion *regiones;
    size_t cantidad_regiones;
    size_t limites_ir;
    size_t cantidad, capacidad;
    FILE *errores;
} Productor;

static void error(FILE *f, size_t linea, const char *fmt, ...) {
    if (!f) return;
    fprintf(f, "IR->VLIW linea %zu: ", linea);
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
}

static int registro(const char *s, size_t limite, unsigned *out) {
    if (!s || s[0] != '$' || s[1] != 'r' || !s[2]) return 0;
    size_t indice = 0;
    for (const char *p = s + 2; *p; ++p) {
        if (*p < '0' || *p > '9') return 0;
        unsigned dig = (unsigned)(*p - '0');
        if (indice > (SIZE_MAX - dig) / 10) return 0;
        indice = indice * 10 + dig;
    }
    if (indice >= limite) return 0;
    *out = IRV_PRIMER_REG + (unsigned)indice;
    return 1;
}

static int entero(const char *s, long *out) {
    if (!s || !*s) return 0;
    if (!strcmp(s, "declare_true")) { *out = 1; return 1; }
    if (!strcmp(s, "declare_false")) { *out = 0; return 1; }
    errno = 0;
    char *fin = NULL;
    long n = strtol(s, &fin, 10);
    if (errno || fin == s || *fin || n < IRV_MIN_IMM || n > IRV_MAX_IMM) return 0;
    *out = n;
    return 1;
}

/* Convertir texto ASCII de UNA instruccion al opcode fisico exacto mediante CB.
 * No se fabrica una palabra binaria ni una mascara a mano. */
static int agregar(Productor *p, unsigned slot, const char *fmt, ...) {
    if (p->cantidad >= VLIW_MAX_BUNDLES || slot >= 4) return 0;
    if (p->cantidad == p->capacidad) {
        size_t nuevo = p->capacidad ? p->capacidad * 2 : 32;
        if (nuevo > VLIW_MAX_BUNDLES) nuevo = VLIW_MAX_BUNDLES;
        VliwCfgOperacion *ops = realloc(p->ops, nuevo * sizeof(*ops));
        if (!ops) return 0;
        p->ops = ops;
        char (*txt)[160] = realloc(p->textos, nuevo * sizeof(*txt));
        if (!txt) return 0;
        p->textos = txt;
        p->capacidad = nuevo;
    }
    char texto[160];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(texto, sizeof texto, fmt, args);
    va_end(args);
    if (len < 0 || (size_t)len >= sizeof texto) return 0;
    char fuente[256];
    int n = snprintf(fuente, sizeof fuente, "%s | %s | %s | nop\n",
                      slot == 0 ? texto : "nop", slot == 1 ? texto : "nop",
                      slot == 2 ? texto : "nop");
    if (n < 0 || (size_t)n >= sizeof fuente) return 0;
    CbPrograma codificado = {0};
    if (cb_ensamblar(fuente, &codificado, p->errores)) {
        cb_liberar(&codificado);
        return 0;
    }
    if (codificado.cantidad != 1 || codificado.bundles[0].slots[slot] == 0) {
        cb_liberar(&codificado);
        return 0;
    }
    size_t i = p->cantidad;
    SimInstruccion decodificada;
    uint32_t palabra = codificado.bundles[0].slots[slot];
    if (sim_decode_palabra(palabra, slot, &decodificada) != SIM_DECODE_OK ||
        ((slot != 2 && decodificada.salto != SIM_SALTO_NINGUNO) ||
         (slot == 2 && decodificada.salto == SIM_SALTO_NINGUNO))) {
        cb_liberar(&codificado);
        return 0;
    }
    p->ops[i] = (VliwCfgOperacion){i + 1, palabra, (VliwSlot)slot,
                                   decodificada.salto,
                                   slot == 2 ? VLIW_CONTROL_SALTO : VLIW_CONTROL_NINGUNO};
    memcpy(p->textos[i], texto, (size_t)len + 1);
    ++p->cantidad;
    cb_liberar(&codificado);
    return 1;
}

/* Inmediatos se materializan ANTES de la operacion, sin dependencias ocultas.
 * El DAG fisico detecta el RAW3 de x28/x29 y de los registros del allocador. */
static int operando(Productor *p, const char *nombre, size_t reg_limite,
                    unsigned auxiliar, unsigned *out) {
    if (registro(nombre, reg_limite, out)) return 1;
    long valor;
    if (!entero(nombre, &valor)) return 0;
    if (!agregar(p, 0, "sumai x%u,x0,%ld", auxiliar, valor)) return 0;
    *out = auxiliar;
    return 1;
}

/* La localizacion de etiquetas es por nombre, nunca por inmediatos de BRU.
 * S8-A reubicara la transferencia tras planificar los bundles. */
static int enlace_control(Productor *p) {
    for (size_t i=0;i<p->cantidad_simbolos;++i)
        for (size_t j=i+1;j<p->cantidad_simbolos;++j)
            if (!strcmp(p->nombres[i].nombre, p->nombres[j].nombre)) {
                error(p->errores, 0, "etiqueta duplicada %s", p->nombres[i].nombre);
                return 0;
            }
    for (size_t i=0;i<p->cantidad_transferencias;++i) {
        size_t j;
        for (j=0;j<p->cantidad_simbolos;++j)
            if (!strcmp(p->destinos_pendientes[i], p->nombres[j].nombre)) break;
        if (j==p->cantidad_simbolos) {
            error(p->errores, 0, "salto a etiqueta no declarada %s", p->destinos_pendientes[i]);
            return 0;
        }
        p->transferencias[i].tomado=p->nombres[j].id;
    }
    return 1;
}
static int salto(Productor *p,const char *etiqueta,VliwDestinoTipo tipo) {
    if (!etiqueta || p->cantidad_transferencias >= p->limites_ir || !p->cantidad)
        return 0;
    size_t i=p->cantidad_transferencias++;
    p->transferencias[i]=(VliwCfgTransferencia){p->cantidad-1,tipo,VLIW_VACIO,
        tipo==VLIW_DEST_CONDICIONAL ? p->cantidad : VLIW_VACIO,NULL,0,true};
    p->destinos_pendientes[i]=etiqueta;
    return 1;
}
static int lowering(Productor *p, const IrPrograma *ir, size_t reg_limite,
                    const IrSpillingBackend *backend) {
    int funcion = 0, hubo_funcion = 0, terminada = 0;
    size_t i = 0;
    for (; i < ir->cantidad; ++i) {
        const IrInstr *inst = &ir->codigo[i];
        unsigned dst, a, b;
        size_t inicio_expansion = p->cantidad;
        if (inst->op != IR_FUNC && inst->op != IR_END_FUNC && !funcion)
            goto unsupported;
        switch (inst->op) {
        case IR_FUNC:
            if (hubo_funcion || funcion || terminada) goto unsupported;
            hubo_funcion = funcion = 1;
            /* Se evita emitir un fragmento sin marco: para este subconjunto
             * no se generan llamadas, solo reservas de pila y spills. */
            if (!agregar(p, 0, "restai x2,x2,%zu", backend->marco.bytes_marco)) return 0;
            if (!agregar(p, 1, "guardap x2,x1,0")) return 0;
            break;
        case IR_END_FUNC:
            if (!funcion || terminada) goto unsupported;
            funcion = 0; terminada = 1;
            if (!agregar(p, 1, "cargai x1,x2,0")) return 0;
            if (!agregar(p, 0, "sumai x2,x2,%zu", backend->marco.bytes_marco)) return 0;
            break;
        case IR_LABEL: {
            if (!inst->dst || p->cantidad_simbolos >= p->limites_ir) goto unsupported;
            size_t n=p->cantidad_simbolos++;
            p->simbolos[n]=(VliwCfgSimbolo){10000+n,1,p->cantidad};
            p->nombres[n]=(NombreEtiqueta){inst->dst,10000+n};
            break;
        }
        case IR_JUMP:
            if (!agregar(p,2,"sye x0,0") || !salto(p,inst->dst,VLIW_DEST_DIRECTO)) return 0;
            break;
        case IR_JFALSE: case IR_JTRUE:
            if (!operando(p,inst->a,reg_limite,IRV_SCRATCH_A,&a)) goto unsupported;
            if (!agregar(p,2, inst->op == IR_JFALSE ? "igualsi x%u,x0,0" :
                         "igualno x%u,x0,0",a) ||
                !salto(p,inst->dst,VLIW_DEST_CONDICIONAL)) return 0;
            break;
        case IR_SPILL_LOAD: {
            size_t off;
            if (!registro(inst->dst, reg_limite, &dst) ||
                ir_spilling_offset(&backend->marco, inst->a, &off)) goto unsupported;
            if (!agregar(p, 1, "cargai x%u,x2,%zu", dst, off)) return 0;
            break;
        }
        case IR_SPILL_STORE: {
            size_t off;
            if (!registro(inst->a, reg_limite, &a) ||
                ir_spilling_offset(&backend->marco, inst->dst, &off)) goto unsupported;
            if (!agregar(p, 1, "guardap x2,x%u,%zu", a, off)) return 0;
            break;
        }
        case IR_COPY:
            if (!registro(inst->dst, reg_limite, &dst)) goto unsupported;
            if (registro(inst->a, reg_limite, &a)) {
                if (!agregar(p, 0, "sumai x%u,x%u,0", dst, a)) return 0;
            } else {
                long valor;
                if (!entero(inst->a, &valor)) goto unsupported;
                if (!agregar(p, 0, "sumai x%u,x0,%ld", dst, valor)) return 0;
            }
            break;
        case IR_BIN: {
            if (!registro(inst->dst, reg_limite, &dst) ||
                !operando(p, inst->a, reg_limite, IRV_SCRATCH_A, &a) ||
                !operando(p, inst->b, reg_limite, IRV_SCRATCH_B, &b)) goto unsupported;
            const char *opcode = inst->operador && !strcmp(inst->operador, "gauss") ? "suma" :
                                 inst->operador && !strcmp(inst->operador, "neumann") ? "resta" : NULL;
            if (!opcode) goto unsupported;
            if (!agregar(p, 0, "%s x%u,x%u,x%u", opcode, dst, a, b)) return 0;
            break;
        }
        case IR_UNARY:
            if (!inst->operador || strcmp(inst->operador, "neumann") ||
                !registro(inst->dst, reg_limite, &dst) ||
                !operando(p, inst->a, reg_limite, IRV_SCRATCH_A, &a)) goto unsupported;
            if (!agregar(p, 0, "resta x%u,x0,x%u", dst, a)) return 0;
            break;
        default:
            goto unsupported;
        }
        /* Si un IR origina varias instrucciones, la entrada del CFG no
         * puede apuntar al interior de la expansion. */
        if (p->cantidad > inicio_expansion + 1) {
            if (p->cantidad_regiones >= p->limites_ir) return 0;
            p->regiones[p->cantidad_regiones++] =
                (VliwCfgRegion){inicio_expansion,p->cantidad};
        }
    }
    if (!enlace_control(p)) return -1;
    if (!hubo_funcion || funcion || !terminada) {
        error(p->errores, 0, "se requiere exactamente una funcion IR_FUNC ... IR_END_FUNC");
        return -1;
    }
    return 1;
unsupported:
    error(p->errores, ir->codigo[i].linea,
          "operacion/operando %d no soportado en fragmento fisico; no se genero binario",
          (int)ir->codigo[i].op);
    return -1;
}

void ir_vliw_liberar(IrVliwResultado *r) {
    if (!r) return;
    vliw_cadena_liberar(&r->cadena);
    ir_liberar(&r->despues_spilling);
    memset(r, 0, sizeof *r);
}

IrVliwEstado ir_vliw_desde_ir(const IrPrograma *original,
    const IrVliwConfiguracion *config, IrVliwResultado *salida, FILE *err) {
    if (!original || !config || !salida || config->registros < 3 ||
        config->registros > IRV_MAX_REG || config->bytes_ocupados < 4 ||
        salida->cadena || salida->despues_spilling.codigo ||
        salida->despues_spilling.bloques || salida->despues_spilling.cantidad) return IR_VLIW_ARGUMENTO;
    IrVliwResultado nuevo = {0};
    Productor p = {0};
    p.errores = err;
    IrVliwEstado estado = IR_VLIW_ARGUMENTO;
    if (ir_aplicar_spilling(original, &nuevo.despues_spilling,
                            config->registros, &nuevo.metricas)) goto fin;
    if (ir_spilling_backend_preparar(config->bytes_ocupados, nuevo.metricas.ranuras,
                                     0, &nuevo.backend)) {
        error(err, 0, "marco/offsets de spilling no representables directamente");
        goto fin;
    }
    if (nuevo.backend.marco.bytes_marco > IRV_MAX_IMM) {
        error(err, 0, "prologo/epilogo requiere expansión fuera de este subset");
        estado = IR_VLIW_NO_SOPORTADO;
        goto fin;
    }
    p.limites_ir = nuevo.despues_spilling.cantidad;
    p.simbolos = calloc(p.limites_ir, sizeof *p.simbolos);
    p.nombres = calloc(p.limites_ir, sizeof *p.nombres);
    p.transferencias = calloc(p.limites_ir, sizeof *p.transferencias);
    p.destinos_pendientes = calloc(p.limites_ir, sizeof *p.destinos_pendientes);
    p.regiones = calloc(p.limites_ir, sizeof *p.regiones);
    if (!p.simbolos || !p.nombres || !p.transferencias ||
        !p.destinos_pendientes || !p.regiones) goto fin;
    int bajo = lowering(&p, &nuevo.despues_spilling, config->registros, &nuevo.backend);
    if (bajo <= 0) { estado = bajo < 0 ? IR_VLIW_NO_SOPORTADO : IR_VLIW_ARGUMENTO; goto fin; }
    if (p.cantidad) {
        p.punteros = calloc(p.cantidad, sizeof *p.punteros);
        p.efectos = calloc(p.cantidad, sizeof *p.efectos);
        if (!p.punteros || !p.efectos) goto fin;
        for (size_t i = 0; i < p.cantidad; ++i) p.punteros[i] = p.textos[i];
    }
    VliwCfgFuncion funcion = {1, 0, p.cantidad};
    VliwCfgEntrada flujo = {0};
    flujo.operaciones = p.ops;
    flujo.cantidad_operaciones = p.cantidad;
    flujo.funciones = p.cantidad ? &funcion : NULL;
    flujo.cantidad_funciones = p.cantidad ? 1 : 0;
    flujo.simbolos = p.simbolos;
    flujo.cantidad_simbolos = p.cantidad_simbolos;
    flujo.transferencias = p.transferencias;
    flujo.cantidad_transferencias = p.cantidad_transferencias;
    flujo.regiones = p.regiones;
    flujo.cantidad_regiones = p.cantidad_regiones;
    flujo.generacion = flujo.generacion_referencias = 1;
    flujo.fronteras_acreditadas = true;
    VliwCadenaEntrada entrada = {0};
    entrada.flujo = &flujo;
    entrada.efectos_adicionales = p.efectos;
    entrada.cantidad_efectos = p.cantidad;
    entrada.textos = p.punteros;
    entrada.cantidad_textos = p.cantidad;
    entrada.generacion = 1;
    /* Solo se acreditan los recursos de este subconjunto: no hay control,
     * referencias, llamadas, accesos BAL, efectos ocultos ni ABI pendiente. */
    entrada.efectos_acreditados = true;
    entrada.abi_acreditado = true;
    entrada.garantias = (VliwGarantiasFisicas){0, true, true};
    VliwCadenaDiagnostico diag = {0};
    if (vliw_cadena_construir(&entrada, &nuevo.cadena, &diag) != VLIW_CAD_OK) {
        error(err, 0, "rechazo VLIW fase %d, estado %d: %s",
              (int)diag.fase, (int)diag.estado, diag.mensaje);
        estado = IR_VLIW_CADENA_FALLO;
        goto fin;
    }
    *salida = nuevo;
    estado = IR_VLIW_OK;
fin:
    free(p.ops); free(p.textos); free(p.punteros); free(p.efectos);
    free(p.simbolos); free(p.nombres); free(p.transferencias);
    free(p.destinos_pendientes); free(p.regiones);
    if (estado != IR_VLIW_OK) ir_vliw_liberar(&nuevo);
    return estado;
}
