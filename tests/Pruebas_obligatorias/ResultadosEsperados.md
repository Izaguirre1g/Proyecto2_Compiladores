# Pruebas obligatorias

Quince programas BAL, tres por cada optimización que pide el enunciado. Con
ellos se puede **observar y medir el efecto de cada optimización por separado**.
Cada archivo explica en su comentario de arriba qué caso difícil prueba, qué
se puede optimizar, qué NO se puede tocar y cuál es el resultado esperado.

Todos guardan su resultado en el arreglo `resultados` de `main`. Así el
resultado se lee en la memoria del simulador al terminar. Escribir en un
arreglo nunca se elimina, así que el resultado siempre queda.

| # | Programa | Optimización | Lo difícil |
|---|---|---|---|
| 01 | `01_unroll_n_constante.bal` | Loop unrolling, N constante | `step 3` con un límite que no es múltiplo del paso, una vuelta que depende de la anterior y un ciclo anidado (el interno se desenrolla primero) |
| 02 | `02_unroll_n_variable_pequena.bal` | Loop unrolling, N variable pequeña | N conocido en `main` (TOTAL) y N como parámetro (PARCIAL ×4 + residuo). Bordes: 0, 1, 3, 4 y 9 vueltas, con inicio negativo |
| 03 | `03_unroll_n_grande.bal` | Loop unrolling, N grande | 1003 vueltas (250 pasadas + 3 sueltas), `step 3` con N desconocido y un `whether` dentro del cuerpo que cambia dos variables |
| 04 | `04_renombre_war.bal` | Renombramiento (WAR) | `x`, `a` y `b` se reutilizan para valores independientes; algunas reescrituras leen su propio valor (RAW verdadero) |
| 05 | `05_renombre_waw.bal` | Renombramiento (WAW) | `t` se escribe seis veces en el mismo bloque y su valor final tiene que ser el último |
| 06 | `06_renombre_war_waw_ciclos.bal` | Renombramiento (WAR y WAW) | Rotación de variables en ciclos (Fibonacci, tribonacci) y en un `whale` (MCD de Euclides), con temporales reescritos en cada vuelta |
| 07 | `07_dce_llamadas_con_efectos.bal` | Código muerto + llamadas con efectos | Asignaciones muertas entre llamadas que escriben en un arreglo, una cadena muerta (dos pasadas) y una división que no se puede quitar |
| 08 | `08_dce_efectos_en_ciclos.bal` | Código muerto + llamadas con efectos | Los dos tipos de código muerto: asignaciones dentro de un ciclo con una llamada, una lectura de memoria sin uso y ramas inalcanzables (`whether (1 > 2)`, `whale (2 < 1)`) con llamadas adentro |
| 09 | `09_dce_operaciones_que_fallan.bal` | Código muerto + llamadas con efectos | Una división entre una variable que nadie usa pero no se puede quitar (puede fallar), llamadas anidadas y una escritura que pisa a otra |
| 10 | `10_reorden_saltos_condicionales.bal` | Reordenamiento + seguridad del control | Cadena `whether`/`alif`/`alif`/`also` en un ciclo, condiciones con `&&`, `\|\|` y `~`, mínimo y máximo |
| 11 | `11_reorden_funciones.bal` | Reordenamiento + funciones | Recursión dentro de un `whether` (factorial, Fibonacci doble), un valor devuelto que depende de una rama, una llamada como argumento y cálculos entre llamadas |
| 12 | `12_reorden_memoria_y_ciclos.bal` | Reordenamiento + memoria | Ordenamiento burbuja: lee y escribe el mismo arreglo con índices que solo se conocen al ejecutar, con ciclos anidados, un `whether` y una llamada que escribe |
| 13 | `13_spilling_expresiones.bal` | Spilling | Árboles de expresiones que necesitan 4 valores vivos (hay 3 registros) mientras las 12 variables siguen vivas |
| 14 | `14_spilling_variables_vivas_en_ciclo.bal` | Spilling | Ocho acumuladores encadenados (s2 += s1, s3 += s2) vivos durante todo el ciclo, con saltos |
| 15 | `15_spilling_llamadas_y_recursion.bal` | Spilling | Valores vivos a través de cinco llamadas, recursión de 30 niveles con un local que sobrevive a la llamada y llamadas anidadas |

