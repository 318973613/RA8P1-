/*
 * Build glue for RT-Thread Studio make-based projects.
 * These HAL drivers are normally pulled in by SCons; the make build only compiles src/.
 */
#include "../libraries/HAL_Drivers/drv_spi.c"
#include "../libraries/HAL_Drivers/drv_i2c.c"