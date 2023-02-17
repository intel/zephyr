/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT microchip_xec_peci

#include <errno.h>
#include <device.h>
#include <drivers/peci.h>
#include <soc.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(peci_mchp_xec, CONFIG_PECI_LOG_LEVEL);

/* Maximum PECI core clock is the main clock 48Mhz */
#define MAX_PECI_CORE_CLOCK 48000u
/* 1 ms */
#define PECI_RESET_DELAY    1000u
#define PECI_RESET_DELAY_MS 1u
/* 100 us */
#define PECI_IDLE_DELAY     100u
/* 5 ms */
#define PECI_IDLE_TIMEOUT   50u
/* Maximum retries */
#define PECI_TIMEOUT_RETRIES 3u
/* Maximum read buffer fill wait retries */
#define PECI_RX_BUF_FILL_WAIT_RETRY 100u

/* 10 us */
#define PECI_IO_DELAY       10

#define OPT_BIT_TIME_MSB_OFS 8u

#define PECI_FCS_LEN         2

struct peci_xec_config {
	PECI_Type *base;
	uint8_t irq_num;
};

struct peci_xec_data {
	struct k_sem tx_lock;
	uint32_t  bitrate;
	int    timeout_retries;
};

static struct peci_xec_data peci_data;

static const struct peci_xec_config peci_xec_config = {
	.base = (PECI_Type *) DT_INST_REG_ADDR(0),
	.irq_num = DT_INST_IRQN(0),
};

static int check_bus_idle(PECI_Type *base)
{
	uint8_t delay_cnt = PECI_IDLE_TIMEOUT;

	/* Wait until PECI bus becomes idle.
	 * Note that when IDLE bit in the status register changes, HW do not
	 * generate an interrupt, so need to poll.
	 */
	while (!(base->STATUS2 & MCHP_PECI_STS2_IDLE)) {
		k_busy_wait(PECI_IDLE_DELAY);
		delay_cnt--;

		if (!delay_cnt) {
			LOG_WRN("Bus is busy");
			return -EBUSY;
		}
	}
	return 0;
}

static int peci_xec_configure(const struct device *dev, uint32_t bitrate)
{
	ARG_UNUSED(dev);

	peci_data.bitrate = bitrate;
	PECI_Type *base = peci_xec_config.base;
	uint16_t value;

	/* Power down PECI interface */
	base->CONTROL = MCHP_PECI_CTRL_PD;

	/* Adjust bitrate */
	value = MAX_PECI_CORE_CLOCK / bitrate;
	base->OPT_BIT_TIME_LSB = value & MCHP_PECI_OPT_BT_LSB_MASK;
	base->OPT_BIT_TIME_MSB = (value >> OPT_BIT_TIME_MSB_OFS) &
				 MCHP_PECI_OPT_BT_MSB_MASK;

	/* Power up PECI interface */
	base->CONTROL &= ~MCHP_PECI_CTRL_PD;

	return 0;
}

static int peci_xec_disable(const struct device *dev)
{
	ARG_UNUSED(dev);
	int ret;
	PECI_Type *base = peci_xec_config.base;

	/* Make sure no transaction is interrupted before disabling the HW */
	ret = check_bus_idle(base);
	if (ret) {
		return ret;
	}

#ifdef CONFIG_PECI_INTERRUPT_DRIVEN
	NVIC_ClearPendingIRQ(peci_xec_config.irq_num);
	irq_disable(peci_xec_config.irq_num);
#endif
	base->CONTROL |= MCHP_PECI_CTRL_PD;

	return 0;
}

static int peci_xec_enable(const struct device *dev)
{
	ARG_UNUSED(dev);
	PECI_Type *base = peci_xec_config.base;

	base->CONTROL &= ~MCHP_PECI_CTRL_PD;

#ifdef CONFIG_PECI_INTERRUPT_DRIVEN
	irq_enable(peci_xec_config.irq_num);
#endif
	return 0;
}

static void peci_xec_bus_recovery(const struct device *dev, bool full_reset)
{
	PECI_Type *base = peci_xec_config.base;

	LOG_WRN("%s full_reset:%d", __func__, full_reset);
	if (full_reset) {
		base->CONTROL = MCHP_PECI_CTRL_PD | MCHP_PECI_CTRL_RST;

		if (k_is_in_isr()) {
			k_msleep(PECI_RESET_DELAY_MS);
		} else {
			k_busy_wait(PECI_RESET_DELAY);
		}

		base->CONTROL &= ~MCHP_PECI_CTRL_RST;

		peci_xec_configure(dev, peci_data.bitrate);
	} else {
		/* Only reset internal FIFOs */
		base->CONTROL |= MCHP_PECI_CTRL_FRST;
	}
}

