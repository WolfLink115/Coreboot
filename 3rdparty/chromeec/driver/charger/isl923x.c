/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Intersil ISL-9237/8 battery charger driver.
 */

#include "adc.h"
#include "battery.h"
#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "common.h"
#include "hooks.h"
#include "i2c.h"
#include "isl923x.h"
#include "timer.h"
#include "util.h"

#define DEFAULT_R_AC 20
#define DEFAULT_R_SNS 10
#define R_AC CONFIG_CHARGER_SENSE_RESISTOR_AC
#define R_SNS CONFIG_CHARGER_SENSE_RESISTOR
#define REG_TO_CURRENT(REG) ((REG) * DEFAULT_R_SNS / R_SNS)
#define CURRENT_TO_REG(CUR) ((CUR) * R_SNS / DEFAULT_R_SNS)
#define AC_REG_TO_CURRENT(REG) ((REG) * DEFAULT_R_AC / R_AC)
#define AC_CURRENT_TO_REG(CUR) ((CUR) * R_AC / DEFAULT_R_AC)

/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

static int learn_mode;

/* Charger parameters */
static const struct charger_info isl9237_charger_info = {
	.name         = CHARGER_NAME,
	.voltage_max  = CHARGE_V_MAX,
	.voltage_min  = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max  = REG_TO_CURRENT(CHARGE_I_MAX),
	.current_min  = REG_TO_CURRENT(CHARGE_I_MIN),
	.current_step = REG_TO_CURRENT(CHARGE_I_STEP),
	.input_current_max  = AC_REG_TO_CURRENT(INPUT_I_MAX),
	.input_current_min  = AC_REG_TO_CURRENT(INPUT_I_MIN),
	.input_current_step = AC_REG_TO_CURRENT(INPUT_I_STEP),
};

static inline int raw_read8(int offset, int *value)
{
	return i2c_read8(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, offset, value);
}

static inline int raw_read16(int offset, int *value)
{
	return i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, offset, value);
}

static inline int raw_write16(int offset, int value)
{
	return i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER, offset, value);
}

static int isl9237_set_current(uint16_t current)
{
	return raw_write16(ISL923X_REG_CHG_CURRENT, CURRENT_TO_REG(current));
}

static int isl9237_set_voltage(uint16_t voltage)
{
	return raw_write16(ISL923X_REG_SYS_VOLTAGE_MAX, voltage);
}

/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
	int rv;
	uint16_t reg = AC_CURRENT_TO_REG(input_current);

	rv = raw_write16(ISL923X_REG_ADAPTER_CURRENT1, reg);
	if (rv)
		return rv;

	return raw_write16(ISL923X_REG_ADAPTER_CURRENT2, reg);
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_ADAPTER_CURRENT1, &reg);
	if (rv)
		return rv;

	*input_current = AC_REG_TO_CURRENT(reg);
	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_MANUFACTURER_ID, &reg);
	if (rv)
		return rv;

	*id = reg;
	return EC_SUCCESS;
}

int charger_device_id(int *id)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_DEVICE_ID, &reg);
	if (rv)
		return rv;

	*id = reg;
	return EC_SUCCESS;
}

int charger_get_option(int *option)
{
	int rv;
	uint32_t controls;
	int reg;

	rv = raw_read8(ISL923X_REG_CONTROL0, &reg);
	if (rv)
		return rv;

	controls = reg;
	rv = raw_read16(ISL923X_REG_CONTROL1, &reg);
	if (rv)
		return rv;

	controls |= reg << 16;
	*option = controls;
	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	int rv;
	uint16_t reg;

	reg = option & 0xffff;
	rv = raw_write16(ISL923X_REG_CONTROL0, reg);

	if (rv)
		return rv;

	reg = (option >> 16) & 0xffff;
	return raw_write16(ISL923X_REG_CONTROL1, reg);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &isl9237_charger_info;
}

int charger_get_status(int *status)
{
	*status = CHARGER_LEVEL_2;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	int rv = EC_SUCCESS;

	/*
	 * See crosbug.com/p/51196.  Always disable learn mode unless it was set
	 * explicitly.
	 */
	if (!learn_mode)
		rv = charger_discharge_on_ac(0);

	/* ISL923X does not support inhibit mode setting. */
	return rv;
}

int charger_get_current(int *current)
{
	int rv;
	int reg;

	rv = raw_read16(ISL923X_REG_CHG_CURRENT, &reg);
	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg);
	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	return isl9237_set_current(current);
}

int charger_get_voltage(int *voltage)
{
	return raw_read16(ISL923X_REG_SYS_VOLTAGE_MAX, voltage);
}

