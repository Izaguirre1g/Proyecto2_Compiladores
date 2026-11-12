# Metadatos físicos parciales — I1-A2a

## Objetivo y alcance

Extraer datos verificables de **una palabra ISA de32 bits y su slot conocido**.
Se reutiliza S2 para reconocer y validar campos. No existe parser, codificador,
productor BAL ni constructor de entradas S7 en este componente.

```text
palabra + slot → S2 → operandos reales → máscaras y mínimos → resultado parcial
```

Un éxito acredita opcode/slot/campos aceptados por S2 y datos locales derivados.
No acredita semántica BAL, efectos externos completos, ABI, CFG, DAG, destinos,
reubicaciones o finalización. El resultado tiene un tipo diferente de
VliwInstruccion y no contiene id, texto, bloque ni metadatos_completos.

## API y propiedad

```c
SimDecodeEstado vliw_metadatos_extraer(uint32_t palabra, unsigned slot,
    VliwMetadatosParciales *salida, VliwMetadatosDiagnostico *diagnostico);
```

Entrada por valor; slot0=ALU,1=LSU,2=BRU,3=CRIPTO. Salida del cliente accesible,
diagnóstico opcional y separado de salida. Solo SIM_DECODE_OK publica resultado.
En ARGUMENTO, NOP, RESERVADA, SLOT o CAMPOS se conservan **todos los bytes** de la
salida anterior; el cliente debe comprobar el retorno antes de consumirla.
Una salida válida previa puede reutilizarse. No hay salidas ocupadas propias,
reservas dinámicas, liberadores, archivos, punteros retenidos ni efectos globales.

El diagnóstico copia estado/palabra/slot y presta un mensaje literal estático.
NULL de salida o slot fuera de0..3 produce ARGUMENTO. Los rechazos se propagan
de S2; NOP no se convierte en una operación funcional adicional. Un registro
internamente incoherente de S2 se rechaza defensivamente como CAMPOS antes de
hacer un desplazamiento; con el S2 actual los operandos ya cumplen esa cota.
C no permite comprobar punteros arbitrarios: su accesibilidad y separación son
responsabilidad del llamador. No se inventan errores ENOMEM en una API sin heap.

## Resultado y procedencia

| Campo | Fuente | Alcance |
|---|---|---|
| operacion, familia | SimInstruccion de S2 | Identidad reconocida, no comportamiento ejecutado |
| slot | Argumento validado y coincidencia S2 | Unidad física0..3 |
| lee/escribe | rs1/rs2 y rd/rd2 de S2 | Máscaras uint64 con bits1..31; x0 excluido |
| efectos_minimos | Opcode y mapa vigente I1-A1/S8-A | Bits VliwEfecto propios; no efectos externos totales |
| salto | SimSalto de S2 | Ninguno, condicional por PC, directo por PC o por registro |
| requiere_auth/init | Requisitos estáticos S2 | No son el estado dinámico AUTH/INIT ni su resultado |
| pendientes | Obligaciones explícitas | Nunca es cero tras éxito; no es una acreditación |

No se copia SimInstruccion completo: inmediatos, direcciones y campos de bóveda
continúan disponibles por la API S2 cuando una fase autorizada los necesite.
No se interpreta endianness de archivos: la API recibe un valor uint32_t; S1/CB
administran formatos de carga/codificación fuera de este módulo.

## Registros y máscaras

SIM_REG_AUSENTE vale32 y es distinto de x0. Primero se distingue ausencia y se
valida índice; luego se excluye x0 porque sus lecturas son constantes y sus
escrituras se descartan. Una operación que escribe x0 no se convierte en NOP.
Se unen los bits de ambos operandos: fuentes coincidentes no se cuentan dos
veces. No se aceptan `$tN`, `$rN` ni nombres BAL porque no hay entrada textual ni
mapeo de virtuales en esta API.

