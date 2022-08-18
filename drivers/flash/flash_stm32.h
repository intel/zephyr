/*
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2017 BayLibre, SAS.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_FLASH_FLASH_STM32_H_
#define ZEPHYR_DRIVERS_FLASH_FLASH_STM32_H_

#if DT_NODE_HAS_PROP(DT_INST(0, st_stm32_flash_controller), clocks) || \
	DT_NODE_HAS_PROP(DT_INST(0, st_stm32h7_flash_controller), clocks)
#include <drivers/clock_control.h>
#include <drivers/clock_control/stm32_clock_control.h>
#endif

struct flash_stm32_priv {
	FLASH_TypeDef *regs;
#if DT_NODE_HAS_PROP(DT_INST(0, st_stm32_flash_controller), clocks) || \
	DT_NODE_HAS_PROP(DT_INST(0, st_stm32h7_flash_controller), clocks)
	/* clock subsystem driving this peripheral */
	struct stm32_pclken pclken;
#endif
	struct k_sem sem;
};

#if DT_PROP(DT_INST(0, soc_nv_flash), write_block_size)
#define FLASH_STM32_WRITE_BLOCK_SIZE \
	DT_PROP(DT_INST(0, soc_nv_flash), write_block_size)
#else
#error Flash write block size not available
	/* Flash Write block size is extracted from device tree */
	/* as flash node property 'write-block-size' */
#endif

/* Differentiate between arm trust-zone non-secure/secure, and others. */
#if defined(FLASH_NSSR_NSBSY)		/* For mcu w. TZ in non-secure mode */
#define FLASH_SECURITY_NS
#define FLASH_STM32_SR		NSSR
#elif defined(FLASH_SECSR_SECBSY)	/* For mcu w. TZ  in secured mode */
#error Flash is not supported in secure mode
#define FLASH_SECURITY_SEC
#else
#define FLASH_SECURITY_NA		/* For series which does not have
					 *  secured or non-secured mode
					 */
#define FLASH_STM32_SR		SR
#endif


#define FLASH_STM32_PRIV(dev) ((struct flash_stm32_priv *)((dev)->data))
#define FLASH_STM32_REGS(dev) (FLASH_STM32_PRIV(dev)->regs)


/* Redefintions of flags and masks to harmonize stm32 series: */
#if defined(CONFIG_SOC_SERIES_STM32G0X)
#if defined(FLASH_FLAG_BSY2)
#define FLASH_STM32_SR_BUSY	(FLASH_FLAG_BSY1 | FLASH_FLAG_BSY2);
#else
#define FLASH_STM32_SR_BUSY	(FLASH_SR_BSY1)
#endif /* defined(FLASH_FLAG_BSY2) */
#else
#define FLASH_STM32_SR_BUSY	(FLASH_FLAG_BSY)
#endif

#if defined(CONFIG_SOC_SERIES_STM32G0X)
#define FLASH_STM32_SR_CFGBSY	(FLASH_SR_CFGBSY)
#elif defined(FLASH_FLAG_CFGBSY)
#define FLASH_STM32_SR_CFGBSY	(FLASH_FLAG_CFGBSY)
#endif

#if defined(CONFIG_SOC_SERIES_STM32G0X)
/* STM32G0 HAL FLASH_FLAG_x don't represent bit-masks, need FLASH_SR_x instead */
#define FLASH_STM32_SR_OPERR	FLASH_SR_OPERR
#define FLASH_STM32_SR_PGERR	0
#define FLASH_STM32_SR_PROGERR	FLASH_SR_PROGERR
#define FLASH_STM32_SR_WRPERR	FLASH_SR_WRPERR
#define FLASH_STM32_SR_PGAERR	FLASH_SR_PGAERR
#define FLASH_STM32_SR_SIZERR	FLASH_SR_SIZERR
#define FLASH_STM32_SR_PGSERR	FLASH_SR_PGSERR
#define FLASH_STM32_SR_MISERR	FLASH_SR_MISERR
#define FLASH_STM32_SR_FASTERR	FLASH_SR_FASTERR
#if defined(FLASH_SR_RDERR)
#define FLASH_STM32_SR_RDERR	FLASH_SR_RDERR
#else
#define FLASH_STM32_SR_RDERR	0
#endif
#define FLASH_STM32_SR_PGPERR	0

