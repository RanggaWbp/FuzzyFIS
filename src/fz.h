/* fz.h -- umbrella header for the fz fuzzy inference library.
 *
 * Arduino-friendly: the core headers are always pulled in, the runtime .fis
 * parser only where it can be afforded.
 *
 *   #include <fz.h>                        // Arduino: parser included
 *   #define FZ_ENABLE_PARSER 1
 *   #include <fz.h>                        // host: opt in explicitly
 *   #define FZ_ARDUINO_SAFE 1
 *   #include <fz.h>                        // AVR under 4 KB SRAM: parser off
 *
 * FZ_ARDUINO_SAFE becomes 1 automatically on AVR parts with less than 4 KB of
 * SRAM (ATmega328P and friends), because the parser needs RAM pools for the MF,
 * rule and index tables. Defining FZ_ENABLE_PARSER overrides that.
 *
 * FZ_HAS_PARSER tells you which branch you got.
 *
 * This header defines no types, no globals and no code of its own. It is C99 and
 * C++11 clean, and safe to include more than once.
 */
#ifndef FZ_H
#define FZ_H

#include "fz_types.h"
#include "fz_mf.h"
#include "fz_ops.h"
#include "fz_defuzz.h"
#include "fz_eval.h"

/* AVR SRAM: RAMEND is the last valid SRAM address, set per part by avr-libc.
 * 2 KB (m328p) / 2.5 KB (m32u4) stay under 0x1000; 6 KB+ (m2560) does not. */
#if !defined(FZ_ARDUINO_SAFE) && defined(ARDUINO) && defined(__AVR__) && defined(RAMEND)
#  if RAMEND < 0x1000
#    define FZ_ARDUINO_SAFE 1
#  endif
#endif

/* Normalise to 0/1 so callers can test it with #if, not only #ifdef. */
#ifndef FZ_ARDUINO_SAFE
#  define FZ_ARDUINO_SAFE 0
#endif

/* FZ_HAS_PARSER is always DEFINED (0 or 1). Sketches gate on its value:
 *   #if FZ_HAS_PARSER ... #else "use generated_static instead" #endif
 * Leaving it undefined on AVR makes `#if !defined(FZ_HAS_PARSER)` true there,
 * which silently compiles the sketch without the parser instead of saying so. */
#if defined(FZ_ENABLE_PARSER) || (defined(ARDUINO) && !FZ_ARDUINO_SAFE) || !defined(ARDUINO)
#  include "fz_fis_parse.h"
#  define FZ_HAS_PARSER 1
#else
#  define FZ_HAS_PARSER 0
#endif

#endif /* FZ_H */