En LSU, base es fuente; store también lee dato, sin destino GPR; load escribe
rd y no lee ese destino. Feistel lee y escribe pares completos: rs1+rs2 y
rd+rd2 de S2, incluidos pares30/31. No se infiere un segundo opcode por tener
dos destinos. ELL lee sus registros de llave, mientras VCR/CAMCON usan dirección
inmediata y no inventan GPR fuente/destino. SETPWD lee su operando real.

## Catálogo de34 operaciones

R1/R2 indican fuentes expuestas por S2; D/D2 sus destinos. Los GPRx0 se excluyen
de las máscaras, no del reconocimiento de instrucciones. Alias mov/not/ret no
son operaciones adicionales: CB las expande; ret llega como SYER. NOP se informa
por separado y no figura como instrucción35.

En **todas** las filas quedan pendientes productor/símbolos, ABI/clobbers,
efectos externos, reubicaciones y CFG/DAG. «Control+destinos» añade la necesidad
específica de clasificar la transferencia según ABI y aportar su destino lógico;
no significa que las ALU estén exentas de reubicación (p.ej. constantes en x13).

| Operación | Familia / slot | GPR leídos | GPR escritos | Efectos mínimos | Requisitos | Información adicional específica |
|---|---|---|---|---|---|---|
| suma | ALU /0 | R1,R2 | D | ninguno | — | — |
| resta | ALU /0 | R1,R2 | D | ninguno | — | — |
| cader | ALU /0 | R1,R2 | D | ninguno | — | — |
| cizq | ALU /0 | R1,R2 | D | ninguno | — | — |
| and | ALU /0 | R1,R2 | D | ninguno | — | — |
| or | ALU /0 | R1,R2 | D | ninguno | — | — |
| xor | ALU /0 | R1,R2 | D | ninguno | — | — |
| cder | ALU /0 | R1,R2 | D | ninguno | — | — |
| mrq | ALU /0 | R1,R2 | D | ninguno | — | — |
| myq | ALU /0 | R1,R2 | D | ninguno | — | — |
| sumai | ALU /0 | R1 | D | ninguno | — | — |
| restai | ALU /0 | R1 | D | ninguno | — | — |
| cizqi | ALU /0 | R1 | D | ninguno | — | — |
| cderi | ALU /0 | R1 | D | ninguno | — | — |
| caderi | ALU /0 | R1 | D | ninguno | — | — |
| xori | ALU /0 | R1 | D | ninguno | — | — |
| andi | ALU /0 | R1 | D | ninguno | — | — |
| ori | ALU /0 | R1 | D | ninguno | — | — |
| guardap | LSU /1 | base,dato | — | escritura memoria | — | — |
| guardab | LSU /1 | base,dato | — | escritura memoria | — | — |
| cargai | LSU /1 | base | D | lectura memoria | — | — |
| cargabai | LSU /1 | base | D | lectura memoria | — | — |
| igualsi | BRU /2 | R1,R2 | — | ninguno | — | control+destinos |
| igualno | BRU /2 | R1,R2 | — | ninguno | — | control+destinos |
| menora | BRU /2 | R1,R2 | — | ninguno | — | control+destinos |
| mayoroigual | BRU /2 | R1,R2 | — | ninguno | — | control+destinos |
| sye | BRU /2 | — | D | ninguno | — | control+destinos |
| syer | BRU /2 | R1 | D | ninguno | — | control+destinos |
| fsl | CRIPTO /3 | R1,R2 (par) | D,D2 (par) | excepción | AUTH | — |
| fsli | CRIPTO /3 | R1,R2 (par) | D,D2 (par) | excepción | AUTH | — |
| ell | CRIPTO /3 | R1,R2 | — | bóveda+excepción | AUTH | — |
| vcr | CRIPTO /3 | — | — | lectura memoria+estado | — | — |
| camcon | CRIPTO /3 | — | — | lectura+escritura memoria+bóveda+excepción | AUTH | — |
| setpwd | CRIPTO /3 | R1 | — | bóveda+estado+excepción | INIT | — |

