/*
 * Copyright (c) 2019 Intel Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/* CANBUS related functions that are generic in all the drivers. */

#include <net/net_pkt.h>
#include <net/socket_can.h>

#ifndef ZEPHYR_DRIVERS_CAN_SOCKET_CAN_GENERIC_H_
#define ZEPHYR_DRIVERS_CAN_SOCKET_CAN_GENERIC_H_

#define SOCKET_CAN_NAME_0 "SOCKET_CAN_0"
#define SOCKET_CAN_NAME_1 "SOCKET_CAN_1"
#define SOCKET_CAN_NAME_2 "SOCKET_CAN_2"
#define SEND_TIMEOUT K_MSEC(100)
#define RX_THREAD_STACK_SIZE 512
#define RX_THREAD_PRIORITY 2
#define BUF_ALLOC_TIMEOUT K_MSEC(50)

/* TODO: make msgq size configurable */
CAN_DEFINE_MSGQ(socket_can_msgq, 5);
K_KERNEL_STACK_DEFINE(rx_thread_stack, RX_THREAD_STACK_SIZE);

struct socket_can_context {
	const struct device *can_dev;
	struct net_if *iface;
	struct k_msgq *msgq;
	can_tx_callback_t tx_cb;
	uint32_t err_mask;
	/* TODO: remove the thread and push data to net directly from rx isr */
	k_tid_t rx_tid;
	struct k_thread rx_thread_data;
};

static struct socket_can_context socket_can_context_0;
static struct socket_can_context socket_can_context_1;

static inline void socket_can_iface_init(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	struct socket_can_context *socket_context = dev->data;

	socket_context->iface = iface;

	LOG_DBG("Init CAN interface %p dev %p", iface, dev);
}

#ifdef CONFIG_NET_SOCKETS_CAN_ERR_FILTER
static bool is_err_masked(uint32_t error_flags, uint32_t err_mask)
{
	if ((error_flags == CAN_TX_ARB_LOST) && (err_mask & CAN_ERR_LOSTARB)) {
		return true;
	}

	if ((error_flags == CAN_TX_BUS_OFF) && (err_mask & CAN_ERR_BUSOFF)) {
		return true;
	}

	return false;
}
#else
static bool is_err_masked(uint32_t error_flags, uint32_t err_mask)
{
	return false;
}
#endif

static inline void tx_irq_callback_0(uint32_t error_flags, void *arg)
{
	bool mask_error = false;
	uint32_t err_mask = socket_can_context_0.err_mask;

	if (error_flags) {
		mask_error = is_err_masked(error_flags, err_mask);
		if (!mask_error) {
			LOG_DBG("CAN0 Callback! error-code: %d", error_flags);
		}
	}
}

static inline void tx_irq_callback_1(uint32_t error_flags, void *arg)
{
	bool mask_error = false;
	uint32_t err_mask = socket_can_context_1.err_mask;

	if (error_flags) {
		mask_error = is_err_masked(error_flags, err_mask);
		if (mask_error) {
			return;
		}
		LOG_DBG("CAN1 Callback! error-code: %d", error_flags);
	}
}

static inline void tx_irq_callback(uint32_t error_flags, void *arg)
{
	char *caller_str = (char *)arg;

	if (error_flags) {
		LOG_DBG("TX error from %s! error-code: %d",
			caller_str, error_flags);
	}
}

/* This is called by net_if.c when packet is about to be sent */
static inline int socket_can_send(const struct device *dev,
				  struct net_pkt *pkt)
{
	struct socket_can_context *socket_context = dev->data;
	int ret;

	if (net_pkt_family(pkt) != AF_CAN) {
		return -EPFNOSUPPORT;
	}

	ret = can_send(socket_context->can_dev,
		       (struct zcan_frame *)pkt->frags->data,
		       SEND_TIMEOUT, socket_context->tx_cb, "socket_can_send");
	if (ret) {
		LOG_DBG("Cannot send socket CAN msg (%d)", ret);
	}

	/* If something went wrong, then we need to return negative value to
	 * net_if.c:net_if_tx() so that the net_pkt will get released.
	 */
	return -ret;
}

