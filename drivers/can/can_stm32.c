/*
 * Copyright (c) 2018 Alexander Wachter
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/clock_control/stm32_clock_control.h>
#include <drivers/clock_control.h>
#include <pinmux/pinmux_stm32.h>
#include <sys/util.h>
#include <string.h>
#include <kernel.h>
#include <soc.h>
#include <errno.h>
#include <stdbool.h>
#include <drivers/can.h>
#include "can_stm32.h"

#include <logging/log.h>
LOG_MODULE_DECLARE(can_driver, CONFIG_CAN_LOG_LEVEL);

#define CAN_INIT_TIMEOUT  (10 * sys_clock_hw_cycles_per_sec() / MSEC_PER_SEC)

#if DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(can1), st_stm32_can, okay) && \
    DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(can2), st_stm32_can, okay)
#error Simultaneous use of CAN_1 and CAN_2 not supported yet
#endif

#define DT_DRV_COMPAT st_stm32_can

#define SP_IS_SET(inst) DT_INST_NODE_HAS_PROP(inst, sample_point) ||

/* Macro to exclude the sample point algorithm from compilation if not used
 * Without the macro, the algorithm would always waste ROM
 */
#define USE_SP_ALGO (DT_INST_FOREACH_STATUS_OKAY(SP_IS_SET) 0)

#define SP_AND_TIMING_NOT_SET(inst) \
	(!DT_INST_NODE_HAS_PROP(inst, sample_point) && \
	!(DT_INST_NODE_HAS_PROP(inst, prop_seg) && \
	DT_INST_NODE_HAS_PROP(inst, phase_seg1) && \
	DT_INST_NODE_HAS_PROP(inst, phase_seg2))) ||

#if DT_INST_FOREACH_STATUS_OKAY(SP_AND_TIMING_NOT_SET) 0
#error You must either set a sampling-point or timings (phase-seg* and prop-seg)
#endif

/*
 * Translation tables
 * filter_in_bank[enum can_filter_type] = number of filters in bank for this type
 * reg_demand[enum can_filter_type] = how many registers are used for this type
 */
static const uint8_t filter_in_bank[] = {2, 4, 1, 2};
static const uint8_t reg_demand[] = {2, 1, 4, 2};

static void can_stm32_signal_tx_complete(struct can_mailbox *mb)
{
	if (mb->tx_callback) {
		mb->tx_callback(mb->error, mb->callback_arg);
	} else  {
		k_sem_give(&mb->tx_int_sem);
	}
}

static void can_stm32_get_msg_fifo(CAN_FIFOMailBox_TypeDef *mbox,
				    struct zcan_frame *msg)
{
	if (mbox->RIR & CAN_RI0R_IDE) {
		msg->id = mbox->RIR >> CAN_RI0R_EXID_Pos;
		msg->id_type = CAN_EXTENDED_IDENTIFIER;
	} else {
		msg->id =  mbox->RIR >> CAN_RI0R_STID_Pos;
		msg->id_type = CAN_STANDARD_IDENTIFIER;
	}

	msg->rtr = mbox->RIR & CAN_RI0R_RTR ? CAN_REMOTEREQUEST : CAN_DATAFRAME;
	msg->dlc = mbox->RDTR & (CAN_RDT0R_DLC >> CAN_RDT0R_DLC_Pos);
	msg->data_32[0] = mbox->RDLR;
	msg->data_32[1] = mbox->RDHR;
#ifdef CONFIG_CAN_RX_TIMESTAMP
	msg->timestamp = ((mbox->RDTR & CAN_RDT0R_TIME) >> CAN_RDT0R_TIME_Pos);
#endif
}

static inline
void can_stm32_rx_isr_handler(CAN_TypeDef *can, struct can_stm32_data *data)
{
	CAN_FIFOMailBox_TypeDef *mbox;
	int filter_match_index;
	struct zcan_frame msg;
	can_rx_callback_t callback;

	while (can->RF0R & CAN_RF0R_FMP0) {
		mbox = &can->sFIFOMailBox[0];
		filter_match_index = ((mbox->RDTR & CAN_RDT0R_FMI)
					   >> CAN_RDT0R_FMI_Pos);

		if (filter_match_index >= CONFIG_CAN_MAX_FILTER) {
			break;
		}

		LOG_DBG("Message on filter index %d", filter_match_index);
		can_stm32_get_msg_fifo(mbox, &msg);

		callback = data->rx_cb[filter_match_index];

		if (callback) {
			callback(&msg, data->cb_arg[filter_match_index]);
		}

		/* Release message */
		can->RF0R |= CAN_RF0R_RFOM0;
	}

	if (can->RF0R & CAN_RF0R_FOVR0) {
		LOG_ERR("RX FIFO Overflow");
	}
}

static inline void can_stm32_bus_state_change_isr(CAN_TypeDef *can,
						  struct can_stm32_data *data)
{
	struct can_bus_err_cnt err_cnt;
	enum can_state state;

	if (!(can->ESR & CAN_ESR_EPVF) && !(can->ESR & CAN_ESR_BOFF)) {
		return;
	}

