# Resolución de saltos (Fase 5)

`src/Resolucion_y_Enlace/resolucion_saltos.c` recibe el ensamblador lineal de los generadores
(instrucciones y etiquetas `__gf_<funcion>_..._N:`) y produce bundles VLIW con
cada salto convertido en un desplazamiento numérico. Usa la tabla de
`resolucion_referencias.c` para registrar etiquetas y calcular desplazamientos.

```c
int rs_resolver(const char *lineal, uint32_t inicio, FILE *salida,
                FILE *diagnosticos, RsResumen *resumen);
```

## Layout

Cada instrucción ocupa un bundle en su slot (`ALU | LSU | BRU | CRIPTO`) y
le siguen dos bundles `nop`. Es la misma regla de `bundle_linear.py`: un valor
escrito en el bundle N se lee desde N+3 por cualquier ruta, incluidos los
destinos de salto, que es lo que exige el chequeo del ensamblador. Es
conservador: no empaqueta varias instrucciones por bundle.

Una etiqueta toma el PC del bundle siguiente. Si queda al final, se agrega un
`nop` para que el destino exista. `inicio` es el PC del primer bundle; el
programa no puede llegar a 0x3FF0, reservado para el manejador de excepciones.

## Cálculo de saltos

`desplazamiento = PC de la etiqueta − PC del bundle del salto`, en bytes,
múltiplo de 16 (contrato de la ISA). Condicionales (`igualsi`, `igualno`,
`menora`, `mayoroigual`): 11 bits, −1024 a 1008. `sye`: 16 bits.
Los destinos ya numéricos y `syer` se copian sin cambios.

Como las etiquetas llevan el nombre de la función y un contador único, los
saltos de condicionales y ciclos anidados no se confunden entre sí.

## Saltos largos

Con dos `nop` por instrucción, un condicional alcanza unas 21 instrucciones.
Las salidas de ciclos con cuerpos medianos ya lo superan (por ejemplo el
`whale` de `acumular` en el ejercicio 3: 1824 bytes). Un condicional fuera de
rango se reescribe con la condición invertida y un `sye`:

```text
igualsi x4,x0,L_fin          igualno x4,x0,__rs_cerca_N   (96 bytes)
                       =>    sye x0,L_fin                 (16 bits)
                             __rs_cerca_N:
```

Después se repite el layout. Cada vuelta solo agrega código, así que cada
condicional se reescribe a lo sumo una vez y el proceso termina.

## Salida

```text
# __gf_f_while_cond_0:
nop | cargai x4,x2,8 | nop | nop
nop
nop
nop | nop | igualsi x4,x0,240 | nop  # -> __gf_f_while_fin_0
```

Las etiquetas quedan como comentarios y cada salto indica su destino.
Errores (etiqueta inexistente o duplicada, instrucción desconocida, operandos
inválidos, programa que no cabe) devuelven 1 sin escribir salida.

## Pruebas

```sh
make test-resolucion-saltos
./tests/test_resolucion_saltos --emitir   # bundles del ejercicio 3
```

- Caso manual con desplazamientos exactos, hacia atrás y hacia adelante.
- Errores sin salida parcial.
- `cycle` dentro de `cycle` con `whether/alif/also` y `whale` con `whether`.
- Cuerpos largos que obligan a reescribir el salto de un `whale` y de un `whether`.
- Ejercicio 3 completo: se reescribe la salida del `whale` de `acumular`.
- Ejercicio 3 enlazado con direcciones reales y ejecutado desde `main`.

Cada salida se verifica de dos formas: cada desplazamiento llega exactamente a
su etiqueta y respeta su rango, y un modelo que avanza por bundles ejecuta el
programa y obtiene el mismo resultado que el modelo lineal con etiquetas.

## Direcciones de funciones

`src/Resolucion_y_Enlace/enlace_funciones.c` (`ef_enlazar`) genera todas las funciones y fija sus
direcciones. Las llamadas cargan en x13 la dirección absoluta de la función,
pero esa dirección depende del layout, y el layout depende del código: una
dirección mayor que 1023 necesita 7 instrucciones (`sumai`, `cizqi`, `ori`)
en lugar de 1. Por eso se itera:

1. Generar el lineal de cada función, precedido por la etiqueta `__fn_<nombre>`,
   con las direcciones supuestas.
2. Armar bundles con `rs_resolver_etiquetas`, que devuelve el PC final de cada
   etiqueta.
3. Si algún `__fn_<nombre>` no coincide con la dirección supuesta, usar las
   reales y volver al paso 1.

Se parte de todas las funciones en `inicio`, el valor mínimo. Entre vueltas
las direcciones y el código solo crecen, así que el proceso converge (con un
tope de 64 vueltas). Las funciones quedan en orden del fuente. El marco de
cada una se calcula con los desplazamientos de `mem_asignar_proyecto`.

En el ejercicio 3 quedan `sumar` en 0, `puntaje` en 528, `acumular` en 2064 y
`main` en 4752, con 642 bundles. La prueba ejecuta el programa desde `main`
siguiendo los saltos y las llamadas reales, y comprueba resultado = 24,
vacio = 0, combinado = 13 y total = 37.

## Arranque

Con `EfArranque` (`ef_enlazar(..., &arranque, ...)`) se agrega el código de
inicio en `inicio`, antes de las funciones. El hardware no inicializa gp ni sp.

```text
__arranque:
    x3 (gp) = arranque.gp       %% el gp_base de mem_asignar_proyecto (0x4000)
    x2 (sp) = arranque.sp       %% tope de la pila, crece hacia abajo (0x10000)
    constantes globales: evaluar y guardap x3, x4, desplazamiento
    x13 = direccion de main
    syer x1, x13, 0
__arranque_fin:
    sye x0, __arranque_fin      %% salto estable: fin del programa
```

Las constantes globales se inicializan en orden del fuente y solo pueden usar
enteros y otras constantes globales. La dirección de `main` entra en la misma
iteración que las demás direcciones de funciones. No se agrega una
instrucción HALT a la ISA: el programa termina en el salto sobre sí mismo.

Pruebas: un programa con dos constantes globales (`base = 5`,
`doble = base gauss base`) y una llamada desde `main`, y el ejercicio 3
completo, ejecutados desde la dirección 0 hasta el salto final. Se comprueban
las globales en 0x4000 y los locales de `main` bajo 0x10000.

## Uso desde balc

```bash
make
./balc -s examples/ejercicio3_puntajes.bal
# [ensamblador] examples/ejercicio3_puntajes.asm: 801 bundles, 4 funciones, 6 saltos (1 reescritos)
```

`balc` recorre semántica, asignación de memoria, generación, enlace con
arranque y layout; `-s` además escribe `<fuente>.asm`. Mapa de memoria del
contrato: código desde 0, globales desde gp = 0x4000 (hasta 0x7FFF) y pila
desde sp = 0x10000 hacia abajo. Las demás opciones y el binario están en
`docs/backend_binario.md`.

| Ejemplo | Resultado de `-s` |
|---|---|
| `factorial.bal` | 189 bundles; ejecutado en el modelo: resultado = 120 |
| `ejercicio3_puntajes.bal` | 801 bundles; ejecutado en el modelo: 24, 0, 13, 37 |
| `ejercicio2_busqueda_matriz.bal` | 291 bundles; ejecutado en el modelo: 7 en [0][1]; sin coincidencia, (-1, -1) |

## Pendiente

- Probar los `.asm` con `tools/assembler.py` y el RTL del repositorio de
  Arquitectura.
