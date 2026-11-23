# O1 y comparación completa de resultados

O1 conserva automáticamente las variables con nombre de `main` mediante
`dce_eliminar_con_observables`. El usuario no selecciona observables. O0 usa
el backend tradicional; O1 reescribe BAL mediante DCE y utiliza ese backend;
O2 conserva su cadena de optimizaciones existente.

El comparador del IDE exige compilación y terminación. Empareja variables por
nombre y, para homónimos, por orden semántico. Compara tipo, tamaño y todos los
bytes del objeto en la captura final de DMEM de cada variante. Una variable
requerida ausente indica diferencia. Datos incompletos o ejecución sin terminar
no acreditan equivalencia. Los offsets pertenecen a cada variante y se validan
antes de acceder a memoria. El texto GTK puede resumir los primeros 16 elementos
con puntos suspensivos; ese resumen no decide la equivalencia.

Cada nivel conserva una captura propia de 64 KiB, liberada por
`ide_optimizacion_liberar`: hasta 192 KiB adicionales por comparación. No se
modifican el simulador, scheduler ni los algoritmos de optimización.

## Pruebas reproducibles

`make test-ide-optimizacion` incluye el banco C y Python del comparador.
`make test-ide-optimizacion-comparador` ejecuta únicamente ese banco, en un
copiado temporal externo. No requiere fuentes de auditorías anteriores.
Incluye escalares, homónimos, tipos/tamaños distintos, arreglos de 16 y 20
componentes, diferencias en la cola, datos incompletos y ejecuciones reales
O0/O1/O2 con los programas de matriz, puntajes y DCE 07–09.

El fixture de `test_niveles_optimizacion.py` usa `muerto_aux` dentro de una
función y conserva `muerto` en `main`. El API de observables del DCE identifica
nombres: un auxiliar homónimo de una observable puede conservarse de forma
conservadora. Exigir su eliminación no sería una prueba válida de ese contrato.
No se modifica esta política para mejorar métricas.

Las pruebas H19 conservan el rechazo de imports inexistentes y comprueban
imports válidos en O0/O1. El banco de ámbitos utiliza un módulo específico sin
`main`; el ejemplo ejecutable público permanece intacto. BAL continúa
rechazando `main` dentro de módulos importados.

El manifiesto de evaluación conserva hashes de fuentes y offsets por variante.
Al cambiar un ejemplo debe actualizarse su hash tras revisar su semántica. Los
offsets O1 de puntajes incluyen `vacio`, que ahora se conserva: resultado=4,
vacio=8, combinado=12, total=16. Se obtuvieron del compilador actual; los
oráculos matemáticos siguen siendo 24, 0, 13 y 37. El manifiesto no configura
selectores del productor O1.

El tamaño estático se calcula como bundles emitidos × 16 bytes, incluidos NOP
y guardas; no incluye la cabecera BALB ni los datos. Las métricas se contrastan
con la base del compañero, no con una política anterior de observabilidad.