	err_cnt.tx_err_cnt = ((can->ESR & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos);
	err_cnt.rx_err_cnt = ((can->ESR & CAN_ESR_REC) >> CAN_ESR_REC_Pos);

	if (can->ESR & CAN_ESR_BOFF) {
		state = CAN_BUS_OFF;
	} else if (can->ESR & CAN_ESR_EPVF) {
		state = CAN_ERROR_PASSIVE;
	} else {
		state = CAN_ERROR_ACTIVE;
	}

	if (data->state_change_isr) {
		data->state_change_isr(state, err_cnt);
	}
}

static inline
void can_stm32_tx_isr_handler(CAN_TypeDef *can, struct can_stm32_data *data)
{
	uint32_t bus_off;

	bus_off = can->ESR & CAN_ESR_BOFF;

	if ((can->TSR & CAN_TSR_RQCP0) | bus_off) {
		data->mb0.error =
				can->TSR & CAN_TSR_TXOK0 ? CAN_TX_OK  :
				can->TSR & CAN_TSR_TERR0 ? CAN_TX_ERR :
				can->TSR & CAN_TSR_ALST0 ? CAN_TX_ARB_LOST :
						 bus_off ? CAN_TX_BUS_OFF :
							   CAN_TX_UNKNOWN;
		/* clear the request. */
		can->TSR |= CAN_TSR_RQCP0;
		can_stm32_signal_tx_complete(&data->mb0);
	}

	if ((can->TSR & CAN_TSR_RQCP1) | bus_off) {
		data->mb1.error =
				can->TSR & CAN_TSR_TXOK1 ? CAN_TX_OK  :
				can->TSR & CAN_TSR_TERR1 ? CAN_TX_ERR :
				can->TSR & CAN_TSR_ALST1 ? CAN_TX_ARB_LOST :
				bus_off                  ? CAN_TX_BUS_OFF :
							   CAN_TX_UNKNOWN;
		/* clear the request. */
		can->TSR |= CAN_TSR_RQCP1;
		can_stm32_signal_tx_complete(&data->mb1);
	}

	if ((can->TSR & CAN_TSR_RQCP2) | bus_off) {
		data->mb2.error =
				can->TSR & CAN_TSR_TXOK2 ? CAN_TX_OK  :
				can->TSR & CAN_TSR_TERR2 ? CAN_TX_ERR :
				can->TSR & CAN_TSR_ALST2 ? CAN_TX_ARB_LOST :
				bus_off                  ? CAN_TX_BUS_OFF :
							   CAN_TX_UNKNOWN;
		/* clear the request. */
		can->TSR |= CAN_TSR_RQCP2;
		can_stm32_signal_tx_complete(&data->mb2);
	}

	if (can->TSR & CAN_TSR_TME) {
		k_sem_give(&data->tx_int_sem);
	}
}

#ifdef CONFIG_SOC_SERIES_STM32F0X

static void can_stm32_isr(const struct device *dev)
{
	struct can_stm32_data *data;
	const struct can_stm32_config *cfg;
	CAN_TypeDef *can;

	data = DEV_DATA(dev);
	cfg = DEV_CFG(dev);
	can = cfg->can;

	can_stm32_tx_isr_handler(can, data);
	can_stm32_rx_isr_handler(can, data);
	if (can->MSR & CAN_MSR_ERRI) {
		can_stm32_bus_state_change_isr(can, data);
		can->MSR |= CAN_MSR_ERRI;
	}
}

#else

static void can_stm32_rx_isr(const struct device *dev)
{
	struct can_stm32_data *data;
	const struct can_stm32_config *cfg;
	CAN_TypeDef *can;

	data = DEV_DATA(dev);
	cfg = DEV_CFG(dev);
	can = cfg->can;

	can_stm32_rx_isr_handler(can, data);
}

static void can_stm32_tx_isr(const struct device *dev)
{
	struct can_stm32_data *data;
	const struct can_stm32_config *cfg;
	CAN_TypeDef *can;

	data = DEV_DATA(dev);
	cfg = DEV_CFG(dev);
	can = cfg->can;

	can_stm32_tx_isr_handler(can, data);
}

static void can_stm32_state_change_isr(const struct device *dev)
{
	struct can_stm32_data *data;
	const struct can_stm32_config *cfg;
	CAN_TypeDef *can;

	data = DEV_DATA(dev);
	cfg = DEV_CFG(dev);
	can = cfg->can;


	/*Signal bus-off to waiting tx*/
	if (can->MSR & CAN_MSR_ERRI) {
		can_stm32_tx_isr_handler(can, data);
		can_stm32_bus_state_change_isr(can, data);
		can->MSR |= CAN_MSR_ERRI;
	}
}

#endif

static int can_enter_init_mode(CAN_TypeDef *can)
{
	uint32_t start_time;

	can->MCR |= CAN_MCR_INRQ;
	start_time = k_cycle_get_32();

	while ((can->MSR & CAN_MSR_INAK) == 0U) {
		if (k_cycle_get_32() - start_time > CAN_INIT_TIMEOUT) {
			can->MCR &= ~CAN_MCR_INRQ;
			return CAN_TIMEOUT;
		}
	}

	return 0;
}

static int can_leave_init_mode(CAN_TypeDef *can)
{
	uint32_t start_time;

	can->MCR &= ~CAN_MCR_INRQ;
	start_time = k_cycle_get_32();

	while ((can->MSR & CAN_MSR_INAK) != 0U) {
		if (k_cycle_get_32() - start_time > CAN_INIT_TIMEOUT) {
			return CAN_TIMEOUT;
		}
	}

	return 0;
}

static int can_leave_sleep_mode(CAN_TypeDef *can)
{
	uint32_t start_time;

	can->MCR &= ~CAN_MCR_SLEEP;
	start_time = k_cycle_get_32();

	while ((can->MSR & CAN_MSR_SLAK) != 0) {
		if (k_cycle_get_32() - start_time > CAN_INIT_TIMEOUT) {
			return CAN_TIMEOUT;
		}
	}

	return 0;
}

int can_stm32_set_mode(const struct device *dev, enum can_mode mode)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	CAN_TypeDef *can = cfg->can;
	struct can_stm32_data *data = DEV_DATA(dev);
	int ret;

	LOG_DBG("Set mode %d", mode);

	k_mutex_lock(&data->inst_mutex, K_FOREVER);
	ret = can_enter_init_mode(can);
	if (ret) {
		LOG_ERR("Failed to enter init mode");
		goto done;
	}

	switch (mode) {
	case CAN_NORMAL_MODE:
		can->BTR &= ~(CAN_BTR_LBKM | CAN_BTR_SILM);
		break;
	case CAN_LOOPBACK_MODE:
		can->BTR &= ~(CAN_BTR_SILM);
		can->BTR |= CAN_BTR_LBKM;
		break;
	case CAN_SILENT_MODE:
		can->BTR &= ~(CAN_BTR_LBKM);
		can->BTR |= CAN_BTR_SILM;
		break;
	case CAN_SILENT_LOOPBACK_MODE:
		can->BTR |= CAN_BTR_LBKM | CAN_BTR_SILM;
		break;
	default:
		break;
	}

done:
	ret = can_leave_init_mode(can);
	if (ret) {
		LOG_ERR("Failed to leave init mode");
	}
	k_mutex_unlock(&data->inst_mutex);
	return ret;
}

