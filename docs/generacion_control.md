# Generacion de control de flujo — Meibel

Esta entrega traduce `AST_SI` (`whether / alif / also`) y `AST_MIENTRAS` (`whale`) desde
`src/Generacion_Codigo/generacion_control.c`. Reutiliza `gb_generar()` para condiciones sin
llamadas: el resultado booleano queda en `x4`. Requiere analisis semantico previo.

## Organizacion

- `src/Generacion_Codigo/generacion_control.c`: bloques, condicionales y etiquetas. Tambien genera ciclos `whale`.
- `src/Generacion_Codigo/generacion_funciones.c`: expresiones con llamadas, sentencias simples,
  retornos y preparacion de la funcion.
- `src/Generacion_Codigo/generacion_interna.h`: contexto compartido privado y puntos de conexion.
  El contador de etiquetas sigue siendo unico dentro de cada funcion.

## Recorrido

1. Evaluar la condicion.
2. Emitir `igualsi x4, x0, etiqueta_else` para la ruta falsa.
3. Generar el bloque verdadero.
4. Si hay alternativa y la rama verdadera continua, saltar a `etiqueta_fin`.
5. Emitir `etiqueta_else` y generar el bloque alternativo o el siguiente `alif`.
6. Emitir `etiqueta_fin` cuando existe un salto hacia ella.

Las etiquetas tienen el prefijo reservado `__gf_<funcion>_` y un contador local.
Esto separa condicionales anidados y funciones distintas. Cada funcion se genera
una sola vez por unidad de salida. Las etiquetas no contienen direcciones.

Los bloques informan si todos sus caminos retornan. Un `give` genera el valor
en `x8` y emite el epilogo en esa ruta: restaura `ra`, recupera `sp` y retorna.
Un condicional garantiza retorno solo si ambas ramas lo garantizan. Si una rama
continua, puede alcanzar instrucciones posteriores y un retorno posterior.
Las funciones void que llegan al final reciben un epilogo implicito.

Se conserva el ensamblador previo de funciones lineales. Los errores se detectan
en una salida temporal antes de publicar instrucciones al destino.

## Probar y ver el resultado

Desde la raiz del proyecto:

```sh
make test-funciones-backend test-referencias-integracion test-condicional-pipeline
./tests/test_condicional_pipeline --emitir
```

`examples/condicional.bal` usa un `whether`, un `alif` y un `also`.
Para `n = 5`, la rama correspondiente asigna 20 a `resultado`.
El test de pipeline utiliza parser, semantica, tabla de simbolos, memoria y
adaptador reales, y permite inspeccionar la salida lineal con `--emitir`.

`tests/test_generacion_funciones.c` comprueba las rutas verdaderas/falsas,
`alif`, anidamiento, ramas que continuan, ausencia de `also`, retornos y
restauracion de pila mediante un modelo secuencial limitado de instrucciones.
Tambien comprueba rechazo sin publicar salida ante estructuras invalidas y
caminos sin retorno. Este modelo no verifica latencias del procesador.

## Contrato con Dylan y Jose

La salida sigue siendo intermedia: instrucciones individuales y etiquetas.
Necesita empaquetado ALU/LSU/BRU/CRIPTO y separaciones por dependencias antes de
calcular PC y desplazamientos. La guia establece bundles de 16 bytes y consumo
de registros a partir de N+3. Los destinos de llamadas absolutas tambien deben
corresponder al layout final. Una etiqueta no consume bytes por si misma.

`rr_desplazamiento()` puede validar un salto una vez definidas sus direcciones;
esta entrega no calcula esas posiciones ni reemplaza el ensamblador binario.
El assembler y el bundler compartidos se consultaron solo como referencia de
arquitectura; no se incorporan ni se modifican en este repositorio. El assembler
admite etiquetas y BRU, pero el bundler compartido solo admite ALU y LSU.
La integracion externa debera contemplar etiquetas y saltos antes de consumir
la salida de control de flujo. Las llamadas con direcciones absolutas requieren
que el mapa de funciones corresponda al layout final de bundles.

## Limites de esta entrega

- Admite `whale` y `cycle` (`AST_RECORRIDO`).
- No admite llamadas dentro de condiciones.
- Se pueden usar variables de la funcion o globales admitidas por el adaptador.
  Admite declaraciones enteras inicializadas en ramas y ciclos, con resolucion
  de ambitos y respeto del orden de declaracion.
- No agrega opciones al CLI `balc`, empaquetado de bundles ni codificacion binaria.
- Falta el arranque e integracion del programa completo y su ejecucion en el simulador RTL.

## Ciclos whale

`generacion_control.c` traduce `AST_MIENTRAS` con dos etiquetas:
`__gf_<funcion>_while_cond_<n>` y `__gf_<funcion>_while_fin_<n>`.
El contador se comparte con los condicionales.

```text
etiqueta_condicion:
    evaluar condicion en x4
    igualsi x4, x0, etiqueta_fin
    generar cuerpo
    sye x0, etiqueta_condicion
etiqueta_fin:
```

