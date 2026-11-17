# Estado de I1-A3

## Preparación de la entrega sobre PR #68

La rama de entrega `feat/i1a3-validacion-ir-rtl-pr68` parte exactamente de
`9cf06e20b2ed6dcba418f8a82764f33152ed561f`, en un worktree independiente.
El parche I1-A3 se aplicó sin conflictos y la rama original con sus cambios
locales se conservó. Las referencias de la sección siguiente describen la
campaña anterior, no el HEAD de este worktree de entrega.

La prueba de CLI fuerza `make -B balc` en su copia temporal: el ejecutable
versionado no puede reutilizarse por sus marcas de tiempo. No se modifica
Makefile ni se reconstruyen binarios en los árboles originales o el worktree.
Las nuevas campañas y sus comandos se conservan en un subdirectorio externo
`cierre-pr68-*` de las evidencias I1-A3; no sustituyen las campañas anteriores.

## Referencias de la campaña anterior (histórico)

La rama local `feat/i1a3-validacion-ir-rtl` se creó desde origin/develop cuando
HEAD era `7463123`. Mientras se trabajaba, origin/develop avanzó a `9cf06e2`
(PR #68). Se auditó ese avance y se probaron los archivos nuevos sobre una
copia externa de ese commit, sin integrar cambios de compañeros en el árbol
local, sin staging y sin commits. La rama puede aparecer cinco commits detrás;
no debe confundirse el HEAD local con el snapshot probado. Antes de publicar,
revisar la actualización de la rama conforme al procedimiento acordado.

Arquitectura se probó en una copia independiente de develop `5e6e45f`. Los manifiestos verifican sus archivos, índice, HEAD y rama activa originales.
Las referencias muestran una actualización concurrente por fetch/pull registrada
en reflogs; no se restauraron ni se atribuyen a esta validación.

## Entrega independiente

- Driver C con productor/spilling/cadena y M2/M1 reales; oráculos aritméticos,
  control, presión, rechazos, publicación atómica y fallos de reserva muestreados.
- Herramienta Python reproducible con bytes CB/ensamblador, captura completa y
  modos RTL 0/2; perfiles auxiliares sin cambiar CLI ni Makefile.
- Suite de 11 métodos, incluidos 17 casos IR y los tres casos BAL de #34.
- Wrapper RTL mínimo: siembra x16..31; instancia el testbench público sin editarlo.
- Documentación propia actualizada y antecedentes conservados como históricos.

El informe externo I1-A3 registra cada comando, código, duración, hashes,
regresiones e instrumentos. Los primeros ensayos fallidos también se conservan;
no se presentan como pruebas aprobadas. Las compilaciones se ejecutan fuera de
los repositorios originales, incluidos ejecutables versionados.

## Cierre y continuación

La validación del subconjunto es independiente de completar el backend.
Continúan PARCIALES #29 y #37 por las fronteras detalladas en
[matriz-cobertura-entrega.md](matriz-cobertura-entrega.md). No se añaden niveles
productivos, terminación artificial ni acreditaciones ABI/reubicación generales.
Los valores de rendimiento DCE se refieren a cortes observados, no a una prueba
de terminación semántica de main. Las autocomparaciones B1 activas conservan
INCONCLUSO. Se requiere acordar ese contrato antes de certificar benchmarks
estrictos o presentar speedup global de programas BAL optimizados.
