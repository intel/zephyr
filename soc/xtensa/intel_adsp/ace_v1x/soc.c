/*
 * Copyright (c) 2022 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/arch/xtensa/cache.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/irq_nextlevel.h>

#include <ace_v1x-regs.h>
#include <adsp_memory.h>
#include <adsp_shim.h>
#include <cpu_init.h>
#include <soc.h>
#include <xtensa/hal.h>
#include <xtensa/xtruntime.h>

extern void soc_mp_init(void);
extern void win_setup(void);
extern void lp_sram_init(void);
extern void hp_sram_init(void);
extern void parse_manifest(void);

#define DSP_INIT_LPGPDMA(x)  (0x71A60 + (2 * x))
#define LPGPDMA_CTLOSEL_FLAG BIT(15)
#define LPGPDMA_CHOSEL_FLAG  0xFF

#define LPSRAM_MASK(x) 0x00000003
#define SRAM_BANK_SIZE (64 * 1024)
#define HOST_PAGE_SIZE 4096

#define MANIFEST_SEGMENT_COUNT 3
#define DELAY_COUNT	       256

__imr void hp_sram_init(void)
{
#ifndef CONFIG_BOARD_INTEL_ADSP_ACE15_MTPM_SIM
	uint32_t hpsram_ebb_quantity = mtl_hpsram_get_bank_count();
	volatile uint32_t *l2hsbpmptr = (volatile uint32_t *)MTL_L2MM->l2hsbpmptr;
	volatile uint8_t *status = (volatile uint8_t *)l2hsbpmptr + 4;
	int idx;

	for (idx = 0; idx < hpsram_ebb_quantity; ++idx) {
		*(l2hsbpmptr + idx * 2) = 0;
	}
	for (idx = 0; idx < hpsram_ebb_quantity; ++idx) {
		while (*(status + idx * 8) != 0) {
			z_idelay(DELAY_COUNT);
		}
	}
#endif
}

__imr void lp_sram_init(void)
{
#ifndef CONFIG_BOARD_INTEL_ADSP_ACE15_MTPM_SIM
	uint32_t lpsram_ebb_quantity = mtl_lpsram_get_bank_count();
	volatile uint32_t *l2usbpmptr = (volatile uint32_t *)MTL_L2MM->l2usbpmptr;

	for (uint32_t idx = 0; idx < lpsram_ebb_quantity; ++idx) {
		*(l2usbpmptr + idx * 2) = 0;
	}
#endif
}

__imr void boot_core0(void)
{
	int prid;

	prid = arch_proc_id();
	if (prid != 0) {
		((void (*)(void))DFDSPBRCP.bootctl[prid].baddr)();
	}

	cpu_early_init();

	hp_sram_init();
	win_setup();
	lp_sram_init();
	parse_manifest();
	soc_trace_init();
	z_xtensa_cache_flush_all();

	/* Zephyr! */
	extern FUNC_NORETURN void z_cstart(void);
	z_cstart();
}

static __imr void power_init(void)
{
	/* Disable idle power gating */
	DFDSPBRCP.bootctl[0].bctl |= DFDSPBRCP_BCTL_WAITIPCG | DFDSPBRCP_BCTL_WAITIPPG;

#if CONFIG_DMA_INTEL_ADSP_GPDMA
	sys_write32(LPGPDMA_CHOSEL_FLAG | LPGPDMA_CTLOSEL_FLAG, DSP_INIT_LPGPDMA(0));
	sys_write32(LPGPDMA_CHOSEL_FLAG | LPGPDMA_CTLOSEL_FLAG, DSP_INIT_LPGPDMA(1));
#endif

	/* Temporary HP clock setup */
	*((uint32_t*)(0x71B78)) = 0x207;
}

static __imr int soc_init(const struct device *dev)
{
	power_init();

#if CONFIG_MP_NUM_CPUS > 1
	soc_mp_init();
#endif

	return 0;
}

SYS_INIT(soc_init, PRE_KERNEL_1, 99);