## Resultados esperados

Son los valores con que termina el arreglo `resultados` de `main` (y otro
arreglo de `main` cuando la prueba lo revisa). Son iguales en `-O0`, `-O1` y
`-O2`, y la prueba automática los compara uno por uno
([tests/test_pruebas_obligatorias.c](../test_pruebas_obligatorias.c)).

### 01 · `01_unroll_n_constante.bal`

`resultados = [28, 22, 121, -6]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `suma` | 28 | 0 + 1 + ... + 7 |
| `[1]` | `saltos` | 22 | 1 + 4 + 7 + 10 (`step 3`: 4 vueltas) |
| `[2]` | `doble` | 121 | d = 2d + m: 1 → 2 → 5 → 12 → 27 → 58 → 121 |
| `[3]` | `anidado` | −6 | suma de a − b, con a en 0..2 y b en 0..3 |

### 02 · `02_unroll_n_variable_pequena.bal`

`resultados = [31, 0, 10, 33, 46, 9]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `x` en `main` (n = 5) | 31 | x = 2x + 1 cinco veces desde 0 |
| `[1]` | `sumar_desde(10, 0)` | 0 | 0 vueltas |
| `[2]` | `sumar_desde(10, 1)` | 10 | 1 vuelta (solo el residuo) |
| `[3]` | `sumar_desde(10, 3)` | 33 | 10 + 11 + 12 |
| `[4]` | `sumar_desde(10, 4)` | 46 | 10 + 11 + 12 + 13 (un grupo exacto de 4) |
| `[5]` | `sumar_desde(-3, 9)` | 9 | −3 − 2 − 1 + 0 + 1 + 2 + 3 + 4 + 5 (dos grupos + 1) |

### 03 · `03_unroll_n_grande.bal`

`resultados = [502503, 335, 250, 128]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `suma` | 502503 | 0 + 1 + ... + 1002 = 1003 · 1002 / 2 |
| `[1]` | `multiplos` | 335 | vueltas de 0 a 1002 con `step 3` |
| `[2]` | `reloj` | 250 | h = h + 7 + k, y si h ≥ 1000 entonces h = h − 1000 (500 vueltas) |
| `[3]` | `giros` | 128 | cuántas veces h pasó de 1000 |

### 04 · `04_renombre_war.bal`

`resultados = [17, 22, 103, 6, 384, 22, 409]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `y` | 17 | x = 3 · 4 = 12; y = 12 + 5 |
| `[1]` | `z` | 22 | x = 5 · 5 = 25; z = 25 − 3 |
| `[2]` | `w` | 103 | x = 4³ = 64; w = 64 + 17 + 22 |
| `[3]` | `a` | 6 | a = 103 mod 10 = 3; a = a · 2 |
| `[4]` | `r` | 384 | a · x = 6 · 64 |
| `[5]` | `b` | 22 | b = (17 + 22) / 2 − (17 − 22) = 19 + 5 = 24; b = b − 2 |
| `[6]` | `s` | 409 | w + r − z − b · 3 + 10 = 103 + 384 − 22 − 66 + 10 |

### 05 · `05_renombre_waw.bal`

`resultados = [24, 35, 36, 18, 4, 22, 256, 377]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `u1` | 24 | t = 7 + 5 = 12; u1 = 12 · 2 |
| `[1]` | `u2` | 35 | t = 7 · 5 |
| `[2]` | `u3` | 36 | t = t + 1 |
| `[3]` | `u` (primer valor) | 18 | u = 36 / 2, guardado antes de reescribir `u` |
| `[4]` | `v` | 4 | t = 7 − 5 = 2; v = 2 · 2 |
| `[5]` | `u` (valor final) | 22 | u = u + v |
| `[6]` | `t` (valor final) | 256 | t = 2⁸, la última de las seis escrituras |
| `[7]` | total | 377 | 24 + 35 + 36 + 22 + 4 + 256 |

### 06 · `06_renombre_war_waw_ciclos.bal`

`resultados = [6765, 1705, 21, 1]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `fibonacci(20)` | 6765 | rotación a = b; b = t; t = a + b |
| `[1]` | `tribonacci(15)` | 1705 | 0, 0, 1, 1, 2, 4, 7, 13, ... |
| `[2]` | `mcd(1071, 462)` | 21 | algoritmo de Euclides con `whale` |
| `[3]` | `mcd(17, 5)` | 1 | números coprimos |

