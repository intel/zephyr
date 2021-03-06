/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

struct bt_conn {
	u16_t handle;
	u8_t role;
	atomic_t ref;

	bt_addr_le_t dst;

	bt_security_t sec_level;
	bt_security_t required_sec_level;

	u16_t interval;
	u16_t latency;
	u16_t timeout;

	enum {
		BT_CONN_DISCONNECTED,
		BT_CONN_CONNECT,
		BT_CONN_CONNECTED,
		BT_CONN_DISCONNECT,
	} state;

	/* Delayed work used to update connection parameters */
	struct k_delayed_work update_work;

	void *gatt_private;
	struct k_sem gatt_notif_sem;
};
