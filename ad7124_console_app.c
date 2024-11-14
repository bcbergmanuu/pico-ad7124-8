/*!
 *****************************************************************************
  @file:  ad7124_console_app.c

  @brief: Implementation for the menu functions that handle the AD7124

  @details:
 -----------------------------------------------------------------------------
Copyright (c) 2019 Analog Devices, Inc.  All rights reserved.


/* includes */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "hardware/timer.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "ad7124.h"
#include "ad7124_regs.h"
#include "ad7124_support.h"
#include "ad7124_regs_configs.h"

#include "ad7124_console_app.h"
#include "adi_console_menu.h"
#include "configuration.h"


#define AD7124_CHANNEL_COUNT 16

#define SHOW_ALL_CHANNELS     false
#define SHOW_ENABLED_CHANNELS  true

#define DISPLAY_DATA_TABULAR    0
#define DISPLAY_DATA_STREAM     1



/*
 * This is the 'live' AD7124 register map that is used by the driver
 * the other 'default' configs are used to populate this at init time
 */
static struct ad7124_st_reg ad7124_register_map[AD7124_REG_NO];

// Pointer to the struct representing the AD7124 device
static struct ad7124_dev * pAd7124_dev = NULL;

// Public Functions

/*!
 * @brief      Initialize the AD7124 device and the SPI port as required
 *
 * @details    This resets and then writes the default register map value to
 *  		   the device.
 */
int32_t ad7124_app_initialize(uint8_t configID)
{
	/*
	 * Copy one of the default/user configs to the live register memory map
	 * Requirement, not checked here, is that all the configs are the same size
	 */
	switch(configID) {
		case AD7124_CONFIG_A:
		{
			memcpy(ad7124_register_map, ad7124_regs_config_a, sizeof(ad7124_register_map));
			break;
		}	
	}

	// Used to create the ad7124 device
    struct	ad7124_init_param sAd7124_init =
  	{  		
  		ad7124_register_map,
  		10000				// Retry count for polling
  	};

  return(ad7124_setup(&pAd7124_dev, sAd7124_init));
}

static void spiInit() {
    // Initialize SPI port.    
    spi_init(spi_default, 500 * 1000);
	

    spi_set_format(spi_default, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);    
    gpio_set_function(PICO_DEFAULT_SPI_CSN_PIN, GPIO_FUNC_SPI);    
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN, GPIO_FUNC_SPI);    
}

//static volatile signed char receivedChar[10] = {0};
void initgpios () {
  for(int i = 0; i < 8; i++) {
    gpio_init(i);
    gpio_set_dir(i, GPIO_IN);     
	gpio_pull_up(i);
  }  
}  



int main() {
	stdio_init_all();    	
	spiInit();	
	initgpios();
	
	ad7124_app_initialize(AD7124_CONFIG_A);	
	adi_do_console_menu(&ad7124_main_menu);		
}

// Private Functions

/*!
 * @brief      reads and displays the status register on the AD7124
 *
 * @details
 */
static void read_status_register(void)
{
	if (ad7124_read_register(pAd7124_dev, &pAd7124_dev->regs[AD7124_Status]) < 0) {
	   printf("\r\nError Encountered reading Status register\r\n");
	} else {
	    uint32_t status_value = (uint32_t)pAd7124_dev->regs[AD7124_Status].value;
        printf("\r\nRead Status Register = 0x%02lx\r\n", status_value);
	}
}

static int32_t set_idle_mode() {
	int32_t error_code = 0;
	pAd7124_dev->regs[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf)); //clear mode bits	
	pAd7124_dev->regs[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(4); //idle mode
	if ( (error_code = ad7124_write_register(pAd7124_dev, pAd7124_dev->regs[AD7124_ADC_Control]) ) < 0) {
		printf("Error (%ld) setting AD7124 power mode to low.\r\n", error_code);
		adi_press_any_key_to_continue();
		return error_code;
	} else {
		printf("idle mode activated\n");
	}
}


