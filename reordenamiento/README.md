# Reordenamiento seguro

Optimización que cambia el orden de las instrucciones **dentro de cada bloque
básico** de la IR ([docs/ir.md](../docs/ir.md)) para que el procesador espere
menos, sin cambiar el resultado del programa.

El procesador VLIW **no tiene forwarding**: un valor escrito en el bundle N
recién se puede leer desde N+3 ([docs/backend_binario.md](../docs/backend_binario.md)).
Si una instrucción lee algo que se acaba de calcular, hay que esperar (NOPs).
El reordenamiento pone trabajo independiente en ese hueco.

```
reordenamiento/
├── reordenamiento.h / .c    la optimización (DAG + list scheduling)
├── ro_reporte.h / .c        reporte visual de balc --reorder
├── ro_json.h / .c           grafo en JSON (balc --reorder-json)
├── visualizar_grafo.html    visualizador del JSON (grafo, ruta crítica, ciclos)
├── README.md                este documento
└── pruebas/
    ├── test_reordenamiento.c   casos dirigidos + 1000 programas aleatorios
    ├── test_bal.sh             pruebas de extremo a extremo con balc
    └── 01..06_*.bal            programas de prueba
```

## Uso

```sh
make
./balc --reorder reordenamiento/pruebas/01_war.bal              # latencia 3
./balc --reorder=2 reordenamiento/pruebas/02_waw.bal            # otra latencia (1..16)
./balc --unroll --reorder reordenamiento/pruebas/06_ciclo_desenrollado.bal
make test-reordenamiento
```

Con `--unroll --reorder` primero se desenrolla y después se reordena: el
desenrollado deja bloques más largos y el reordenamiento los aprovecha.

El reporte muestra los bloques que cambiaron (instrucciones movidas, ciclos
antes → después y dependencias por tipo), las métricas totales y la IR antes y
después lado a lado. `*` marca lo que cambió de lugar, y cada instrucción
conserva su **número original** para que se vea a dónde se movió.

Desde código:

```c
RoOpciones op; RoResumen r;
ro_opciones_defecto(&op);              /* latencia 3 */
ro_reordenar(&ir, &op, &r);            /* ir debe tener sus bloques básicos */
/* r.movidas, r.ciclos_antes, r.ciclos_despues, r.raw, r.war, ... */
ro_resumen_liberar(&r);
```

## Ver el grafo y la ruta crítica

Igual que con el AST (`--ast-json` + `tools/visualizar_ast.html`), el grafo de
dependencias se puede exportar a JSON y verlo en el navegador:

```sh
./balc --reorder-json reordenamiento/pruebas/01_war.bal > 01_war.grafo.json
./balc --reorder-json=2 reordenamiento/pruebas/02_waw.bal > 02_waw.grafo.json   # otra latencia
./balc --unroll --reorder-json reordenamiento/pruebas/06_ciclo_desenrollado.bal > 06.grafo.json
```

Después se abre `reordenamiento/visualizar_grafo.html` (doble clic; funciona
sin Internet) y se elige el `.json`. Para cada bloque básico muestra:

- **El grafo (DAG).** Cada caja es una instrucción, y cada flecha, una
  dependencia (la de arriba va antes). Los colores indican el tipo: RAW, WAR,
  WAW, memoria, efecto o control, y la etiqueta dice qué variable la causa y
  cuántos ciclos hay que esperar. Las dependencias de control (todo → salto
  final) vienen ocultas, pero se pueden activar.
- **La ruta crítica**, en rojo. Es la cadena de esperas más larga, y ningún
  orden puede terminar en menos ciclos que ella. Si el orden nuevo tarda lo
  mismo que la ruta crítica, el resultado es óptimo.
- **El detalle de una instrucción.** Al pulsarla se ve qué lee y qué escribe,
  de quién depende y quién depende de ella, y en qué ciclo sale antes y
  después.
- **Una línea de tiempo antes/después**, con un ciclo por casilla. Las casillas
  rayadas son esperas (NOPs) y se ve cómo el reordenamiento las llena.

El JSON sale del **mismo análisis** que usa `ro_reordenar` (la función
`ro_analizar_bloque`), así que el grafo y los ciclos coinciden con el reporte
de `--reorder`. `--reorder-json` no se combina con `--reorder`.

### Formato del JSON