int can_stm32_set_timing(const struct device *dev,
			 const struct can_timing *timing,
			 const struct can_timing *timing_data)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	CAN_TypeDef *can = cfg->can;
	struct can_stm32_data *data = DEV_DATA(dev);
	int ret = -EIO;

	ARG_UNUSED(timing_data);

	k_mutex_lock(&data->inst_mutex, K_FOREVER);
	ret = can_enter_init_mode(can);
	if (ret) {
		LOG_ERR("Failed to enter init mode");
		goto done;
	}

	can->BTR = (can->BTR & ~(CAN_BTR_BRP_Msk | CAN_BTR_TS1_Msk | CAN_BTR_TS2_Msk)) |
	     (((timing->phase_seg1 - 1) << CAN_BTR_TS1_Pos) & CAN_BTR_TS1_Msk) |
	     (((timing->phase_seg2 - 1) << CAN_BTR_TS2_Pos) & CAN_BTR_TS2_Msk) |
	     (((timing->prescaler  - 1) << CAN_BTR_BRP_Pos) & CAN_BTR_BRP_Msk);

	if (timing->sjw != CAN_SJW_NO_CHANGE) {
		can->BTR = (can->BTR & ~CAN_BTR_SJW_Msk) |
			   (((timing->sjw - 1) << CAN_BTR_SJW_Pos) & CAN_BTR_SJW_Msk);
	}

	ret = can_leave_init_mode(can);
	if (ret) {
		LOG_ERR("Failed to leave init mode");
	} else {
		ret = 0;
	}

done:
	k_mutex_unlock(&data->inst_mutex);
	return ret;
}

int can_stm32_get_core_clock(const struct device *dev, uint32_t *rate)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	const struct device *clock;
	int ret;

	clock = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);

	ret = clock_control_get_rate(clock,
				     (clock_control_subsys_t *) &cfg->pclken,
				     rate);
	if (ret != 0) {
		LOG_ERR("Failed call clock_control_get_rate: return [%d]", ret);
		return -EIO;
	}

	return 0;
}

static int can_stm32_init(const struct device *dev)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	struct can_stm32_data *data = DEV_DATA(dev);
	CAN_TypeDef *can = cfg->can;
	struct can_timing timing;
#if DT_NODE_HAS_STATUS(DT_NODELABEL(can2), okay)
	CAN_TypeDef *master_can = cfg->master_can;
#endif
	const struct device *clock;
	int ret;

	k_mutex_init(&data->inst_mutex);
	k_sem_init(&data->tx_int_sem, 0, 1);
	k_sem_init(&data->mb0.tx_int_sem, 0, 1);
	k_sem_init(&data->mb1.tx_int_sem, 0, 1);
	k_sem_init(&data->mb2.tx_int_sem, 0, 1);
	data->mb0.tx_callback = NULL;
	data->mb1.tx_callback = NULL;
	data->mb2.tx_callback = NULL;
	data->state_change_isr = NULL;

	data->filter_usage = (1ULL << CAN_MAX_NUMBER_OF_FILTERS) - 1ULL;
	(void)memset(data->rx_cb, 0, sizeof(data->rx_cb));
	(void)memset(data->cb_arg, 0, sizeof(data->cb_arg));

	clock = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);

	ret = clock_control_on(clock, (clock_control_subsys_t *) &cfg->pclken);
	if (ret != 0) {
		LOG_ERR("HAL_CAN_Init clock control on failed: %d", ret);
		return -EIO;
	}

	/* Configure dt provided device signals when available */
	ret = stm32_dt_pinctrl_configure(cfg->pinctrl,
					 cfg->pinctrl_len,
					 (uint32_t)cfg->can);
	if (ret < 0) {
		LOG_ERR("CAN pinctrl setup failed (%d)", ret);
		return ret;
	}

	ret = can_leave_sleep_mode(can);
	if (ret) {
		LOG_ERR("Failed to exit sleep mode");
		return ret;
	}

	ret = can_enter_init_mode(can);
	if (ret) {
		LOG_ERR("Failed to enter init mode");
		return ret;
	}

#if DT_NODE_HAS_STATUS(DT_NODELABEL(can2), okay)
	master_can->FMR &= ~CAN_FMR_CAN2SB; /* Assign all filters to CAN2 */
#endif

	/* Set TX priority to chronological order */
	can->MCR |= CAN_MCR_TXFP;
	can->MCR &= ~CAN_MCR_TTCM & ~CAN_MCR_ABOM & ~CAN_MCR_AWUM &
		    ~CAN_MCR_NART & ~CAN_MCR_RFLM;
#ifdef CONFIG_CAN_RX_TIMESTAMP
	can->MCR |= CAN_MCR_TTCM;
#endif
#ifdef CONFIG_CAN_AUTO_BUS_OFF_RECOVERY
	can->MCR |= CAN_MCR_ABOM;
#endif
	timing.sjw = cfg->sjw;
	if (cfg->sample_point && USE_SP_ALGO) {
		ret = can_calc_timing(dev, &timing, cfg->bus_speed,
				      cfg->sample_point);
		if (ret == -EINVAL) {
			LOG_ERR("Can't find timing for given param");
			return -EIO;
		}
		LOG_DBG("Presc: %d, TS1: %d, TS2: %d",
			timing.prescaler, timing.phase_seg1, timing.phase_seg2);
		LOG_DBG("Sample-point err : %d", ret);
	} else {
		timing.prop_seg = 0;
		timing.phase_seg1 = cfg->prop_ts1;
		timing.phase_seg2 = cfg->ts2;
		ret = can_calc_prescaler(dev, &timing, cfg->bus_speed);
		if (ret) {
			LOG_WRN("Bitrate error: %d", ret);
		}
	}

	ret = can_stm32_set_timing(dev, &timing, NULL);
	if (ret) {
		return ret;
	}

	ret = can_stm32_set_mode(dev, CAN_NORMAL_MODE);
	if (ret) {
		return ret;
	}

	cfg->config_irq(can);
	can->IER |= CAN_IER_TMEIE;
	LOG_INF("Init of %s done", dev->name);
	return 0;
}