/*!
 * @brief      Continuously acquires samples in Continuous Conversion mode
 *
 * @details   The ADC is run in continuous mode, and all samples are acquired
 *            and assigned to the channel they come from. Escape key an be used
 *            to exit the loop
 */
static int32_t do_continuous_conversion(bool doVoltageConvertion)
{
	static uint32_t toZeroValue = 0;

	int32_t error_code;
	int32_t sample_data;
	uint32_t portvalues;
	
	//select continuous convertion mode, all zero
	pAd7124_dev->regs[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));

	
	//select full power
	pAd7124_dev->regs[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_POWER_MODE(0x3));
	pAd7124_dev->regs[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_POWER_MODE(0x2);

	if ((error_code = ad7124_write_register(pAd7124_dev, pAd7124_dev->regs[AD7124_ADC_Control]) < 0)) {
		printf("Error (%ld) setting AD7124 Continuous conversion mode.\r\n", error_code);		
		return(MENU_CONTINUE);
	}

	uint8_t channel_read = 0;							
	// Continuously read the channels, and store sample values
	int pressedchar = 0;
    while (pressedchar !=27) {  		  			    	
		pressedchar = getchar_timeout_us(0);
		if(pressedchar == 48) {
			toZeroValue = to_ms_since_boot(get_absolute_time());
		}
		/*
		*  this polls the status register READY/ bit to determine when conversion is done
		*  this also ensures the STATUS register value is up to date and contains the
		*  channel that was sampled as well.
		*  Generally, no need to read STATUS separately, but for faster sampling
		*  enabling the DATA_STATUS bit means that status is appended to ADC data read
		*  so the channel being sampled is read back (and updated) as part of the same frame
		*/
		if ( (error_code = ad7124_wait_for_conv_ready(pAd7124_dev, 10000)) < 0) {
				printf("Error/Timeout waiting for conversion ready %ld\r\n", error_code);
				return -1;
			}
		channel_read = pAd7124_dev->regs[AD7124_Status].value & 0x0000000F;
		if(pAd7124_dev->regs[AD7124_Channel_0 + channel_read].value & AD7124_CH_MAP_REG_CH_ENABLE) {

			if ( (error_code = ad7124_read_data(pAd7124_dev, &sample_data)) < 0) {
				printf("Error reading ADC Data (%ld).\r\n", error_code);
				return -1;
			}
			
			if (channel_read == 0) {				
				printf("\n%09d, ", to_ms_since_boot(get_absolute_time()) - toZeroValue);
				portvalues = gpio_get_all();
        		
				printf("%i, ", portvalues & 0XFF);
			} else {
				printf(", ");
			}
			if(doVoltageConvertion) {
				printf("%.8f", ad7124_convert_sample_to_voltage(pAd7124_dev, channel_read, sample_data) );				
			} else { printf("%i", sample_data); }
		}		
	}		

    

	error_code = set_idle_mode();
	if (error_code < 0) printf("error occured continuous conversion");
	return(MENU_CONTINUE);
}



static int32_t set_zero_scale_calibration() {
	// 5 = system zero scale calibration
	int32_t error_code = 0;
	pAd7124_dev->regs[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf) | AD7124_ADC_CTRL_REG_POWER_MODE(0x3));
	pAd7124_dev->regs[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(0b0111) | AD7124_ADC_CTRL_REG_POWER_MODE(0x01);
	if ( (error_code = ad7124_write_register(pAd7124_dev, pAd7124_dev->regs[AD7124_ADC_Control]) ) < 0) {
		printf("Error (%ld) setting AD7124 ADC into zero scale calibration.\r\n", error_code);
		adi_press_any_key_to_continue();
		return error_code;
	} else {
		printf("zero scale calibration completed\n");
	}
}