int charger_set_voltage(int voltage)
{
	/* The ISL923X will drop voltage to as low as requested. As the
	 * charger state machine will pass in 0 voltage, protect the system
	 * voltage by capping to the minimum. The reason is that the ISL923X
	 * only can regulate the system voltage which will kill the board's
	 * power if below 0. */
	if (voltage == 0) {
		const struct battery_info *bi = battery_get_info();
		voltage = bi->voltage_min;
	}

	return isl9237_set_voltage(voltage);
}

int charger_post_init(void)
{
	int rv, reg;

#ifdef CONFIG_TRICKLE_CHARGING
	const struct battery_info *bi = battery_get_info();

	rv = raw_write16(ISL923X_REG_SYS_VOLTAGE_MIN, bi->voltage_min);
	if (rv)
		return rv;
#endif

	/*
	 * [10:9]: Prochot# Debounce time
	 *         11b: 1ms
	 */
	rv = raw_read16(ISL923X_REG_CONTROL2, &reg);
	if (rv)
		return rv;

	rv = raw_write16(ISL923X_REG_CONTROL2,
			reg |
			ISL923X_C2_PROCHOT_DEBOUNCE_1000 |
			ISL923X_C2_ADAPTER_DEBOUNCE_150);
	if (rv)
		return rv;

#ifdef CONFIG_CHARGE_RAMP_HW
#ifdef CONFIG_ISL9237
	rv = charger_get_option(&reg);
	if (rv)
		return rv;

	/* Set input voltage regulation reference voltage for charge ramp */
	reg &= ~ISL923X_C0_VREG_REF_MASK;
	reg |= ISL923X_C0_VREG_REF_4200;

	return charger_set_option(reg);
#else
	/* ISL9238 default input voltage regulation is 4096 mV. */
	return EC_SUCCESS;
#endif
#else
	rv = charger_get_option(&reg);
	if (rv)
		return rv;

	/* Disable voltage regulation loop to disable charge ramp */
	reg |= ISL923X_C0_DISABLE_VREG;

	return charger_set_option(reg);
#endif
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int control1;

	rv = raw_read16(ISL923X_REG_CONTROL1, &control1);
	if (rv)
		return rv;

	control1 &= ~ISL923X_C1_LEARN_MODE_AUTOEXIT;
	if (enable)
		control1 |= ISL923X_C1_LEARN_MODE_ENABLE;
	else
		control1 &= ~ISL923X_C1_LEARN_MODE_ENABLE;

	rv = raw_write16(ISL923X_REG_CONTROL1, control1);

	learn_mode = !rv && enable;
	return rv;
}

/*****************************************************************************/
/* Hardware current ramping */

#ifdef CONFIG_CHARGE_RAMP_HW
int charger_set_hw_ramp(int enable)
{
	int rv, reg;

	rv = charger_get_option(&reg);
	if (rv)
		return rv;

	/* HW ramp is controlled by input voltage regulation reference bits */
	if (enable)
		reg &= ~ISL923X_C0_DISABLE_VREG;
	else
		reg |= ISL923X_C0_DISABLE_VREG;

	return charger_set_option(reg);
}

int chg_ramp_is_stable(void)
{
	/*
	 * Since ISL cannot read the current limit that the ramp has settled
	 * on, then we can never consider the ramp stable, because we never
	 * know what the stable limit is.
	 */
	return 0;
}

int chg_ramp_is_detected(void)
{
	return 1;
}

int chg_ramp_get_current_limit(void)
{
	/*
	 * ISL doesn't have a way to get this info, so return the nominal
	 * current limit as an estimate.
	 */
	int input_current;

	if (charger_get_input_current(&input_current) != EC_SUCCESS)
		return 0;
	return input_current;
}
#endif /* CONFIG_CHARGE_RAMP_HW */


