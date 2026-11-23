# Observabilidad explícita de O1 y comparación de resultados

Implementación integrada desde el experimento recuperado, sobre develop
`fb486b4628d8a6020c92dabf8aecf9e7bfe232d1`, en `fix/h19-o1-observabilidad`.
Las API históricas siguen disponibles. No se cambian las pasadas DCE ni los
productores O0/O2, el simulador, scheduler, ABI o Arquitectura.

## Contrato implementado

La ruta histórica de DCE solo considera resultados de main los nombres que
se leen después. Esto permite eliminar una salida externa como
`columna_encontrada` o el almacenamiento de `vacio`; una llamada con efectos
puede permanecer aunque desaparezca la variable que recibía su retorno.

El contrato añade raíces mediante `dce_eliminar_con_observables`, sin
cambiar ese algoritmo ni desactivar DCE. No hay excepciones por nombre.
Con cero selectores, la producción O1 conserva exactamente la ruta histórica.
O0 y el backend O2 mantienen sus productores anteriores.

```sh
./balc -O1 --observable=main:fila_encontrada \
  --observable=main:columna_encontrada -s -o /tmp/matriz.bin \
  examples/ejercicio2_busqueda_matriz.bal
```

Cada selector identifica un objeto de main. `main:nombre` requiere una única
declaración; `main:nombre@linea:columna` selecciona el declarador original,
incluso dentro de un bloque. Un nombre inexistente, ambiguo o coordenadas
incompletas se rechazan. Máximo 64 selectores. La CLI admite esta opción solo
con `-O1` explícito. No configura O2 ni altera su ABI.

En C, `IdeObservable` contiene nombre prestado y línea/columna, ambas cero
para búsqueda única. Los consumidores usan:

- `ide_dce_analizar_con_observables` para DCE y reescritura.
- `ide_optimizacion_analizar_con_observables` para compilar, medir y comparar
  O0/O1/O2 con el mismo contrato.
- `vliw_opt_fuente_preparar_con_observables` desde CLI.
- `ide_optimizacion_comparar` para comparar resultados ya ejecutados.

Las APIs históricas delegan con NULL/0. Los selectores se prestan durante la
llamada; los resultados, vínculos, textos y bytes completos pertenecen a la
salida y se liberan con los liberadores públicos existentes. Un error de
reserva se propaga; no se autentican punteros arbitrarios del consumidor.

## Identidad y reescritura

En modo explícito se clona el AST y cada referencia variable se vincula al
símbolo semántico que resuelve el backend. Se usan alias privados por índice
de símbolo durante AST→IR: dos homónimos ya no comparten una raíz textual.
No se modifica el AST ni la tabla prestada. Se guardan vínculos entre tokens
y declaradores para retirar declaraciones muertas sin confundir homónimos.
El informe traduce el destino de asignaciones eliminadas al nombre BAL para
el reescritor existente; las decisiones ya se tomaron con identidades únicas.

Las sustituciones del modo explícito conservan saltos de línea para mantener
coordenadas originales. Las transformaciones inseguras para el reescritor
siguen conservándose de forma prudente. El clon limita la profundidad a 256;
la reserva de vínculos comprueba el tamaño. Resolver símbolos recorre la tabla:
el coste puede ser cuadrático en el tamaño de AST/tabla, además del DCE existente.

## Comparación implementada

- **1 / EQUIVALENTE:** todos los objetos requeridos están disponibles y coinciden.
- **0 / DIFERENTE:** hay una diferencia demostrada de valor, tamaño o tipo.
- **−1 / NO VERIFICABLE:** falta un objeto obligatorio, identidad inequívoca,
  datos completos, compilación o ejecución terminada.

Una diferencia demostrada prevalece aunque falte otra salida. Un contrato
vacío en el comparador público no demuestra igualdad. Una interna ausente que
no pertenece al contrato no perjudica la comparación. Sin contrato explícito,
la API histórica usa todos los objetos de O0 y devuelve NO VERIFICABLE si
alguno falta: eso no significa que DCE sea incorrecto.

