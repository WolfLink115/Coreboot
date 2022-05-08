/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Button module for Chrome EC */

#include "button.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "hooks.h"
#include "keyboard_protocol.h"
#include "led_common.h"
#include "power_button.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

/* Console output macro */
#define CPRINTS(format, args...) cprints(CC_SWITCH, format, ## args)

struct button_state_t {
	uint64_t debounce_time;
	int debounced_pressed;
};

static struct button_state_t __bss_slow state[CONFIG_BUTTON_COUNT];

static uint64_t __bss_slow next_deferred_time;

#ifdef CONFIG_CMD_BUTTON
static int siml_btn_presd;

static int simulated_button_pressed(void)
{
	static int button = 1;

	button = !button;
	return button;
}
#endif

/*
 * Whether a button is currently pressed.
 */
static int raw_button_pressed(const struct button_config *button)
{
	int raw_value =
#ifdef CONFIG_CMD_BUTTON
			siml_btn_presd ?
			simulated_button_pressed() :
#endif
			gpio_get_level(button->gpio);

	return button->flags & BUTTON_FLAG_ACTIVE_HIGH ?
				       raw_value : !raw_value;
}

#ifdef CONFIG_BUTTON_RECOVERY

#ifdef CONFIG_LED_COMMON
static void button_blink_hw_reinit_led(void)
{
	int led_state = LED_STATE_ON;
	timestamp_t deadline;
	timestamp_t now = get_time();

	/* Blink LED for 3 seconds. */
	deadline.val = now.val + (3 * SECOND);

	while (!timestamp_expired(deadline, &now)) {
		led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED, led_state);
		led_state = !led_state;
		watchdog_reload();
		msleep(100);
		now = get_time();
	}

	/* Reset LED to default state. */
	led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED, LED_STATE_RESET);
}
#endif

/*
 * Whether recovery button (or combination of equivalent buttons) is pressed
 */
static int is_recovery_button_pressed(void)
{
	int i;
	for (i = 0; i < recovery_buttons_count; i++) {
		if (!raw_button_pressed(recovery_buttons[i]))
			return 0;
	}
	return 1;
}

/*
 * If the EC is reset and recovery is requested, then check if HW_REINIT is
 * requested as well. Since the EC reset occurs after volup+voldn+power buttons
 * are held down for 10 seconds, check the state of these buttons for 20 more
 * seconds. If they are still held down all this time, then set host event to
 * indicate HW_REINIT is requested. Also, make sure watchdog is reloaded in
 * order to prevent watchdog from resetting the EC.
 */
static void button_check_hw_reinit_required(void)
{
	timestamp_t deadline;
	timestamp_t now = get_time();

	deadline.val = now.val + (20 * SECOND);

	CPRINTS("Checking for HW_REINIT request");

	while (!timestamp_expired(deadline, &now)) {
		if (!is_recovery_button_pressed() ||
		    !power_button_signal_asserted()) {
			CPRINTS("No HW_REINIT request");
			return;
		}
		now = get_time();
		watchdog_reload();
	}

	CPRINTS("HW_REINIT requested");
	host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY_HW_REINIT);

#ifdef CONFIG_LED_COMMON
	button_blink_hw_reinit_led();
#endif
}

static int is_recovery_boot(void)
{
	if (system_jumped_to_this_image())
		return 0;
	if (!(system_get_reset_flags() &
	    (RESET_FLAG_RESET_PIN | RESET_FLAG_POWER_ON)))
		return 0;
	if (!is_recovery_button_pressed())
		return 0;
	return 1;
}
#endif	/* CONFIG_BUTTON_RECOVERY */

/*
 * Button initialization.
 */
void button_init(void)
{
	int i;

	CPRINTS("init buttons");
	next_deferred_time = 0;
	for (i = 0; i < CONFIG_BUTTON_COUNT; i++) {
		state[i].debounced_pressed = raw_button_pressed(&buttons[i]);
		state[i].debounce_time = 0;
		gpio_enable_interrupt(buttons[i].gpio);
	}

#ifdef CONFIG_BUTTON_RECOVERY
	if (is_recovery_boot()) {
		host_set_single_event(EC_HOST_EVENT_KEYBOARD_RECOVERY);
		button_check_hw_reinit_required();
	}
#endif
}

/*
 * Handle debounced button changing state.
 */

static void button_change_deferred(void);
DECLARE_DEFERRED(button_change_deferred);

#ifdef CONFIG_EMULATED_SYSRQ
static void debug_mode_handle(void);
DECLARE_DEFERRED(debug_mode_handle);
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, debug_mode_handle, HOOK_PRIO_LAST);
#endif