static void can_stm32_register_state_change_isr(const struct device *dev,
						can_state_change_isr_t isr)
{
	struct can_stm32_data *data = DEV_DATA(dev);
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	CAN_TypeDef *can = cfg->can;

	data->state_change_isr = isr;

	if (isr == NULL) {
		can->IER &= ~CAN_IER_EPVIE;
	} else {
		can->IER |= CAN_IER_EPVIE;
	}
}

static enum can_state can_stm32_get_state(const struct device *dev,
					  struct can_bus_err_cnt *err_cnt)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	CAN_TypeDef *can = cfg->can;

	if (err_cnt) {
		err_cnt->tx_err_cnt =
			((can->ESR & CAN_ESR_TEC) >> CAN_ESR_TEC_Pos);
		err_cnt->rx_err_cnt =
			((can->ESR & CAN_ESR_REC) >> CAN_ESR_REC_Pos);
	}

	if (can->ESR & CAN_ESR_BOFF) {
		return CAN_BUS_OFF;
	}

	if (can->ESR & CAN_ESR_EPVF) {
		return CAN_ERROR_PASSIVE;
	}

	return CAN_ERROR_ACTIVE;

}

#ifndef CONFIG_CAN_AUTO_BUS_OFF_RECOVERY
int can_stm32_recover(const struct device *dev, k_timeout_t timeout)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	struct can_stm32_data *data = DEV_DATA(dev);
	CAN_TypeDef *can = cfg->can;
	int ret = CAN_TIMEOUT;
	int64_t start_time;

	if (!(can->ESR & CAN_ESR_BOFF)) {
		return 0;
	}

	if (k_mutex_lock(&data->inst_mutex, K_FOREVER)) {
		return CAN_TIMEOUT;
	}

	ret = can_enter_init_mode(can);
	if (ret) {
		goto done;
	}

	can_leave_init_mode(can);

	start_time = k_uptime_ticks();

	while (can->ESR & CAN_ESR_BOFF) {
		if (!K_TIMEOUT_EQ(timeout, K_FOREVER) &&
		    k_uptime_ticks() - start_time >= timeout.ticks) {
			goto done;
		}
	}

	ret = 0;

done:
	k_mutex_unlock(&data->inst_mutex);
	return ret;
}
#endif /* CONFIG_CAN_AUTO_BUS_OFF_RECOVERY */


int can_stm32_send(const struct device *dev, const struct zcan_frame *msg,
		   k_timeout_t timeout, can_tx_callback_t callback,
		   void *callback_arg)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	struct can_stm32_data *data = DEV_DATA(dev);
	CAN_TypeDef *can = cfg->can;
	uint32_t transmit_status_register = can->TSR;
	CAN_TxMailBox_TypeDef *mailbox = NULL;
	struct can_mailbox *mb = NULL;

	LOG_DBG("Sending %d bytes on %s. "
		    "Id: 0x%x, "
		    "ID type: %s, "
		    "Remote Frame: %s"
		    , msg->dlc, dev->name
		    , msg->id
		    , msg->id_type == CAN_STANDARD_IDENTIFIER ?
		    "standard" : "extended"
		    , msg->rtr == CAN_DATAFRAME ? "no" : "yes");

	__ASSERT(msg->dlc == 0U || msg->data != NULL, "Dataptr is null");

	if (msg->dlc > CAN_MAX_DLC) {
		LOG_ERR("DLC of %d exceeds maximum (%d)", msg->dlc, CAN_MAX_DLC);
		return CAN_TX_EINVAL;
	}

	if (can->ESR & CAN_ESR_BOFF) {
		return CAN_TX_BUS_OFF;
	}

	k_mutex_lock(&data->inst_mutex, K_FOREVER);
	while (!(transmit_status_register & CAN_TSR_TME)) {
		k_mutex_unlock(&data->inst_mutex);
		LOG_DBG("Transmit buffer full");
		if (k_sem_take(&data->tx_int_sem, timeout)) {
			return CAN_TIMEOUT;
		}

		k_mutex_lock(&data->inst_mutex, K_FOREVER);
		transmit_status_register = can->TSR;
	}

	if (transmit_status_register & CAN_TSR_TME0) {
		LOG_DBG("Using mailbox 0");
		mailbox = &can->sTxMailBox[0];
		mb = &(data->mb0);
	} else if (transmit_status_register & CAN_TSR_TME1) {
		LOG_DBG("Using mailbox 1");
		mailbox = &can->sTxMailBox[1];
		mb = &data->mb1;
	} else if (transmit_status_register & CAN_TSR_TME2) {
		LOG_DBG("Using mailbox 2");
		mailbox = &can->sTxMailBox[2];
		mb = &data->mb2;
	}

	mb->tx_callback = callback;
	mb->callback_arg = callback_arg;
	k_sem_reset(&mb->tx_int_sem);

	/* mailbox identifier register setup */
	mailbox->TIR &= CAN_TI0R_TXRQ;

	if (msg->id_type == CAN_STANDARD_IDENTIFIER) {
		mailbox->TIR |= (msg->id << CAN_TI0R_STID_Pos);
	} else {
		mailbox->TIR |= (msg->id << CAN_TI0R_EXID_Pos)
				| CAN_TI0R_IDE;
	}

	if (msg->rtr == CAN_REMOTEREQUEST) {
		mailbox->TIR |= CAN_TI1R_RTR;
	}

	mailbox->TDTR = (mailbox->TDTR & ~CAN_TDT1R_DLC) |
			((msg->dlc & 0xF) << CAN_TDT1R_DLC_Pos);

	mailbox->TDLR = msg->data_32[0];
	mailbox->TDHR = msg->data_32[1];

	mailbox->TIR |= CAN_TI0R_TXRQ;
	k_mutex_unlock(&data->inst_mutex);

	if (callback == NULL) {
		k_sem_take(&mb->tx_int_sem, K_FOREVER);
		return mb->error;
	}

	return 0;
}

