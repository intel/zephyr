/*
 * INTEL CONFIDENTIAL
 *
 * Copyright 2021 Intel Corporation
 *
 * This software and the related documents are Intel copyrighted materials,
 * and your use of them is governed by the express license under which they
 * were provided to you (License). Unless the License provides otherwise, you
 * may not use, modify, copy, publish, distribute, disclose or transmit this
 * software or the related documents without Intel's prior written permission.
 *
 * This software and the related documents are provided as is, with no express
 * or implied warranties, other than those that are expressly stated in the
 * License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr.h>

#include <net/socket.h>
#include <net/socket_can.h>

#include <drivers/can.h>
#include "lib.h"
#include "canutils_if.h"
#include <logging/log.h>
LOG_MODULE_REGISTER(can_utils_send, LOG_LEVEL_DBG);
#define CAN_0_ARRAY_INDEX (1)
#define CAN_1_ARRAY_INDEX (0)
#define MAX_CAN_IFACE (2)
/*Usage:
 * eg: cansend <canif_id> <can_msgid>#<canmsg>
 * eg: cansend: cansend can0 001#1122334455667788
 */
struct ud {
	struct net_if *can_iface_list[MAX_CAN_IFACE];
	uint8_t can_iface_count;
};

static void iface_cb(struct net_if *iface, void *user_data)
{
	struct ud *ud = user_data;

	if (net_if_l2(iface) == &NET_L2_GET_NAME(CANBUS_RAW)) {
		if (ud->can_iface_count < MAX_CAN_IFACE) {
			ud->can_iface_list[ud->can_iface_count++] = iface;
		}
	}
}

static void print_usage(void)
{
	LOG_ERR("------------------------------");
	LOG_ERR("Wrong CAN-frame format! Try:");
	LOG_ERR("    <can_id>#{data}            for STD CAN 2.0 data frames");
	LOG_ERR("    <can_id>#R{dlc}            for STD CAN 2.0 RTR frames");
	LOG_ERR("    <can_id>##<flags>{data}    for CAN FD frames\n");
	LOG_ERR("<can_id> can have 3 (STF) or 8 (EXT) hex chars");
	LOG_ERR("{data} has 0..8 (0..64 CAN FD) ASCII hex-values (optionally");
	LOG_ERR(" separated by '.')");
	LOG_ERR("{dlc} is length code 1..8 value for RTR frames");
	LOG_ERR("<flags> a single ASCII Hex value (0 .. F) for CANFD flags");
	LOG_ERR("e.g. 5A1#11.2233.44556677.88 / 123#DEADBEEF / 5AA# / ");
	LOG_ERR("123##1 / 213##311.22.33.44");
	LOG_ERR("123#R1");
	LOG_ERR("------------------------------");
}

int cansend(int param_count, char **argv)
{
	static struct ud can_bus;
	/* can raw socket */
	int s;
	int req_payload_size;
#ifdef CONFIG_NET_SOCKETS_CAN_FD
	int enable_canfd = 1;
#endif
	struct sockaddr_can addr;
	struct can_frame frame;
	int can_if_idx;
	int net_if_index = 0;

	/* check command line options */
	if (param_count != 2) {
		LOG_ERR("Usage: %s <device> <can_frame>.\n", argv[0]);
		return (-CAN_UTILS_PARAM_ERR);
	}
	CAN_UTILS_DBG_LOG("%s invoked %s %s %d\n", __func__, argv[0],
			  argv[1], strlen(argv[1]));
	/* parse CAN frame */
	req_payload_size = parse_canframe(argv[1], &frame);
	if (!req_payload_size) {
		print_usage();
		return -CAN_UTILS_INVALID_ARG;
	}
	LOG_DBG("CAN Frame parsed. MTU %d", req_payload_size);
	/* To get the interface idx used */
	can_bus.can_iface_count = 0;
	net_if_foreach(iface_cb, &can_bus);

	/* open socket */
	s = socket(AF_CAN, SOCK_RAW, CAN_RAW);
	if (s < 0) {
		LOG_ERR("Cannot create CAN socket (%d)", -errno);
		return -CAN_UTILS_INT_ERROR;
	}
	LOG_DBG("CAN_RAW socket create success\n");
	if (!strcmp(argv[0], "can0")) {
		net_if_index = CAN_0_ARRAY_INDEX;
	} else if (!strcmp(argv[0], "can1")) {
		net_if_index = CAN_1_ARRAY_INDEX;
	}

	can_if_idx = net_if_get_by_iface(can_bus.can_iface_list[net_if_index]);

	if (can_if_idx < 0) {
		LOG_ERR("Can test interface not found %d\n", net_if_index);
	} else {
		LOG_DBG("CAN if idx to send is %d\n", can_if_idx);
	}

	memset(&addr, 0, sizeof(addr));
	addr.can_family = PF_CAN;

	addr.can_ifindex = can_if_idx;

	if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("Cannot bind CAN socket (%d)\n", -errno);
		return -CAN_UTILS_INT_ERROR;
	}
	LOG_DBG("CAN sock bind success\n");

	if (req_payload_size > CAN_MTU_BYTES) {
#ifdef CONFIG_NET_SOCKETS_CAN_FD
		/* interface is ok - try to switch the socket into CAN FD mode
		 */
		if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_canfd,
			       sizeof(enable_canfd))) {
			LOG_ERR("error (%d) when enabling CAN FD support\n",
				-errno);
			return -CAN_UTILS_INT_ERROR;
		}
		LOG_DBG("CAN FD sock support EN success\n");
#else
		LOG_ERR("error CAN FD support not configured");
		return -CAN_UTILS_PARAM_ERR;
#endif          /* CONFIG_NET_SOCKETS_CAN_FD */
	}

	/* send frame */
	if (send(s, &frame, sizeof(frame), -1) < 0) {
		LOG_ERR("send error (%d)\n", -errno);
		return -CAN_UTILS_INT_ERROR;
	}
	LOG_DBG("CAN frame send success\n");
	close(s);
	return CAN_UTILS_OK;
}

