#include <stdio.h>
#include "pico/stdio.h"
#include "pico/stdlib.h"

#define MAX_LEN 32
const uint LED_PIN_INDICATOR = 25;

void chars_available_callback(void* param)
{
    // toggle led
    gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);   

    // get any pending key presses
    volatile signed char *c = (signed char *) param;
    while ((*c = getchar_timeout_us(0)) > 0)
        c++;
    *c = '\0';
}

int main()
{
    stdio_init_all();

    // There isn't a strict rule that you must always call sleep_ms()
    // after stdio_init_all(). However, in some cases, it can be a helpful
    // precautionary measure to ensure that the UART has properly 
    // initialized and is ready to transmit data without any issues.
    sleep_ms(2000);

    printf("--==Init==--\n");

    // Init all inputs and outputs
    gpio_init(LED_PIN_INDICATOR);

    gpio_set_dir(LED_PIN_INDICATOR, GPIO_OUT);

    // Set initial state
    gpio_put(LED_PIN_INDICATOR, 0);
    
    static volatile signed char receivedChar[MAX_LEN] = {0};
    stdio_set_chars_available_callback(&chars_available_callback, (void*) receivedChar);

    // Send to console
    printf("Start!\n");

    while (1) {
        tight_loop_contents();
        if (!receivedChar[0]) continue;

        printf("String: %s", receivedChar);
        receivedChar[0] = '\0';
    }
}
