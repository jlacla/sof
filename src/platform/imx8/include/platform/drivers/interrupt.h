/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2019 NXP
 *
 * Author: Daniel Baluta <daniel.baluta@nxp.com>
 */

#ifdef __SOF_DRIVERS_INTERRUPT_H__

#ifndef __PLATFORM_DRIVERS_INTERRUPT_H__
#define __PLATFORM_DRIVERS_INTERRUPT_H__

#define IRQ_NUM_TIMER0		2	/* Level 2 */
#define IRQ_NUM_TIMER1		3	/* Level 3 */
#define IRQ_NUM_MU		7	/* Level 2 */
#define IRQ_NUM_SOFTWARE0	8	/* Level 1 */
#define IRQ_NUM_SOFTWARE1	9	/* Level 2 */
/**
 * The 512 IMX8 IRQ steer channels are interrupting HIFI4 through the eight IRQ
 * 19 to 26. Several interrupt source can share a same IRQ.
 * The used IRQ is equal to the steer irq channel number divided by 64.
 */
#define IRQ_NUM_IRQSTR_DSP0	19	/* Level 2 */
#define IRQ_NUM_IRQSTR_DSP1	20	/* Level 2 */
#define IRQ_NUM_IRQSTR_DSP2	21	/* Level 2 */
#define IRQ_NUM_IRQSTR_DSP3	22	/* Level 2 */
#define IRQ_NUM_IRQSTR_DSP4	23	/* Level 2 */
#define IRQ_NUM_IRQSTR_DSP5	24	/* Level 2 */
#define IRQ_NUM_IRQSTR_DSP6	25	/* Level 2 */
#define IRQ_NUM_IRQSTR_DSP7	26	/* Level 2 */

/* IRQ Masks */
#define IRQ_MASK_TIMER0		(1 << IRQ_NUM_TIMER0)
#define IRQ_MASK_TIMER1		(1 << IRQ_NUM_TIMER1)
#define IRQ_MASK_MU		(1 << IRQ_NUM_MU)
#define IRQ_MASK_SOFTWARE0	(1 << IRQ_NUM_SOFTWARE0)
#define IRQ_MASK_SOFTWARE1	(1 << IRQ_NUM_SOFTWARE1)
#define IRQ_MASK_IRQSTR_DSP0	(1 << IRQ_NUM_IRQSTR_DSP0)
#define IRQ_MASK_IRQSTR_DSP1	(1 << IRQ_NUM_IRQSTR_DSP1)
#define IRQ_MASK_IRQSTR_DSP2	(1 << IRQ_NUM_IRQSTR_DSP2)
#define IRQ_MASK_IRQSTR_DSP3	(1 << IRQ_NUM_IRQSTR_DSP3)
#define IRQ_MASK_IRQSTR_DSP4	(1 << IRQ_NUM_IRQSTR_DSP4)
#define IRQ_MASK_IRQSTR_DSP5	(1 << IRQ_NUM_IRQSTR_DSP5)
#define IRQ_MASK_IRQSTR_DSP6	(1 << IRQ_NUM_IRQSTR_DSP6)
#define IRQ_MASK_IRQSTR_DSP7	(1 << IRQ_NUM_IRQSTR_DSP7)

/*
 * PLATFORM_IRQ_CHILDREN : In the IMX's SOF interrupt model (steer irq)
 * all the children interrupt descriptors will be queued in a same list of their
 * parent: &parent->child[SOF_IRQ_BIT(irq)]. As a consequence SOF_IRQ_BIT(irq)
 * must always be equal to 0 which is fine.
 * So the dimension of this array is 1, so PLATFORM_IRQ_CHILDREN=1.
 */
#define PLATFORM_IRQ_CHILDREN	1 /* one single list of children per parent*/

#endif /* __PLATFORM_DRIVERS_INTERRUPT_H__ */

#else

#error "This file shouldn't be included from outside of sof/drivers/interrupt.h"

#endif /* __SOF_DRIVERS_INTERRUPT_H__ */
