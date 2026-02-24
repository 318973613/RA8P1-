/*
 * Build glue for RT-Thread Studio make-based projects.
 * These sources are normally pulled in by SCons; the make build only compiles src/.
 */
#include "../rt-thread/components/drivers/i2c/i2c_core.c"
#include "../rt-thread/components/drivers/i2c/i2c_dev.c"
#include "../rt-thread/components/drivers/i2c/i2c-bit-ops.c"
#include "../rt-thread/components/drivers/i2c/soft_i2c.c"