static inline int socket_can_setsockopt(const struct device *dev, void *obj,
					int level, int optname,
					const void *optval, socklen_t optlen)
{
	struct socket_can_context *socket_context = dev->data;
	struct net_context *ctx = obj;
	uint32_t *err_mask_p;
	int ret = 0;

	if (level != SOL_CAN_RAW) {
		errno = EINVAL;
		return -1;
	}

	switch (optname) {
	case CAN_RAW_FILTER:
		__ASSERT(optlen == sizeof(struct zcan_filter),
			 "Filter size mismatch in setsockopt\n");
		if (optlen != sizeof(struct zcan_filter)) {
			LOG_ERR("Filter size mismatch in setsockopt");
			ret = -1;
			break;
		}
		if (optval == NULL) {
			errno = -EINVAL;
			ret = -1;
			break;
		}
		ret = can_attach_msgq(socket_context->can_dev,
				      socket_context->msgq,
				      optval);
		if (ret == CAN_NO_FREE_FILTER) {
			LOG_ERR("Failed to attach msgq");
			errno = ENOSPC;
		}
		break;

#ifdef CONFIG_NET_SOCKETS_CAN_FD
	case CAN_RAW_FD_FRAMES:
		if (optval == NULL) {
			errno = -EINVAL;
			LOG_ERR("Invalid FD optval");
			ret = -1;
			break;
		}

		if (optlen != sizeof(uint32_t)) {
			errno = -EINVAL;
			LOG_ERR("Invalid FD optlen");
			ret = -1;
			break;
		}

		ret = can_ioctl(socket_context->can_dev, CAN_IOCTL_FD_MODE,
				(void *)optval);
		if (ret != 0) {
			errno = -EINVAL;
			LOG_ERR("Failed to set can ioctl value");
		}
		break;
#endif

#ifdef CONFIG_NET_SOCKETS_CAN_ERR_FILTER
	case CAN_RAW_ERR_FILTER:
		if (optlen != sizeof(uint32_t)) {
			errno = -EINVAL;
			ret = -1;
			break;
		}

		if (optval == NULL) {
			errno = -EINVAL;
			ret = -1;
			break;
		}
		err_mask_p = (uint32_t *)(optval);

		if (*err_mask_p & (CAN_ERR_LOSTARB | CAN_ERR_BUSOFF)) {
			socket_context->err_mask = *(err_mask_p);
		} else {
			errno = -ENOTSUP;
			ret = -1;
		}
		break;
#endif

	default:
		LOG_ERR("Unsupported CAN socket option");
		ret = -1;
	}

	if (!ret) {
		net_context_set_filter_id(ctx, ret);
	}
	return ret;
}

static inline void socket_can_close(const struct device *dev, int filter_id)
{
	struct socket_can_context *socket_context = dev->data;

	can_detach(socket_context->can_dev, filter_id);
}

static struct canbus_api socket_can_api = {
	.iface_api.init = socket_can_iface_init,
	.send = socket_can_send,
	.close = socket_can_close,
	.setsockopt = socket_can_setsockopt,
};

static inline void rx_thread(void *ctx, void *unused1, void *unused2)
{
	struct socket_can_context *socket_context = ctx;
	struct net_pkt *pkt;
	struct zcan_frame msg;
	int ret;

	ARG_UNUSED(unused1);
	ARG_UNUSED(unused2);

	while (1) {
		k_msgq_get((struct k_msgq *)socket_context->msgq, &msg,
			   K_FOREVER);

		pkt = net_pkt_rx_alloc_with_buffer(socket_context->iface,
						   sizeof(msg),
						   AF_CAN, 0,
						   BUF_ALLOC_TIMEOUT);
		if (!pkt) {
			LOG_ERR("Failed to obtain RX buffer");
			continue;
		}

		if (net_pkt_write(pkt, (void *)&msg, sizeof(msg))) {
			LOG_ERR("Failed to append RX data");
			net_pkt_unref(pkt);
			continue;
		}

		ret = net_recv_data(socket_context->iface, pkt);
		if (ret < 0) {
			net_pkt_unref(pkt);
		}
	}
}

#endif /* ZEPHYR_DRIVERS_CAN_SOCKET_CAN_GENERIC_H_ */
