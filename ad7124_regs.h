/***************************************************************************//**
*   @file    ad7124_regs.h
*   @brief   AD7124 register file.
*            Devices: AD7124-4, AD7124-8
*
********************************************************************************
* Copyright 2015-2019(c) Analog Devices, Inc.
*/


#ifndef __AD7124_REGS_H__
#define __AD7124_REGS_H__

#include "ad7124.h"

/*! Array holding the info for the ad7124 registers - address, initial value,
    size and access type. */
extern struct ad7124_st_reg ad7124_regs[AD7124_REG_NO];

#endif /* __AD7124_REGS_H__ */
