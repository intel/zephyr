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
#ifndef CANUTILSIF_H
#define CANUTILSIF_H
#include <stdarg.h>
/**
 * @brief can_send debug enable
 * Defines Enable DEBUG logs from canutils
 */

#ifdef CONFIG_CAN_UTILS_DEBUG
#ifndef CAN_UTILS_DBG_LOG
#define CAN_UTILS_DBG_LOG LOG_DBG
#endif/* CAN_UTILS_DBG_LOG */
#else
#define CAN_UTILS_DBG_LOG dummy_print
#endif/* CONFIG_CAN_UTILS_DEBUG */
/*
 * @brief can_send retval enum
 * Defines the return values of can_send
 */
enum retcode {
	CAN_UTILS_OK            = 0,
	CAN_UTILS_PARAM_ERR     = 1,
	CAN_UTILS_INVALID_ARG   = 2,
	CAN_UTILS_INT_ERROR     = 3
};

/*
 * @brief can_dump retval enum
 * Defines the return values of can_dump
 */
enum arg_return {
	SUCCESS_OP,
	BAD_PARAM,
	INVALID_OPTION,
	INTERNAL_MOD_ERR,
	INVALID_RETURN_ARG
};

/*
 * @brief CAN Interface
 * @defgroup can-utils-sample CAN Utils
 * @{
 */

/*
 * @brief Send a single CAN frame to CAN bus
 *
 * @param argc Count of variable arguments
 * @param argv filters and data. Refer eg:
 * @retval CAN_UTILS_OK on Success
 *		   < 0 on Errors
 * Usage:
 * CAN STD/EXTD frames
 * - string layout <can_id>#{R{len}|data}
 * - {data} has 1 to 8 hex-values that can (optionally) be separated by '.'
 * - {len} can take values from 1 to 8
 * - return value on successful parsing: CAN_MTU
 *
 * CAN FD frames
 * - string layout <can_id>##<flags>{data}
 * - <flags> a single ASCII Hex value (0 .. F) which defines canfd_frame.flags
 * - {data} has 0 to 64 hex-values that can (optionally) be separated by '.'
 * - return value on successful parsing: CANFD_MTU
 *
 * Return value on error: 0
 *
 * <can_id> can have 3 (standard frame format) or 8 (extended frame format)
 * hexadecimal chars
 * Examples:
 *
 * 123#R1 -> standard CAN-Id = 0x123, len = 0, RTR-frame
 *
 * 123#12 -> standard CAN-Id = 0x123, len = 1, data[0] = 0x12
 * 123#11.22.33.44.55.66.77.88 -> standard CAN-Id = 0x123, len = 8
 * CAN FD frames
 * 123##0112233 -> CAN FD frame standard CAN-Id = 0x123, flags = 0, len = 3
 * eg: cansend <paramcount> <canif_id> <can_msgid>#<canmsg>
 * eg: cansend 2 can0 001#1122334455667788
 * eg: cansend 2 can1 002##1122334455667788
 */
int cansend(int argc, char **argv);

/*
 * @brief Receive and bridge CAN frames to CAN IF
 *
 * @param num Count of variable arguments
 * @param ... filters and data. Refer eg:
 * @retval SUCCESS_OP on Success
 *		   < 0 on Errors
 * Usage:
 * candump : display and/or bridge CAN data to console
 *
 * Default behaviour: Waits to receive 1 CAN frames
 * in the specfied can interface. The num of frames
 * can be increased by -n <val> option
 *
 * Supported options list:
 * "candump usage..."
 * "-b=<canif> (bridge mode - send received frames to <canif>)"
 * "-n=<count> (end after receiption of <count> CAN frames
 * Default value = 1)")
 * "-e=<level> (Err frame level:
 *		0-Disable<Default>,
 *		1-ARB Enable,
 *		2-BUSOFF Enable
 *		3-ARB+BUSOFF Enable)")
 * "-r canid_start~canid_end:
 * Set filter for canids(in hex) rece[tion in range
 * canid_start<Default 0x1> to canid_end <Default 0x1>"
 * "-f=y/Y (Enable CAN FD reception & transmission
 * Default disabled)")
 * NB: Once FD Enabled,
 * all CAN frames on the IF are sent as FD frames
 * "-s=<level> (<level=1> Silent mode,
 * <level = 0> Console mode)"
 */
int candump(int num, ...);
/**
 * @}
 */
#endif/* CANUTILSIF_H */

