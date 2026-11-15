# Contrato de entrada física VLIW — I1-A1

## Alcance y estado

I1-A1 valida una **descripción física explícita** con las estructuras existentes
`VliwEntrada`, `VliwInstruccion`, `VliwDependencia`, `VliwBloque` y `VliwDestinos`.
No produce esas estructuras desde BAL. El productor IR→ISA/ABI/asignación/spills
continúa pendiente de acuerdo con Dylan, José y Meibel.

```text
IR optimizada                         pendiente de conexión al backend
  → lowering y asignación/spilling     productor físico pendiente
  → flujo físico estructurado          entrada externa explícita
  → validación I1-A1                    este módulo
  → VliwEntrada ya proporcionada        no se construye ni corrige aquí
  → S7 → verificador S7 → S8-A          bibliotecas existentes
  → codificación/enlace estable        integración futura, no resuelta por I1-A1
```

El enlace puede requerir iteraciones ANTES de la codificación final: si cambian
instrucciones o tamaños al relajar un salto/materializar una dirección, deben
regenerarse el flujo, sus dependencias, el plan y los destinos. El diagrama no
promete una pasada final de enlazado que arregle un plan obsoleto.

## Tres garantías diferentes

1. **Estructura:** cantidades, punteros requeridos, índices y particiones válidos.
2. **Coherencia física local:** texto ISA reconocido por CB/S2, máscaras y efectos
   mínimos coherentes, CFG/destinos consistentes y restricciones declaradas.
3. **Completitud semántica del productor:** todos los efectos del programa, ABI,
   identidad de símbolos, clobbers, direcciones dinámicas y correspondencia BAL.

`VLIW_INT_OK` acredita las dos primeras dentro del alcance descrito; **no prueba
la tercera**, equivalencia BAL ni que el programa quepa al calendarizar. El
productor debe aportar `metadatos_completos=true` para una entrada no vacía.
El validador exige esa declaración, nunca la escribe y no verifica su autoría.
Una arista ausente no certifica independencia: se contrastan también los riesgos
físicos visibles, pero un efecto omitido desconocido sigue siendo responsabilidad
externa. El verificador de S7 y el código completo validado por S8-A siguen siendo
obligatorios; I1-A1 no recibe ni certifica planes.

## Representación e identidad

Cada operación real tiene `id` único (puede ser SIZE_MAX y no tiene que ser
consecutivo), posición en el arreglo, slot ALU=0/LSU=1/BRU=2/CRIPTO=3, texto
ISA no-NOP de menos de160 bytes, máscaras, efectos, control y bloque. Índices
de aristas/destinos son posiciones, no `id`. NOP se representa mediante slots
vacíos del plan posterior; no es una operación de entrada.

Los GPR son x0–x31. Nombres BAL, `$tN` y `$rN` no son operandos físicos ISA;
no se inventa un mapeo. CB admite sus alias ABI reales. Las máscaras solo usan
bits0..31; el bit0 se ignora en riesgos y al contrastar operandos porque x0 es
cero y descarta escrituras. Escribir x0 sigue siendo una operación no-NOP.
Feistel declara ambos registros fuente y destino. El ABI incluye ra=x1, sp=x2,
gp=x3, argumentos x8–x11 y destino de llamada materializado en x13. Los clobbers
y efectos de la función llamada los garantiza el productor; las llamadas
requieren al menos `VLIW_EF_DESCONOCIDOS` y se tratan como barreras.

Se reutilizan las enumeraciones existentes, sin añadir instrucciones ni opcodes.
CB/S2 son la fuente de reconocimiento ISA. Los efectos mínimos son los mismos
que contrasta S8-A: loads lectura; stores escritura; FSL/FSLI excepción; ELL
bóveda+excepción; SETPWD bóveda+estado+excepción; VCR lectura+estado; CAMCON
lectura+escritura+bóveda+excepción. Se permiten efectos conservadores adicionales
compatibles con la unidad. No se accede al estado de bóveda ni se ejecuta CRIPTO.

## Reconocimiento local del texto

Para no aceptar mnemónicos ficticios ni máscaras inventadas se utiliza el parser
CB existente sobre **un contexto privado de validación de una operación**, con
el texto en su slot y los demás vacíos. No es un plan, un productor ni una salida
publicada. S2 decodifica la palabra obtenida y contrasta operandos y efectos.
No hay gramática ISA ni codificador nuevos en I1-A1.

