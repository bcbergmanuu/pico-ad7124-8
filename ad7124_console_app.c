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

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "ad7124.h"
#include "ad7124_regs.h"
#include "ad7124_support.h"
#include "ad7124_regs_configs.h"

#include "ad7124_console_app.h"
#include "adi_console_menu.h"


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

// Last Sampled values for All ADC channels
static uint32_t channel_samples[AD7124_CHANNEL_COUNT] = {0};
// How many times a given channel is sampled in total for one sample run
static uint32_t channel_samples_count[AD7124_CHANNEL_COUNT] = {0};


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
		case AD7124_CONFIG_B:
		{
			memcpy(ad7124_register_map, ad7124_regs_config_b, sizeof(ad7124_register_map));
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

int main() {
	stdio_init_all();    	
	spiInit();	
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
	if (ad7124_read_register(pAd7124_dev, &ad7124_register_map[AD7124_Status]) < 0) {
	   printf("\r\nError Encountered reading Status register\r\n");
	} else {
	    uint32_t status_value = (uint32_t)ad7124_register_map[AD7124_Status].value;
        printf("\r\nRead Status Register = 0x%02lx\r\n", status_value);
	}
}


/*!
 * @brief      displays the current sample value for a ADC channels
 *
 * @details
 *
 * @param showOnlyEnabledChannels  only channels that are enabled are displayed
 *
 */
static void dislay_channel_samples(bool showOnlyEnabledChannels, uint8_t console_mode)
{
	switch(console_mode) {
		case DISPLAY_DATA_TABULAR:
		{
			printf("\tCh\tValue\t\tCount\t\tVoltage\r\n");
			for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {
				// if showing all channels, or channel is enabled
				if ((showOnlyEnabledChannels == false) || (ad7124_register_map[AD7124_Channel_0 + i].value & AD7124_CH_MAP_REG_CH_ENABLE) ) {
				   printf("\t%-2d\t%-10ld\t%ld\t\t% .6f\r\n", \
							i, channel_samples[i], channel_samples_count[i],
							ad7124_convert_sample_to_voltage(pAd7124_dev, i, channel_samples[i]) );
				}
			}
			break;
		}
		case DISPLAY_DATA_STREAM:
		{
			// Output a CSV list of the sampled channels as voltages on a single line
			bool channel_printed = false;

			for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {
				// if showing all channels, or channel is enabled
				if ((showOnlyEnabledChannels == false) || (ad7124_register_map[AD7124_Channel_0 + i].value & AD7124_CH_MAP_REG_CH_ENABLE) ) {
					/*
					 *  add the comma before we output the next channel but
					 *  only if at least one channel has been printed
					 */
				   if (channel_printed) {
					   printf(", ");
				   }
					printf("%.6f", \
							ad7124_convert_sample_to_voltage(pAd7124_dev, i, channel_samples[i]) );
					channel_printed = true;
				}
			}
			printf("\r\n");
			break;
		}
		default:
		{
			// ASSERT(false);
		}
	}
}


/*!
 * @brief      resets the channelSampleCounts to zero
 *
 * @details
 */
static void clear_channel_samples(void)
{
    for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {
    	channel_samples[i] = 0;
    	channel_samples_count[i] = 0;
    }
}


/*!
 * @brief      Continuously acquires samples in Continuous Conversion mode
 *
 * @details   The ADC is run in continuous mode, and all samples are acquired
 *            and assigned to the channel they come from. Escape key an be used
 *            to exit the loop
 */
static int32_t do_continuous_conversion(uint8_t display_mode)
{
	int32_t error_code;
	int32_t sample_data;

	// Clear the ADC CTRL MODE bits, has the effect of selecting continuous mode
    ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));
	ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_POWER_MODE(0x3);
	if ((error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_ADC_Control]) ) < 0) {
		printf("Error (%ld) setting AD7124 Continuous conversion mode.\r\n", error_code);
		adi_press_any_key_to_continue();
		return(MENU_CONTINUE);
	}

	clear_channel_samples();

	/*
	 *  If displaying data in stream form, want to output a channel header
	 */
	if (display_mode == DISPLAY_DATA_STREAM)
	{
		bool channel_printed = false;

		for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {
		   // if showing all channels, or channel is enabled
			if (ad7124_register_map[AD7124_Channel_0 + i].value & AD7124_CH_MAP_REG_CH_ENABLE) {
				/*
				 *  add the comma before we output the next channel but
				 *  only if at least one channel has been printed
				 */
			   if (channel_printed) {
				   printf(", ");
			   }
				printf("%d", i);
			}
			channel_printed = true;
		}
		printf("\r\n");
	}

	// Continuously read the channels, and store sample values
    while (getchar_timeout_us(0) != 27) {  		  			
    	if (display_mode == DISPLAY_DATA_TABULAR) {
			sleep_ms(100);
    		adi_clear_console();
    		printf("Running continuous conversion mode...\n");
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

    	if ( (error_code = ad7124_read_data(pAd7124_dev, &sample_data)) < 0) {
			printf("Error reading ADC Data (%ld).\r\n", error_code);
			return -1;
		}

		/*
		 * No error, need to process the sample, what channel has been read? update that channelSample
		 */
		uint8_t channel_read = ad7124_register_map[AD7124_Status].value & 0x0000000F;

		if (channel_read < AD7124_CHANNEL_COUNT) {
			channel_samples[channel_read] = sample_data;
			channel_samples_count[channel_read]++;
		} else {
			printf("Channel Read was %d, which is not < AD7124_CHANNEL_COUNT\r\n", channel_read);
			return -1;
		}

		dislay_channel_samples(SHOW_ENABLED_CHANNELS, display_mode);
    }

    // All done, ADC put into standby mode
    ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));
    // 2 = sleep/standby mode
    ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(2);

	if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_ADC_Control]) ) < 0) {
		printf("Error (%ld) setting AD7124 ADC into standby mode.\r\n", error_code);
		return -1;
	}

	return(MENU_CONTINUE);
}