static int peci_xec_write(const struct device *dev, struct peci_msg *msg)
{
	ARG_UNUSED(dev);
	int i;
	int ret;

	struct peci_buf *tx_buf = &msg->tx_buffer;
	struct peci_buf *rx_buf = &msg->rx_buffer;
	PECI_Type *base = peci_xec_config.base;

	/* Check if FIFO is full */
	if (base->STATUS2 & MCHP_PECI_STS2_WFF) {
		LOG_WRN("%s FIFO is full", __func__);
		return -EIO;
	}

	base->CONTROL &= ~MCHP_PECI_CTRL_FRST;

	/* Add PECI transaction header to TX FIFO */
	base->WR_DATA = msg->addr;
	base->WR_DATA = tx_buf->len;
	base->WR_DATA = rx_buf->len;

	/* Add PECI payload to Tx FIFO only if write length is valid */
	if (tx_buf->len) {
		base->WR_DATA = msg->cmd_code;
		for (i = 0; i < tx_buf->len - 1; i++) {
			if (!(base->STATUS2 & MCHP_PECI_STS2_WFF)) {
				base->WR_DATA = tx_buf->buf[i];
			}
		}
	}

	/* Check bus is idle before starting a new transfer */
	ret = check_bus_idle(base);
	if (ret) {
		return ret;
	}

	base->CONTROL |= MCHP_PECI_CTRL_TXEN;
	k_busy_wait(PECI_IO_DELAY);

	/* Wait for transmission to complete */
#ifdef CONFIG_PECI_INTERRUPT_DRIVEN
	if (k_sem_take(&peci_data.tx_lock, PECI_IO_DELAY * tx_buf->len)) {
		return -ETIMEDOUT;
	}
#else
	/* In worst case, overall timeout will be 1msec (100 * 10usec) */
	uint8_t wait_timeout_cnt = 100;

	while (!(base->STATUS1 & MCHP_PECI_STS1_EOF)) {
		k_busy_wait(PECI_IO_DELAY);
		wait_timeout_cnt--;
		if (!wait_timeout_cnt) {
			LOG_WRN("Tx timeout");
			peci_data.timeout_retries++;
			/* Full reset only if multiple consecutive failures */
			if (peci_data.timeout_retries > PECI_TIMEOUT_RETRIES) {
				peci_xec_bus_recovery(dev, true);
			} else {
				peci_xec_bus_recovery(dev, false);
			}

			return -ETIMEDOUT;
		}
	}
#endif
	peci_data.timeout_retries = 0;

	return 0;
}

static int peci_xec_read(const struct device *dev, struct peci_msg *msg)
{
	ARG_UNUSED(dev);
	int i;
	int ret;
	uint8_t tx_fcs;
	uint8_t bytes_rcvd;
	uint8_t wait_timeout_cnt;
	struct peci_buf *rx_buf = &msg->rx_buffer;
	PECI_Type *base = peci_xec_config.base;

	/* Attempt to read data from RX FIFO */
	bytes_rcvd = 0;
	for (i = 0; i < (rx_buf->len + PECI_FCS_LEN); i++) {
		/* Worst case timeout will be 1msec (100 * 10usec) */
		wait_timeout_cnt = PECI_RX_BUF_FILL_WAIT_RETRY;
		/* Wait for read buffer to fill up */
		while (base->STATUS2 & MCHP_PECI_STS2_RFE) {
			k_usleep(PECI_IO_DELAY);
			wait_timeout_cnt--;
			if (!wait_timeout_cnt) {
				LOG_WRN("Rx buffer empty");
				return -ETIMEDOUT;
			}
		}

		if (i == 0) {
			/* Get write block FCS just for debug */
			tx_fcs = base->RD_DATA;
			LOG_DBG("TX FCS %x", tx_fcs);
		} else if (i == (rx_buf->len + 1)) {
			/* Get read block FCS, but don't count it */
			rx_buf->buf[i-1] = base->RD_DATA;
		} else {
			/* Get response */
			rx_buf->buf[i-1] = base->RD_DATA;
			bytes_rcvd++;
		}
	}

	/* Check if transaction is as expected */
	if (rx_buf->len != bytes_rcvd) {
		LOG_INF("Incomplete %x vs %x", bytes_rcvd, rx_buf->len);
	}

	/* Once write-read transaction is complete, ensure bus is idle
	 * before resetting the internal FIFOs
	 */
	ret = check_bus_idle(base);
	if (ret) {

		return ret;
	}

	return 0;
}

