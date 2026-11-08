# Loop unrolling (Proyecto II — Meibel)

Optimización que desenrolla ciclos sobre la IR de tres direcciones
([docs/ir.md](../docs/ir.md)). Repite el cuerpo del ciclo varias veces para
eliminar comparaciones y saltos, y deja bloques básicos más largos para que el
reordenamiento y la calendarización VLIW tengan más operaciones para empaquetar.

```
loop_unrolling/
├── loop_unrolling.h         interfaz pública (opciones, resumen, lu_desenrollar)
├── loop_unrolling.c         implementación
├── README.md                este documento
└── pruebas/
    ├── test_loop_unrolling.c   pruebas unitarias con intérprete de IR
    ├── test_bal.sh             pruebas de extremo a extremo con balc
    └── 01..05_*.bal            programas de prueba
```

## Uso

```sh
make                                              # compila balc
./balc --unroll  loop_unrolling/pruebas/01_n_constante.bal   # factor 4
./balc --unroll=8 loop_unrolling/pruebas/03_variable_grande.bal
make test-loop-unrolling                          # corre todas las pruebas
```

`--unroll[=k]` valida la semántica, imprime la IR **antes**, el registro de
decisiones, un resumen (instrucciones antes → después) y la IR **después**.
`k` debe estar entre 2 y 64. Igual que `--ir`, no genera `.asm` ni `.bin`.

Desde código:

```c
LuOpciones op; LuResumen r;
lu_opciones_defecto(&op);
op.factor = 4;
lu_desenrollar(&ir, &op, &r, stdout);   /* recalcula los bloques básicos */
```

## Forma canónica que se reconoce

Es la que genera `cycle` (y también un `whale` contador):

```
i = inicio
$Lx:                         ← cabecera
$tc = i < limite             (o <=)
ifFalse $tc goto $Ly
   ...cuerpo...
$tp = i gauss paso           (paso: entero constante > 0)
i = $tp
goto $Lx
$Ly:                         ← salida
```

## Precondiciones (si una falla, el ciclo no cambia y se informa el motivo)

| Regla | Por qué |
|---|---|
| La condición es `i < limite` o `i <= limite`, con `limite` variable o entero | Así se puede calcular cuántas vueltas faltan. |
| El iterador avanza solo al final con `i = i gauss paso`, con paso constante positivo | El número de vueltas se puede predecir. |
| El cuerpo **no escribe** el iterador ni el límite | Si no, la cantidad de vueltas cambia durante el ciclo. |
| El cuerpo no tiene `return` ni salta fuera del ciclo | Las copias deben ejecutarse completas. |
| El cuerpo no tiene ciclos internos (saltos hacia atrás) | Los ciclos internos se desenrollan primero; si el interno no se puede desenrollar del todo, el externo se deja igual. |
| El cuerpo no pasa de `max_cuerpo` (32) instrucciones | Evita que el código crezca demasiado. |

Las **llamadas** (`arg`/`call`) sí se permiten: cada copia las conserva en el
mismo orden y con los mismos argumentos, así que los efectos no cambian. Las
variables locales no se pueden modificar desde otra función, porque la IR
todavía no baja globales.

## Heurística

`N` es el número de vueltas. Se conoce si `inicio` y `limite` son enteros, o
variables cuyo último valor antes del ciclo, en el mismo bloque, es una
constante (por ejemplo `n = 3`).

| Caso | Decisión |
|---|---|
| `N` conocido, `N ≤ 16` y `N·(cuerpo+2) ≤ 64` | **Total**: N copias, sin comparación ni saltos. |
| `N` conocido pero grande | **Parcial con factor k**: ciclo principal + `N mod k` copias en línea recta (sin ciclo residuo). |
| `N` desconocido (parámetro, resultado de una llamada…) | **Parcial con factor k** + el ciclo original como **residuo**. |
| `k·(cuerpo+2) > 64` | Se baja `k` hasta que quepa; si `k < 2`, no se desenrolla. |

El factor por defecto es 4 y se cambia con `--unroll=k`.

### Desenrollado parcial

El ciclo principal solo entra si quedan **al menos k vueltas**:

```
$Lm:
$ta = i gauss (k-1)*paso
$tc = $ta < limite           (misma relación que el original)
ifFalse $tc goto $Lx         ← al ciclo residuo (o a las copias sobrantes si N es conocido)
cuerpo ; i = i + paso        ← k veces
goto $Lm
$Lx: ...ciclo original...    ← ejecuta las 0..k-1 vueltas que faltan
```

### Renombrado en cada copia