Se copian y comparan todos los bytes de cada objeto desde la memoria del
simulador, verificando multiplicaciones, desplazamientos y límites. Los
arreglos visibles siguen resumiéndose a 16 elementos, pero sus veredictos usan
el objeto completo. También la tabla GTK usa este comparador, sin deducir
igualdad de textos truncados ni de coincidencias parciales. Las filas con
homónimos sin un contrato inequívoco se muestran NO VERIFICABLE.

## Pruebas reproducibles

```sh
make -B test-observabilidad
make -B test-ide-optimizacion test-pruebas-obligatorias test-metricas
python3 tests/vliw/test_niveles_optimizacion.py -v
make -B balc
make -B ide
make -B -k test
```

`test-observabilidad` ejecuta productores y simulador reales, el comparador
con objetos sintéticos y negativos de contrato: homónimos, ámbitos hermanos,
nombres inexistentes, duplicados idempotentes, locales de otra función y
globales fuera del contrato. Comprueba un arreglo real de 18 elementos y un
objeto de 4096 enteros, tamaños distintos y diferencias después del elemento
16. La CLI rechaza coordenadas no representables antes de crear selectores.
Los artefactos de prueba en `evidencias/observabilidad/` son regenerables y no
forman parte de la entrega Git.

Los 15 programas obligatorios declaran sus salidas en los oráculos de prueba;
se mantienen las comprobaciones de valores y actuación de las cinco pasadas.
Las mediciones adicionales del informe comparan también el modo histórico.
El fixture DCE histórico conserva 528→435 bundles (8448→6960 bytes) y
6194→2177 ciclos software. Los bundles incluyen NOP y guardas; los bytes son
solo instrucciones, sin cabecera BALB ni datos. No se comparan esos ciclos
con el horizonte fijo de una ejecución RTL.

## Correcciones H19 independientes

- `test_06_modulos_y_lexer` exigía el rechazo universal de `bring`. El commit
  `14c00d3` ya había añadido copia de módulos al snapshot O1. Se comprueba
  ahora un import válido en O0/O1, el rechazo semántico de un módulo ausente
  sin publicar imágenes y la aparición de `bring` dentro de un comentario.
- `58e330d` convirtió `examples/ambitos.bal` en un programa con `main`.
  La prueba de generación de módulos usa ahora
  `tests/modulos/ambitos_generacion.bal`, con la función original y el mismo
  retorno esperado 44. BAL sigue prohibiendo `main` en un módulo importado;
  el ejemplo público conserva su punto de entrada.

## Límites

- Los resultados explícitos son objetos de main expuestos por los productores:
  enteros, booleanos, caracteres y sus arreglos. Globales, textos, listas,
  dispositivos, excepciones y trazas completas no acreditan equivalencia con
  esta API. Las peticiones fuera de ese contrato no se aceptan silenciosamente.
- La ventana GTK presenta EQUIVALENTE/DIFERENTE/NO VERIFICABLE, pero no añade
  un selector visual de observables: usa todos los objetos de O0 como referencia.
  Una ausencia puede dejar el veredicto NO VERIFICABLE aunque DCE sea válido.
- Las coordenadas identifican el declarador del texto analizado; no son una
  identidad persistente entre ediciones. Las salidas deben liberarse antes de
  reutilizarlas; no se autentican punteros arbitrarios del consumidor.
- El modo explícito puede rechazar IR/reescritura no soportadas. No se acredita
  para todo módulo importado ni profundidad arbitraria. Los alias privados
  pueden aparecer en la IR diagnóstica. El listado histórico de vivas no es
  un certificado de las raíces explícitas.
- Compilar GTK no demuestra una inspección visual. El informe de ejecución
  distingue compilación, apertura y revisión visual, si se pudieron realizar.

Las evidencias y el informe final se guardan fuera del repositorio bajo
`Respaldos/integracion_definitiva_o1_h19/`; no se incorporan logs ni binarios.