static int peci_xec_transfer(const struct device *dev, struct peci_msg *msg)
{
	ARG_UNUSED(dev);
	int ret;
	PECI_Type *base = peci_xec_config.base;
	uint8_t err_val;

	ret = peci_xec_write(dev, msg);
	if (ret) {
		return ret;
	}

	/* If a PECI transmission is successful, it may or not involve
	 * a read operation, check if transaction expects a response
	 */
	if (msg->rx_buffer.len) {
		ret = peci_xec_read(dev, msg);
		if (ret) {
			return ret;
		}
	}

	/* Cleanup */
	if (base->STATUS1 & MCHP_PECI_STS1_EOF) {
		base->STATUS1 |= MCHP_PECI_STS1_EOF;
	}

	/* Check for error conditions and perform bus recovery if necessary */
	err_val = base->ERROR;
	if (err_val) {
		if (err_val & MCHP_PECI_ERR_RDOV) {
			LOG_ERR("Read buffer is not empty");
		}

		if (err_val & MCHP_PECI_ERR_WRUN) {
			LOG_ERR("Write buffer is not empty");
		}

		if (err_val & MCHP_PECI_ERR_BERR) {
			LOG_ERR("PECI bus error");
		}

		LOG_DBG("PECI err %x", err_val);
		LOG_DBG("PECI sts1 %x", base->STATUS1);
		LOG_DBG("PECI sts2 %x", base->STATUS2);

		/* ERROR is a clear-on-write register, need to clear errors
		 * occurring at the end of a transaction. A temp variable is
		 * used to overcome complaints by the static code analyzer
		 */
		base->ERROR = err_val;
		peci_xec_bus_recovery(dev, false);
		return -EIO;
	}

	return 0;
}

#ifdef CONFIG_PECI_INTERRUPT_DRIVEN
static void peci_xec_isr(const void *arg)
{
	ARG_UNUSED(arg);
	PECI_Type *base = peci_xec_config.base;

	MCHP_GIRQ_SRC(MCHP_PECI_GIRQ) = MCHP_PECI_GIRQ_VAL;

	if (base->ERROR) {
		base->ERROR = base->ERROR;
	}

	if (base->STATUS2 & MCHP_PECI_STS2_WFE) {
		LOG_WRN("TX FIFO empty ST2:%x", base->STATUS2);
		k_sem_give(&peci_data.tx_lock);
	}

	if (base->STATUS2 & MCHP_PECI_STS2_RFE) {
		LOG_WRN("RX FIFO full ST2:%x", base->STATUS2);
	}
}
#endif

static const struct peci_driver_api peci_xec_driver_api = {
	.config = peci_xec_configure,
	.enable = peci_xec_enable,
	.disable = peci_xec_disable,
	.transfer = peci_xec_transfer,
};

static int peci_xec_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	PECI_Type *base = peci_xec_config.base;
#ifdef CONFIG_PECI_INTERRUPT_DRIVEN
	k_sem_init(&peci_data.tx_lock, 0, 1);
#endif

	/* Reset PECI interface */
	base->CONTROL |= MCHP_PECI_CTRL_RST;
	k_msleep(PECI_RESET_DELAY_MS);
	base->CONTROL &= ~MCHP_PECI_CTRL_RST;

#ifdef CONFIG_PECI_INTERRUPT_DRIVEN
	/* Enable interrupt for errors */
	base->INT_EN1 = (MCHP_PECI_IEN1_EREN | MCHP_PECI_IEN1_EIEN);

	/* Enable interrupt for Tx FIFO is empty */
	base->INT_EN2 |= MCHP_PECI_IEN2_ENWFE;
	/* Enable interrupt for Rx FIFO is full */
	base->INT_EN2 |= MCHP_PECI_IEN2_ENRFF;

	base->CONTROL |= MCHP_PECI_CTRL_MIEN;

	/* Direct NVIC */
	IRQ_CONNECT(peci_xec_config.irq_num,
		    DT_INST_IRQ(0, priority),
		    peci_xec_isr, NULL, 0);
#endif
	return 0;
}

DEVICE_DT_INST_DEFINE(0,
		    &peci_xec_init,
		    NULL,
		    NULL, NULL,
		    POST_KERNEL, CONFIG_PECI_INIT_PRIORITY,
		    &peci_xec_driver_api);