static inline int can_stm32_check_free(void **arr, int start, int end)
{
	int i;

	for (i = start; i <= end; i++) {
		if (arr[i] != NULL) {
			return 0;
		}
	}
	return 1;
}

static int can_stm32_shift_arr(void **arr, int start, int count)
{
	void **start_ptr = arr + start;
	size_t cnt;

	if (start > CONFIG_CAN_MAX_FILTER) {
		return CAN_NO_FREE_FILTER;
	}

	if (count > 0) {
		void *move_dest;

		/* Check if nothing used will be overwritten */
		if (!can_stm32_check_free(arr, CONFIG_CAN_MAX_FILTER - count,
					       CONFIG_CAN_MAX_FILTER - 1)) {
			return CAN_NO_FREE_FILTER;
		}

		/* No need to shift. Destination is already outside the arr*/
		if ((start + count) >= CONFIG_CAN_MAX_FILTER) {
			return 0;
		}

		cnt = (CONFIG_CAN_MAX_FILTER - start - count) * sizeof(void *);
		move_dest = start_ptr + count;
		memmove(move_dest, start_ptr, cnt);
		(void)memset(start_ptr, 0, count * sizeof(void *));
	} else if (count < 0) {
		count = -count;

		if (start - count < 0) {
			return CAN_NO_FREE_FILTER;
		}

		cnt = (CONFIG_CAN_MAX_FILTER - start) * sizeof(void *);
		memmove(start_ptr - count, start_ptr, cnt);
		(void)memset(arr + CONFIG_CAN_MAX_FILTER - count, 0,
			     count * sizeof(void *));
	}

	return 0;
}

enum can_filter_type can_stm32_get_filter_type(int bank_nr, uint32_t mode_reg,
					       uint32_t scale_reg)
{
	uint32_t mode_masked  = (mode_reg  >> bank_nr) & 0x01;
	uint32_t scale_masked = (scale_reg >> bank_nr) & 0x01;
	enum can_filter_type type = (scale_masked << 1) | mode_masked;

	return type;
}

static int can_calc_filter_index(int filter_nr, uint32_t mode_reg, uint32_t scale_reg)
{
	int filter_bank = filter_nr / 4;
	int cnt = 0;
	uint32_t mode_masked, scale_masked;
	enum can_filter_type filter_type;
	/*count filters in the banks before */
	for (int i = 0; i < filter_bank; i++) {
		filter_type = can_stm32_get_filter_type(i, mode_reg, scale_reg);
		cnt += filter_in_bank[filter_type];
	}

	/* plus the filters in the same bank */
	mode_masked  = mode_reg & (1U << filter_bank);
	scale_masked = scale_reg & (1U << filter_bank);
	cnt += (!scale_masked && mode_masked) ? filter_nr & 0x03 :
					       (filter_nr & 0x03) >> 1;
	return cnt;
}

static void can_stm32_set_filter_bank(int filter_nr,
				      CAN_FilterRegister_TypeDef *filter_reg,
				      enum can_filter_type filter_type,
				      uint32_t id, uint32_t mask)
{
	switch (filter_type) {
	case CAN_FILTER_STANDARD:
		switch (filter_nr & 0x03) {
		case 0:
			filter_reg->FR1 = (filter_reg->FR1 & 0xFFFF0000) | id;
			break;
		case 1:
			filter_reg->FR1 = (filter_reg->FR1 & 0x0000FFFF)
					  | (id << 16);
			break;
		case 2:
			filter_reg->FR2 = (filter_reg->FR2 & 0xFFFF0000) | id;
			break;
		case 3:
			filter_reg->FR2 = (filter_reg->FR2 & 0x0000FFFF)
					   | (id << 16);
			break;
		}

		break;
	case CAN_FILTER_STANDARD_MASKED:
		switch (filter_nr & 0x02) {
		case 0:
			filter_reg->FR1 = id | (mask << 16);
			break;
		case 2:
			filter_reg->FR2 = id | (mask << 16);
			break;
		}

		break;
	case CAN_FILTER_EXTENDED:
		switch (filter_nr & 0x02) {
		case 0:
			filter_reg->FR1 = id;
			break;
		case 2:
			filter_reg->FR2 = id;
			break;
		}

		break;
	case CAN_FILTER_EXTENDED_MASKED:
		filter_reg->FR1 = id;
		filter_reg->FR2 = mask;
		break;
	}
}

static inline void can_stm32_set_mode_scale(enum can_filter_type filter_type,
					    uint32_t *mode_reg, uint32_t *scale_reg,
					    int  bank_nr)
{
	uint32_t mode_reg_bit  = (filter_type & 0x01) << bank_nr;
	uint32_t scale_reg_bit = (filter_type >>   1) << bank_nr;

	*mode_reg &= ~(1 << bank_nr);
	*mode_reg |= mode_reg_bit;

	*scale_reg &= ~(1 << bank_nr);
	*scale_reg |= scale_reg_bit;
}

static inline uint32_t can_generate_std_mask(const struct zcan_filter *filter)
{
	return  (filter->id_mask  << CAN_FIRX_STD_ID_POS) |
		(filter->rtr_mask << CAN_FIRX_STD_RTR_POS) |
		(1U               << CAN_FIRX_STD_IDE_POS);
}

static inline uint32_t can_generate_ext_mask(const struct zcan_filter *filter)
{
	return  (filter->id_mask  << CAN_FIRX_EXT_EXT_ID_POS) |
		(filter->rtr_mask << CAN_FIRX_EXT_RTR_POS) |
		(1U               << CAN_FIRX_EXT_IDE_POS);
}

static inline uint32_t can_generate_std_id(const struct zcan_filter *filter)
{

	return  (filter->id << CAN_FIRX_STD_ID_POS) |
		(filter->rtr    << CAN_FIRX_STD_RTR_POS);

}

static inline uint32_t can_generate_ext_id(const struct zcan_filter *filter)
{
	return  (filter->id  << CAN_FIRX_EXT_EXT_ID_POS) |
		(filter->rtr << CAN_FIRX_EXT_RTR_POS) |
		(1U          << CAN_FIRX_EXT_IDE_POS);
}

