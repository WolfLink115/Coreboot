/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tasks for scheduling test.
 */

#include "common.h"
#include "cts_common.h"
#include "task.h"
#include "timer.h"

static int repeat_count;
static int wake_count[3];

void clear_state(void)
{
	wake_count[0] = wake_count[1] = wake_count[2] = 0;
}

void task_abc(void *data)
{
	int task_id = task_get_current();
	int id = task_id - TASK_ID_A;
	task_id_t next = task_id + 1;

	if (next > TASK_ID_C)
		next = TASK_ID_A;

	task_wait_event(-1);

	CPRINTS("%c Starting", 'A' + id);
	cflush();

	while (1) {
		wake_count[id]++;
		if (id == 2 && wake_count[id] == repeat_count)
			task_set_event(TASK_ID_CTS, TASK_EVENT_WAKE, 1);
		else
			task_set_event(next, TASK_EVENT_WAKE, 1);
	}
}

void task_tick(void *data)
{
	task_wait_event(-1);
	ccprintf("\n[starting Task T]\n");

	/* Wake up every tick */
	while (1)
		/* Wait for timer interrupt message */
		usleep(3000);
}

enum cts_rc test_task_switch(void)
{
	uint32_t event;

	repeat_count = 3000;

	task_wake(TASK_ID_A);
	event = task_wait_event(5 * SECOND);

	if (event != TASK_EVENT_WAKE) {
		CPRINTS("Woken up by unexpected event: 0x%08x", event);
		return CTS_RC_FAILURE;
	}

	if (wake_count[0] != repeat_count || wake_count[1] != repeat_count) {
		CPRINTS("Unexpected counter values: %d %d %d",
			wake_count[0], wake_count[1], wake_count[2]);
		return CTS_RC_FAILURE;
	}

	/* TODO: Verify no tasks are ready, no events are pending. */
	if (*task_get_event_bitmap(TASK_ID_A)
			|| *task_get_event_bitmap(TASK_ID_B)
			|| *task_get_event_bitmap(TASK_ID_C)) {
		CPRINTS("Events are pending");
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

enum cts_rc test_task_priority(void)
{
	uint32_t event;

	repeat_count = 2;

	task_wake(TASK_ID_A);
	task_wake(TASK_ID_C);

	event = task_wait_event(5 * SECOND);

	if (event != TASK_EVENT_WAKE) {
		CPRINTS("Woken up by unexpected event: 0x%08x", event);
		return CTS_RC_FAILURE;
	}

	if (wake_count[0] != repeat_count - 1
			|| wake_count[1] != repeat_count - 1) {
		CPRINTS("Unexpected counter values: %d %d %d",
			wake_count[0], wake_count[1], wake_count[2]);
		return CTS_RC_FAILURE;
	}

	/* TODO: Verify no tasks are ready, no events are pending. */
	if (*task_get_event_bitmap(TASK_ID_A)
			|| *task_get_event_bitmap(TASK_ID_B)
			|| *task_get_event_bitmap(TASK_ID_C)) {
		CPRINTS("Events are pending");
		return CTS_RC_FAILURE;
	}

	return CTS_RC_SUCCESS;
}

#include "cts_testlist.h"

void cts_task(void)
{
	enum cts_rc rc;
	int i;

	task_wake(TASK_ID_TICK);

	for (i = 0; i < CTS_TEST_ID_COUNT; i++) {
		clear_state();
		rc = tests[i].run();
		CPRINTF("\n%s %d\n", tests[i].name, rc);
		cflush();
	}

	CPRINTS("Task test suite finished");
	cflush();

	/* Sleep forever */
	task_wait_event(-1);
}