static int32_t set_idle_mode() {
	int32_t error_code = 0;
	ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf)); //clear mode bits	
	ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(4); //idle mode
	if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_ADC_Control]) ) < 0) {
		printf("Error (%ld) setting AD7124 power mode to low.\r\n", error_code);
		adi_press_any_key_to_continue();
		return error_code;
	} else {
		printf("idle mode activated\n");
	}
}

static int32_t set_zero_scale_calibration() {
	// 5 = system zero scale calibration
	int32_t error_code = 0;
	ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));
	ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(0b0111);
	if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_ADC_Control]) ) < 0) {
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
	ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));
	ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(0b1000);
	if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_ADC_Control]) ) < 0) {
		printf("Error (%ld) setting AD7124 ADC into internal full scale calibration.\r\n", error_code);		
		return error_code;
	} else {
		printf("full scale calibration completed\n");
	}
}

static int32_t read_error() {
	int32_t error_code;
	error_code = ad7124_read_register( pAd7124_dev, &ad7124_register_map[AD7124_Error]);
		if(error_code < 0) {
			printf("error reading errorcode (%ld)", error_code);
			return error_code;
		} else {
			printf("error reg: %i\n", ad7124_register_map[AD7124_Error].value);
		}
}

static int32_t  do_fullscale_calibration() {	
	int32_t error_code = 0;
	int32_t channel_is_enabled = 0;	
	

	for (uint8_t i = 0; i < 8; i++)
	{
		//write zero in offset register of each configuration 
		ad7124_register_map[AD7124_Offset_0+i].value = 0x800000;
		if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_Offset_0+i]) ) < 0) {
			printf("Error (%ld) writing offset for setup 0.\r\n", error_code);
			return error_code;			
		}
	}
			
	//disable all channels
	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {		
		if (ad7124_register_map[AD7124_Channel_0 + i].value & AD7124_CH_MAP_REG_CH_ENABLE) {
			channel_is_enabled += (1 << i);			
		}
		//disable for now
		ad7124_register_map[AD7124_Channel_0 + i].value &= ~(AD7124_CH_MAP_REG_CH_ENABLE);
		if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_Channel_0 + i]) ) < 0) {
			printf("Error (%ld) enable channel %i.\r\n", error_code, i);
			return error_code;
		} else {
			printf("disabled channel %i\n", i);
		}
			
	}
	
	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) { 
		if(channel_is_enabled & (1<<i)) {
			//enable for calibration
			ad7124_register_map[AD7124_Channel_0 + i].value |= (AD7124_CH_MAP_REG_CH_ENABLE);
			if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_Channel_0 + i]) ) < 0) {
				printf("Error (%ld) disable channel %i.\r\n", error_code, i);
				adi_press_any_key_to_continue();
				return error_code;
			} else {				
				printf("enabled channel %i\n", i);
			}
			//sleep_ms(150);									
			
			set_zero_scale_calibration();											
			ad7124_wait_for_conv_ready(pAd7124_dev, 10000);
			
			//sleep_ms(150);						
			//ad7124_wait_for_conv_ready(pAd7124_dev, 10000);
			//set_full_scale_calibration();
			//ad7124_wait_for_conv_ready(pAd7124_dev, 10000);
			//sleep_ms(150);
			
			ad7124_register_map[AD7124_Channel_0 + i].value &= ~(AD7124_CH_MAP_REG_CH_ENABLE);
			if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_Channel_0 + i]) ) < 0) {
				printf("Error (%ld) enable channel %i.\r\n", error_code, i);
				adi_press_any_key_to_continue();
				return error_code;
			} else {
				printf("disabled channel %i\n", i);
			}
			
		}
	}
	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) { 
		if(channel_is_enabled & (1<<i)) {
			ad7124_register_map[AD7124_Channel_0 + i].value |= AD7124_CH_MAP_REG_CH_ENABLE;
			if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_Channel_0 + i]) ) < 0) {
				printf("Error (%ld) enable channel %i.\r\n", error_code, i);
				return error_code;
			} else {
				printf("enabled channel %i\n", i);
			}
		}
	}
	
	// All done, ADC put into standby mode
    ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));
    // 2 = sleep/standby mode
    ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(2);
}