static inline int can_stm32_set_filter(const struct zcan_filter *filter,
				       struct can_stm32_data *device_data,
				       CAN_TypeDef *can,
				       int *filter_index)
{
	uint32_t mask = 0U;
	uint32_t id = 0U;
	int filter_nr = 0;
	int filter_index_new = CAN_NO_FREE_FILTER;
	int bank_nr;
	uint32_t bank_bit;
	int register_demand;
	enum can_filter_type filter_type;
	enum can_filter_type bank_mode;

	if (filter->id_type == CAN_STANDARD_IDENTIFIER) {
		id = can_generate_std_id(filter);
		filter_type = CAN_FILTER_STANDARD;

		if (filter->id_mask != CAN_STD_ID_MASK) {
			mask = can_generate_std_mask(filter);
			filter_type = CAN_FILTER_STANDARD_MASKED;
		}
	} else {
		id = can_generate_ext_id(filter);
		filter_type = CAN_FILTER_EXTENDED;

		if (filter->id_mask != CAN_EXT_ID_MASK) {
			mask = can_generate_ext_mask(filter);
			filter_type = CAN_FILTER_EXTENDED_MASKED;
		}
	}

	register_demand = reg_demand[filter_type];

	LOG_DBG("Setting filter ID: 0x%x, mask: 0x%x", filter->id,
		filter->id_mask);
	LOG_DBG("Filter type: %s ID %s mask (%d)",
		    (filter_type == CAN_FILTER_STANDARD ||
		     filter_type == CAN_FILTER_STANDARD_MASKED) ?
		    "standard" : "extended",
		    (filter_type == CAN_FILTER_STANDARD_MASKED ||
		     filter_type == CAN_FILTER_EXTENDED_MASKED) ?
		    "with" : "without",
		    filter_type);

	do {
		uint64_t usage_shifted = (device_data->filter_usage >> filter_nr);
		uint64_t usage_demand_mask = (1ULL << register_demand) - 1;
		bool bank_is_empty;

		bank_nr = filter_nr / 4;
		bank_bit = (1U << bank_nr);
		bank_mode = can_stm32_get_filter_type(bank_nr, can->FM1R,
						      can->FS1R);

		bank_is_empty = CAN_BANK_IS_EMPTY(device_data->filter_usage,
						  bank_nr);

		if (!bank_is_empty && bank_mode != filter_type) {
			filter_nr = (bank_nr + 1) * 4;
		} else if (usage_shifted & usage_demand_mask) {
			device_data->filter_usage &=
				~(usage_demand_mask << filter_nr);
			break;
		} else {
			filter_nr += register_demand;
		}

		if (!usage_shifted) {
			LOG_INF("No free filter bank found");
			return CAN_NO_FREE_FILTER;
		}
	} while (filter_nr < CAN_MAX_NUMBER_OF_FILTERS);

	/* set the filter init mode */
	can->FMR |= CAN_FMR_FINIT;
	can->FA1R &= ~bank_bit;

	/* TODO fifo balancing */
	if (filter_type != bank_mode) {
		int shift_width, start_index;
		int res;
		uint32_t mode_reg  = can->FM1R;
		uint32_t scale_reg = can->FS1R;

		can_stm32_set_mode_scale(filter_type, &mode_reg, &scale_reg,
					 bank_nr);

		shift_width = filter_in_bank[filter_type] - filter_in_bank[bank_mode];

		filter_index_new = can_calc_filter_index(filter_nr, mode_reg,
							 scale_reg);

		start_index = filter_index_new + filter_in_bank[bank_mode];

		if (shift_width && start_index <= CAN_MAX_NUMBER_OF_FILTERS) {
			res = can_stm32_shift_arr((void **)device_data->rx_cb,
						start_index,
						shift_width);

			res |= can_stm32_shift_arr(device_data->cb_arg,
						   start_index,
						   shift_width);

			if (filter_index_new >= CONFIG_CAN_MAX_FILTER || res) {
				LOG_INF("No space for a new filter!");
				filter_nr = CAN_NO_FREE_FILTER;
				goto done;
			}
		}

		can->FM1R = mode_reg;
		can->FS1R = scale_reg;
	} else {
		filter_index_new = can_calc_filter_index(filter_nr, can->FM1R,
							 can->FS1R);
		if (filter_index_new >= CAN_MAX_NUMBER_OF_FILTERS) {
			filter_nr = CAN_NO_FREE_FILTER;
			goto done;
		}
	}

	can_stm32_set_filter_bank(filter_nr, &can->sFilterRegister[bank_nr],
				  filter_type, id, mask);
done:
	can->FA1R |= bank_bit;
	can->FMR &= ~(CAN_FMR_FINIT);
	LOG_DBG("Filter set! Filter number: %d (index %d)",
		    filter_nr, filter_index_new);
	*filter_index = filter_index_new;
	return filter_nr;
}

static inline int can_stm32_attach(const struct device *dev,
				   can_rx_callback_t cb,
				   void *cb_arg,
				   const struct zcan_filter *filter)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	struct can_stm32_data *data = DEV_DATA(dev);
	CAN_TypeDef *can = cfg->master_can;
	int filter_index = 0;
	int filter_nr;

	filter_nr = can_stm32_set_filter(filter, data, can, &filter_index);
	if (filter_nr != CAN_NO_FREE_FILTER) {
		data->rx_cb[filter_index] = cb;
		data->cb_arg[filter_index] = cb_arg;
	}

	return filter_nr;
}

int can_stm32_attach_isr(const struct device *dev, can_rx_callback_t isr,
			 void *cb_arg,
			 const struct zcan_filter *filter)
{
	struct can_stm32_data *data = DEV_DATA(dev);
	int filter_nr;

	k_mutex_lock(&data->inst_mutex, K_FOREVER);
	filter_nr = can_stm32_attach(dev, isr, cb_arg, filter);
	k_mutex_unlock(&data->inst_mutex);
	return filter_nr;
}