#ifdef CONFIG_CHARGER_PSYS
static void charger_enable_psys(void)
{
	int val;

	/*
	 * enable system power monitor PSYS function
	 */
	if (!raw_read16(ISL923X_REG_CONTROL1, &val)) {
		val |= ISL923X_C1_ENABLE_PSYS;
		raw_write16(ISL923X_REG_CONTROL1, val);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, charger_enable_psys, HOOK_PRIO_DEFAULT);

static void charger_disable_psys(void)
{
	int val;

	/*
	 * disable system power monitor PSYS function
	 */
	if (!raw_read16(ISL923X_REG_CONTROL1, &val)) {
		val &= ~ISL923X_C1_ENABLE_PSYS;
		raw_write16(ISL923X_REG_CONTROL1, val);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, charger_disable_psys, HOOK_PRIO_DEFAULT);

#ifdef CONFIG_CMD_CHARGER_PSYS
/* TODO(crosbug.com/p/61166): This dead code is ISL9237-specific. */
#define PSYS_ADC_READ_COUNT 100
static int charger_get_system_power(void)
{
	int adc = 0;
	int i;
	int ret;
	int val;

	ret = raw_read16(ISL923X_REG_CONTROL2, &val);
	if (ret)
		return ret;

	/* Read ADC */
	for (i = 0; i < PSYS_ADC_READ_COUNT; i++) {
		adc += adc_read_channel(ADC_PSYS);
		usleep(10);
	}

	/*
	 * Calculate the power in mW (Power = adc * gain)
	 *
	 * System power monitor PSYS output gain
	 * [0]: 0 = 1.44 uA/W
	 *      1 = 0.36 uA/W
	 *
	 * Do not divide the constants first to ensure precision is not lost.
	 */
	if (val & ISL923X_C2_PSYS_GAIN)
		return ((adc * ISL923X_C2_PSYS_GAIN_0_36) /
				PSYS_ADC_READ_COUNT);
	else
		return ((adc * ISL923X_C2_PSYS_GAIN_1_44) /
				PSYS_ADC_READ_COUNT);
}

static int console_command_psys(int argc, char **argv)
{
	CPRINTF("system power = %d mW\n", charger_get_system_power());
	return 0;
}
DECLARE_CONSOLE_COMMAND(psys, console_command_psys,
			NULL,
			"Get the system power in mW");
#endif /* CONFIG_CMD_CHARGER_PSYS */
#endif /* CONFIG_CHARGER_PSYS */

#ifdef CONFIG_CMD_CHARGER_ADC_AMON_BMON
enum amon_bmon { AMON, BMON };

static int print_amon_bmon(enum amon_bmon amon, int direction,
			   int resistor, char *text)
{
	int adc, curr, reg, ret;

#ifdef CONFIG_CHARGER_ISL9238
	ret = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
			ISL9238_REG_CONTROL3, &reg);
	if (ret)
		return ret;

	/* Switch direction */
	if (direction)
		reg |= ISL9238_C3_AMON_BMON_DIRECTION;
	else
		reg &= ~ISL9238_C3_AMON_BMON_DIRECTION;
	ret = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
			ISL9238_REG_CONTROL3, reg);
	if (ret)
		return ret;
#endif

	ret = i2c_read16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
			 ISL923X_REG_CONTROL1, &reg);
	if (ret)
		return ret;

	/* Switch between AMON/BMON */
	if (amon == AMON)
		reg &= ~ISL923X_C1_SELECT_BMON;
	else
		reg |= ISL923X_C1_SELECT_BMON;

	/* Enable monitor */
	reg &= ~ISL923X_C1_DISABLE_MON;
	ret = i2c_write16(I2C_PORT_CHARGER, I2C_ADDR_CHARGER,
			ISL923X_REG_CONTROL1, reg);
	if (ret)
		return ret;

	adc = adc_read_channel(ADC_AMON_BMON);
	curr = adc / resistor;
	CPRINTF("%s: %d uV, %d mA\n", text, adc, curr);

	return ret;
}

/**
 * Get charger AMON and BMON current.
 */
static int console_command_amon_bmon(int argc, char **argv)
{
	int ret = EC_SUCCESS;

	if (argc == 1 || (argc >= 2 && argv[1][0] == 'a'))
		ret |= print_amon_bmon(AMON, 0,
				       CONFIG_CHARGER_SENSE_RESISTOR_AC,
				       "AMON");

	if (argc == 1 || (argc >= 2 && argv[1][0] == 'b' &&
			 (argv[1][1] == '\0' || argv[1][1] == 'd')))
		ret |= print_amon_bmon(BMON, 1,
				       CONFIG_CHARGER_SENSE_RESISTOR,
				       "BMON(discharging)");

#ifdef CONFIG_CHARGER_ISL9238
	if (argc == 1 || (argc >= 2 && argv[1][0] == 'b' &&
				(argv[1][1] == '\0' || argv[1][1] == 'c')))
		ret |= print_amon_bmon(BMON, 0,
				       /*
					* charging current monitor has
					* 2x amplification factor
					*/
				       2*CONFIG_CHARGER_SENSE_RESISTOR,
				       "BMON(charging)");
#endif

	return ret;
}
DECLARE_CONSOLE_COMMAND(amonbmon, console_command_amon_bmon,
#ifdef CONFIG_CHARGER_ISL9237
			"amonbmon [a|b]",
#else
			"amonbmon [a|b[c|d]]",
#endif
			"Get charger AMON/BMON voltage diff, current");
#endif /* CONFIG_CMD_CHARGER_ADC_AMON_BMON */