#else /* !defined(CONFIG_SOC_SERIES_STM32G0X) */
#if defined(FLASH_FLAG_OPERR)
#define FLASH_STM32_SR_OPERR	FLASH_FLAG_OPERR
#else
#define FLASH_STM32_SR_OPERR	0
#endif

#if defined(FLASH_FLAG_PGERR)
#define FLASH_STM32_SR_PGERR	FLASH_FLAG_PGERR
#else
#define FLASH_STM32_SR_PGERR	0
#endif

#if defined(FLASH_FLAG_PROGERR)
#define FLASH_STM32_SR_PROGERR	FLASH_FLAG_PROGERR
#else
#define FLASH_STM32_SR_PROGERR	0
#endif

#if defined(FLASH_FLAG_WRPERR)
#define FLASH_STM32_SR_WRPERR	FLASH_FLAG_WRPERR
#else
#define FLASH_STM32_SR_WRPERR	0
#endif

#if defined(FLASH_FLAG_PGAERR)
#define FLASH_STM32_SR_PGAERR	FLASH_FLAG_PGAERR
#else
#define FLASH_STM32_SR_PGAERR	0
#endif

#if defined(FLASH_FLAG_SIZERR)
#define FLASH_STM32_SR_SIZERR	FLASH_FLAG_SIZERR
#else
#define FLASH_STM32_SR_SIZERR	0
#endif

#if defined(FLASH_FLAG_PGSERR)
#define FLASH_STM32_SR_PGSERR	FLASH_FLAG_PGSERR
#else
#define FLASH_STM32_SR_PGSERR	0
#endif

#if defined(FLASH_FLAG_MISERR)
#define FLASH_STM32_SR_MISERR	FLASH_FLAG_MISERR
#else
#define FLASH_STM32_SR_MISERR	0
#endif

#if defined(FLASH_FLAG_FASTERR)
#define FLASH_STM32_SR_FASTERR	FLASH_FLAG_FASTERR
#else
#define FLASH_STM32_SR_FASTERR	0
#endif

#if defined(FLASH_FLAG_RDERR)
#define FLASH_STM32_SR_RDERR	FLASH_FLAG_RDERR
#else
#define FLASH_STM32_SR_RDERR	0
#endif

#if defined(FLASH_FLAG_PGPERR)
#define FLASH_STM32_SR_PGPERR	FLASH_FLAG_PGPERR
#else
#define FLASH_STM32_SR_PGPERR	0
#endif

#endif /* !defined(CONFIG_SOC_SERIES_STM32G0X) */

#define FLASH_STM32_SR_ERRORS  (FLASH_STM32_SR_OPERR |			\
				FLASH_STM32_SR_PGERR |			\
				FLASH_STM32_SR_PROGERR |		\
				FLASH_STM32_SR_WRPERR |			\
				FLASH_STM32_SR_PGAERR |			\
				FLASH_STM32_SR_SIZERR |			\
				FLASH_STM32_SR_PGSERR |			\
				FLASH_STM32_SR_MISERR |			\
				FLASH_STM32_SR_FASTERR |		\
				FLASH_STM32_SR_RDERR |			\
				FLASH_STM32_SR_PGPERR)


#ifdef CONFIG_FLASH_PAGE_LAYOUT
static inline bool flash_stm32_range_exists(const struct device *dev,
					    off_t offset,
					    uint32_t len)
{
	struct flash_pages_info info;

	return !(flash_get_page_info_by_offs(dev, offset, &info) ||
		 flash_get_page_info_by_offs(dev, offset + len - 1, &info));
}
#endif	/* CONFIG_FLASH_PAGE_LAYOUT */

bool flash_stm32_valid_range(const struct device *dev, off_t offset,
			     uint32_t len, bool write);

int flash_stm32_write_range(const struct device *dev, unsigned int offset,
			    const void *data, unsigned int len);

int flash_stm32_block_erase_loop(const struct device *dev,
				 unsigned int offset,
				 unsigned int len);

int flash_stm32_wait_flash_idle(const struct device *dev);

#ifdef CONFIG_SOC_SERIES_STM32WBX
int flash_stm32_check_status(const struct device *dev);
#endif /* CONFIG_SOC_SERIES_STM32WBX */

#ifdef CONFIG_FLASH_PAGE_LAYOUT
void flash_stm32_page_layout(const struct device *dev,
			     const struct flash_pages_layout **layout,
			     size_t *layout_size);
#endif

#endif /* ZEPHYR_DRIVERS_FLASH_FLASH_STM32_H_ */
