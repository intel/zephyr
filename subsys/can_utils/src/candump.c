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
#include <zephyr.h>
#include <string.h>
#include <net/socket.h>
#include <net/socket_can.h>
#include "canutils_if.h"
#include <logging/log.h>
#if !defined(CONFIG_CAN_UTILS_DEBUG)
#include "lib.h"
#endif
LOG_MODULE_REGISTER(can_utils_dump, LOG_LEVEL_DBG);

#define PRIORITY 7
#define STACKSIZE 750
#define SLEEP_PERIOD K_SECONDS(1)

/* CAN test interface starting from 1 to indicate the can interface to be
 * tested.
 */
#define CAN_INTERFACE0 (0)
#define CAN_INTERFACE1 (1)
#define CAN_0_INDEX (1)
#define CAN_1_INDEX (0)
#define MAX_CAN_IFACE (2)
#define MAX_CANIF_NSIZE (7)
/* CAN msgq can hold max CAN_MSGQ_MAX_DEPTH frames */
#define CAN_MSGQ_MAX_DEPTH (2)
/* CAN STD 11 bit identifier*/
#define CAN_STD_ID_MAX (2047)
#define CAN_EXT_ID_DEFAULT (0xFFFF0)
#define CAN_EXT_ID_MAX (0x1FFFFFFF)
#define CAN_ID_MAX_LEN (10)

/**
 * @brief can_dump error frame enum
 * Defines enums for candump err fr enable
 * Not to be exposed to interface layer
 */
enum can_err_frame_level {
	NO_ERR_FRAME,
	ARB_LOST_FRAME_EN,
	BUS_OFF_EN,
	ARB_AND_BUSOFF_EN,
	CAN_ERR_FRAME_INVALID
};

static k_tid_t tx_tid;
static K_THREAD_STACK_DEFINE(tx_stack, STACKSIZE);
static struct k_thread tx_data;
static bool tx_thread_exit;
static uint32_t canid_range_start = 1;          /* RX filter CAN id accept start */
static uint32_t canid_range_end = 1;            /* RX filter CAN id accept end */
static bool bridge_mode_en_b;                   /* Enable bridge. 1 En 0 Dis(def)*/
static uint32_t can_frame_rx_count_u32 = 1;     /* Wait for 1 frame */
static bool silent_mode_en_b;                   /* 1 - Silent,  0 - Print(default) */
static uint32_t can_err_frame_level;            /* 0 Disable,  1 ARB 2 BUSOFF 3 1+2 */
static bool can_fd_frame_en;                    /* can_err_frame_level */
static int tx_fd;                               /* TX socket */
static int rx_fd;                               /* RX socket */
static char tx_canif_name[MAX_CANIF_NSIZE];     /* Name of TX can if */
static char rx_canif_name[MAX_CANIF_NSIZE];     /* Name of RX can if */
/* Setup message queue for bridge mode */
K_MSGQ_DEFINE(can_msgq, sizeof(struct zcan_frame), CAN_MSGQ_MAX_DEPTH, 4);

static bool is_canfilter_limit_valid(int canifd, int filter_count)
{
	bool ret = true;

	switch (canifd) {
	case CAN_INTERFACE0: {
		if (filter_count > (CONFIG_CAN0_STD_FILTER_COUNT +
				    CONFIG_CAN0_EXT_FILTER_COUNT)) {
			ret = false;
		}
		break;
	}
	case CAN_INTERFACE1: {
		if (filter_count > (CONFIG_CAN1_STD_FILTER_COUNT +
				    CONFIG_CAN1_EXT_FILTER_COUNT)) {
			ret = false;
		}
		break;
	}
	default:
		ret = false;
	}
	return ret;
}

static void print_usage(void)
{
	LOG_ERR("\n\ncandump usage...\n");
	LOG_ERR("-b=<canif> (bridge mode - send received frames to <canif>)");
	LOG_ERR("-n=<count> (end after receiption of <count> CAN frames."
		"Default value = 1)");
	LOG_ERR("-e=<level> (Err frame level:"
		"0-Disable<Default>,"
		"1-ARB Enable,"
		"2-BUSOFF Enable,"
		"3-ARB+BUSOFF Enable)");
	LOG_ERR("-r canid_start~canid_end:"
		"Set filter for canids in range "
		"canid_start<Def 1> to canid_end <Def 1>");
	LOG_ERR("-f=y/Y (Enable CAN FD reception & transmission."
		"Default disabled)");
	LOG_ERR("-s=<level> (<level=1> Silent mode,"
		"<level = 0> Console mode)");
	LOG_ERR("\n");
}

