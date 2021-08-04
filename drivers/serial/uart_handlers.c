/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/uart.h>
#include <syscall_handler.h>

#define UART_SIMPLE(op_) \
	static inline int z_vrfy_uart_##op_(const struct device *dev) \
	{							\
		Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, op_)); \
		return z_impl_uart_ ## op_(dev); \
	}

#define UART_SIMPLE_VOID(op_) \
	static inline void z_vrfy_uart_##op_(const struct device *dev) \
	{							 \
		Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, op_)); \
		z_impl_uart_ ## op_(dev); \
	}

UART_SIMPLE(err_check)
#include <syscalls/uart_err_check_mrsh.c>

static inline int z_vrfy_uart_poll_in(const struct device *dev,
				      unsigned char *p_char)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, poll_in));
	Z_OOPS(Z_SYSCALL_MEMORY_WRITE(p_char, sizeof(unsigned char)));
	return z_impl_uart_poll_in(dev, p_char);
}
#include <syscalls/uart_poll_in_mrsh.c>

static inline void z_vrfy_uart_poll_out(const struct device *dev,
					unsigned char out_char)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, poll_out));
	z_impl_uart_poll_out((const struct device *)dev, out_char);
}
#include <syscalls/uart_poll_out_mrsh.c>

static inline int z_vrfy_uart_config_get(const struct device *dev,
					 struct uart_config *cfg)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, config_get));
	Z_OOPS(Z_SYSCALL_MEMORY_WRITE(cfg, sizeof(struct uart_config)));

	return z_impl_uart_config_get(dev, cfg);
}
#include <syscalls/uart_config_get_mrsh.c>

static inline int z_vrfy_uart_configure(const struct device *dev,
					const struct uart_config *cfg)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, config_get));
	Z_OOPS(Z_SYSCALL_MEMORY_READ(cfg, sizeof(struct uart_config)));

	return z_impl_uart_configure(dev, cfg);
}
#include <syscalls/uart_configure_mrsh.c>

#ifdef CONFIG_UART_ASYNC_API
/* callback_set() excluded as we don't allow ISR callback installation from
 * user mode
 *
 * rx_buf_rsp() excluded as it's designed to be called from ISR callbacks
 */

static inline int z_vrfy_uart_tx(const struct device *dev, const uint8_t *buf,
				 size_t len, int32_t timeout)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, tx));
	Z_OOPS(Z_SYSCALL_MEMORY_READ(buf, len));
	return z_impl_uart_tx(dev, buf, len, timeout);
}
#include <syscalls/uart_tx_mrsh.c>

UART_SIMPLE(tx_abort);
#include <syscalls/uart_tx_abort_mrsh.c>

static inline int z_vrfy_uart_rx_enable(const struct device *dev,
					uint8_t *buf,
					size_t len, int32_t timeout)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, rx_enable));
	Z_OOPS(Z_SYSCALL_MEMORY_WRITE(buf, len));
	return z_impl_uart_rx_enable(dev, buf, len, timeout);
}
#include <syscalls/uart_rx_enable_mrsh.c>

UART_SIMPLE(rx_disable);
#include <syscalls/uart_rx_disable_mrsh.c>
#endif /* CONFIG_UART_ASYNC_API */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
UART_SIMPLE_VOID(irq_tx_enable)
UART_SIMPLE_VOID(irq_tx_disable)
UART_SIMPLE_VOID(irq_rx_enable)
UART_SIMPLE_VOID(irq_rx_disable)
UART_SIMPLE_VOID(irq_err_enable)
UART_SIMPLE_VOID(irq_err_disable)
UART_SIMPLE(irq_is_pending)
UART_SIMPLE(irq_update)
#include <syscalls/uart_irq_tx_enable_mrsh.c>
#include <syscalls/uart_irq_tx_disable_mrsh.c>
#include <syscalls/uart_irq_rx_enable_mrsh.c>
#include <syscalls/uart_irq_rx_disable_mrsh.c>
#include <syscalls/uart_irq_err_enable_mrsh.c>
#include <syscalls/uart_irq_err_disable_mrsh.c>
#include <syscalls/uart_irq_is_pending_mrsh.c>
#include <syscalls/uart_irq_update_mrsh.c>
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