void can_stm32_detach(const struct device *dev, int filter_nr)
{
	const struct can_stm32_config *cfg = DEV_CFG(dev);
	struct can_stm32_data *data = DEV_DATA(dev);
	CAN_TypeDef *can = cfg->master_can;
	int bank_nr;
	int filter_index;
	uint32_t bank_bit;
	uint32_t mode_reg;
	uint32_t scale_reg;
	enum can_filter_type type;
	uint32_t reset_mask;

	__ASSERT_NO_MSG(filter_nr >= 0 && filter_nr < CAN_MAX_NUMBER_OF_FILTERS);

	k_mutex_lock(&data->inst_mutex, K_FOREVER);

	bank_nr = filter_nr / 4;
	bank_bit = (1U << bank_nr);
	mode_reg  = can->FM1R;
	scale_reg = can->FS1R;

	filter_index = can_calc_filter_index(filter_nr, mode_reg, scale_reg);
	type = can_stm32_get_filter_type(bank_nr, mode_reg, scale_reg);

	LOG_DBG("Detatch filter number %d (index %d), type %d", filter_nr,
		    filter_index,
		    type);

	reset_mask = ((1 << (reg_demand[type])) - 1) << filter_nr;
	data->filter_usage |= reset_mask;
	can->FMR |= CAN_FMR_FINIT;
	can->FA1R &= ~bank_bit;

	can_stm32_set_filter_bank(filter_nr, &can->sFilterRegister[bank_nr],
				  type, 0, 0xFFFFFFFF);

	if (!CAN_BANK_IS_EMPTY(data->filter_usage, bank_nr)) {
		can->FA1R |= bank_bit;
	} else {
		LOG_DBG("Bank number %d is empty -> deakivate", bank_nr);
	}

	can->FMR &= ~(CAN_FMR_FINIT);
	data->rx_cb[filter_index] = NULL;
	data->cb_arg[filter_index] = NULL;

	k_mutex_unlock(&data->inst_mutex);
}

static const struct can_driver_api can_api_funcs = {
	.set_mode = can_stm32_set_mode,
	.set_timing = can_stm32_set_timing,
	.send = can_stm32_send,
	.attach_isr = can_stm32_attach_isr,
	.detach = can_stm32_detach,
	.get_state = can_stm32_get_state,
#ifndef CONFIG_CAN_AUTO_BUS_OFF_RECOVERY
	.recover = can_stm32_recover,
#endif
	.register_state_change_isr = can_stm32_register_state_change_isr,
	.get_core_clock = can_stm32_get_core_clock,
	.timing_min = {
		.sjw = 0x1,
		.prop_seg = 0x00,
		.phase_seg1 = 0x01,
		.phase_seg2 = 0x01,
		.prescaler = 0x01
	},
	.timing_max = {
		.sjw = 0x07,
		.prop_seg = 0x00,
		.phase_seg1 = 0x0F,
		.phase_seg2 = 0x07,
		.prescaler = 0x400
	}
};

#if DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(can1), st_stm32_can, okay)

static void config_can_1_irq(CAN_TypeDef *can);

static const struct soc_gpio_pinctrl pins_can_1[] =
	ST_STM32_DT_PINCTRL(can1, 0);

static const struct can_stm32_config can_stm32_cfg_1 = {
	.can = (CAN_TypeDef *)DT_REG_ADDR(DT_NODELABEL(can1)),
	.master_can = (CAN_TypeDef *)DT_REG_ADDR(DT_NODELABEL(can1)),
	.bus_speed = DT_PROP(DT_NODELABEL(can1), bus_speed),
	.sample_point = DT_PROP_OR(DT_NODELABEL(can1), sample_point, 0),
	.sjw = DT_PROP_OR(DT_NODELABEL(can1), sjw, 1),
	.prop_ts1 = DT_PROP_OR(DT_NODELABEL(can1), prop_seg, 0) +
		    DT_PROP_OR(DT_NODELABEL(can1), phase_seg1, 0),
	.ts2 = DT_PROP_OR(DT_NODELABEL(can1), phase_seg2, 0),
	.pclken = {
		.enr = DT_CLOCKS_CELL(DT_NODELABEL(can1), bits),
		.bus = DT_CLOCKS_CELL(DT_NODELABEL(can1), bus),
	},
	.config_irq = config_can_1_irq,
	.pinctrl = pins_can_1,
	.pinctrl_len = ARRAY_SIZE(pins_can_1)
};

static struct can_stm32_data can_stm32_dev_data_1;

DEVICE_DT_DEFINE(DT_NODELABEL(can1), &can_stm32_init, NULL,
		    &can_stm32_dev_data_1, &can_stm32_cfg_1,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &can_api_funcs);

static void config_can_1_irq(CAN_TypeDef *can)
{
	LOG_DBG("Enable CAN1 IRQ");
#ifdef CONFIG_SOC_SERIES_STM32F0X
	IRQ_CONNECT(DT_IRQN(DT_NODELABEL(can1)),
		    DT_IRQ(DT_NODELABEL(can1), priority),
		    can_stm32_isr, DEVICE_DT_GET(DT_NODELABEL(can1)), 0);
	irq_enable(DT_IRQN(DT_NODELABEL(can1)));
#else
	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_NODELABEL(can1), rx0, irq),
		    DT_IRQ_BY_NAME(DT_NODELABEL(can1), rx0, priority),
		    can_stm32_rx_isr, DEVICE_DT_GET(DT_NODELABEL(can1)), 0);
	irq_enable(DT_IRQ_BY_NAME(DT_NODELABEL(can1), rx0, irq));

	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_NODELABEL(can1), tx, irq),
		    DT_IRQ_BY_NAME(DT_NODELABEL(can1), tx, priority),
		    can_stm32_tx_isr, DEVICE_DT_GET(DT_NODELABEL(can1)), 0);
	irq_enable(DT_IRQ_BY_NAME(DT_NODELABEL(can1), tx, irq));

	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_NODELABEL(can1), sce, irq),
		    DT_IRQ_BY_NAME(DT_NODELABEL(can1), sce, priority),
		    can_stm32_state_change_isr,
		    DEVICE_DT_GET(DT_NODELABEL(can1)), 0);
	irq_enable(DT_IRQ_BY_NAME(DT_NODELABEL(can1), sce, irq));