### 07 · `07_dce_llamadas_con_efectos.bal`

`resultados = [100, 60]` y `datos = [60, 20, 30, 0, 0]`

| Variable | Valor | De dónde sale |
|---|---|---|
| `datos` | [60, 20, 30, 0, 0] | `registrar` escribe 10, 20 y 30; `acumular` suma 10 + 20 + 30 y deja 60 en `datos[0]` |
| `resultados[0]` | 100 | r2 + total = 40 + 60 |
| `resultados[1]` | 60 | total |

Si el DCE quitara una llamada de `registrar` (aunque `r1` nunca se lee),
`datos` y `total` cambiarían.

### 08 · `08_dce_efectos_en_ciclos.bal`

`resultados = [16, 15, 9]` y `hist = [1, 2, 3, 4, 15]`

| Variable | Valor | De dónde sale |
|---|---|---|
| `hist` | [1, 2, 3, 4, 15] | el ciclo suma i + 1 en `hist[i]`; después `marcar(hist, 4, 10)` deja `hist[4]` = 15 |
| `resultados[0]` | 16 | hist[0] + hist[4] = 1 + 15 |
| `resultados[1]` | 15 | `copia`, lo que devolvió `marcar(hist, 4, 10)` |
| `resultados[2]` | 9 | hist[1] + hist[2] + hist[3] = 2 + 3 + 4 |

Las llamadas con 1000 están en ramas que nunca se ejecutan: si alguna
corriera, `hist` tendría un 1000 de más.

### 09 · `09_dce_operaciones_que_fallan.bal`

`resultados = [21]` y `res = [21, 7, 25, 10]`

| Variable | Valor | De dónde sale |
|---|---|---|
| `res[1]` | 7 | `uno = guardar(res, 1, 7)` |
| `res[0]` | 21 | `guardar(res, 0, dividir(7 · 6, 2))` = 42 / 2 |
| `res[2]` | 25 | y + d = 21 + 4 |
| `res[3]` | 10 | `guardar(res, 3, 50)` escribe 50 y después la asignación lo cambia por 50 / 5 = 10 |
| `resultados[0]` | 21 | `y` |

Si se invirtiera el orden de la última línea, `res[3]` quedaría en 50.

### 10 · `10_reorden_saltos_condicionales.bal`

Datos: `[12, -7, 0, 33, 5, -20, 7, -1]`. `resultados = [3, 1, 2, 2, 120, -20, 33, 4]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `negativos` | 3 | −7, −20, −1 |
| `[1]` | `ceros` | 1 | 0 |
| `[2]` | `chicos` (1..9) | 2 | 5, 7 |
| `[3]` | `grandes` (≥ 10) | 2 | 12, 33 |
| `[4]` | `puntos` | 120 | negativo: 1 − 2x (15 + 41 + 3); chico: 3x (15 + 21); grande: x − 10 (2 + 23) |
| `[5]` | `minimo` | −20 | |
| `[6]` | `maximo` | 33 | |
| `[7]` | `especiales` | 4 | entre 10 y 50, o igual a −1 o a −7: 12, 33, −7, −1 |

### 11 · `11_reorden_funciones.bal`

`resultados = [5040, 55, 5, 7, 3]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `fact(7)` | 5040 | 7!, recursivo |
| `[1]` | `fib(10)` | 55 | dos llamadas recursivas por nivel |
| `[2]` | `absoluto(-5)` | 5 | el valor que se devuelve lo cambia el `whether` |
| `[3]` | `combinar(3, fib(6), 4)` | 7 | 3 + 8 − 4 |
| `[4]` | z − w | 3 | w = 12 y z = 15, calculados entre llamadas |

### 12 · `12_reorden_memoria_y_ciclos.bal`

