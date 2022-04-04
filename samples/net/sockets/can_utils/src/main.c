/*
 * Copyright (c) 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#include <zephyr.h>
#include <net/socket.h>
#include <net/socket_can.h>
#include <canutils_if.h>

/*
 * @brief can_baud_rate enum
 * List of supported bit rates of PSE CAN
 */
enum can_baud_rates_t {
	CAN_BAUDRATE_20_KBPS    = 20000U,
	CAN_BAUDRATE_50_KBPS    = 50000U,
	CAN_BAUDRATE_100_KBPS   = 100000U,
	CAN_BAUDRATE_125_KBPS   = 125000U,
	CAN_BAUDRATE_250_KBPS   = 250000U,
	CAN_BAUDRATE_500_KBPS   = 500000U,
	CAN_BAUDRATE_800_KBPS   = 800000U,
	CAN_BAUDRATE_1_MBPS     = 1000000U,
	CAN_BAUDRATE_2_MBPS     = 2000000U,
};

/*
 * CAN UTILS Test
 * Test set 1 --->cansend api
 * Test set 2---->candump api
 */
#define CANTEST 5
#define CAN_TEST_DEFAULT_DATA_RATE (500000)

void main(void)
{
	const struct device *can_dev = NULL;

	/* ----Test Set1-----*/
#if CANTEST < 5
	int count = 2;
	char *test_vect[2];
#endif

	can_dev = device_get_binding("CAN_SEDI_0");
	if (!can_dev) {
		printk("Failed to get CAN device 0\n");
		return;
	}

	can_set_mode(can_dev, CAN_NORMAL_MODE);
	can_set_bitrate(can_dev, CAN_BAUDRATE_500_KBPS, CAN_BAUDRATE_500_KBPS);

	can_dev = device_get_binding("CAN_SEDI_1");
	if (!can_dev) {
		printk("Failed to get CAN device 1\n");
		return;
	}

	can_set_mode(can_dev, CAN_NORMAL_MODE);
	can_set_bitrate(can_dev, CAN_BAUDRATE_500_KBPS, CAN_BAUDRATE_500_KBPS);

/* cansend test EXT frame */
#if CANTEST == 1
	test_vect[0] = "can0";
	test_vect[1] = "12345678#1122334455667788";
	printk("\nCANTEST1\n");
	cansend(count, (char **)&test_vect);
/* cansend test FD */
#elif CANTEST == 2
	test_vect[0] = "can0";
	/* <can_id>##<flags>{data} */
	test_vect[1] = "014##0.1122.33445566778811223344";
	printk("\nCANTEST2\n");
	cansend(count, (char **)&test_vect);
/* cansend test remote */
#elif CANTEST == 3
	test_vect[0] = "can0";
	test_vect[1] = "005#R2";
	printk("\nCANTEST3\n");
	cansend(count, (char **)&test_vect);
/* cansend test STD frame */
#elif CANTEST == 4
	test_vect[0] = "can0";
	test_vect[1] = "123#11223344";
	printk("\nCANTEST4\n");
	cansend(count, (char **)&test_vect);
/* ----Test Set2---- */
#elif CANTEST == 5
	printk("\nCANTEST5\n");
	/* candump test STD+EXT frames */
	candump(5, "can0", "-n=25", "-b=can1", "-r=2047~2052", "-f=Y");
#elif CANTEST == 6
	printk("\nCANTEST6\n");
	/* candump test error frame enable */
	candump(3, "can0", "-b=can1", "-e=2");
#elif CANTEST == 7
	printk("\nCANTEST7\n");
	/* EXT frames */
	candump(3, "can0", "-n=25", "-r=1048561~1048562");
#elif CANTEST == 8
	printk("\nCANTEST8\n");
	/* STD frames */
	candump(4, "can0", "-b=can1", "-n=25", "-r=1~10");
#else
	printk("Canutils: Unsupported test\n");
/* CANTEST */
#endif
}
