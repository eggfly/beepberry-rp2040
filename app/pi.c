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

struct led_state g_led_state;
struct led_state g_led_flash_state;
alarm_id_t g_led_flash_alarm = -1;

enum pi_state
{
	PI_STATE_OFF = 0,
	PI_STATE_ON = 1,
};

static enum pi_state g_pi_state;

void pi_power_init(void)
{
	adc_init();
	adc_gpio_init(PIN_BAT_ADC);
	adc_select_input(0);

	gpio_init(PIN_PI_PWR);
	gpio_set_dir(PIN_PI_PWR, GPIO_OUT);
	gpio_put(PIN_PI_PWR, 0);
	g_pi_state = PI_STATE_OFF;
}

void pi_power_on(enum power_on_reason reason)
{
	struct led_state state;

	if (g_pi_state == PI_STATE_ON) {
		return;
	}

	gpio_put(PIN_PI_PWR, 1);
	g_pi_state = PI_STATE_ON;

	// LED green while booting until driver loaded
	state.setting = LED_SET_ON;
	state.r = 0;
	state.g = 128;
	state.b = 0;
	led_set(&state);

	// Update startup reason
	reg_set_value(REG_ID_STARTUP_REASON, reason);
}

void pi_power_off(void)
{
	if (g_pi_state == PI_STATE_OFF) {
		return;
	}

	gpio_put(PIN_PI_PWR, 0);
	g_pi_state = PI_STATE_OFF;
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
	if (g_power_off_alarm < 0) {
		return 0;
	}

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

static void led_sync(bool enable, uint8_t r, uint8_t g, uint8_t b)
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
	uint16_t pwm_r = (0xFF - r) * 0x101;
	uint16_t pwm_g = (0xFF - g) * 0x101;
	uint16_t pwm_b = (0xFF - b) * 0x101;

	// Set the PWM duty cycle for each channel
	pwm_set_gpio_level(PIN_LED_R, pwm_r);
	pwm_set_gpio_level(PIN_LED_G, pwm_g);
	pwm_set_gpio_level(PIN_LED_B, pwm_b);

	// Enable PWM channels
	pwm_set_enabled(slice_r, true);
	pwm_set_enabled(slice_g, true);
	pwm_set_enabled(slice_b, true);

}

void led_init(void)
{
	// Set up PWM channels
	gpio_set_function(PIN_LED_R, GPIO_FUNC_PWM);
	gpio_set_function(PIN_LED_G, GPIO_FUNC_PWM);
	gpio_set_function(PIN_LED_B, GPIO_FUNC_PWM);

	// Default off
	g_led_state.setting = LED_SET_OFF;
	g_led_flash_state.setting = LED_SET_OFF;
	led_sync(false, 0, 0, 0);
}

static int64_t pi_led_flash_alarm_callback(alarm_id_t _, void* __)
{
	static bool led_enabled = false;
	uint32_t alarm_ms;

	// Flash canceled
	if (g_led_flash_alarm < 0) {
		return 0;
	}

	// Toggle LED
	led_enabled = !led_enabled;
	if (g_led_flash_state.setting == LED_SET_FLASH_UNTIL_KEY) {

		// Switch between flash until key and base LED setting
		if (led_enabled) {
			led_sync(true, g_led_flash_state.r, g_led_flash_state.g,  g_led_flash_state.b);
		} else {
			led_sync((g_led_state.setting == LED_SET_ON), g_led_state.r, g_led_state.g,  g_led_state.b);
		}

	// Regular flash
	} else if (g_led_state.setting == LED_SET_FLASH_ON) {
		led_sync(led_enabled, g_led_state.r, g_led_state.g,  g_led_state.b);

	// Flash canceled
	} else {
		led_sync((g_led_state.setting == LED_SET_ON), g_led_state.r, g_led_state.g,  g_led_state.b);
		g_led_flash_alarm = -1;
		return 0;
	}

	// Reschedule timer
	alarm_ms = (led_enabled)
		? LED_FLASH_ON_MS
		: (LED_FLASH_CYCLE_MS - LED_FLASH_ON_MS);
	g_led_flash_alarm = add_alarm_in_ms(alarm_ms, pi_led_flash_alarm_callback, NULL, true);

	return 0;
}

static void pi_led_stop_flash_alarm_callback(uint8_t key, enum key_state state)
{
	// Don't restore if power key (sent during shutdown)
	if (key == KEY_POWER) {
		return;
	}

	// Restore original LED state
	g_led_flash_state.setting = LED_SET_OFF;

	// Remove key callback
	keyboard_remove_key_callback(pi_led_stop_flash_alarm_callback);
}
static struct key_callback pi_led_stop_flash_key_callback = {
	.func = pi_led_stop_flash_alarm_callback,
	.next = NULL
};

void led_set(struct led_state const* state)
{
	// Store LED state
	if (state->setting == LED_SET_FLASH_UNTIL_KEY) {
		g_led_flash_state = *state;
	} else {
		g_led_state = *state;
	}

	// Schedule flash callback
	if ((state->setting == LED_SET_FLASH_ON) || (state->setting == LED_SET_FLASH_UNTIL_KEY)) {

		// Apply LED setting and schedule new timer using callback
		if (g_led_flash_alarm < 0) {
			g_led_flash_alarm = 1;
			(void)pi_led_flash_alarm_callback(0, NULL);
		}

		// Add key calback to disable flash when key is pressed
		if (state->setting == LED_SET_FLASH_UNTIL_KEY) {
			keyboard_add_key_callback(&pi_led_stop_flash_key_callback);
		}

	// Regular on / off LED setting
	} else {
		led_sync((state->setting == LED_SET_ON), state->r, state->g,  state->b);
	}
}