static void button_change_deferred(void)
{
	int i;
	int new_pressed;
	uint64_t soonest_debounce_time = 0;
	uint64_t time_now = get_time().val;

	for (i = 0; i < CONFIG_BUTTON_COUNT; i++) {
		/* Skip this button if we are not waiting to debounce */
		if (state[i].debounce_time == 0)
			continue;

		if (state[i].debounce_time <= time_now) {
			/* Check if the state has changed */
			new_pressed = raw_button_pressed(&buttons[i]);
			if (state[i].debounced_pressed != new_pressed) {
				state[i].debounced_pressed = new_pressed;
#ifdef CONFIG_EMULATED_SYSRQ
				/*
				 * Calling deferred function for handling debug
				 * mode so that button change processing is not
				 * delayed.
				 */
				hook_call_deferred(&debug_mode_handle_data, 0);
#endif
				CPRINTS("Button '%s' was %s",
					buttons[i].name, new_pressed ?
					"pressed" : "released");
#if defined(HAS_TASK_KEYPROTO) || defined(CONFIG_KEYBOARD_PROTOCOL_MKBP)
				keyboard_update_button(buttons[i].type,
					new_pressed);
#endif
			}

			/* Clear the debounce time to stop checking it */
			state[i].debounce_time = 0;
		} else {
			/*
			 * Make sure the next deferred call happens on or before
			 * each button needs it.
			 */
			soonest_debounce_time = (soonest_debounce_time == 0) ?
				state[i].debounce_time :
				MIN(soonest_debounce_time,
				    state[i].debounce_time);
		}
	}

	if (soonest_debounce_time != 0) {
		next_deferred_time = soonest_debounce_time;
		hook_call_deferred(&button_change_deferred_data,
				   next_deferred_time - time_now);
	}
}

/*
 * Handle a button interrupt.
 */
void button_interrupt(enum gpio_signal signal)
{
	int i;
	uint64_t time_now = get_time().val;

	for (i = 0; i < CONFIG_BUTTON_COUNT; i++) {
		if (buttons[i].gpio != signal)
			continue;

		state[i].debounce_time = time_now + buttons[i].debounce_us;
		if (next_deferred_time <= time_now ||
		    next_deferred_time > state[i].debounce_time) {
			next_deferred_time = state[i].debounce_time;
			hook_call_deferred(&button_change_deferred_data,
					   next_deferred_time - time_now);
		}
		break;
	}
}

#ifdef CONFIG_CMD_BUTTON
static int button_present(enum keyboard_button_type type)
{
	int i;

	for (i = 0; i < CONFIG_BUTTON_COUNT; i++)
		if (buttons[i].type == type)
			break;

	return i;
}

static void button_interrupt_simulate(int button)
{
	button_interrupt(buttons[button].gpio);
	usleep(buttons[button].debounce_us >> 2);
	button_interrupt(buttons[button].gpio);
}

