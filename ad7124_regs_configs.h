/*!
Copyright (c) 2018-19 Analog Devices, Inc.
*/

#ifndef AD7124_REGS_CONFIGS_H_
#define AD7124_REGS_CONFIGS_H_

#include "ad7124.h"

/*
 * Arrays holding the info for the AD7124 registers - address, initial value,
 * size and access type.
 */
extern struct ad7124_st_reg ad7124_regs_config_a[AD7124_REG_NO];
extern struct ad7124_st_reg ad7124_regs_config_b[AD7124_REG_NO];

#endif /* AD7124_REGS_CONFIGS_H_ */