static int32_t set_full_scale_calibration() {
	// 6 = system full scale calibration
	int32_t error_code = 0;
	pAd7124_dev->regs[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf) | AD7124_ADC_CTRL_REG_POWER_MODE(0x3) );
	pAd7124_dev->regs[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(0b1000) | AD7124_ADC_CTRL_REG_POWER_MODE(0x01);
	if ( (error_code = ad7124_write_register(pAd7124_dev, pAd7124_dev->regs[AD7124_ADC_Control]) ) < 0) {
		printf("Error (%ld) setting AD7124 ADC into internal full scale calibration.\r\n", error_code);		
		return error_code;
	} else {
		printf("full scale calibration completed\n");
	}
}

static int32_t read_error() {
	int32_t error_code;
	error_code = ad7124_read_register( pAd7124_dev, &pAd7124_dev->regs[AD7124_Error]);
		if(error_code < 0) {
			printf("error reading errorcode (%ld)", error_code);
			return error_code;
		} else {
			printf("error reg: %i\n", pAd7124_dev->regs[AD7124_Error].value);
		}
}



static int32_t set_slow_filters(bool enable) {
	//set high filter for calibration
	int32_t error_code = 0;
	enum ad7124_registers reg_nr;	
	
	for(reg_nr = AD7124_Filter_0; (reg_nr < AD7124_Offset_0) && !(error_code < 0); reg_nr++) {
		if(enable) {									
			struct ad7124_st_reg reg;
			reg=pAd7124_dev->regs[reg_nr];
			reg.value = AD7124_FILT_REG_FS(1024) | AD7124_FILT_REG_REJ60 | AD7124_FILT_REG_POST_FILTER(0b110); //cannot use higher than 1024, don't know why...
			error_code = ad7124_write_register(pAd7124_dev, reg);			
			//printf("set slow filters: %d\n", reg.value);			
		} else {
			// struct ad7124_st_reg reg;
			// reg=pAd7124_dev->regs[reg_nr];
			// reg.value = 0;
			// error_code = ad7124_write_register(pAd7124_dev, reg); //zero all?
			error_code = ad7124_write_register(pAd7124_dev, pAd7124_dev->regs[reg_nr]); //original value
			//printf("set original filters: %d\n", pAd7124_dev->regs[reg_nr].value);			
		}						
		if (error_code < 0) break;		
	}			
}

static uint32_t switch_channel(bool enable, enum ad7124_registers channel) {
	
	if(enable) {
	 	pAd7124_dev->regs[channel].value |= AD7124_CH_MAP_REG_CH_ENABLE;
	} else {
		pAd7124_dev->regs[channel].value &= ~(AD7124_CH_MAP_REG_CH_ENABLE);
	}
	
	int32_t error_code = 0;
	error_code |= ad7124_write_register(pAd7124_dev, pAd7124_dev->regs[channel]);										
	printf("%s channel %i\n", enable ? "enabled" : "disabled", channel);			
	
	return error_code;
}

static int32_t do_fullscale_calibration() {	
	int32_t error_code = 0;	
	int32_t enabled_channels = 0;

	for (enum ad7124_registers i = AD7124_Offset_0; i < AD7124_Gain_0; i++)
	{
		//write zero in offset register of each configuration 
		pAd7124_dev->regs[i].value = 0x800000;
		if ( (error_code = ad7124_write_register(pAd7124_dev, pAd7124_dev->regs[i]) ) < 0) {
			printf("Error (%ld) writing offset for setup 0.\r\n", error_code);
			return error_code;			
		}
	}

	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {		
		if (pAd7124_dev->regs[AD7124_Channel_0 + i].value & AD7124_CH_MAP_REG_CH_ENABLE) {
			enabled_channels += (1 << i);	
			error_code |= switch_channel(false, AD7124_Channel_0 + i);
		}	
	}	
	
	error_code |= set_slow_filters(true);

	//loop channels for calibration
	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) { 
		if(enabled_channels & (1<<i)) {
			//enable for calibration
			switch_channel(true, AD7124_Channel_0 +i);
						
			//full scale must be done before zero scale calibration
			// set_full_scale_calibration();
			// ad7124_wait_for_conv_ready(pAd7124_dev, 10000);
			
			set_zero_scale_calibration();											
			ad7124_wait_for_conv_ready(pAd7124_dev, 10000);
						
			switch_channel(false, AD7124_Channel_0 +i);
		}
	}

	error_code |= set_slow_filters(false);

	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {  
		if(enabled_channels & (1<<i)) {
			error_code |= switch_channel(true, AD7124_Channel_0 + i);
		}
	}
	
	error_code = set_idle_mode();
	if(error_code < 0) {
		adi_press_any_key_to_continue();
		printf("error fullscale calibraion");
	}
}


