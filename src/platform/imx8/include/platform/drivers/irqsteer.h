/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2019 NXP
 *
 * File:   irqsteer.h
 * Author: Author: Jerome Laclavere <jerome.laclavere@nxp.com>
 */

#ifndef IRQSTEER_H
#define IRQSTEER_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Management of the STEER Shared Peripheral Interrupt channels (SPI).
 *
 * the 512 Shared Peripheral Interrupts (SPI) from the IRQ_STEER are multiplexed
 * over 8 different interrupts vectors on the HIFI4
 * in the HIFI4, from vectors 19 to 26
 * Example : the ESAI0's SPI is 441: 441/64=6.89 => DSP's IRQ 25
 */
#define STEERSPI2IRQ(spi) ((uint32_t)\
			(((spi) / 64UL) + (uint32_t)IRQ_NUM_IRQSTR_DSP0))

inline uint32_t sof_imx_irq(uint32_t spi)
	{return SOF_ID_IRQ(spi, 0, 0, 0, STEERSPI2IRQ(spi)); }

/**
 * Mapping of the IMX8 HIFI4 STEER IRQ sources
 */

#define STEER_NB_IRQ_VECTORS          (8U)

/* I.MX8 Shared Peripheral Interrupt number for the ESAI0 interrupts */
#define STEER_ESAI0_CTRL_ID    420U  /* ESAI0 interrupts : errors  */
#define STEER_ESAI0_DMA_ID     420U  /* DMA#0 CH6 and DMA#0 CH7*/

/* I.MX8 Shared Peripheral Interrupt number for the ESAI1 interrupts */
#define STEER_ESAI1_CTRL_ID    421U  /* ESAI1 interrupts : errors  */
#define STEER_ESAI1_DMA_ID     421U  /* DMA#1 CH6 and DMA#1 CH7*/

/* I.MX8 shared Peripheral interrupt numbers for the SAI0's interrupts */
#define STEER_SAI0_CTRL_ID     346U /* SAI0 interrupts : errors  */
#define STEER_SAI0_DMA_ID      347U /* DMA#0 CH12 (RX)  DMA#0 CH13 (TX)*/

/* I.MX8 shared Peripheral interrupt numbers for the SAI1's interrupts */
#define STEER_SAI1_CTRL_ID     348U /* SAI1 interrupts : errors  */
#define STEER_SAI1_DMA_ID      349U /* DMA#0 CH14 (RX)  DMA#0 CH15 (TX)*/

/* I.MX8 shared Peripheral interrupt numbers for the SAI2's interrupts */
#define STEER_SAI2_CTRL_ID     350U /* SAI2 interrupts : errors  */
#define STEER_SAI2_DMA_ID      351U /* DMA#0 CH16 (RX) */

/* I.MX8 shared Peripheral interrupt numbers for the SAI3's interrupts */
#define STEER_SAI3_CTRL_ID     355U /* SAI3 interrupts : errors  */
#define STEER_SAI3_DMA_ID      356U /* DMA#0 CH17 (RX) */

#ifdef __cplusplus
}
#endif

#endif /* IRQSTEER_H */