/*!
 * @brief      Samples all enabled channels and displays in tabular form
 *
 * @details
 */
static int32_t menu_continuous_conversion_tabular(void)
{
	do_continuous_conversion(DISPLAY_DATA_TABULAR);

	adi_clear_console();
	printf("Continuous Conversion completed...\r\n\r\n");
	dislay_channel_samples(SHOW_ALL_CHANNELS, DISPLAY_DATA_TABULAR);
	adi_press_any_key_to_continue();

	return(MENU_CONTINUE);
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
static int32_t menu_continuous_conversion_stream(void)
{
	do_continuous_conversion(DISPLAY_DATA_STREAM);
	printf("Continuous Conversion completed...\r\n\r\n");
	adi_press_any_key_to_continue();
	return(MENU_CONTINUE);
}


/*!
 * @brief      Samples all enabled channels once in Single Conversion mode
 *
 * @details    This stores all channels that are enabled in a bitmask, and then
 *             runs the ADC in single conversion mode, which acquires one channel
 *             of data at a time. After capture, that channel is disabled, and
 *             single conversion run again, until no channels are enabled.
 *             The original enable state of each channel is then restored.
 */
static int32_t menu_single_conversion(void)
{
	int32_t    error_code;
	uint16_t   channel_enable_mask = 0;
	uint8_t    channel_count = 0;
	int32_t    sample_data;

	// Need to store which channels are enabled in this config so it can be restored
	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {
		if (ad7124_register_map[AD7124_Channel_0 + i].value & AD7124_CH_MAP_REG_CH_ENABLE) {
			channel_enable_mask |= (1 << i);
			channel_count++;
		}
	}

	clear_channel_samples();
	adi_clear_console();
	printf("Running Single conversion mode...\r\nPress Escape to stop\r\n\r\n");

	// Clear the ADC CTRL MODE bits, selecting continuous mode
    ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));

	// read the channels, and store sample values
    for (uint8_t loopCount = 0; ((loopCount < channel_count)) ; loopCount++) {
    	
    	// 1 = single conversion mode
        ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(1);

    	if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_ADC_Control]) ) < 0) {
    		printf("Error (%ld) setting AD7124 Single conversion mode.\r\n", error_code);
    		adi_press_any_key_to_continue();
    		continue;
    	}

        /*
         *  this polls the status register READY/ bit to determine when conversion is done
         *  this also ensures the STATUS register value is up to date and contains the
         *  channel that was sampled as well. No need to read STATUS separately
         */
    	if ( (error_code = ad7124_wait_for_conv_ready(pAd7124_dev, 10000)) < 0) {
    		printf("Error/Timeout waiting for conversion ready %ld\r\n", error_code);
    		continue;
    	}

    	if ( (error_code = ad7124_read_data(pAd7124_dev, &sample_data)) < 0) {
			printf("Error reading ADC Data (%ld).\r\n", error_code);
			continue;
		}
    	/*
		 * No error, need to process the sample, what channel has been read? update that channelSample
		 */
		uint8_t channelRead = ad7124_register_map[AD7124_Status].value & 0x0000000F;

		if (channelRead < AD7124_CHANNEL_COUNT) {
			channel_samples[channelRead] = sample_data;
			channel_samples_count[channelRead]++;

			/* also need to clear the channel enable bit so the next single conversion cycle will sample the next channel */
			ad7124_register_map[AD7124_Channel_0 + channelRead].value &= ~AD7124_CH_MAP_REG_CH_ENABLE;
			if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_Channel_0 + channelRead]) ) < 0) {
				printf("Error (%ld) Clearing channel %d Enable bit.\r\n", error_code, channelRead);
				adi_press_any_key_to_continue();
				continue;
			}
		} else {
			printf("Channel Read was %d, which is not < AD7124_CHANNEL_COUNT\r\n", channelRead);
		}
    }

    // All done, ADC put into standby mode
    ad7124_register_map[AD7124_ADC_Control].value &= ~(AD7124_ADC_CTRL_REG_MODE(0xf));
    // 2 = sleep/standby mode
    ad7124_register_map[AD7124_ADC_Control].value |= AD7124_ADC_CTRL_REG_MODE(2);

	// Need to restore the channels that were disabled during acquisition
	for (uint8_t i = 0; i < AD7124_CHANNEL_COUNT; i++) {
		if (channel_enable_mask & (1 << i)) {
			ad7124_register_map[AD7124_Channel_0 + i].value |= AD7124_CH_MAP_REG_CH_ENABLE;
	    	if ( (error_code = ad7124_write_register(pAd7124_dev, ad7124_register_map[AD7124_Channel_0 + i]) ) < 0) {
	    		printf("Error (%ld) Setting channel %d Enable bit.\r\r\n", error_code, i);
	    		adi_press_any_key_to_continue();
	    		return(MENU_CONTINUE);
	    	}
		}
	}

	printf("Single Conversion completed...\r\n\r\n");
	dislay_channel_samples(SHOW_ENABLED_CHANNELS, DISPLAY_DATA_TABULAR);

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
	if (ad7124_read_register(pAd7124_dev, &ad7124_register_map[AD7124_ID]) < 0) {
	   printf("\r\nError Encountered reading ID register\r\n");
	} else {
	   printf("\r\nRead ID Register = 0x%02lx\r\n",
			   (uint32_t)ad7124_register_map[AD7124_ID].value );
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
 * Definition of the Sampling Menu Items and menu itself
 */
static console_menu_item acquisition_menu_items[] = {
		{"Single Conversion Mode",	                 	'S', menu_single_conversion},
	    {"Continuous Conversion Mode - Table View",	    'T', menu_continuous_conversion_tabular},
		{"Continuous Conversion Mode - Stream Data",	'C', menu_continuous_conversion_stream},
		
};

static console_menu acquisition_menu = {
    "Data Acquisition Menu",
	acquisition_menu_items,
	ARRAY_SIZE(acquisition_menu_items),
	true
};


/*!
 * @brief      displays and handles the Sample Channel menu
 *
 * @details
 */
static int32_t menu_sample_channels(void)
{
	return(adi_do_console_menu(&acquisition_menu));
}


/*
 * Definition of the Main Menu Items and menu itself
 */
console_menu_item main_menu_items[] = {	
	{"Set to Configuration A",		'A', menu_reset_to_configuration_a},
    {"Set to Configuration B",		'B', menu_reset_to_configuration_b},
	{"", 								'\00', NULL},
	{"Read ID Register",				'I', menu_read_id},
	{"Read Status Register",			'T', menu_read_status},
	{"", 								'\00', NULL},
    {"Sample Channels...",				'S', menu_sample_channels},
	{"Zero and full scale calibration", 'Z', menu_fullscale_calibration},
};

console_menu ad7124_main_menu = {
    "AD7124 Main Menu",
    main_menu_items,
	ARRAY_SIZE(main_menu_items),
	false
};
