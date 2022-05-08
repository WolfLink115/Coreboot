/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Commons acc/gyro function for ST sensors oin Chrome EC */

#ifndef __CROS_EC_ST_COMMONS_H
#define __CROS_EC_ST_COMMONS_H

#include "common.h"
#include "util.h"
#include "accelgyro.h"
#include "console.h"
#include "i2c.h"

/* Common debug funcions */
#define CPRINTF(format, args...) cprintf(CC_ACCEL, format "\n", ## args)

/* X, Y, Z axis data len */
#define OUT_XYZ_SIZE			6

#ifdef CONFIG_ACCEL_FIFO
#define FIFO_BUFFER_NUM_PATTERN		16
/* Define number of data to be read from FIFO each time
 * It must be a multiple of OUT_XYZ_SIZE.
 * In case of LSM6DSM FIFO contains pattern depending ODR
 * of Acc/gyro, be sure that FIFO can contains almost
 * FIFO_BUFFER_NUM_PATTERNpattern
 */
#define FIFO_READ_LEN			(FIFO_BUFFER_NUM_PATTERN * OUT_XYZ_SIZE)

#endif /* CONFIG_ACCEL_FIFO */

/**
 * Read single register
 */
extern inline int raw_read8(const int port, const int addr, const int reg,
		     int *data_ptr);

/**
 * Write single register
 */
extern inline int raw_write8(const int port, const int addr, const int reg,
		      int data);

/**
 * Read n bytes for read
 */
int st_raw_read_n(const int port, const int addr, const uint8_t reg,
	       uint8_t *data_ptr, const int len);

 /**
 * write_data_with_mask - Write register with mask
 * @s: Motion sensor pointer
 * @reg: Device register
 * @mask: The mask to search
 * @data: Data pointer
 */
int st_write_data_with_mask(const struct motion_sensor_t *s, int reg,
			 uint8_t mask, uint8_t data);

 /**
 * set_resolution - Set bit resolution
 * @s: Motion sensor pointer
 * @res: Bit resolution
 * @rnd: Round bit
 */
int st_set_resolution(const struct motion_sensor_t *s, int res, int rnd);

 /**
 * get_resolution - Get bit resolution
 * @s: Motion sensor pointer
 *
 * TODO: must support multiple resolution
 */
int st_get_resolution(const struct motion_sensor_t *s);

/**
 * set_offset - Set data offset
 * @s: Motion sensor pointer
 * @offset: offset vector
 * @temp: Temp
 */
int st_set_offset(const struct motion_sensor_t *s,
		  const int16_t *offset, int16_t temp);

/**
 * get_offset - Get data offset
 * @s: Motion sensor pointer
 * @offset: offset vector
 * @temp: Temp
 */
int st_get_offset(const struct motion_sensor_t *s,
		  int16_t *offset, int16_t *temp);

/**
 * get_data_rate - Get data rate (ODR)
 * @s: Motion sensor pointer
 */
int st_get_data_rate(const struct motion_sensor_t *s);

/**
 * normalize - Apply to LSB data sensitivity and rotation
 * @s: Motion sensor pointer
 * @v: vector
 * @data: LSB raw data
 */
void st_normalize(const struct motion_sensor_t *s, vector_3_t v, uint8_t *data);

/* Internal data structure for sensors */
struct stprivate_data {
	struct accelgyro_saved_data_t base;
	int16_t offset[3];
	uint8_t resol;
};

#endif /* __CROS_EC_ST_COMMONS_H */
