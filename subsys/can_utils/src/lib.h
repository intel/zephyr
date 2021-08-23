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

#ifndef CAN_UTILS_LIB_H
#define CAN_UTILS_LIB_H

#include <stdio.h>
/* Enable pvt lib debug logs */
#ifdef CONFIG_LIB_DEBUG_ENABLE
#define PRINT LOG_DBG
#else
#define PRINT dummy_print
#endif

#define MAX_CAN_IFACE (2)

/* special address description flags for the CAN_ID */
#define CAN_EFF_FLAG 0x80000000U        /* EFF/SFF is set in the MSB */
#define CAN_RTR_FLAG 0x40000000U        /* remote transmission request */
#define CAN_ERR_FLAG 0x20000000U        /* error message frame */

/* valid bits in CAN ID for frame formats */
#define CAN_SFF_MASK 0x000007FFU        /* standard frame format (SFF) */
#define CAN_EFF_MASK 0x1FFFFFFFU        /* extended frame format (EFF) */
#define CAN_ERR_MASK 0x1FFFFFFFU        /* omit EFF, RTR, ERR flags */

/* CAN 2.0 payload length */
#define CAN_2_0_MAX_DLC    (8)
#define CAN_2_0_MAX_DLEN   (8)
/* CAN FD payload length */
#define CANFD_MAX_DLEN     (64)
#define CAN_MTU_BYTES   (CAN_2_0_MAX_DLEN) * 4
#define CANFD_MTU_BYTES (CANFD_MAX_DLEN) * 4

/*
 * Parse API parameters into options/data and fill can frame struct
 */
int parse_canframe(char *cs, struct can_frame *cf);
/*
 * Dummy print when debug is disabled
 */
#if !defined(CONFIG_LIB_DEBUG_ENABLE) || !defined(CONFIG_CAN_UTILS_DEBUG)
void dummy_print(char *ch, ...);
#endif  /* CONFIG_LIB_DEBUG_ENABLE || CONFIG_CAN_UTILS_DEBUG */

#endif  /* CAN_UTILS_LIB_H */

