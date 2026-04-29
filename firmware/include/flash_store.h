#ifndef RP2040JOY_FLASH_STORE_H
#define RP2040JOY_FLASH_STORE_H

#include <stdbool.h>
#include "config.h"

// Read config from flash into the singleton. If absent or corrupt, populates
// defaults. Returns true if a valid config was loaded; false if defaults used.
bool flash_store_load(joy_config_t *out);

// Erase the config sector and program the new contents. CRC is recomputed.
// Disables interrupts during the operation.
bool flash_store_save(joy_config_t *c);

#endif