La condicion se evalua antes de cada vuelta, incluida la primera. El `stop;`
del lenguaje cierra la sintaxis del ciclo; no es una instruccion break.
Si todo el cuerpo retorna, no se emite el salto de regreso; el `give` usa el
mismo epilogo que cualquier retorno de funcion. El ciclo no garantiza por si
solo el retorno de una funcion escalar porque puede ejecutarse cero veces.

Se permiten ciclos y condicionales anidados, asignaciones sobre variables del
ambito de la funcion y retornos dentro del cuerpo. Se mantienen los limites:
sin llamadas en condiciones; las declaraciones enteras inicializadas dentro de
bloques ya se admiten mediante el adaptador.

```sh
make test-funciones-backend test-referencias-integracion test-whale-pipeline
./tests/test_condicional_pipeline --whale --emitir
```

`examples/whale.bal` combina tres vueltas con un `whether / also`;
el resultado esperado es 21. La prueba del pipeline procesa ese BAL real hasta
ensamblador lineal. Las pruebas del generador ejecutan en el modelo secuencial
cero, una y cuatro vueltas, ciclos anidados con un condicional y retornos en el
cuerpo. Tambien verifican rechazo sin publicar salida ante ciclos mal formados
y funciones sin retorno fuera del ciclo. No se incorporan assembler ni bundler.

## Ciclos cycle

`cycle i let a until b step p { ... } endgame;` se traduce con
`__gf_<funcion>_cycle_cond_<n>` y `__gf_<funcion>_cycle_fin_<n>`, usando el
mismo contador de etiquetas:

```text
    evaluar a en x4; guardar en i
etiqueta_condicion:
    x4 = i < b   (paso positivo)  o  i > b   (paso negativo)
    igualsi x4, x0, etiqueta_fin
    generar cuerpo
    evaluar p en x4; i = i + x4
    sye x0, etiqueta_condicion
etiqueta_fin:
```

El limite es exclusivo y, como en whale, el limite y el paso se evaluan en
cada vuelta. Si el paso es una constante (`step 2`, `step neumann 1` o el `1`
implicito) el sentido se fija al generar. Si es una expresion, se decide en
ejecucion: `(p > 0 && i < b) || (~(p > 0) && i > b)`; el limite queda en la
pila mientras se evalua el paso. Un paso constante cero se rechaza.
Inicio, limite y paso pueden contener llamadas.

La variable de control vive en el ambito `cycle_N` que crea la semantica,
ligado al cuerpo del ciclo, pero su declaracion esta en la cabecera. El
adaptador la considera visible en todo el cuerpo, y el generador la resuelve
con una copia local de su identificador colgada del cuerpo, sin modificar el
AST. Como whale, un cycle no garantiza el retorno de la funcion.

`examples/cycle.bal` combina un paso 2, un paso `neumann 1` y dos cycle
anidados con un `whether`. `make test-cycle-pipeline` lo procesa hasta
ensamblador lineal; para verlo: `./tests/test_condicional_pipeline --cycle --emitir`.

`make test-ambitos-generacion` ejecuta en el modelo secuencial: paso implicito,
paso 2, paso `neumann 1`, paso variable en ambos sentidos, cero vueltas y dos
cycle anidados con un whether y limite tomado de una variable externa.

## Variables de bloques anidados

La semantica conserva enlaces prestados desde cada `SimAmbito` al bloque AST
que lo origino y desde cada `SimSimbolo` al identificador de su declaracion.
No modifica el AST ni reserva nuevas ubicaciones de memoria: `memory.c` ya
asigna offsets diferentes a los locales de todos los bloques de una funcion.

El adaptador busca el bloque del uso mediante los enlaces `padre` del AST,
consulta ese ambito y asciende hasta funcion/global. Comprueba el orden del
arbol: antes de una declaracion local el mismo nombre sigue refiriendose al
simbolo externo. Desde la declaracion (incluido el inicializador, como hace
semantica), se refiere al local. Al salir del bloque se recupera el externo;
los bloques hermanos no comparten sus variables. No depende de `bloque_N`,
lineas del fuente ni del orden global de insercion de simbolos.

El AST y la tabla deben conservarse juntos hasta terminar la generacion.
Los enlaces son prestados y no se liberan desde la tabla. Las tablas manuales
sin estos enlaces conservan compatibilidad con la generacion simple de funcion.

```sh
make test-ambitos-generacion
```

La prueba utiliza parser, semantica y asignacion de memoria reales. Ejecuta el
ensamblador lineal con el modelo secuencial de pruebas y comprueba sombras,
uso anterior a la declaracion, bloques hermanos y un temporal de ciclo que se
inicializa en cada vuelta. Tambien revisa offsets de parametros/globales,
rechazo de nombres fuera de alcance y preservacion de la salida ante offsets
invalidos. `examples/ambitos.bal` es un modulo de ejemplo cuya funcion
`f` retorna 44; no tiene `main`. No se simulan bundles ni latencias de hardware.