Datos: `[13, -3, 8, 21, 0, -9]`; ordenado: `[-9, -3, 0, 8, 13, 21]`. `resultados = [-9, 21, 8, 10]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `v[0]` | −9 | el menor |
| `[1]` | `v[5]` | 21 | el mayor |
| `[2]` | `v[2] + v[3]` | 8 | 0 + 8 |
| `[3]` | `cambios` | 10 | intercambios que hizo la burbuja |

### 13 · `13_spilling_expresiones.bal`

Con a..l = 1..12. `resultados = [36, 2, -6, 106, -51]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `e1` | 36 | ((1+2) + (3+4)) + ((5+6) + (7+8)) |
| `[1]` | `e2` | 2 | ((9−10) + (11−12)) − ((1−3) + (5−7)) = −2 − (−4) |
| `[2]` | `e3` | −6 | ((2+4) − (6+8)) + ((10+12) − (9+11)) = −8 + 2 |
| `[3]` | `total` | 106 | e1 − e2 + e3 + (a + ... + l) = 36 − 2 − 6 + 78 |
| `[4]` | `e4` | −51 | ((e1−a) + (e2−b)) − ((e3−c) + (total−k)) = 35 − 86 |

### 14 · `14_spilling_variables_vivas_en_ciclo.bal`

