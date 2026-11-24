/* Adaptadores de las herramientas existentes. Sin GTK ni algoritmos nuevos.
 * Las cadenas devueltas son propias y se liberan con g_free. */
#ifndef IDE_HERRAMIENTAS_H
#define IDE_HERRAMIENTAS_H
#include <gio/gio.h>
#include "token.h"
const char *ide_token_categoria(TokenType tipo);
/* Ejecuta el compilador de raiz sobre una instantánea; nunca guarda el editor.
 * Devuelve JSON propio solo tras éxito. Propaga los diagnósticos reales. */
gchar *ide_generar_json(const char *raiz, const char *texto, gboolean spilling, GError **error);
/* Ejecuta balc -x en una carpeta privada y devuelve ambos archivos propios.
 * Ningún archivo al lado del ASM se modifica durante la validación. */
gboolean ide_ensamblar_archivo(const char *raiz, const char *ruta,
                               GBytes **bin, GBytes **mem, GError **error);
/* MEM se recodifica con CB desde el ASM mostrado; BIN se conserva byte a byte. */
GBytes *ide_generar_mem(const char *asm_texto, GError **error);
/* Sin reemplazar: creación exclusiva. Con reemplazo: GIO publica por archivo.
 * No es una transacción entre varios archivos. Nunca acepta directorios. */
gboolean ide_guardar_bytes(const char *ruta, GBytes *bytes, gboolean reemplazar, GError **error);
#endif