Cada copia del cuerpo recibe **temporales `$tN` y etiquetas `$LN` nuevos**. Sin
eso, un `whether` dentro del ciclo definiría la misma etiqueta varias veces y la
IR quedaría incorrecta. Las variables del programa no se renombran.

## Ejemplos antes / después

### N constante → total (`01_n_constante.bal`)

```
ANTES (13 instr, 4 bloques)          DESPUÉS (24 instr, 1 bloque)
i = 0                                i = 0
$L1:                                 $t4 = suma gauss i
$t1 = i < 5                          suma = $t4
ifFalse $t1 goto $L2                 $t5 = i gauss 1
$t2 = suma gauss i                   i = $t5
suma = $t2                           ... (5 copias en total)
$t3 = i gauss 1                      $t13 = i gauss 1
i = $t3                              i = $t13
goto $L1                             endfunc main
$L2:
```

Se ejecuta la mitad de instrucciones (48 → 24 en el intérprete) y todo queda en
**un solo bloque básico** para la calendarización VLIW.

### N desconocido → parcial + residuo (`sumar_hasta(n)` en `02_variable_pequeno.bal`)

```
$L5:                                 ← ciclo principal (factor 4)
$t9 = i gauss 3
$t10 = $t9 < n
ifFalse $t10 goto $L1
  suma += i ; i += 1   (×4, temporales $t11..$t18)
goto $L5
$L1:                                 ← ciclo original = residuo
$t1 = i < n
ifFalse $t1 goto $L2
...
```

## Resultados de las pruebas

`make test-loop-unrolling` ejecuta dos tipos de prueba:

1. **Unitarias** ([pruebas/test_loop_unrolling.c](pruebas/test_loop_unrolling.c)):
   construyen la IR, la desenrollan y la **ejecutan con un intérprete** antes y
   después. Las variables finales y la traza de llamadas deben coincidir
   exactamente. También verifican que no haya etiquetas duplicadas y que el CFG
   se pueda reconstruir.
2. **Extremo a extremo** ([pruebas/test_bal.sh](pruebas/test_bal.sh)): corren
   `balc --unroll` sobre los `.bal` y revisan la decisión que se tomó.

Instrucciones ejecutadas por el intérprete (factor 4 salvo que se indique otro):

| Prueba | IR estática | Ejecutadas antes → después | Decisión |
|---|---|---|---|
| N constante (5) | 13 → 24 | 48 → 24 (−50 %) | total |
| n conocido = 3 | 14 → 17 | 33 → 17 | total |
| n desconocido = 1000 | 16 → 37 | 8011 → 5265 (−34 %) | parcial + residuo |
| n conocido = 1002 | 14 → 35 | 8025 → 5268 | parcial + 2 sueltas |
| factor 2 / 4 / 6 / 8, n = 101 | 29 / 37 / 45 / 53 | 819 → 673 / 548 / 519 / 499 | parcial |
| `whether` + llamada, 4 vueltas | 24 → 61 | 73 → 53 | total |
| `whether` + llamada, 50 vueltas | 24 → 95 | 809 → 670 | parcial |
| anidado 2×3 | 21 → 34 | 78 → 34 | total + total |
| anidado 40×3 | 21 → 70 | 1408 → 659 | interno total, externo parcial |
| modifica iterador / límite | sin cambio | igual | rechazado |

Cuando `n` es desconocido y muy pequeño (`n < k`) se ejecutan unas pocas
instrucciones más (la comprobación del ciclo principal). Es el costo del
desenrollado parcial y se recupera desde `n ≥ k`.

## Limitaciones conocidas

- El límite debe ser una variable o un entero. Un límite calculado en la
  cabecera (`until n gauss 1`) no se reconoce, y el ciclo se deja igual con el
  motivo indicado.
- Solo se reconocen ciclos crecientes (`<`, `<=`, paso positivo). La IR todavía
  no genera `cycle` con paso negativo.
- No se renombra el iterador para romper la cadena `i = i + paso` entre copias.
  Una mejora posible para dar más paralelismo al VLIW es usar `i + c·paso` en
  cada copia y un solo incremento al final.

## Integración con el resto del grupo

- **Entrada:** la IR de `ir_desde_ast` (Dylan). **Salida:** una IR válida con
  los bloques básicos ya recalculados.
- Orden sugerido del pipeline: `IR → loop unrolling → DCE (José) →
  reordenamiento (Meibel, parte 2) → registros/spilling → VLIW (Javier)`.
- `LuResumen` trae los datos para las métricas del grupo: ciclos totales,
  parciales y rechazados, e instrucciones antes y después.