En BRU directo/condicional, S8-A sustituirá el último operando por el destino
estructurado. I1-A1 valida su forma léxica (identificador o entero con base) y
usa temporalmente offset0 para consultar CB sin necesitar el layout futuro.
No consulta ni reutiliza el destino numérico viejo; no certifica alcance final.
SYER/RET conservan operandos: su dirección efectiva sigue siendo externa.
El texto prestado nunca se modifica. S8-A deberá comprobar el programa completo:
el contexto local no demuestra latencias por rutas ni reubicaciones dinámicas.

## Bloques, control y destinos

Los bloques físicos son intervalos contiguos no vacíos que cubren todas las
operaciones; cada operación referencia el bloque correcto. No se copia sin más
el CFG IR: una expansión puede crear bloques y una llamada BRU termina bloque.
Un bloque sin control tiene únicamente caída al siguiente, o ninguna al final.
Sucesores únicos, indexados por bloque y dentro de rango. Backedges del CFG son
válidos y no se convierten en ciclos del DAG de dependencias.

Cada BRU tiene exactamente una entrada `VliwDestino`, y ninguna operación sin
control la tiene. NULL destinos equivale a tabla vacía. Directo SYE identifica
bloque tomado; condicional identifica tomado y caída secuencial. Una llamada
requiere continuación al siguiente bloque, si existe; directo enumera tomado
más continuación. Indirecto SYER y retorno RET/SYER tienen tomado=VLIW_VACIO;
pueden enumerar destinos posibles, pero no se inventa uno estático. Retorno no
es llamada. NO_RESUELTO se rechaza. Las formas y reglas son las de S8-A.

La identidad lógica de un bloque no es su PC: el plan sitúa su etiqueta antes de
los NOP de protección. Las reubicaciones de enteros en x13, tablas o punteros a
código no caben en VliwDestinos y no se certifican aquí. Falta acordar su contrato
con el productor. Saltos largos/relajación/enlace no están implementados por
I1-A1 ni S8-A. No reutilizar PC del backend tradicional tras cambiar el layout.

## Dependencias y capacidad

El productor entrega aristas externas, nunca deducidas únicamente de nombres IR.
RAW tiene distancia≥3 bundles; WAR/WAW/memoria/control/barrera≥1. Extremos
válidos, orden original `antes < despues`, tipos reconocidos; no se permiten
aristas hacia atrás del DAG aunque haya un backedge válido en CFG. Aristas
repetidas son admisibles y se conserva la mayor restricción por caminos.
RAW/WAR/WAW deben corresponder a intersecciones reales, excluido x0.

Se valida que los riesgos visibles de pares físicos estén cubiertos mediante un
camino del DAG de longitud suficiente: RAW3, WAR/WAW1, dos accesos a memoria1,
control1 y barreras3. Esto comprueba restricciones; no agrega aristas ni arregla
la entrada. CRIPTO y llamadas/retornos son barreras conservadoras como en S7.
La ausencia de un riesgo físico visible no prueba ausencia de efectos externos.

Máximo estructural:4092 operaciones (4×1023), como S7. El límite definitivo es
1023 bundles incluyendo NOP/guardas, con 0x3FF0 reservado al vector. Puede haber
entradas localmente válidas que S7 rechace por capacidad real. Una distancia
≥1023 ya es imposible y se rechaza; todas las cantidades se acotan antes de
multiplicar/recorrer. No se promete un plan para toda entrada aceptada.

## API, memoria y errores

`vliw_integracion_validar(entrada,destinos,diagnostico)` devuelve un estado;
no produce planes, copias de entrada ni salidas propias. Diagnóstico opcional
por valor, con índices y mensaje literal prestado; no requiere liberación.
Argumentos/arreglos/sucesores/textos son prestados e inmutables y deben seguir
vivos durante la llamada. El llamador garantiza almacenamiento accesible real,
texto terminado en NUL y diagnóstico separado de entradas. No es posible
validar punteros arbitrarios en C. Punteros noNULL con cantidad0 no se recorren.