static int parse_can_rx_ids(char *ch)
{
	int ret;
	char *delim;
	int count;
	char buf[CAN_ID_MAX_LEN];

	/* replace with safe func later */
	memset(buf, 0, sizeof(buf));

	if (ch != NULL && (strstr(ch, "~") != NULL)) {
		CAN_UTILS_DBG_LOG("\nParse can id for %s\n", ch);
		delim = strstr(ch, "~");
		/* Skip the equals also */
		count = delim - ch + 1;
		if (count < CAN_ID_MAX_LEN) {
			strncpy(buf, ch, sizeof(buf) - 1);
		} else {
			return -INVALID_OPTION;
		}
		canid_range_start = atoi(buf);
		ch = ch + count;
		memset(buf, 0, sizeof(buf));

		if (strlen(ch) < CAN_ID_MAX_LEN) {
			strncpy(buf, ch, sizeof(buf) - 1);
		} else {
			return -INVALID_OPTION;
		}
		canid_range_end = atoi(buf);

		if (canid_range_start > canid_range_end) {
			ret = -INVALID_OPTION;
		}

		if (canid_range_end > CAN_EXT_ID_MAX) {
			ret = -INVALID_OPTION;
		}

		if (canid_range_end == 0 || canid_range_start == 0) {
			ret = -INVALID_OPTION;
		} else {
			LOG_DBG("\nCAN filter range %d ~ %d\n",
				canid_range_start,
				canid_range_end);
			ret = SUCCESS_OP;
		}
	} else {
		ret = -INVALID_OPTION;
	}
	return ret;
}

static int parse_args(char *opts)
{
	char *temp = opts;
	char count_ch[32];
	int ret = SUCCESS_OP;

	if (opts == NULL) {
		return (-BAD_PARAM);
	}
	CAN_UTILS_DBG_LOG("\n%s enter %s\n", __func__, opts);
	if (rx_canif_name[0] == '\0') {
		if (!strncmp(opts, "can0", MAX_CANIF_NSIZE)) {
			LOG_DBG("\nReq for receive on CAN0\n");
			strncpy(rx_canif_name, "can0", sizeof("can0"));
		} else if (!strncmp(opts, "can1", MAX_CANIF_NSIZE)) {
			LOG_DBG("\nReq for receive on CAN1\n");
			strncpy(rx_canif_name, "can1", sizeof("can1"));
		} else {
			ret = -BAD_PARAM;
		}
	}
	if (ret == SUCCESS_OP) {
		while ((temp != NULL) && (*temp) == '-' &&
		       ((*temp != '\0') || (*(temp + 1) != '\0'))) {
			if ((temp + 1) != NULL) {
				temp++;
			}
			switch (*temp) {
			case 'b': {
				bridge_mode_en_b = true;
				/* Skip the = or space */
				temp = temp + 2;
				if (!strncmp(temp, "can0", MAX_CANIF_NSIZE)) {
					strncpy(tx_canif_name, "can0",
						sizeof("can0"));
					LOG_DBG("\nBridge req to %s\n",
						tx_canif_name);
					temp += MAX_CANIF_NSIZE;
				} else if (!strncmp(temp, "can1",
						    MAX_CANIF_NSIZE)) {
					strncpy(tx_canif_name, "can1",
						sizeof("can1"));
					LOG_DBG("\nBridge req to %s\n",
						tx_canif_name);
					temp += MAX_CANIF_NSIZE;
				} else {
					ret = -BAD_PARAM;
				}
				break;
			}
			case 'n': {
				/* Skip the = or space */
				temp = temp + 2;

				if (strlen(temp) < sizeof(count_ch)) {
					strncpy(count_ch, temp,
						sizeof(count_ch) - 1);
				} else {
					return -BAD_PARAM;
				}
				can_frame_rx_count_u32 = atoi(count_ch);
				temp += sizeof(count_ch);
				LOG_DBG("\nCAN receive count set to %d\n",
					can_frame_rx_count_u32);
				break;
			}
			case 'r': {
				/* Skip the = or space */
				temp = temp + 2;
				ret = parse_can_rx_ids(temp);
				break;
			}
			case 's': {
				/* Skip the = or space */
				temp = temp + 2;

				if (strlen(temp) < sizeof(count_ch)) {
					strncpy(count_ch, temp,
						sizeof(count_ch) - 1);
				} else {
					return -BAD_PARAM;
				}
				if (strncmp(count_ch, "1", strlen(temp))) {
					silent_mode_en_b = true;
				}
				temp += sizeof(count_ch);
				break;
			}
			case 'e': {
				/* Skip the = or space */
				temp = temp + 2;

				if (strlen(temp) < sizeof(count_ch)) {
					strncpy(count_ch, temp,
						sizeof(count_ch) - 1);
				} else {
					return -BAD_PARAM;
				}
				can_err_frame_level = atoi(count_ch);
				temp += sizeof(count_ch);
				LOG_DBG("\nCAN err frame level set to %d\n",
					can_err_frame_level);
				break;
			}
			case 'f': {
				/* Skip the = or space */
				temp = temp + 2;
				if (!strncmp(temp, "y", sizeof("y")) ||
				    !strncmp(temp, "Y", sizeof("Y"))) {
					can_fd_frame_en = true;
				}
				LOG_DBG("\nCAN FD frame enabled\n");
				break;
			}
			default:
				LOG_ERR("\nInvalid option %s\n", temp);
				ret = -INVALID_OPTION;
			}
		}
	}
	CAN_UTILS_DBG_LOG("\n%s completed %d\n", __func__, ret);
	return ret;
}