```jsonc
{
  "formato": "bal-reordenamiento", "version": 1,
  "archivo": "01_war.bal", "latencia": 3, "factor_unroll": null,   // o el factor k
  "bloques": [{
    "id": 0, "inicio": 0, "fin": 11, "sucesores": [],
    "fijas": [{"pos": 0, "texto": "func main", "linea": 5}],      // no se mueven
    "ciclos_antes": 16, "ciclos_despues": 13, "aplicado": true,
    "orden": [0, 1, 2, 4, 5, 3, 6, 7, 8, 9],     // nodo que queda en cada lugar
    "ruta_critica": {"ciclos": 13, "nodos": [0, 2, 3, 7, 8, 9]},
    "nodos": [{
      "id": 2, "pos": 3, "texto": "$t1 = a pitagoras 2", "linea": 8,
      "lee": ["a"], "escribe": ["$t1"],
      "altura": 11,                 // camino crítico desde aquí (prioridad)
      "critico": true,
      "ciclo_antes": 3, "ciclo_despues": 3,   // null si no ocupa ciclo (endfunc)
      "nueva_pos": 2
    }],
    "aristas": [{
      "de": 2, "a": 3, "latencia": 3,
      "tipos": ["RAW"],             // RAW, WAR, WAW, MEMORIA, EFECTO, CONTROL
      "por": "$t1",                 // variable que la causa (null en efecto/control)
      "critica": true
    }]
  }],
  "resumen": {"bloques": 1, "bloques_cambiados": 1, "instrucciones": 11,
              "movidas": 3, "ciclos_antes": 16, "ciclos_despues": 13}
}
```

`de` y `a` son índices de `nodos` del mismo bloque, y siempre `de < a`
(el orden original). `pos` es la posición en la IR.

## Reglas (qué NO se puede mover)

Para cada par de instrucciones `i` (antes) y `j` (después) del mismo bloque:

| Dependencia | Cuándo | Ejemplo | Regla |
|---|---|---|---|
| **RAW** | `j` lee lo que `i` escribe | `$t1 = a * 2` → `b = $t1` | `j` va después de `i` y espera la latencia |
| **WAR** | `j` escribe lo que `i` lee | `$t1 = a * 2` → `a = 20` | `j` no puede subir antes de `i` (si no, `i` leería 20) |
| **WAW** | ambas escriben lo mismo | `r = p + 1` → `r = p + 7` | conservan su orden (el valor final es el de `j`) |
| **Memoria** | `store`, listas, arreglos y `load` | `store r, 42` → `v = load r` | se ordenan como si "la memoria" fuera una variable |
| **Efectos** | `arg` / `call` | `arg x`, `call f` | son barreras: nada las cruza y no se mueven entre sí |
| **Control** | etiquetas, saltos, `return` | `$L1:` … `ifFalse $t goto $L2` | etiquetas/`func`/`param` quedan al inicio; el salto/`return`/`endfunc` queda al final |

Además, **ninguna instrucción sale de su bloque básico**. Así nada cruza un
salto condicional ni entra o sale de una rama de un `whether`.

Para RAW y WAW solo se agrega la dependencia con la escritura **más cercana**,
y para WAR, con las lecturas que hay desde esa escritura. El resto queda
cubierto por transitividad (es la construcción estándar del DAG).

## Algoritmo

1. **Partes fijas.** Se separan las etiquetas/`func`/`param` del inicio. El
   terminador (salto, `return`, `endfunc`) recibe una arista de control desde
   todas las demás instrucciones, así que siempre queda último.
2. **DAG.** Cada arista `i → j` guarda cuántos ciclos después de `i` puede
   emitirse `j`: la latencia para RAW y 1 para lo demás.
3. **List scheduling.** Ciclo a ciclo se elige, entre las instrucciones listas
   (dependencias ya emitidas y valores disponibles), la de **camino crítico más
   largo**. Si ninguna está lista, se avanza al primer ciclo en que alguna lo
   esté. Empates: la que estaba antes.
4. **Verificación.** Se comprueba que el orden nuevo respete *todas* las
   aristas. Además, el bloque **solo cambia si el orden nuevo necesita menos
   ciclos** según el modelo. Si no mejora, queda igual.

### Modelo de ciclos

Se emite una instrucción por ciclo, en orden, y un valor escrito en el ciclo `t`
se puede leer desde `t + latencia`. Las etiquetas, `func`, `endfunc` y `param`
no ocupan ciclos. Es una estimación para comparar órdenes. Los ciclos reales
dependen de cómo Javier empaquete en bundles, aunque ese empaquetado se
beneficia de lo mismo: escrituras y lecturas más separadas.

