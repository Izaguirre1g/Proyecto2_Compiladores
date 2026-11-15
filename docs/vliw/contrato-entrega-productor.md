# Contrato de entrega al consumidor VLIW

## Frontera disponible y responsables

IMPLEMENTADO: el consumidor de Javier recibe `VliwCadenaEntrada` definida en
`include/vliw_cadena_fisica.h`, genera metadatos mediante I1-A2a y construye
CFG/DAG/proyección antes de ejecutar I1-A1/S7/S8-A/CB/S2/S8-C. El productor
no debe fabricar aristas ni copiar un grafo de dependencias virtuales.
Puede usar por separado `VliwProyeccionEntrada` si ya dispone de CFG/DAG reales.

Dylan mantiene representación/asignación/spilling; Meibel transforma la IR;
José mantiene DCE y sus observables/rutas; el equipo debe acordar el backend,
ABI y enlace. Este documento solicita acuerdos, no atribuye compromisos.
La rama feature/register-spilling sigue separada: no se fusionó ni se copiaron
sus archivos. Su disponibilidad no implica integración o API estabilizada.

## Datos exactos que debe entregar el productor

| Dato público | Contenido exigido | Estado |
|---|---|---|
| VliwCfgEntrada.operaciones | ID único, palabra ISA no-NOP, slot, SimSalto y VliwControl explícito | Consumidor IMPLEMENTADO; productor BLOQUEADO |
| funciones | IDs e intervalos físicos que cubren el flujo | PENDIENTE DE ACUERDO para BAL |
| simbolos | ID inequívoco, función y posición física | PENDIENTE DE ACUERDO |
| transferencias | Posición BRU, tipo, símbolo tomado, continuación y posibles dinámicos completos | Consumidor IMPLEMENTADO; acreditación externa |
| fronteras/regiones | Líderes adicionales e intervalos indivisibles de expansiones | PENDIENTE DE ACUERDO |
| generacion/referencias | Identidad vigente compartida, renovada tras cualquier transformación relevante | Consumidor IMPLEMENTADO; protocolo externo pendiente |
| efectos_adicionales | Un elemento por operación, incluso cero, con efectos acreditados no deducibles de S2 | PENDIENTE DE ACUERDO |
| textos/cantidad | Una operación ISA compatible por posición, menos de160 bytes | Consumidor IMPLEMENTADO |
| efectos_acreditados/abi_acreditado | Afirmaciones respaldadas por evidencia de esta generación | PENDIENTE DE ACUERDO |
| garantias | pendientes=0, completitud_declarada y reubicaciones_resueltas explícitos para no vacío | Consumidor IMPLEMENTADO; acreditación externa |

Los textos no declaran etiquetas ni bundles completos. El destino textual
directo es marcador; la identidad del destino viene de la tabla estructurada,
no de su entero antiguo. Los IDs no son posiciones ni PCs. No se usa nombre
textual ambiguo como identidad única; alias requieren IDs simbólicos distintos.

Las máscaras físicas se obtienen con S2 y se contrastan: x0 no genera
riesgos y AUSENTE no es x0. Feistel necesita ambos destinos/fuentes reales.
Ningún nombre $tN/$rN puede ocupar el lugar de una máscara física.

## Acuerdo requerido con Dylan

PENDIENTE DE ACUERDO: mapeo final $tN/$rN→xN; pool y registros reservados,
argumentos/retornos, auxiliares y registros de expansiones. Deben definirse
vida útil, reutilización, valores vivos en llamadas, clobbers y preservación.
El consumidor no decide unilateralmente ABI ni añade clobbers del callee a un
SYER: las operaciones reales de guardar/restaurar deben existir en el flujo.

DISPONIBLE EN RAMA SEPARADA: trabajo de register-spilling conocido por su
referencia local verificada. No se acredita aquí su funcionalidad ni su
integración productiva. Se necesita IR resultante, procedencia original→clones
/inserciones, ranuras, offsets, marco final y auxiliares. Acreditar accesos
fuera de rango, expansión definitiva, actualización CFG e invalidación de
análisis después de insertar prólogos/spills. No se consumen copias informales.

## Símbolos, expansiones y enlace compartido

PENDIENTE DE ACUERDO: identidad simbólica, función/ámbito, referencias,
continuaciones, alias, materialización y reubicación. Las funciones se identifican
antes de convertirlas en direcciones físicas. El productor debe conservar
operaciones afectadas, clase de reubicación, símbolo, generación, alcance,
adendo, fragmentos y longitud de cada expansión.

S8-A solo reescribe directos estructurados; no reubica las ALU que forman x13.
No se permite reutilizar direcciones anteriores, convertir SYER en SYE ni
volver a empaquetar el plan con rs_resolver. Cambio de longitud/operaciones,
registros, efectos, símbolos o destinos invalida metadatos, CFG, DAG, plan y
verificaciones. Acordar protocolo de convergencia con límite y fallo sin
publicación. BLOQUEADO: ese enlace productivo no existe en el consumidor.

Un productor con obligaciones pendientes debe informarlas y provocar rechazo,
no marcar reubicaciones_resueltas=true para superar una prueba. Los fixtures
call/ret explícitos no acreditan direcciones o preservación del backend real.

## Acuerdo requerido con Meibel

Las transformaciones pueden clonar/mover/eliminar instrucciones y cambiar
presión de registros, bloques y funciones. Se requiere procedencia persistente,
efectos conservados, límites y orden acordado respecto de asignación/spills.
Unrolling/reordenamiento actuales trabajan sobre nombres IR; su grafo/latencia
estimada no sustituye al DAG físico global RAW3/WAR/WAW/memoria/barreras.
Después de cualquier cambio se deben renovar generación, fronteras y análisis.
El consumidor no altera sus módulos ni presume que sus interfaces sean definitivas.

## Acuerdo requerido con José y el equipo

PENDIENTE DE ACUERDO: resultados BAL observables, correspondencia de objetos
entre versiones, GPR/DMEM, excepciones, errores y finalización comparable.
B1 mantiene equivalencia estricta pública de 32 GPR/DMEM/CRIPTO observable;
renombrar registros o cambiar marcos puede dar DIFERENTE sin probar defecto
semántico de BAL. No se redefine B1 para obtener un speedup.

DCE/IDE pueden reescribir BAL y recompilarlo por el backend tradicional; eso
no demuestra IR optimizada→cadena VLIW. Las decisiones sobre eliminar efectos,
retorno de main y cortes arquitectónicos deben conservarse explícitas.

## Propiedad, errores y entrega verificable

El productor posee sus tablas/textos durante la llamada y entrega almacenamiento
real accesible, inmutable, con cantidades coherentes. No presta AST/IR destruida
o mutable sin vida acordada. La cadena copia lo necesario, publica atómicamente
y su liberador no destruye préstamos. Diagnóstico/salida no se solapan con entrada.

Para conectar sin reescribir consumidores se requiere: exportación física
estructurada con estos tipos, acreditación de ABI/efectos/reubicación, prueba
CB/S2, generación renovada, fixtures de funciones/spills reales, contraparte
tradicional comparable y criterios observables acordados. Rechazos de los
consumidores son errores visibles, no oportunidades de inventar información.

IMPLEMENTADO: frontera consumidora y diagnósticos verificables.
DISPONIBLE EN RAMA SEPARADA: spilling, sin incorporación aquí.
PENDIENTE DE ACUERDO: ABI, productor, símbolos/expansiones y observables.
BLOQUEADO: BAL productivo hacia S7/S8-A hasta acreditar esas obligaciones.