static void tx(int *can_fd)
{
	int fd = POINTER_TO_INT(can_fd);
	struct zcan_frame msg;
	struct can_frame frame = { 0 };
	int ret;

	while (!tx_thread_exit) {
		if (k_msgq_get(&can_msgq, &msg, K_FOREVER) == 0) {
			can_copy_zframe_to_frame(&msg, &frame);
			ret = send(fd, &frame, sizeof(frame), -1);
			if (ret < 0) {
				LOG_ERR("\nCannot send CAN message (%d)\n",
					-errno);
			} else {
				LOG_DBG("\nSend CAN data success\n");
			}
		} else {
			LOG_ERR("TX tid: Msgq get failed %d", -errno);
		}
	}
}

static void rx(int *can_fd)
{
	int fd = POINTER_TO_INT(can_fd);
	struct sockaddr_can can_addr;
	socklen_t addr_len;
	struct zcan_frame msg;
	struct can_frame frame;
	int ret;
	int retry_count;

	LOG_DBG("\nWaiting CAN data...\n");

	while (can_frame_rx_count_u32) {
		uint8_t *data;

		memset(&frame, 0, sizeof(frame));
		addr_len = sizeof(can_addr);
		ret = recvfrom(fd, &frame, sizeof(struct can_frame), 0,
			       (struct sockaddr *)&can_addr, &addr_len);
		if (ret < 0) {
			LOG_ERR("Cannot receive CAN message (%d)", ret);
			continue;
		}

		can_copy_frame_to_zframe(&frame, &msg);
		/* Immediately send to the TX thread */
		if (bridge_mode_en_b) {
			/* send data to TX */
			retry_count = 5;
			while ((k_msgq_put(&can_msgq, &msg, K_NO_WAIT) != 0) &&
			       (retry_count != 0)) {
				/* message queue is full: purge old data & try
				 * again
				 */
				LOG_ERR("\nQueue full. Puge older msgs!\n");
				k_msgq_purge(&can_msgq);
				retry_count--;
			}
			if (retry_count == 0) {
				LOG_ERR("\nRX->TX msg send failed. Skipping\n");
			} else {
				LOG_DBG("\nRX->TX msg send success\n");
			}
		}
		if (!silent_mode_en_b) {
			LOG_INF("CAN msg: |type| 0x%x |RTR| 0x%x |DLC| 0x%x",
				msg.id_type, msg.rtr, msg.dlc);

			if (!msg.rtr) {
				if (msg.dlc > 8) {
					data = (uint8_t *)msg.data_32;
				} else {
					data = msg.data;
				}
				/* Avoid LOG newline using printk */
				printk("Data(hex):");
				for (int i = 0; i < msg.dlc; i++) {
					printk(" %x ", data[i]);
				}
				printk("\n");
			}
			can_frame_rx_count_u32--;
		} else {
			LOG_INF("EXT Remote message received");
		}
	}
}

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

