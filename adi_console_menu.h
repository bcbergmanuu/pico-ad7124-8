/*!
 *****************************************************************************
  @file:  adi_console_menu.h

  @brief:	A simple console menu manager handler

  @details:
 -----------------------------------------------------------------------------
Copyright (c) 2019 Analog Devices, Inc.  All rights reserved.

*/

#ifndef ADI_CONSOLE_MENU_H_
#define ADI_CONSOLE_MENU_H_

#include <stdbool.h>
#include <stdint.h>

#define MENU_ESCAPED			-1
#define MENU_CONTINUE           0
#define MENU_DONE               1

#define ESCAPE_KEY_CODE			(char)0x1B

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) ((sizeof (x)) / (sizeof ((x)[0])))
#endif

/* Type Definitions */
// Each menu item is defined by this struct
typedef struct {
	// String displayed for menu item
	char * displayText;
	// character that can be pressed to select menu item
	char  shortcutKey;
	// Function to be called when menu item is selected, if NULL, no function is called
	int32_t (*menuAction)(void);
} console_menu_item;

// This defines a complete menu with items
typedef struct{
	// String to be displayed as the menu title
	char * title;
	// Array of all the menu items
	console_menu_item * items;
	// Number of menuItems
	uint8_t itemCount;
	// Should the escape key to exit the menu be enabled?
	bool enableEscapeKey;
} console_menu;

/* Function Declarations */
/* Display a console menu, and handle user interactions */
int32_t adi_do_console_menu(const console_menu * menu);
void adi_clear_console(void);
void adi_press_any_key_to_continue(void);

#endif /* ADI_CONSOLE_MENU_H_ */
