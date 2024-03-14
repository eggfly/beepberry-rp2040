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

#define LED_STATES_LEN 4
struct led_state g_led_states[LED_STATES_LEN];
uint32_t g_led_state_idx = 0;
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
	g_led_states[0].setting = LED_SET_OFF;
	g_led_state_idx = 0;
	led_sync(false, 0, 0, 0);
}


static void set_led_state(struct led_state const* state);
static void pop_led_state();
static void push_led_state(struct led_state const* state);

static int64_t pi_led_flash_alarm_callback(alarm_id_t _, void* __)
{
	static bool led_enabled = false;
	bool not_flashing;
	uint32_t alarm_ms;

	// Flash canceled
	not_flashing = (g_led_states[g_led_state_idx].setting != LED_SET_FLASH_ON)
		&& (g_led_states[g_led_state_idx].setting != LED_SET_FLASH_UNTIL_KEY);
	if ((g_led_flash_alarm < 0) || not_flashing) {
		g_led_flash_alarm = -1;
		return 0;
	}

	// Toggle LED
	led_enabled = !led_enabled;
	led_sync(led_enabled, g_led_states[g_led_state_idx].r, g_led_states[g_led_state_idx].g,  g_led_states[g_led_state_idx].b);

	// Reschedule timer
	alarm_ms = (led_enabled)
		? LED_FLASH_ON_MS
		: (LED_FLASH_CYCLE_MS - LED_FLASH_ON_MS);
	g_led_flash_alarm = add_alarm_in_ms(alarm_ms, pi_led_flash_alarm_callback, NULL, true);

	return 0;
}

static void pi_led_stop_flash_alarm_callback(uint8_t key, enum key_state state)
{
	// Don't restore if power key (simulated shutdown)
	if (key == KEY_POWER) {
		return;
	}

	// Restore original LED state
	if (g_led_states[g_led_state_idx].setting == LED_SET_FLASH_UNTIL_KEY) {
		pop_led_state();
	}

	// Remove key callback
	keyboard_remove_key_callback(pi_led_stop_flash_alarm_callback);
}
static struct key_callback pi_led_stop_flash_key_callback = { .func = pi_led_stop_flash_alarm_callback };

static void apply_led_state(struct led_state const* state)
{
	// Cancel any current flash timer
	if (g_led_flash_alarm > 0) {
		cancel_alarm(g_led_flash_alarm);
		g_led_flash_alarm = -1;
	}

	// Set new state
	led_sync((state->setting == LED_SET_ON),
		state->r, state->g, state->b);

	// Schedule flash callback
	if ((state->setting == LED_SET_FLASH_ON) || (state->setting == LED_SET_FLASH_UNTIL_KEY)) {

		if (g_led_flash_alarm < 0) {
			g_led_flash_alarm = add_alarm_in_ms(LED_FLASH_ON_MS, pi_led_flash_alarm_callback, NULL, true);
		}

		// Add key calback to disable flash when key is pressed
		if (state->setting == LED_SET_FLASH_UNTIL_KEY) {
			keyboard_add_key_callback(&pi_led_stop_flash_key_callback);
		}
	}
}

static void overwrite_led_state(struct led_state const* state)
{
	g_led_states[g_led_state_idx] = *state;
	apply_led_state(state);
}

static void pop_led_state()
{
	if (g_led_state_idx == 0) {
		return;
	}

	// Set new state
	g_led_state_idx--;
	apply_led_state(&g_led_states[g_led_state_idx]);
}

static void push_led_state(struct led_state const* state)
{
	if (g_led_state_idx == (LED_STATES_LEN - 1)) {
		return;
	}

	// Store new state
	g_led_states[g_led_state_idx + 1] = *state;
	g_led_state_idx++;

	// Apply new state
	apply_led_state(state);
}

void led_set(struct led_state const* state)
{
	struct led_state temp_state;

	// Replace temporary flash with on: push state
	if (g_led_states[g_led_state_idx].setting == LED_SET_FLASH_UNTIL_KEY) {

		// Replace base state with off, but reapply flash
		if (state->setting == LED_SET_OFF) {
			temp_state = g_led_states[g_led_state_idx];
			overwrite_led_state(state);
			push_led_state(&temp_state);
			apply_led_state(state);

		// Push new state
		} else {
			push_led_state(state);
		}

	// Add temporary flash
	} else if (state->setting == LED_SET_FLASH_UNTIL_KEY) {
		push_led_state(state);
	
	// No temporary flash, overwrite and apply setting
	} else {
		overwrite_led_state(state);
	}
}