static struct ud can_bus;


static int setup_socket(char *can_if_name, bool setup_filter)
{
	struct zcan_filter zfilter = { .id_type = CAN_STANDARD_IDENTIFIER,
				       .rtr = CAN_DATAFRAME,
				       .id = canid_range_start,
				       .rtr_mask = 1,
				       .id_mask = CAN_STD_ID_MASK };
	struct zcan_filter zfilter_extd = {
		.id_type = CAN_EXTENDED_IDENTIFIER,
		.rtr = CAN_DATAFRAME,
		.id = CAN_EXT_ID_DEFAULT,
		.rtr_mask = 1,
		.id_mask = CAN_EXT_ID_MASK
	};
	struct can_filter filter, filter_extd;
	struct sockaddr_can can_addr;
	int fd;
	int ret;
	int can_if_idx = -1;
	int can_if_setup;
	bool limit_valid;
	uint32_t filter_count;

	can_bus.can_iface_count = 0;
	net_if_foreach(iface_cb, &can_bus);

	if (can_bus.can_iface_count == 0) {
		LOG_ERR("No CAN device found");
		return -EPERM;
	}

	fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
	if (fd < 0) {
		LOG_ERR("Cannot create CAN socket (%d)", -errno);
		return -errno;
	}
	LOG_DBG("CAN RAW sock setup success");

	if (can_if_name == NULL) {
		LOG_ERR("CAN IF name is NULL");
		ret = -EPERM;
		goto cleanup;
	}
	if (!strncmp(can_if_name, "can0", sizeof("can0"))) {
		can_if_idx =
			net_if_get_by_iface(can_bus.can_iface_list[CAN_0_INDEX]);
		can_if_setup = CAN_INTERFACE0;
		CAN_UTILS_DBG_LOG("can0 under test");
	} else if (!strncmp(can_if_name, "can1", sizeof("can1"))) {
		can_if_idx =
			net_if_get_by_iface(can_bus.can_iface_list[CAN_1_INDEX]);
		can_if_setup = CAN_INTERFACE1;
		CAN_UTILS_DBG_LOG("can1 under test");
	} else {
		ret = -INVALID_OPTION;
		goto cleanup;
	}
	if (can_if_idx < 0) {
		LOG_ERR("Can test interface %s not found", can_if_name);
		ret = -EPERM;
		goto cleanup;
	}
	can_addr.can_ifindex = can_if_idx;
	can_addr.can_family = AF_CAN;

	ret = bind(fd, (struct sockaddr *)&can_addr, sizeof(can_addr));
	if (ret < 0) {
		ret = -errno;
		LOG_ERR("Cannot bind CAN socket (%d)", ret);
		goto cleanup;
	} else {
		LOG_DBG("Bind socket to IF (%d)success", can_addr.can_ifindex);
	}

	limit_valid = is_canfilter_limit_valid(can_if_setup,
					       canid_range_end -
					       canid_range_start);
	if (limit_valid && setup_filter) {
		/* Apply the filters for the rage of ids specified */
		for (filter_count = canid_range_start;
		     filter_count <= canid_range_end; filter_count++) {
			/* Apply STD or Extended filter based on can id range */
			if (filter_count <= CAN_STD_ID_MAX) {
				zfilter.id = filter_count;
				/* There is a pre-processing done before
				 * applying the
				 * can id to filter,  so there is no way to
				 * avoid the copy.
				 * There is no penalty as only 2 u32 fields are
				 * copied.
				 */
				can_copy_zfilter_to_filter(&zfilter, &filter);
				ret =
					setsockopt(fd,
						   SOL_CAN_RAW,
						   CAN_RAW_FILTER,
						   &filter,
						   sizeof(filter));
				if (ret < 0) {
					ret = -errno;
					LOG_ERR("Fail set CAN sockopt (%d) "
						"canid %d",
						ret, filter_count);
					goto cleanup;
				} else {
					LOG_DBG(
						"CAN STD filter 0x%x setup success",
						filter.can_id);
				}
			} else {
				/* There is a pre-processing done before
				 * applying the
				 * can id to filter,  so there is no way to
				 * avoid the copy.
				 * There is no penalty as only 2 u32 fields are
				 * copied.
				 */
				zfilter_extd.id = filter_count;
				can_copy_zfilter_to_filter(&zfilter_extd,
							   &filter_extd);
				ret = setsockopt(fd, SOL_CAN_RAW,
						 CAN_RAW_FILTER, &filter_extd,
						 sizeof(filter_extd));
				if (ret < 0) {
					ret = -errno;
					LOG_ERR("Fail set CAN sockopt (%d) "
						"canid %d",
						ret, filter_count);
					goto cleanup;
				} else {
					LOG_DBG("CAN EXTD filter 0x%x setup "
						"success",
						filter_extd.can_id);
				}
			}
		}
	}
#ifdef CONFIG_NET_SOCKETS_CAN_FD
	if (can_fd_frame_en == true) {
		int enable_fd = true;

		ret = setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd,
				 sizeof(enable_fd));
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("Cannot set CAN sockopt (%d)", ret);
			goto cleanup;
		} else {
			LOG_DBG("CAN FD sock setup success");
		}
	}
