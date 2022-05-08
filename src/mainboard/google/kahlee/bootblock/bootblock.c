/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2017 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <baseboard/variants.h>
#include <bootblock_common.h>
#include <soc/southbridge.h>
#include <variant/ec.h>

void bootblock_mainboard_init(void)
{
	size_t num_gpios;
	const struct soc_amd_stoneyridge_gpio *gpios;
	gpios = variant_early_gpio_table(&num_gpios);
	sb_program_gpios(gpios, num_gpios);

	/* Enable the EC as soon as we have visibility */
	mainboard_ec_init();

	/* Setup TPM decode before verstage */
	sb_tpm_decode_spi();
}
