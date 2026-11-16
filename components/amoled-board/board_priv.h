// Shared between the files of this component only.

#pragma once

#include "driver/i2c_master.h"

// The board's I2C bus, valid after board_init().
i2c_master_bus_handle_t board_i2c_bus(void);