static int console_command_button(int argc, char **argv)
{
	int button;
	int press_ms = 50;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "vup"))
		button = button_present(KEYBOARD_BUTTON_VOLUME_UP);
	else if (!strcasecmp(argv[1], "vdown"))
		button = button_present(KEYBOARD_BUTTON_VOLUME_DOWN);
	else
		return EC_ERROR_PARAM1;

	if (button == CONFIG_BUTTON_COUNT)
		return EC_ERROR_PARAM1;

	if (argc > 2) {
		press_ms = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	siml_btn_presd = 1;

	/* Press the button */
	button_interrupt_simulate(button);

	/* Hold the button */
	msleep(press_ms);

	/* Release the button */
	button_interrupt_simulate(button);

	/* Wait till button processing is finished */
	msleep(100);

	siml_btn_presd = 0;

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(button, console_command_button,
			"vup|vdown msec",
			"Simulate button press");
#endif

#ifdef CONFIG_EMULATED_SYSRQ

enum debug_state {
	STATE_DEBUG_NONE,
	STATE_DEBUG_CHECK,
	STATE_STAGING,
	STATE_DEBUG_MODE_ACTIVE,
	STATE_SYSRQ_PATH,
	STATE_WARM_RESET_PATH,
	STATE_SYSRQ_EXEC,
	STATE_WARM_RESET_EXEC,
};

#define DEBUG_BTN_POWER         (1 << 0)
#define DEBUG_BTN_VOL_UP        (1 << 1)
#define DEBUG_BTN_VOL_DN        (1 << 2)
#define DEBUG_TIMEOUT           (10 * SECOND)

static enum debug_state curr_debug_state = STATE_DEBUG_NONE;
static enum debug_state next_debug_state = STATE_DEBUG_NONE;
static timestamp_t debug_state_deadline;
static int debug_button_hit_count;

static int debug_button_mask(void)
{
	int mask = 0;

	/* Get power button state */
	if (power_button_is_pressed())
		mask |= DEBUG_BTN_POWER;

	/* Get volume up state */
	if (state[BUTTON_VOLUME_UP].debounced_pressed)
		mask |= DEBUG_BTN_VOL_UP;

	/* Get volume down state */
	if (state[BUTTON_VOLUME_DOWN].debounced_pressed)
		mask |= DEBUG_BTN_VOL_DN;

	return mask;
}

static int debug_button_pressed(int mask)
{
	return debug_button_mask() == mask;
}

static int debug_mode_blink_led(void)
{
	return ((curr_debug_state != STATE_DEBUG_NONE) &&
		(curr_debug_state != STATE_DEBUG_CHECK));
}

static void debug_mode_transition(enum debug_state next_state)
{
	timestamp_t now = get_time();
#ifdef CONFIG_LED_COMMON
	int curr_blink_state = debug_mode_blink_led();
#endif

	/* Cancel any deferred calls. */
	hook_call_deferred(&debug_mode_handle_data, -1);

	/* Update current debug mode state. */
	curr_debug_state = next_state;

	/* Set deadline to 10seconds from current time. */
	debug_state_deadline.val = now.val + DEBUG_TIMEOUT;

	switch (curr_debug_state) {
	case STATE_DEBUG_NONE:
		/*
		 * Nothing is done here since some states can transition to
		 * STATE_DEBUG_NONE in this function. Wait until all other
		 * states are evaluated to take the action for STATE_NONE.
		 */
		break;
	case STATE_DEBUG_CHECK:
	case STATE_STAGING:
		/*
		 * Schedule a deferred call after DEBUG_TIMEOUT to check for
		 * button state if it does not change during the timeout
		 * duration.
		 */
		hook_call_deferred(&debug_mode_handle_data, DEBUG_TIMEOUT);
		break;
	case STATE_DEBUG_MODE_ACTIVE:
		debug_button_hit_count = 0;
		break;
	case STATE_SYSRQ_PATH:
		/*
		 * Increment debug_button_hit_count and ensure it does not go
		 * past 3. If it exceeds the limit transition to STATE_NONE.
		 */
		debug_button_hit_count++;
		if (debug_button_hit_count == 4)
			curr_debug_state = STATE_DEBUG_NONE;
		break;
	case STATE_WARM_RESET_PATH:
		break;
	case STATE_SYSRQ_EXEC:
		/*
		 * Depending upon debug_button_hit_count, send appropriate
		 * number of sysrq events to host and transition to STATE_NONE.
		 */
		while (debug_button_hit_count) {
			host_send_sysrq('x');
			CPRINTS("DEBUG MODE: sysrq-x sent");
			debug_button_hit_count--;
		}
		curr_debug_state = STATE_DEBUG_NONE;
		break;
	case STATE_WARM_RESET_EXEC:
		/* Warm reset the host and transition to STATE_NONE. */
		chipset_reset(0);
		CPRINTS("DEBUG MODE: Warm reset triggered");
		curr_debug_state = STATE_DEBUG_NONE;
		break;
	default:
		curr_debug_state = STATE_DEBUG_NONE;
	}

	if (curr_debug_state != STATE_DEBUG_NONE)
		return;

	/* If state machine reached initial state, reset all variables. */
	CPRINTS("DEBUG MODE: Exit!");
	next_debug_state = STATE_DEBUG_NONE;
	debug_state_deadline.val = 0;
	debug_button_hit_count = 0;
#ifdef CONFIG_LED_COMMON
	if (curr_blink_state)
		led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_RESET);
#endif
}

