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

enum led_setting g_led_setting = LED_SET_OFF;
bool g_led_flash_until_key = false;
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
	led_set(LED_SET_ON);

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
	pi_cancel_power_alarms();
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

	led_set(LED_SET_OFF);
}

static void led_sync(bool enable)
{
	if (!enable) {
		pwm_set_gpio_level(PIN_LED_R, 0xFFFF);
		pwm_set_gpio_level(PIN_LED_G, 0xFFFF);
		pwm_set_gpio_level(PIN_LED_B, 0xFFFF);
		return;
	}

	// Set the PWM slice for each channel
	uint slice_r = pwm_gpio_to_slice_num(PIN_LED_R);
	uint slice_g = pwm_gpio_to_slice_num(PIN_LED_G);
	uint slice_b = pwm_gpio_to_slice_num(PIN_LED_B);

	// Calculate the PWM value for each channel
	uint16_t pwm_r = (0xFF - reg_get_value(REG_ID_LED_R)) * 0x101;
	uint16_t pwm_g = (0xFF - reg_get_value(REG_ID_LED_G)) * 0x101;
	uint16_t pwm_b = (0xFF - reg_get_value(REG_ID_LED_B)) * 0x101;

	// Set the PWM duty cycle for each channel
	pwm_set_gpio_level(PIN_LED_R, pwm_r);
	pwm_set_gpio_level(PIN_LED_G, pwm_g);
	pwm_set_gpio_level(PIN_LED_B, pwm_b);

	// Enable PWM channels
	pwm_set_enabled(slice_r, true);
	pwm_set_enabled(slice_g, true);
	pwm_set_enabled(slice_b, true);

}

static int64_t pi_led_flash_alarm_callback(alarm_id_t _, void* __)
{
	static bool led_enabled = false;
	uint32_t alarm_ms;

	// Exit if flash canceled
	if (!g_led_flash_until_key && (g_led_setting != LED_SET_FLASH_ON)) {
		g_led_flash_alarm = -1;
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

static void pi_led_stop_flash_alarm_callback(uint8_t key, enum key_state state)
{
	// Restore original LED state
	if (g_led_flash_until_key) {
		g_led_flash_until_key = false;
		led_set(g_led_setting);
	}

	// Remove key callback
	keyboard_remove_key_callback(pi_led_stop_flash_alarm_callback);
}
static struct key_callback pi_led_stop_flash_key_callback = { .func = pi_led_stop_flash_alarm_callback };

void led_set(enum led_setting setting)
{
	// Update LED setting
	if (setting == LED_SET_FLASH_UNTIL_KEY) {
		g_led_flash_until_key = true;
	} else {
		g_led_setting = setting;
	}

	// Turn off
	if (setting == LED_SET_OFF) {
		led_sync(false);

	// Turn on
	} else if (setting == LED_SET_ON) {
		led_sync(true);

	// Schedule flash callback
	} else if ((setting == LED_SET_FLASH_ON) || (setting == LED_SET_FLASH_UNTIL_KEY)) {

		led_sync(false);

		if (g_led_flash_alarm < 0) {
			g_led_flash_alarm = add_alarm_in_ms(LED_FLASH_ON_MS, pi_led_flash_alarm_callback, NULL, true);
		}

		// Add key calback to disable flash when key is pressed
		if (setting == LED_SET_FLASH_UNTIL_KEY) {
			keyboard_add_key_callback(&pi_led_stop_flash_key_callback);
		}
	}
}
