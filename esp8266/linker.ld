/* ===========================================================================
 * linker.ld — MiniOS-ESP8266 linker wrapper
 *
 * The NonOS SDK's linker script remains the source of truth for section
 * placement. This wrapper only adapts the entry symbol to the libmain.a
 * shipped with the installed SDK, which exports call_user_start instead of
 * call_user_start1.
 * =========================================================================== */

ENTRY(call_user_start)
EXTERN(call_user_start)
PROVIDE(call_user_start1 = call_user_start);

INCLUDE "eagle.app.v6.ld"