static void debug_mode_handle(void)
{
	int mask;

	switch (curr_debug_state) {
	case STATE_DEBUG_NONE:
		/*
		 * If user pressed Vup+Vdn, check for next 10 seconds to see if
		 * user keeps holding the keys.
		 */
		if (debug_button_pressed(DEBUG_BTN_VOL_UP | DEBUG_BTN_VOL_DN))
			debug_mode_transition(STATE_DEBUG_CHECK);
		break;
	case STATE_DEBUG_CHECK:
		/*
		 * If no key is pressed or any key combo other than Vup+Vdn is
		 * held, then quit debug check mode.
		 */
		if (!debug_button_pressed(DEBUG_BTN_VOL_UP | DEBUG_BTN_VOL_DN))
			debug_mode_transition(STATE_DEBUG_NONE);
		else if (timestamp_expired(debug_state_deadline, NULL)) {
			/*
			 * If Vup+Vdn are held down for 10 seconds, then its
			 * time to enter debug mode.
			 */
			CPRINTS("DEBUG MODE: Active!");
			next_debug_state = STATE_DEBUG_MODE_ACTIVE;
			debug_mode_transition(STATE_STAGING);
		}
		break;
	case STATE_STAGING:
		mask = debug_button_mask();

		/* If no button is pressed, transition to next state. */
		if (!mask) {
			debug_mode_transition(next_debug_state);
			return;
		}

		/* Exit debug mode if keys are stuck for > 10 seconds. */
		if (timestamp_expired(debug_state_deadline, NULL))
			debug_mode_transition(STATE_DEBUG_NONE);
		else {
			timestamp_t now = get_time();

			/*
			 * Schedule a deferred call in case timeout hasn't
			 * occurred yet.
			 */
			hook_call_deferred(&debug_mode_handle_data,
					(debug_state_deadline.val - now.val));
		}

		break;
	case STATE_DEBUG_MODE_ACTIVE:
		mask = debug_button_mask();

		/*
		 * Continue in this state if button is not pressed and timeout
		 * has not occurred.
		 */
		if (!mask && !timestamp_expired(debug_state_deadline, NULL))
			return;

		/* Exit debug mode if valid buttons are not pressed. */
		if ((mask != DEBUG_BTN_VOL_UP) && (mask != DEBUG_BTN_VOL_DN)) {
			debug_mode_transition(STATE_DEBUG_NONE);
			return;
		}

		/*
		 * Transition to STAGING state with next state set to:
		 * 1. SYSRQ_PATH     : If Vup was pressed.
		 * 2. WARM_RESET_PATH: If Vdn was pressed.
		 */
		if (mask == DEBUG_BTN_VOL_UP)
			next_debug_state = STATE_SYSRQ_PATH;
		else
			next_debug_state = STATE_WARM_RESET_PATH;

		debug_mode_transition(STATE_STAGING);
		break;
	case STATE_SYSRQ_PATH:
		mask = debug_button_mask();

		/*
		 * Continue in this state if button is not pressed and timeout
		 * has not occurred.
		 */
		if (!mask && !timestamp_expired(debug_state_deadline, NULL))
			return;

		/* Exit debug mode if valid buttons are not pressed. */
		if ((mask != DEBUG_BTN_VOL_UP) && (mask != DEBUG_BTN_VOL_DN)) {
			debug_mode_transition(STATE_DEBUG_NONE);
			return;
		}

		if (mask == DEBUG_BTN_VOL_UP) {
			/*
			 * Else transition to STAGING state with next state set
			 * to SYSRQ_PATH.
			 */
			next_debug_state = STATE_SYSRQ_PATH;
		} else {
			/*
			 * Else if Vdn is pressed, transition to STAGING with
			 * next state set to SYSRQ_EXEC.
			 */
			next_debug_state = STATE_SYSRQ_EXEC;
		}
		debug_mode_transition(STATE_STAGING);
		break;
	case STATE_WARM_RESET_PATH:
		mask = debug_button_mask();

		/*
		 * Continue in this state if button is not pressed and timeout
		 * has not occurred.
		 */
		if (!mask && !timestamp_expired(debug_state_deadline, NULL))
			return;

		/* Exit debug mode if valid buttons are not pressed. */
		if (mask != DEBUG_BTN_VOL_UP) {
			debug_mode_transition(STATE_DEBUG_NONE);
			return;
		}

		next_debug_state = STATE_WARM_RESET_EXEC;
		debug_mode_transition(STATE_STAGING);
		break;
	case STATE_SYSRQ_EXEC:
	case STATE_WARM_RESET_EXEC:
	default:
		debug_mode_transition(STATE_DEBUG_NONE);
		break;
	}
}

#ifdef CONFIG_LED_COMMON
static void debug_led_tick(void)
{
	static int led_state = LED_STATE_OFF;

	if (debug_mode_blink_led()) {
		led_state = !led_state;
		led_control(EC_LED_ID_SYSRQ_DEBUG_LED, led_state);
	}
}
DECLARE_HOOK(HOOK_TICK, debug_led_tick, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_LED_COMMON */

#endif /* CONFIG_EMULATED_SYSRQ */