La entrada vacía sin bloques/aristas/destinos se acepta incluso con la declaración
de completitud desactivada, como S7. No se publica una certificación parcial.
La API no tiene salida ocupable; no sustituye una sesión/plan del cliente.
Reserva solo distancias temporales y recursos privados de CB/diagnóstico;
libera todo antes de retornar, también en error. Recursos/MEMORIA y E/S se
distinguen de ISA rechazada. CB ofrece retorno entero+texto: la clasificación
MEMORIA usa el diagnóstico actual `sin memoria`, igual que S8-A; un diagnóstico
no reconocido se informa como rechazo ISA, nunca se convierte en éxito.

## Matriz de invariantes

Los nombres de escenarios coinciden con los grupos de pruebas C; Python ejecuta
sus grupos normales, adversariales, de integración y de recursos.

| Invariante | Fuente | Validación posible | Limitación | Error | Prueba |
|---|---|---|---|---|---|
| Cantidades/punteros coherentes | API C/S7 | NULL requeridos y overflow antes de recorrer | No prueba accesibilidad arbitraria | ARGUMENTO/ESTRUCTURA/CAPACIDAD | estructura |
| IDs únicos; índices separados de IDs | S7 | Comparación e índices acotados | No autentica procedencia | ESTRUCTURA | estructura |
| Slot0..3 y texto ISA real | S7/CB/S2 | CB local + S2, sin NOP | No valida layout final | SLOT/ISA | isa |
| Máscaras x0..31 y operandos exactos | S7/S2 | Bits altos y contraste decodificado sin x0 | No asigna registros | REGISTRO/METADATOS | isa |
| x0 y múltiples destinos | ISA/S2 | Ignorar bit0; declarar par completo | No valida valores ejecutados | METADATOS | isa |
| Efectos reconocidos y mínimos | S8-A/S2 | Bits, unidades y cobertura opcode | Efectos externos dependen productor | EFECTOS | isa |
| Control legal | S7/S8-A | BRU/forma/terminador | ABI semántico externo | CFG/DESTINO | destinos |
| Bloques no vacíos/cobertura | S7 | Partición contigua y pertenencia | No deriva CFG desde IR | CFG | estructura |
| Sucesores/backedges | S7/S8-A | Rango, únicos, caída y tabla | Indirectos reales externos | CFG/DESTINO | destinos |
| Extremos/tipos/orden DAG | S7 | Índices antes<después, intersecciones | No construye aristas | DEPENDENCIA | dependencias |
| Distancias y cobertura de riesgos | S7/arquitectura | Caminos ponderados y mínimos | No sustituye verificador del plan | DEPENDENCIA/CAPACIDAD | dependencias |
| BRU y destinos únicos/resueltos | S8-A | Forma, tomado, caída y CFG | No relocaliza direcciones | DESTINO/NO_RESUELTO | destinos |
| Memoria conservadora | S7/S8-A | Orden entre accesos con efectos | Alias/effects omitidos externos | DEPENDENCIA/EFECTOS | dependencias |
| Barreras/CRIPTO | S7/S2 | Aislamiento contractual por distancia/efectos | AUTH/INIT dinámicos no se ejecutan | EFECTOS/DEPENDENCIA | isa/dependencias |
| Capacidad preliminar | S7 | 4×1023 y distancia<1023 | Tamaño final lo decide S7 | CAPACIDAD | limites |
| Recursos privados | API/CB | Fallos inyectados, limpieza y entradas intactas | Herramientas no prueban todo UB | MEMORIA/IO | recursos |
| Metadatos acreditados externamente | VliwEntrada | Exigir declaración, nunca activarla | No prueba completitud semántica | NO_ACREDITADOS | estructura |

## Coordinación y prueba reproducible

Solo fixtures físicos explícitos: no BAL compilado ni evidencia de equivalencia.
Las pruebas controladas encadenan validador→S7→verificador→S8-A y vuelven a
inspeccionar CB/S2, sin conectar la CLI. La suite Python compila en un temporal
con C11 y advertencias como errores; admite CC con flags de sanitizadores.
No modifica Makefile. Ejecutar desde la raíz:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tests/vliw/test_vliw_integracion.py -v
```

Dylan debe confirmar productor físico, símbolos/tipos, asignación y spills;
José, efectos/errores DCE y presentación; Meibel, transformaciones y CFG.
Javier valida el contrato consumidor. No se cambian sus APIs ni sus archivos.
I1-A2, productor real, política de niveles y enlace quedan fuera de I1-A1.