La clasificación mínima coincide con fisica de I1-A1 y verificar_codigo de
S8-A. S2 no expone VliwEfecto y esas funciones son privadas: el mapa pequeño de
opcode→efectos es necesario sin modificar interfaces existentes. No se copian
sus validadores de programa. Si cambia ISA/política, se debe revisar este mapa
y sus34 oráculos; no refactorizar los módulos protegidos durante esta fase.

## Control, ABI e información pendiente

No se devuelve VliwControl: ni toda escritura a x1 es una llamada ni toda lectura
de x1 es retorno. SYE identifica salto por PC; SYER por registro; las cuatro
condiciones leen fuentes y tienen forma condicional. Los offsets no se suman a
un PC ni se convierten a bloques, y no se materializan destinos indirectos.
S2 exige alineación del offset directo/condicional pero no del offset SYER.

| Bit pendiente | Cuándo | Responsabilidad externa |
|---|---|---|
| PRODUCTOR | Siempre | Identidad/símbolos, semántica BAL, efectos externos, ABI/clobbers y reubicaciones incluso en ALU |
| CFG_DAG | Siempre | Bloques, sucesores, dependencias, control y distancias RAW N+3 del programa |
| CONTROL_ABI | Transferencia S2 | Clasificación de salto/llamada/retorno con evidencia del productor |
| DESTINOS | Transferencia S2 | Destino lógico, continuación, reubicación y layout futuro |

Un mínimo de efectos cero no implica ausencia acreditada de efectos externos.
AUTH/INIT requeridos no se consultan ni actualizan; no se ejecuta la CPU.
No existe un indicador que transforme estos datos en una entrada lista para S7.

## Relación con I1-A1 y fases posteriores

I1-A1 valida un programa descrito mediante estructuras completas y texto ISA;
I1-A2a solo extrae de una palabra. No llama reconocer/fisica ni construye CFG,
DAG o VliwDestinos. S7/S8-A siguen exigiendo las garantías completas del productor.
El flujo futuro propuesto, aún sin implementar, es productor→extracción local→
CFG/DAG físicos→construcción explícita completa→I1-A1→S7/verificador→S8-A.

I1-A2b requiere acordar las fronteras del productor, ABI/asignación/spills y
reubicaciones con Dylan y el equipo; los efectos DCE con José y los cambios de
CFG de Meibel. No se conectan CLI, backend, Makefile ni niveles de optimización.
Una sola palabra no demuestra RAW N+3, aliasing entre instrucciones, finalización
ni equivalencia de un programa BAL.

## Pruebas y reproducción

La suite C contiene34 oráculos manuales, construidos con CB, casos de x0/x1/x31,
pares Feistel, fuentes/destinos coincidentes, LSU y ambigüedad ABI. Comprueba
rechazos y conservación byte a byte de salidas, reutilización y bits libres REG
que S2 ignora. Un barrido adicional de8192 combinaciones tipo/ID/slot con campos
bajos cero contrasta los estados con S2; no cubre todas las2^32 palabras.

No es posible representar un inmediato textual mayor que su campo dentro de
una palabra: se prueban límites representables y restricciones estáticas de S2
(pares, offset directo desalineado). No se inventan rechazos por bits que S2
ignora ni se introduce un parser para nombres virtuales.

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -B tests/vliw/test_vliw_metadatos.py -v
```

La suite compila C11 con -Wall -Wextra -Werror -pedantic en temporales externos.
Una sonda adicional enlaza solo extractor+S2. CB se enlaza únicamente en los
fixtures diferenciales. CC admite flags sanitizadores; I1A2A_EVIDENCIA puede
señalar un JSON externo. No se modifica validar_vliw.py ni Makefile.

ASan/UBSan/LSan y Memcheck se aplican a ejecutables C, no a Python/RTL. Memcheck
complementa las verificaciones de memoria no inicializada; ASan no la demuestra
por sí solo. Los resultados reales de cada pasada y de las regresiones se
conservan en las evidencias externas, sin convertir estas pruebas de palabras
en validación del compilador optimizado.
