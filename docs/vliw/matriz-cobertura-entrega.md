# Matriz de cobertura de la entrega I1-A3

## Corte de cobertura posterior al PR #69 (histórico)

La [guía consolidada](entrega-final-javier.md) y el [contrato O2](integracion-productiva-o2.md)
describen el estado vigente: `-O0` conserva el backend tradicional; `-O1`
utiliza DCE mediante BAL reescrito y recompilación tradicional; `-O2` cuenta con
una ruta productiva optativa dentro del subconjunto y límites documentados, sin
fallback al backend tradicional. Esta matriz conserva el corte anterior a la
integración O2: sus rechazos del productor, porcentajes y dependencias no deben
interpretarse como cobertura actual de O2 ni como resultados de una campaña
posterior. B1/B2 sigue siendo una dimensión de certificación independiente.


Referencias: Compiladores `9cf06e2`, Arquitectura `5e6e45f`. Resultados reales
en `evidencias-proyecto2-compiladores-i1a3`, fuera del repositorio.

## Quince fuentes existentes: inventario, no quince pares certificados

| Categoría | Fuente | Backend tradicional | Análisis de categoría | Productor IR-VLIW |
|---|---|---|---|---|
| loop_unrolling | `loop_unrolling/pruebas/01_n_constante.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| loop_unrolling | `loop_unrolling/pruebas/02_variable_pequeno.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| loop_unrolling | `loop_unrolling/pruebas/03_variable_grande.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| renombramiento | `examples/expresiones_basicas.bal` | RECHAZO (1) | PASS (0) | RECHAZO (1) |
| renombramiento | `examples/ambitos.bal` | RECHAZO (1) | RECHAZO (1) | RECHAZO (1) |
| renombramiento | `examples/multiplicacion.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| dce | `examples/dce/dce_ensamblador.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| dce | `examples/ejercicio3_puntajes.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| dce | `examples/dce/dce_llamadas.bal` | RECHAZO (1) | PASS (0) | RECHAZO (1) |
| saltos_funciones | `examples/condicional.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| saltos_funciones | `examples/whale.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| saltos_funciones | `examples/llamadas.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| spilling | `examples/factorial.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| spilling | `examples/ejercicio2_busqueda_matriz.bal` | PASS (0) | PASS (0) | RECHAZO (1) |
| spilling | `examples/cycle.bal` | PASS (0) | RECHAZO (1) | RECHAZO (1) |

12 referencias tradicionales compilan; las 15 entradas son rechazadas por el
productor experimental. El inventario no acredita un par por ejecutar análisis IR.
`ambitos.bal` no contiene main; `expresiones_basicas.bal` falla en el backend
tradicional por referencia semántica; `dce_llamadas.bal` requiere soporte de
parámetros compuestos. `cycle.bal` compila tradicionalmente pero su análisis de
spilling rechaza el paso no constante positivo. Se conservan los diagnósticos.

## Pares reales y límites de certificación

DCE del IDE transforma de verdad dos fuentes de la categoría dce:

| Fuente | Observables BAL | Corte original→DCE | Bundles estáticos original→DCE |
|---|---|---:|---:|
| ejercicio3_puntajes.bal | resultado=24, combinado=13, total=37 | 2748→2742 | 801→795 |
| dce_ensamblador.bal | cuadrado4=16, suma_hasta5=15 | 6193→2176 | 528→435 |

Los cuatro cortes fueron recalculados con B5 sobre PR #68, no copiados de
resultados históricos. Cada variante coincide completa con M2 y RTL modos 0/2;
las direcciones de los observables se derivan de su propio marco y layout.
Son **2/15 pares contrastados semánticamente**; faltan 13 pares para esa matriz.
B1 no acepta cortes ACTIVO como terminación: incluso las tres autocomparaciones
de demostración resultan INCONCLUSO, código 3, propagado por B2. Por tanto hay
**0/15 equivalencias estrictas oficiales B1/B2 acreditadas en esta ejecución**.
No se habilita `--exigir-cobertura` ni se rellena el lote con fixtures.

## Estado por responsabilidad

| Trabajo | Estado | Límite |
|---|---|---|
| Productor parcial y consumidor físico | PASS del subconjunto | No backend BAL completo |
| RTL modos 0/2, L2 real y flush | PASS de 17 fixtures, preview CLI y tres BAL tradicionales | No igualdad de tiempos entre modelos |
| #34 tres pruebas | 3 casos contrastados | Llamadas por backend tradicional; ABI experimental pendiente |
| #29 niveles | PARCIAL | Perfiles auxiliares verificados; opciones oficiales -O pendientes |
| #37 quince programas | PARCIAL | 15 inventariados, 12 referencias compilables, 2 pares semánticos, 0 estrictos |
| #41 documentación propia | Actualizada | No sustituye documentación compartida de los compañeros |

## Dependencias pendientes

- Dylan: variables BAL por nombre, CALL/RETURN/PARAM, operaciones generales y
  expansiones fuera de rango. `src/ir_vliw.c` en PR #68 acepta suma/resta en
  líneas 222–229 y rechaza el resto en 238–239: es una frontera explícita.
- Equipo/backend: bootstrap, ABI completo x16..31, símbolos materiales,
  reubicación y contrato de finalización/corte aceptable para evaluación.
- José: mantener la ruta DCE real y sus efectos; no sustituirla por `--dce`
  suponiendo que esa opción emite binario optimizado.
- Meibel/equipo: orden y procedencia de pasadas; unrolling/reorder no acreditan
  renombramiento físico ni variantes ejecutables generales por sí solos.

No se repararon componentes compartidos ni se atribuyeron fallos de fuentes
exploratorias a una regresión causada por estas pruebas.
