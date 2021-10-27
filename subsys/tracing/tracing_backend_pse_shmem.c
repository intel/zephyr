/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <kernel.h>
#include <string.h>
#include <tracing_core.h>
#include <tracing_buffer.h>
#include <tracing_backend.h>
#include "soc.h"
#include "sedi.h"

#define SIZE_CTF_SHMEM    (CONFIG_PSE_SHMEM_TRACING_BUFFER_SIZE)
#if (SIZE_CTF_SHMEM & (ATT_ADDR_ALIGNED - 1))
#error "Wrong CTF SHMEM size is set, must be (N * 4096)"
#endif

#define SIZE_CTF_SHMEM_LBUF ((uint32_t)(SIZE_CTF_SHMEM - \
					sizeof(struct shmem_mgnt)))
#define NUM_ATT_ENTRY       (ATT_ENTRY_4_L2SRAM)
#define NUM_MISC_REGION     (MISC_SRAM_PROTECTION_REGION_4)
#define BAR_SIZE_CTF_SHMEM  (1)

static int ctf_dcache_enabled;

static uint8_t ctf_shmem[SIZE_CTF_SHMEM]
	__attribute__ ((aligned(SIZE_CTF_SHMEM))) = { 0 };

static struct shmem_mgnt {
	/* host to PSE, must be (N * CACHE_LINE_SIZE) */
	uint32_t head; /* Loop Buffer Head Index */
	uint32_t padding[7];

	/* PSE to host */
	uint32_t size; /* Loop Buffer Size */
	uint32_t tail; /* Loop Buffer Tail Index */
	uint32_t padding1[6];

	/* Loop Buffer */
	uint8_t lbuf[];
} *shmm = (void *)&ctf_shmem[0];

static void shmem_invalid_dcache(void *addr, uint32_t size)
{
	uint32_t off = (uint32_t)addr & (CACHE_LINE_SIZE - 1);
	uint32_t *aligned = (uint32_t *)((uint32_t)addr - off);

	if (!ctf_dcache_enabled) {
		if (!(SCB->CCR & (uint32_t)SCB_CCR_DC_Msk)) {
			return;
		}

		ctf_dcache_enabled = 1;
	}

	SCB_InvalidateDCache_by_Addr(aligned, (int32_t)(off + size));
}

static void shmem_flush_dcache(void *addr, uint32_t size)
{
	uint32_t off = (uint32_t)addr & (CACHE_LINE_SIZE - 1);
	uint32_t *aligned = (uint32_t *)((uint32_t)addr - off);

	if (!ctf_dcache_enabled) {
		if (!(SCB->CCR & (uint32_t)SCB_CCR_DC_Msk)) {
			return;
		}

		ctf_dcache_enabled = 1;
	}

	SCB_CleanDCache_by_Addr(aligned, (int32_t)(off + size));
}

static void tracing_backend_shmem_output(
		const struct tracing_backend *backend,
		uint8_t *data, uint32_t length)
{
	uint32_t ending = 0;
	uint32_t heading = 0;
	uint32_t head, tail, left;
	unsigned int key;

	key = irq_lock();

	shmem_invalid_dcache(&shmm->head, sizeof(shmm->head));
	head = shmm->head;
	tail = shmm->tail;

	if ((head >= SIZE_CTF_SHMEM_LBUF)
	    || (tail >= SIZE_CTF_SHMEM_LBUF)
	    || (shmm->size != SIZE_CTF_SHMEM_LBUF)) {
		printk("CTF: wrong SHMEM (%u, %u)(%u, %u)\n",
		       head, tail,
		       shmm->size, SIZE_CTF_SHMEM_LBUF);
		irq_unlock(key);
		return;
	}

	/* heading: empty buffer size @0x0
	 * ending:  empty buffer size @tail
	 */
	if (head > tail) {
		ending = head - tail;
	} else {
		heading = head;
		ending = shmm->size - tail;
	}

	if ((ending + heading) < (length + BAR_SIZE_CTF_SHMEM)) {
		/* bytes of BAR_SIZE_CTF_SHMEM is left empty and as bar
		 * to prevent tail index to keep up with head index
		 * no enough empty room, discard
		 */
		static uint32_t discarded;
		static uint32_t dts;

		discarded++;
		if ((k_uptime_get_32() - dts) > 10000) {
			printk("CTF Bottom: no room (0x%x, 0x%x, 0x%x), "
			       "discarded %u in last 10 seconds\n",
			       head, tail, shmm->size,
			       discarded);
			dts = k_uptime_get_32();
			discarded = 0;
		}

		irq_unlock(key);
		return;
	}

	/* copy data @tail first */
	if (ending) {
		if (ending >= length) {
			ending = length;
		}

		memcpy(&shmm->lbuf[tail], data, ending);
		shmem_flush_dcache(&shmm->lbuf[tail], ending);
		tail += ending;
		data += ending;
	}

	left = length - ending;
	/* then wrap and copy @0 */
	if (left > 0) {
		memcpy(&shmm->lbuf[0], data, left);
		shmem_flush_dcache(&shmm->lbuf[0], left);
		tail += left;
	}

	/* update Tail Index */
	if (tail >= shmm->size) {
		tail -= shmm->size;
	}
	shmm->tail = tail;
	shmem_flush_dcache(&shmm->tail, sizeof(shmm->tail));

	irq_unlock(key);
}

static void tracing_backend_shmem_init(void)
{
	uint32_t base;

	shmm->head = 0;
	shmm->size = SIZE_CTF_SHMEM_LBUF;
	shmm->tail = 0;
	shmem_flush_dcache(shmm, sizeof(*shmm));

	base = sys_read32(ATT_MMIO_BASE(NUM_ATT_ENTRY));
	sys_write32((uint32_t)shmm,
		    ATT_OCP_XLATE_OFFSET(NUM_ATT_ENTRY));
	sys_write32(~((uint32_t)SIZE_CTF_SHMEM - 1),
		    ATT_OCP_XLATE_MASK(NUM_ATT_ENTRY));
	sys_write32(base + SIZE_CTF_SHMEM,
		    ATT_MMIO_LIMIT(NUM_ATT_ENTRY));
	sys_write32((base | ATT_MMIO_BASE_VALID),
		    ATT_MMIO_BASE(NUM_ATT_ENTRY));

	sys_write32((uint32_t)shmm,
		    MISC_SRAM_PROTECTION_BASE(NUM_MISC_REGION));
	sys_write32(MISC_SRAM_SIZE_TO_PRG_SIZE(SIZE_CTF_SHMEM),
		    MISC_SRAM_PROTECTION_SIZE(NUM_MISC_REGION));
}

const struct tracing_backend_api tracing_backend_shmem_api = {
	.init = tracing_backend_shmem_init,
	.output  = tracing_backend_shmem_output
};

TRACING_BACKEND_DEFINE(tracing_backend_shmem, tracing_backend_shmem_api);