Con i = 1..20. `resultados = [210, 1540, 8855, -10, 10946, 4, -13]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `s1` | 210 | suma de i |
| `[1]` | `s2` | 1540 | s2 += s1 (suma de los números triangulares) |
| `[2]` | `s3` | 8855 | s3 += s2 |
| `[3]` | `s4` | −10 | 1 − 2 + 3 − ... − 20 |
| `[4]` | `fib2` | 10946 | Fibonacci de 21 |
| `[5]` | `mx` | 4 | máximo de valor = s4 + 2 · signo − i / 4 |
| `[6]` | `mn` | −13 | mínimo de la misma función |

### 15 · `15_spilling_llamadas_y_recursion.bal`

`resultados = [49, 56, 930, 91, 1035]`

| Posición | Qué es | Valor | De dónde sale |
|---|---|---|---|
| `[0]` | `p1 = horner(1, 2, 3, 4)` | 49 | 1 + 2·2 + 4·3 + 8·4 |
| `[1]` | `p2 = horner(6, 5, 4, 3)` | 56 | 6 + 2·5 + 4·4 + 8·3 (mismos números, otro orden) |
| `[2]` | `s = suma_dobles(30)` | 930 | 2 + 4 + ... + 60, con 30 niveles de recursión |
| `[3]` | `anidado = horner(p1, 5, suma_dobles(2), 1)` | 91 | 49 + 2·5 + 4·6 + 8·1 |
| `[4]` | `vivos` | 1035 | (1+2+3+4+5+6) + 49 − 56 + 930 + 91 |

### Lo que mide el simulador

Además del resultado (que es igual en los tres niveles), cambian los ciclos y
el tamaño del programa. `-O1` solo cambia algo donde hay código muerto que
quitar (07, 08 y 09); en los demás compila lo mismo que `-O0`.

| # | Ciclos -O0 | Ciclos -O1 | Ciclos -O2 | Bundles -O0 | Bundles -O1 | Bundles -O2 |
|---|---:|---:|---:|---:|---:|---:|
| 01 | 1 661 | 1 661 | 959 | 402 | 402 | 933 |
| 02 | 1 842 | 1 842 | 1 495 | 705 | 705 | 617 |
| 03 | 85 490 | 85 490 | 56 002 | 363 | 363 | 770 |
| 04 | 10 094 | 10 094 | 7 811 | 897 | 897 | 735 |
| 05 | 8 033 | 8 033 | 6 224 | 708 | 708 | 618 |
| 06 | 10 708 | 10 708 | 8 635 | 828 | 828 | 990 |
| 07 | 6 985 | 4 543 | 5 373 | 990 | 804 | 843 |
| 08 | 1 703 | 1 433 | 1 238 | 801 | 540 | 678 |
| 09 | 6 959 | 6 905 | 5 555 | 1 014 | 984 | 649 |
| 10 | 2 231 | 2 231 | 3 011 | 741 | 741 | 956 |
| 11 | 29 698 | 29 698 | 26 892 | 996 | 996 | 769 |
| 12 | 3 544 | 3 544 | 3 823 | 585 | 585 | 1 017 |
| 13 | 524 | 524 | 562 | 525 | 525 | 561 |
| 14 | 45 266 | 45 266 | 37 036 | 597 | 597 | 634 |
| 15 | 5 296 | 5 296 | 4 562 | 951 | 951 | 666 |

Estos números son del compilador actual. Si un compañero cambia una
optimización pueden cambiar, pero el resultado de `main` no debe cambiar
nunca.

## Cómo comprobarlos

```sh
make test-pruebas-obligatorias
```

Para cada programa la prueba ([tests/test_pruebas_obligatorias.c](../test_pruebas_obligatorias.c)):

1. Compila con `-O0`, `-O1` y `-O2` y ejecuta los tres binarios en el simulador hasta que `main` termina.
2. Revisa que `resultados` tenga los valores esperados en los tres niveles. Los valores se calcularon aparte y coinciden con los de los tres niveles.
3. Revisa que `-O1` y `-O2` dejen las variables de `main` iguales a `-O0` (regla de oro). Las que `-O1` borró por ser código muerto no cuentan.
4. Revisa que la optimización de su categoría **actuó** en `-O2`:
   - **unrolling:** ciclos desenrollados;
   - **renombramiento:** dependencias WAR y WAW quitadas;
   - **código muerto:** instrucciones eliminadas, en `-O2` y en `-O1`;
   - **reordenamiento:** instrucciones movidas;
   - **spilling:** valores guardados y vueltos a cargar.

Cada optimización por separado, con su reporte:

```sh
./balc --unroll  tests/Pruebas_obligatorias/03_unroll_n_grande.bal       # decisión por ciclo
./balc --dce     tests/Pruebas_obligatorias/08_dce_efectos_en_ciclos.bal # qué se elimina y por qué
./balc --reorder tests/Pruebas_obligatorias/10_reorden_saltos_condicionales.bal
./balc --ir-spilling tests/Pruebas_obligatorias/13_spilling_expresiones.bal
./balc -O1       tests/Pruebas_obligatorias/07_dce_llamadas_con_efectos.bal # DCE sobre el código BAL
./balc -O2       tests/Pruebas_obligatorias/05_renombre_waw.bal          # todas las pasadas juntas
```

En el IDE: se abre el archivo y se usan los botones **Loop unrolling**,
**Código muerto**, **Reordenamiento** y **-O0/-O1/-O2**. Este último compara los
tres niveles: ciclos, tamaño, qué hizo cada nivel y cada pasada, y si el
resultado de `main` es el mismo.

## Límites que condicionan el tamaño de los programas

Los programas son tan exigentes como permiten los dos backends:

- **Memoria de instrucciones de 1024 bundles**, en los dos niveles. En `-O2`
  cada multiplicación o acceso a un arreglo con índice variable se expande a
  unos 50 bundles, y el unrolling los copia. Por eso varios programas usan
  sumas donde se podría multiplicar.
- **`-O0` (backend tradicional):**
  - como máximo 4 argumentos por llamada;
  - 4 registros temporales por expresión;
  - no acepta una llamada como operando de otra operación (`f(x) gauss 1`), ni sentencias después de un `give`.
- **`-O2`:** trabaja con 3 registros temporales (el spilling se ve en todos los programas).
- **`-O1`:** reescribe el código BAL, así que solo puede quitar lo que es una
  sentencia completa. Si el DCE quita algo de la IR que no se puede escribir
  en BAL, `-O1` no compila el programa (igual que `balc -O1`). Pasa con dos
  patrones comunes, que estos programas evitan a propósito:
  - un `give` dentro de un `whether` deja en la IR un salto inalcanzable;
  - un valor inicial que se pisa antes de leerse (la declaración necesita valor).

Con estos programas `-O2` no siempre ejecuta menos ciclos. En el 10 y el 12
ejecuta más, porque el spilling agrega cargas y guardados dentro de los
ciclos. Son datos reales del simulador y ayudan a explicar la relación entre
el reordenamiento, el unrolling y la presión de registros.
