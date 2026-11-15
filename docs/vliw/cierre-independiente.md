# Estado del cierre independiente de Javier

## Frontera conseguida

La implementación consumidora recibe datos ISA físicos explícitos acreditados,
no IR virtual ni BAL. Construye metadatos/CFG/DAG y ejecuta proyección,
I1-A1, S7, verificador, S8-A, CB/S2 y estadísticas/JSON S8-C. Puede producir
imágenes mediante las APIs CB existentes y evaluarlas con simulador/B1/B2 en
los casos comparables. Los fixtures de prueba no acreditan al backend real.

La ejecución y sus resultados definitivos (incluidos instrumentos, códigos,
fallos iniciales y posibles omisiones) se conservan en el directorio externo
`evidencias-proyecto2-compiladores-cierre-independiente`. Este documento describe
contratos y estado funcional; no sustituye el informe de pruebas ni convierte
una ejecución pendiente/fallida en aprobación histórica.

## Componentes y dependencias

| Componente | API/entrada→salida | Propietario y estado | Garantía/límite; siguiente requisito |
|---|---|---|---|
| S1–S6 | Imagen/ISA→pipeline simulado | Javier; existente | Mantiene ISA/latencias; no genera código optimizado |
| S7 | vliw_planificar/VliwEntrada→VliwPlan | Javier; existente | Plan físico conservador; requiere datos completos |
| S8-A | vliw_emitir/plan+destinos→ASM | Javier; existente | Directos lógicos y CB; no enlace de constantes materiales |
| S8-C | vliw_estadisticas/vliw_visualizar→métricas estáticas/JSON | Javier; existente | No son ciclos/IPC dinámicos |
| M1/M2 | Sesiones/ejecución→métricas dinámicas | Javier; existente | Medición real bajo horizonte definido |
| B1 | Imágenes comparables→EQUIVALENTE/DIFERENTE/INCONCLUSO/ERROR | Javier; existente | Contrato estricto público, no equivalencia BAL general |
| B2 | Manifiesto+B1→JSON/CSV/agregados | Javier; observación de descendientes reparada | Conserva timeouts/errores; cuatro regresiones nuevas aprobadas |
| B3–B5 | Compilación/finalización/cortes→evidencia | Javier; existente | Corte ACTIVO no certifica retorno semántico de main |
| I1-A1 | vliw_integracion_validar→coherencia local | Javier; existente | No autentica productor/ABI/reubicaciones |
| I1-A2a | vliw_metadatos_extraer/palabra+slot→metadatos parciales | Javier; existente | No borra obligaciones externas |
| CFG/DAG | vliw_cfg_construir/vliw_dag_construir→análisis físicos | Javier; existente | Generación coherente no autentica historia del flujo |
| Proyección | vliw_proyeccion_construir→VliwEntrada/destinos propios | Javier; completado con fixtures | Copia y valida; requiere créditos externos reales |
| Cadena | vliw_cadena_construir→plan/ASM/CB/S8-C propios | Javier; completado con fixtures | Procesamiento automático de ISA acreditada; no productor BAL |
| IR/asignación/spills | Exportación física pendiente | Dylan/equipo; pendiente de acuerdo/rama separada | Mapeo, ABI, marcos y expansiones deben acreditarse |
| DCE | Transformación IR y rutas IDE/CLI | José; existente | Observables/comparabilidad requieren acuerdo |
| Unrolling/reordenamiento | Transformación/análisis virtual | Meibel; existente | Procedencia e invalidación; no sustituye DAG físico |
| Backend/enlace | ISA/símbolos/reubicaciones finales | Equipo; bloqueado para integración nueva | Exportar antes de perder identidades; convergencia con S7 |

## Verificación y casos integrales

Las dos suites nuevas usan fixtures explícitos. La proyección conserva exactamente
DAG/CFG, copia textos y vive sin sus préstamos. La cadena procesa positivos,
RAW/WAR/WAW/LSU/CRIPTO, backedges/call-ret y negativos con diagnósticos de fase.
Se prueban publicación atómica, reserva fallida en cada punto observado,
recuperación, fallos de consumidores y apertura fallida de diagnóstico.
No se afirma cobertura de toda E/S o ausencia universal de UB.

La demostración ALU/load/ALU/store compara dos imágenes físicas mediante B1 y
B2, manteniendo la categoría demostración y la equivalencia estricta antes de
usar métricas. No son benchmarks oficiales de niveles -O. Las estadísticas de
ocupación/guardas son estáticas; ciclos/IPC provienen del simulador real.

## Estado de fases actuales

- Completado con fixtures: consumidor físico completo y evaluación aplicable.
- Implementado: proyección/coordinación, garantías explícitas, diagnósticos,
  recursos y documentación; el resultado global se acredita con pruebas nuevas.
- Pendiente de acuerdo: productor estructurado, ABI, efectos/observables,
  procedencia, símbolos y protocolo de reubicación/invalidación.
- Bloqueado: BAL optimizado→asignación/spilling→ISA final→cadena, hasta cumplir
  `contrato-entrega-productor.md`.
- No iniciado: opciones -O en CLI, backend alternativo, enlace compartido nuevo,
  fusiones de ramas, commits o publicación de este trabajo.

La numeración histórica no se usa para fingir que I1-A3 o el productor están
completos. No se modificaron los módulos de compañeros ni Makefile/CLI.

## Qué falta antes de integrar Dylan

Entregar palabras/textos compatibles, IDs y símbolos persistentes, funciones y
transferencias completas, auxiliares/expansiones definitivos, marcos y offsets
válidos, preservación de registros vivos y clobbers acordados. Renovar generación
y análisis después de cambios. Acreditar efectos, reubicaciones y observables
antes de declarar completitud. El grafo virtual o una rama de spilling separada
no son sustitutos de esos datos.

Javier dispone de la frontera consumidora; no decide ni implementa por otros
integrantes sus contratos. La integración productiva exige coordinación nueva.

## Resultado de la verificación de este cierre

Las cuatro pasadas completas (normal, ASan, UBSan y ASan+UBSan) descubrieron
22 suites y ejecutaron 408 métodos cada una, con cero fallos y cero omisiones.
`make -B balc` y `make -B -k test` terminaron con código cero en copia externa.
Las dos suites nuevas pasaron LSan por separado; 64 procesos Memcheck en
O0/O1/O2/O3 terminaron sin errores ni bloques pendientes al salir.

La proyección ejecutó 177 escenarios C y la cadena 391, incluidos recursos y
rechazos; estos conteos son distintos de los métodos Python. La repetición B2
corregida ejecutó 185 métodos sin fallos. Las cifras describen estas ejecuciones,
no una garantía universal de ausencia de carreras, fugas o comportamiento indefinido.

El alcance independiente queda completado y verificado con entradas físicas
explícitas. La ruta productiva BAL sigue bloqueada por los contratos externos
anteriores. La GUI GTK interactiva y un testbench RTL nuevo para la imagen de
la demostración no se validaron; las regresiones conservaron los contrastes RTL
previos aplicables. Comandos, códigos, manifiestos y límites se detallan en el
informe externo A–BG.
