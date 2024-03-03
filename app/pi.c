#include "pi.h"
#include "reg.h"
#include "keyboard.h"
#include "gpioexp.h"
#include "backlight.h"
#include "hardware/adc.h"
#include <hardware/pwm.h>

#include <pico/stdlib.h>

#define LED_FLASH_CYCLE_MS 3000
#define LED_FLASH_ON_MS 200

// Globals
alarm_id_t g_power_on_alarm = -1;
alarm_id_t g_power_off_alarm = -1;

uint8_t g_led_flash_enabled = 0;
alarm_id_t g_led_flash_alarm = -1;

enum pi_state
{
	PI_STATE_OFF = 0,
	PI_STATE_ON = 1,
};

static enum pi_state state;

void pi_power_init(void)
{
	adc_init();
	adc_gpio_init(PIN_BAT_ADC);
	adc_select_input(0);

	gpio_init(PIN_PI_PWR);
	gpio_set_dir(PIN_PI_PWR, GPIO_OUT);
	gpio_put(PIN_PI_PWR, 0);
	state = PI_STATE_OFF;
}

void pi_power_on(enum power_on_reason reason)
{
	if (state == PI_STATE_ON) {
		return;
	}

	gpio_put(PIN_PI_PWR, 1);
	state = PI_STATE_ON;

	// LED green while booting until driver loaded
    reg_set_value(REG_ID_LED, 1);
    reg_set_value(REG_ID_LED_R, 0);
    reg_set_value(REG_ID_LED_G, 128);
    reg_set_value(REG_ID_LED_B, 0);
	led_sync(1);

	// Update startup reason
	reg_set_value(REG_ID_STARTUP_REASON, reason);
}

void pi_power_off(void)
{
	if (state == PI_STATE_OFF) {
		return;
	}

	gpio_put(PIN_PI_PWR, 0);
	state = PI_STATE_OFF;
}

static int64_t pi_power_on_alarm_callback(alarm_id_t _, void* __)
{
	pi_power_on(POWER_ON_REWAKE);

	return 0;
}

void pi_schedule_power_on(uint32_t ms)
{
	// Cancel existing alarm if scheduled
	if (g_power_on_alarm >= 0) {
		cancel_alarm(g_power_on_alarm);
		g_power_on_alarm = -1;
	}

	// Schedule new alarm
	g_power_on_alarm = add_alarm_in_ms(ms, pi_power_on_alarm_callback, NULL, true);
}

static int64_t pi_power_off_alarm_callback(alarm_id_t _, void* __)
{
	pi_power_off();

	return 0;
}

void pi_schedule_power_off(uint32_t ms)
{
	// Cancel existing alarm if scheduled
	if (g_power_off_alarm >= 0) {
		cancel_alarm(g_power_off_alarm);
		g_power_off_alarm = -1;
	}

	// Schedule new alarm
	g_power_off_alarm = add_alarm_in_ms(ms, pi_power_off_alarm_callback, NULL, true);
}

void pi_cancel_power_alarms()
{
	// Cancel power on alarm
	if (g_power_on_alarm >= 0) {
		cancel_alarm(g_power_on_alarm);
		g_power_on_alarm = -1;
	}

	// Cancel power off alarm
	if (g_power_off_alarm >= 0) {
		cancel_alarm(g_power_off_alarm);
		g_power_off_alarm = -1;
	}
}

void led_init(void)
{
    // Set up PWM channels
    gpio_set_function(PIN_LED_R, GPIO_FUNC_PWM);
    gpio_set_function(PIN_LED_G, GPIO_FUNC_PWM);
    gpio_set_function(PIN_LED_B, GPIO_FUNC_PWM);

    //default off
    reg_set_value(REG_ID_LED, 0);

    led_sync(0);
}

void led_sync(uint8_t enable)
{
    // Set the PWM slice for each channel
    uint slice_r = pwm_gpio_to_slice_num(PIN_LED_R);
    uint slice_g = pwm_gpio_to_slice_num(PIN_LED_G);
    uint slice_b = pwm_gpio_to_slice_num(PIN_LED_B);

    // Calculate the PWM value for each channel
    uint16_t pwm_r = (0xFF - reg_get_value(REG_ID_LED_R)) * 0x101;
    uint16_t pwm_g = (0xFF - reg_get_value(REG_ID_LED_G)) * 0x101;
    uint16_t pwm_b = (0xFF - reg_get_value(REG_ID_LED_B)) * 0x101;

    // Set the PWM duty cycle for each channel
    if (enable == 0){
        pwm_set_gpio_level(PIN_LED_R, 0xFFFF);
        pwm_set_gpio_level(PIN_LED_G, 0xFFFF);
        pwm_set_gpio_level(PIN_LED_B, 0xFFFF);

    } else {
        pwm_set_gpio_level(PIN_LED_R, pwm_r);
        pwm_set_gpio_level(PIN_LED_G, pwm_g);
        pwm_set_gpio_level(PIN_LED_B, pwm_b);
    }

    // Enable PWM channels
    pwm_set_enabled(slice_r, true);
    pwm_set_enabled(slice_g, true);
    pwm_set_enabled(slice_b, true);
}

static int64_t pi_led_flash_alarm_callback(alarm_id_t _, void* __)
{
	static uint8_t led_enabled = 0;
	uint32_t alarm_ms;

	// Exit if flash canceled
	if (reg_get_value(REG_ID_LED_FLASH) == 0) {
		return 0;
	}

	// Toggle LED
	led_enabled = !led_enabled;
	led_sync(led_enabled);

	// Reschedule timer
	alarm_ms = (led_enabled)
		? LED_FLASH_ON_MS
		: (LED_FLASH_CYCLE_MS - LED_FLASH_ON_MS);
	g_led_flash_alarm = add_alarm_in_ms(alarm_ms, pi_led_flash_alarm_callback, NULL, true);

	return 0;
}

void led_flash(uint8_t enable)
{
	if (enable) {

		// Schedule flash timer
		if (g_led_flash_alarm < 0) {
			g_led_flash_alarm = add_alarm_in_ms(700, pi_led_flash_alarm_callback, NULL, true);
		}

	} else {

		// Cancel flash timer
		if (g_led_flash_alarm >= 0) {
			cancel_alarm(g_led_flash_alarm);
			g_led_flash_alarm = -1;
		}

		// Restore LED to original state
		led_sync(reg_get_value(REG_ID_LED));
	}
}
