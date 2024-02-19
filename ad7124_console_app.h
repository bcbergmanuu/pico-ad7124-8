/*!
 *****************************************************************************
  @file:  ad7124_console_app.h

  @brief: defines the console menu structure for the AD7124 example code

  @details:
 -----------------------------------------------------------------------------
Copyright (c) 2019 Analog Devices, Inc.  All rights reserved.

*/

#ifndef AD7124_CONSOLE_APP_H_
#define AD7124_CONSOLE_APP_H_

#include "adi_console_menu.h"

/* #defines */
#define AD7124_CONFIG_A       0
#define AD7124_CONFIG_B       1

/* Public Declarations */
static void spiInit();
int32_t ad7124_app_initialize(uint8_t configID);
extern console_menu ad7124_main_menu;


#endif /* AD7124_CONSOLE_APP_H_ */