#endif
	can->IER |= CAN_IER_TMEIE | CAN_IER_ERRIE | CAN_IER_FMPIE0 |
		    CAN_IER_FMPIE1 | CAN_IER_BOFIE;
}

#if defined(CONFIG_NET_SOCKETS_CAN)

#include "socket_can_generic.h"

static struct socket_can_context socket_can_context_1;

static int socket_can_init_1(const struct device *dev)
{
	const struct device *can_dev = DEVICE_DT_GET(DT_NODELABEL(can1));
	struct socket_can_context *socket_context = dev->data;

	LOG_DBG("Init socket CAN device %p (%s) for dev %p (%s)",
		dev, dev->name, can_dev, can_dev->name);

	socket_context->can_dev = can_dev;
	socket_context->msgq = &socket_can_msgq;

	socket_context->rx_tid =
		k_thread_create(&socket_context->rx_thread_data,
				rx_thread_stack,
				K_KERNEL_STACK_SIZEOF(rx_thread_stack),
				rx_thread, socket_context, NULL, NULL,
				RX_THREAD_PRIORITY, 0, K_NO_WAIT);

	return 0;
}

NET_DEVICE_INIT(socket_can_stm32_1, SOCKET_CAN_NAME_1, socket_can_init_1,
		NULL, &socket_can_context_1, NULL,
		CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		&socket_can_api,
		CANBUS_RAW_L2, NET_L2_GET_CTX_TYPE(CANBUS_RAW_L2), CAN_MTU);

#endif /* CONFIG_NET_SOCKETS_CAN */

#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(can1), okay) */

#if DT_NODE_HAS_COMPAT_STATUS(DT_NODELABEL(can2), st_stm32_can, okay)

static void config_can_2_irq(CAN_TypeDef *can);

static const struct soc_gpio_pinctrl pins_can_2[] =
	ST_STM32_DT_PINCTRL(can2, 0);

static const struct can_stm32_config can_stm32_cfg_2 = {
	.can = (CAN_TypeDef *)DT_REG_ADDR(DT_NODELABEL(can2)),
	.master_can = (CAN_TypeDef *)DT_PROP(DT_NODELABEL(can2),
								master_can_reg),
	.bus_speed = DT_PROP(DT_NODELABEL(can2), bus_speed),
	.sample_point = DT_PROP_OR(DT_NODELABEL(can2), sample_point, 0),
	.sjw = DT_PROP_OR(DT_NODELABEL(can2), sjw, 1),
	.prop_ts1 = DT_PROP_OR(DT_NODELABEL(can2), prop_seg, 0) +
		    DT_PROP_OR(DT_NODELABEL(can2), phase_seg1, 0),
	.ts2 = DT_PROP_OR(DT_NODELABEL(can2), phase_seg2, 0),
	.pclken = {
		.enr = DT_CLOCKS_CELL(DT_NODELABEL(can2), bits),
		.bus = DT_CLOCKS_CELL(DT_NODELABEL(can2), bus),
	},
	.config_irq = config_can_2_irq,
	.pinctrl = pins_can_2,
	.pinctrl_len = ARRAY_SIZE(pins_can_2)
};

static struct can_stm32_data can_stm32_dev_data_2;

DEVICE_DT_DEFINE(DT_NODELABEL(can2), &can_stm32_init, NULL,
		    &can_stm32_dev_data_2, &can_stm32_cfg_2,
		    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &can_api_funcs);

static void config_can_2_irq(CAN_TypeDef *can)
{
	LOG_DBG("Enable CAN2 IRQ");
	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_NODELABEL(can2), rx0, irq),
		    DT_IRQ_BY_NAME(DT_NODELABEL(can2), rx0, priority),
		    can_stm32_rx_isr, DEVICE_DT_GET(DT_NODELABEL(can2)), 0);
	irq_enable(DT_IRQ_BY_NAME(DT_NODELABEL(can2), rx0, irq));

	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_NODELABEL(can2), tx, irq),
		    DT_IRQ_BY_NAME(DT_NODELABEL(can2), tx, priority),
		    can_stm32_tx_isr, DEVICE_DT_GET(DT_NODELABEL(can2)), 0);
	irq_enable(DT_IRQ_BY_NAME(DT_NODELABEL(can2), tx, irq));

	IRQ_CONNECT(DT_IRQ_BY_NAME(DT_NODELABEL(can2), sce, irq),
		    DT_IRQ_BY_NAME(DT_NODELABEL(can2), sce, priority),
		    can_stm32_state_change_isr,
		    DEVICE_DT_GET(DT_NODELABEL(can2)), 0);
	irq_enable(DT_IRQ_BY_NAME(DT_NODELABEL(can2), sce, irq));
	can->IER |= CAN_IER_TMEIE | CAN_IER_ERRIE | CAN_IER_FMPIE0 |
		    CAN_IER_FMPIE1 | CAN_IER_BOFIE;
}

#if defined(CONFIG_NET_SOCKETS_CAN)

#include "socket_can_generic.h"

static struct socket_can_context socket_can_context_2;

static int socket_can_init_2(const struct device *dev)
{
	const struct device *can_dev = DEVICE_DT_GET(DT_NODELABEL(can2));
	struct socket_can_context *socket_context = dev->data;

	LOG_DBG("Init socket CAN device %p (%s) for dev %p (%s)",
		dev, dev->name, can_dev, can_dev->name);

	socket_context->can_dev = can_dev;
	socket_context->msgq = &socket_can_msgq;

	socket_context->rx_tid =
		k_thread_create(&socket_context->rx_thread_data,
				rx_thread_stack,
				K_KERNEL_STACK_SIZEOF(rx_thread_stack),
				rx_thread, socket_context, NULL, NULL,
				RX_THREAD_PRIORITY, 0, K_NO_WAIT);

	return 0;
}

NET_DEVICE_INIT(socket_can_stm32_2, SOCKET_CAN_NAME_2, socket_can_init_2,
		NULL, &socket_can_context_2, NULL,
		CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		&socket_can_api,
		CANBUS_RAW_L2, NET_L2_GET_CTX_TYPE(CANBUS_RAW_L2), CAN_MTU);

#endif /* CONFIG_NET_SOCKETS_CAN */

#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(can2), okay) */
