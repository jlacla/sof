// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Jerome Laclavere <jerome.laclavere@nxp.com>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sof/debug/panic.h>
#include <sof/lib/io.h>

#include <sof/lib/alloc.h>
#include <platform/platform.h>
#include <sof/drivers/interrupt.h>
#include <sof/trace/trace.h>
#include <platform/drivers/irqsteer.h>
#include <sof/drivers/interrupt.h>
#include <ipc/topology.h>

typedef bool (*steer_irq_handler_f)(void *arg);

#define trace_pl_irq(__e, ...) trace_event(TRACE_CLASS_IRQ, __e, ##__VA_ARGS__)

static struct irq_desc *
steer_irq_parent[STEER_NB_IRQ_VECTORS] = {0};

/**
 * Unmask the given shared peripheral interrupt.
 * @param spi in range [0..511]
 */
#define IRQSTEER_CHCTRL(_base) ((_base) + 0U)

#define IRQSTEER_MASK(_base, _a) ((_base) + 4U + (4UL * (_a)))

/**
 * STEER registers access facilities
 */
static void steer_enable_channel(uint32_t chanctl)
{
	io_reg_update_bits(IRQSTEER_CHCTRL(BASE_IRQSTR_DSP),
			   BIT(chanctl),
			   BIT(chanctl));
}

static void steer_mask_updt(uint32_t spi, uint32_t val)
{
	assert(spi < 512U);

	uint32_t ch_num = (15UL - (spi / 32UL));
	uint32_t spi_bit = BIT(spi % 32UL);

	io_reg_update_bits(IRQSTEER_MASK(BASE_IRQSTR_DSP, ch_num),
			   spi_bit,
			   (val == 0U ? 0U : BIT(val)));
}


static void
steer_unmask_spi(unsigned int spi)
{
	steer_mask_updt(spi, 1U);
}

/**
 * Unmask the given shared peripheral interrupt.
 * @param spi in range [0..511]
 */
static void
steer_mask_spi(unsigned int spi)
{
	steer_mask_updt(spi, 0U);
}

static void irq_handler_parent(void *arg)
{
	uint32_t parent_idx = (uint32_t)arg;
	struct irq_desc *child;
	struct list_item *clist;
	bool handled = false;

	struct irq_desc *parent = steer_irq_parent[parent_idx];

	assert(parent);
	assert(parent->enabled_count > 0U);

	list_for_item(clist, &parent->child[0])	{
		steer_irq_handler_f f;

		child = container_of(clist, struct irq_desc, irq_list);
		// the child's handler returns a boolean so a cast is necessary
		f = (steer_irq_handler_f)child->handler;
		handled = handled || (*f)(child->handler_arg);
	}
	assert(handled);
}

static struct irq_desc *get_irq_parent(uint32_t irq)
{
	uint32_t parent_idx;
	struct irq_desc *parent;
	uint32_t irq_num = SOF_IRQ_NUMBER(irq);

	parent_idx = irq_num - IRQ_NUM_IRQSTR_DSP0;
	parent = steer_irq_parent[parent_idx];

	if (!parent) {
		parent = rzalloc(RZONE_RUNTIME,
				 SOF_MEM_CAPS_RAM,
				 sizeof(struct irq_desc));
		assert(parent);
		parent->handler = (void (*)(void *))irq_handler_parent;
		parent->handler_arg = (void *)parent_idx;
		parent->irq = irq;
		steer_irq_parent[parent_idx] = parent;
	}
	return parent;
}

void platform_interrupt_init(void)
{
	/* enable the Speer channel associated to DSP on IMX8*/
	steer_enable_channel(2U);
}

struct irq_desc *platform_irq_get_parent(uint32_t irq)
{
	struct irq_desc *parent = NULL;
	uint32_t irq_num = SOF_IRQ_NUMBER(irq);

	assert(irq_num < 32U);

	if (irq_num >= IRQ_NUM_IRQSTR_DSP0 && irq_num < IRQ_NUM_IRQSTR_DSP7)
		parent = get_irq_parent(irq);

	return parent;
}

void platform_interrupt_set(int irq)
{
	arch_interrupt_set(irq);
}

void platform_interrupt_clear(uint32_t irq, uint32_t mask)
{
	arch_interrupt_clear(irq);
}

uint32_t platform_interrupt_get_enabled(void)
{
	return 0;
}

void platform_interrupt_mask(uint32_t irq, uint32_t mask)
{
	struct irq_desc *parent = platform_irq_get_parent(irq);

	assert(parent);

	steer_mask_spi(SOF_IRQ_ID(irq));
}

void platform_interrupt_unmask(uint32_t irq, uint32_t mask)
{
	struct irq_desc *parent = platform_irq_get_parent(irq);

	assert(parent);

	steer_unmask_spi(SOF_IRQ_ID(irq));
}