/*!
 * @brief      Internal full-scale (gain) calibration.
 *
 * @details
 */
static int32_t menu_fullscale_calibration(void){
	do_fullscale_calibration();
	printf("calibration completed...\r\n\r\n");
	adi_press_any_key_to_continue();
	return 0;
}


/*!
 * @brief      Samples all enabled channels and displays on the console
 *
 * @details
 */
static int32_t menu_continuous_conversion_stream()
{
	do_continuous_conversion(true);
	printf("Continuous Conversion completed...\n");
	adi_press_any_key_to_continue();
	return(MENU_CONTINUE);
}

static int32_t menu_raw_conversion_stream() {
	do_continuous_conversion(false);
	printf("Continuous Conversion completed...\n");
	adi_press_any_key_to_continue();
	return(MENU_CONTINUE);
}

/*!
 * @brief      menu item that reads the status register the AD7124
 *
 * @details
 */
static int32_t menu_read_status(void)
{
	read_status_register();
	adi_press_any_key_to_continue();
	return(MENU_CONTINUE);
}


/*!
 * @brief      reads the ID register on the AD7124
 *
 * @details
 */
static int32_t menu_read_id(void)
{
	if (ad7124_read_register(pAd7124_dev, &pAd7124_dev->regs[AD7124_ID]) < 0) {
	   printf("\r\nError Encountered reading ID register\r\n");
	} else {
	   printf("\r\nRead ID Register = 0x%02lx\r\n",
			   (uint32_t)pAd7124_dev->regs[AD7124_ID].value );
	}
	adi_press_any_key_to_continue();
	return(MENU_CONTINUE);
}


/*!
 * @brief      Initialize the part with a specific configuration
 *
 * @details
 */
static void init_with_configuration(uint8_t configID)
{
	int32_t status = 0;

	do {
		if ((status = ad7124_remove(pAd7124_dev)) < 0) {
			break;
		}

		if ((status = ad7124_app_initialize(configID)) < 0) {
			break;
		}
	} while(0);

	if (status < 0) {
		printf("\r\n\r\n Error setting Configuration %c \r\n\r\n", (char)(configID + 'A'));
	} else {
		printf("\r\n\r\n Configuration %c Set\r\n\r\n", (char)(configID + 'A'));
	}
	adi_press_any_key_to_continue();
}

/*!
 * @brief      Reset and set the ad7124 with configuration A
 *
 * @details
 */
static int32_t menu_reset_to_configuration_a(void)
{
	init_with_configuration(AD7124_CONFIG_A);
	return(MENU_CONTINUE);
}


/*!
 * @brief      Reset and the ad7124 with configuration B
 *
 * @details
 */
static int32_t menu_reset_to_configuration_b(void)
{
	init_with_configuration(AD7124_CONFIG_B);
	return(MENU_CONTINUE);
}


/*
 * Definition of the Main Menu Items and menu itself
 */
console_menu_item main_menu_items[] = {			
    {"Start continuous conversion",		'S', menu_continuous_conversion_stream},
	{"Continous conversion raw",		'R', menu_raw_conversion_stream},
	{"", 								'\00', NULL},
	{"Zero and full scale calibration", 'Z', menu_fullscale_calibration},
	{"Read Status Register",			'T', menu_read_status},	
	{"Read ID Register ", 				'I', menu_read_id}
};

console_menu ad7124_main_menu = {
    "AD7124 Main Menu " boardname,
    main_menu_items,
	ARRAY_SIZE(main_menu_items),
	false
};