## Ejemplo antes / después (`01_war.bal`, latencia 3)

```
IR ANTES                       │ IR DESPUÉS  (16→13 ciclos)
  0003  $t1 = a pitagoras 2    │   0003  $t1 = a pitagoras 2
* 0004  b = $t1                │ * 0005  a = 20               ← WAR: sigue después de 0003
* 0005  a = 20                 │ * 0006  $t2 = x pitagoras 3  ← independiente, llena el hueco
* 0006  $t2 = x pitagoras 3    │ * 0004  b = $t1              ← $t1 ya está listo
  0007  c = $t2                │   0007  c = $t2
  0008  $t3 = a gauss b        │   0008  $t3 = a gauss b
```

`b = $t1` tenía que esperar 2 ciclos a `$t1`. Ahora esos ciclos se usan para
`a = 20` y `$t2 = x * 3`. `a = 20` no sube antes de `0003`, que lee el valor
viejo de `a` (WAR).

## Pruebas y resultados

`make test-reordenamiento` ejecuta dos tipos de prueba:

1. **Unitarias** ([pruebas/test_reordenamiento.c](pruebas/test_reordenamiento.c)).
   Cada programa se ejecuta con un **intérprete** antes y después: las
   variables, la memoria y la traza de llamadas deben coincidir. También se
   comprueba que las etiquetas y saltos sigan en su lugar, que nada salga de su
   bloque y que los ciclos nunca empeoren.
   - Casos dirigidos: WAR, WAW, cadena RAW (no se mueve nada), control,
     llamadas, memoria y latencia 1 (sin esperas, no se mueve nada).
   - **1000 programas aleatorios** con 5 variables (muchos WAR/WAW), `if/else`,
     llamadas, `store` y `load`, con latencias 1 a 4.
2. **Extremo a extremo** ([pruebas/test_bal.sh](pruebas/test_bal.sh)) con `balc --reorder`.
   También revisa el JSON de `--reorder-json`: los mismos ciclos que el
   reporte, que el orden nuevo respete cada arista y que ningún bloque
   termine en menos ciclos que su ruta crítica.

| Prueba | Qué muestra | Movidas | Ciclos estimados |
|---|---|---|---|
| `01_war.bal` | WAR: `a` se lee y luego se reescribe | 3 de 11 | 16 → 13 (−19 %) |
| `02_waw.bal` | WAW: `r` se escribe dos veces | 4 de 11 | 19 → 14 (−26 %) |
| `03_war_waw_cadena.bal` | `t` reutilizada: RAW 17, WAR 3, WAW 2 | 10 de 17 | 32 → 23 (−28 %) |
| `04_salto_condicional.bal` | `whether`: solo dentro de cada bloque | 8 de 19 | 28 → 25 |
| `05_llamadas.bal` | `call` como barrera | 4 de 27 | 43 → 38 |
| `06_ciclo_desenrollado.bal` | `--unroll --reorder` | 34 de 96 | 161 → 128 (−20 %) |
| 1000 aleatorios | mismos resultados en todos | 21 497 | 81 261 → 61 877 (−24 %) |

## Limitaciones conocidas

- Solo reordena **dentro** de un bloque básico; no mueve código entre bloques
  (eso sería *global scheduling*).
- No renombra variables para romper dependencias WAR/WAW falsas (por ejemplo,
  usar `a'` en vez de reescribir `a`). Las respeta tal como están.
- Las llamadas son barreras completas, porque la IR todavía no tiene análisis
  de efectos.
- El modelo de ciclos es de una instrucción por ciclo. El empaquetado real en
  bundles con varios slots lo hace la calendarización VLIW (Javier).

## Integración con el resto del grupo

- **Entrada:** IR con bloques básicos (`ir_desde_ast`, de Dylan, y
  opcionalmente después de `lu_desenrollar`). **Salida:** la misma IR con las
  instrucciones reordenadas. La cantidad de instrucciones y los bloques no
  cambian.
- Orden sugerido: `IR → loop unrolling → DCE (José) → reordenamiento →
  registros/spilling → VLIW (Javier)`.
- `RoResumen` trae las métricas para las gráficas del grupo: instrucciones
  movidas, ciclos estimados antes/después y dependencias por tipo.