#ifdef CONFIG_UART_LINE_CTRL
static inline int z_vrfy_uart_line_ctrl_set(const struct device *dev,
					    uint32_t ctrl, uint32_t val)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, line_ctrl_set));
	return z_impl_uart_line_ctrl_set((const struct device *)dev, ctrl,
					 val);
}
#include <syscalls/uart_line_ctrl_set_mrsh.c>

static inline int z_vrfy_uart_line_ctrl_get(const struct device *dev,
					    uint32_t ctrl, uint32_t *val)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, line_ctrl_get));
	Z_OOPS(Z_SYSCALL_MEMORY_WRITE(val, sizeof(uint32_t)));
	return z_impl_uart_line_ctrl_get((const struct device *)dev, ctrl,
					 (uint32_t *)val);
}
#include <syscalls/uart_line_ctrl_get_mrsh.c>
#endif /* CONFIG_UART_LINE_CTRL */

#ifdef CONFIG_UART_DRV_CMD
static inline int z_vrfy_uart_drv_cmd(const struct device *dev, uint32_t cmd,
				      uint32_t p)
{
	Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, drv_cmd));
	return z_impl_uart_drv_cmd((const struct device *)dev, cmd, p);
}
#include <syscalls/uart_drv_cmd_mrsh.c>
#endif /* CONFIG_UART_DRV_CMD */

static inline int z_vrfy_uart_read_buffer_polled(const struct device *dev, uint8_t *buff,
                                                 int32_t len, uint32_t *status)
{
        Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, read_buffer_polled));
        Z_OOPS(Z_SYSCALL_MEMORY_WRITE(buff, len));
        return z_impl_uart_read_buffer_polled((const struct device *)dev,
                                              (uint8_t *)buff, (int32_t)len, (uint32_t *)status);
}
#include <syscalls/uart_read_buffer_polled_mrsh.c>

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
static inline int z_vrfy_uart_enable_unsol_receive(const struct device *dev,
                                                   uint8_t *buff, int32_t size, uart_unsol_rx_cb_t cb,
                                                   void *usr_param)
{
        Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, enable_unsol_receive));
        Z_OOPS(Z_SYSCALL_MEMORY_READ(buff, (int32_t)size));
        Z_OOPS(Z_SYSCALL_VERIFY_MSG(cb == NULL,
                                    "Callbacks forbidden from user mode"));
        return z_impl_uart_enable_unsol_receive((const struct device *)dev,
                                                (uint8_t *)buff, (int32_t)size, (uart_unsol_rx_cb_t)cb,
                                                (void *)usr_param);
}
#include <syscalls/uart_enable_unsol_receive_mrsh.c>

static inline int z_vrfy_uart_disable_unsol_receive(const struct device *dev)
{
        Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, disable_unsol_receive));
        return z_impl_uart_disable_unsol_receive((const struct device *)dev);
}
#include <syscalls/uart_disable_unsol_receive_mrsh.c>

static inline int z_vrfy_uart_get_unsol_data(const struct device *dev,
                                             uint8_t *buff, int32_t len)
{
        Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, get_unsol_data));
        Z_OOPS(Z_SYSCALL_MEMORY_WRITE(buff, len));
        return z_impl_uart_get_unsol_data((const struct device *)dev,
                                          (uint8_t *)buff, (int32_t)len);
}
#include <syscalls/uart_get_unsol_data_mrsh.c>

static inline int z_vrfy_uart_get_unsol_data_len(const struct device *dev,
                                                 int32_t *p_len)
{
        Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, get_unsol_data_len));
        Z_OOPS(Z_SYSCALL_MEMORY_WRITE(p_len, sizeof(int32_t)));
        return z_impl_uart_get_unsol_data_len((const struct device *)dev,
                                              (int32_t *)p_len);
}
#include <syscalls/uart_get_unsol_data_len_mrsh.c>
#endif

#ifdef CONFIG_UART_RS_485
static inline int z_vrfy_uart_rs_485_config_set(const struct device *dev,
                                                struct uart_rs_485_config *config)
{
        Z_OOPS(Z_SYSCALL_DRIVER_UART(dev, rs_485_config_set));
        Z_OOPS(Z_SYSCALL_MEMORY_READ(config,
                                     sizeof(struct uart_rs_485_config)));
        return z_impl_uart_rs_485_config_set((const struct device *)dev,
                                             (struct uart_rs_485_config *)config);
}
#include <syscalls/uart_rs_485_config_set_mrsh.c>
#endif