#endif

#ifdef CONFIG_NET_SOCKETS_CAN_ERR_FILTER
	if (can_err_frame_level > 0) {
		uint32_t err_mask;

		if (can_err_frame_level == ARB_LOST_FRAME_EN ||
		    can_err_frame_level == ARB_AND_BUSOFF_EN) {
			err_mask = CAN_ERR_LOSTARB;
			ret = setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
					 &err_mask, sizeof(err_mask));
		} else if (can_err_frame_level == BUS_OFF_EN ||
			   can_err_frame_level == ARB_AND_BUSOFF_EN) {
			err_mask = CAN_ERR_BUSOFF;
			ret = setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
					 &err_mask, sizeof(err_mask));
		}
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("Cannot set CAN sockopt (%d)", ret);
			goto cleanup;
		} else {
			LOG_DBG("CAN err filter setup success");
		}
	}
#endif

	return fd;

cleanup:
	(void)close(fd);
	return ret;
}

int candump(int num, ...)
{
	int ret = SUCCESS_OP;
	va_list args;

	va_start(args, num);
	for (int i = 0; i < num; i++) {
		char *ch = (char *)va_arg(args, uint32_t);

		LOG_DBG("%s\n", ch);
		if (parse_args(ch) < 0) {
			LOG_ERR("Parse args failed");
			ret = -BAD_PARAM;
		} else {
			LOG_DBG("Parse args success");
		}
	}
	va_end(args);
	if (ret < 0) {
		print_usage();
		goto err;
	}

	LOG_DBG("\nSetup Rx on IF %s\n", rx_canif_name);

	rx_fd = setup_socket(rx_canif_name, true);
	if (rx_fd < 0) {
		LOG_ERR("Cannot start CAN Rx socket (%d)", rx_fd);
		ret = -INTERNAL_MOD_ERR;
		goto cleanup;
	}
	/* CAN dump funcionality impl */
	if (bridge_mode_en_b == true) {
		LOG_DBG("Setup Tx on IF %s", tx_canif_name);
		tx_fd = setup_socket(tx_canif_name, false);
		if (tx_fd < 0) {
			LOG_ERR("Cannot start CAN Tx socket (%d)", tx_fd);
			ret = -INTERNAL_MOD_ERR;
			goto err;
		}
		/* create tx thread for bridge mode */
		tx_tid = k_thread_create(
			&tx_data, tx_stack, K_THREAD_STACK_SIZEOF(tx_stack),
			(k_thread_entry_t)tx, INT_TO_POINTER(tx_fd), NULL, NULL,
			PRIORITY, 0, K_NO_WAIT);
		if (!tx_tid) {
			ret = -ENOENT;
			errno = -ret;
			LOG_ERR("Cannot create TX thread!");
			goto err;
		} else {
			LOG_DBG("Started socket CAN TX thread");
		}
	}
	rx(INT_TO_POINTER(rx_fd));
	LOG_DBG("%s completed with return %d", __func__, ret);

/* Close the sockets */
cleanup:
	if (bridge_mode_en_b == true) {
		close(tx_fd);
	}
	if (ret != -INTERNAL_MOD_ERR) {
		close(rx_fd);
	}

err:
	return ret;
}

