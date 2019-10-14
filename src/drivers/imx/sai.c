// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>
// Author: Jerome Laclavere <jerome.laclavere@nxp.com>
// Author: Guido Roncarolo <guido.roncarolo@nxp.com>

#include <errno.h>
#include <stdint.h>
#include <sof/audio/component.h>
#include <sof/bit.h>
#include <sof/drivers/edma.h>
#include <sof/drivers/sai.h>
#include <sof/lib/dai.h>
#include <sof/lib/dma.h>
#include <ipc/dai.h>

#define REG_TX_DIR 0
#define REG_RX_DIR 1

static void sai_start(struct dai *dai, int direction)
{
	tracev_sai("SAI: sai_start");

	dai_update_bits(dai, REG_SAI_XCSR(direction),
			REG_SAI_CSR_FRDE, REG_SAI_CSR_FRDE);
	/* enable DMA requests */
	dai_update_bits(dai, REG_SAI_XCSR(direction),
			REG_SAI_CSR_FWDE, REG_SAI_CSR_FWDE);
	/* transmitter enable */
	dai_update_bits(dai, REG_SAI_XCSR(direction),
			REG_SAI_CSR_TERE, REG_SAI_CSR_TERE);
	/* TODO: for the time being use half FIFO size as watermark */
	dai_update_bits(dai, REG_SAI_XCR1(direction),
			REG_SAI_CR1_RFW_MASK, SAI_FIFO_WORD_SIZE / 2);
	dai_update_bits(dai, REG_SAI_XCR3(direction),
			REG_SAI_CR3_TRCE_MASK, REG_SAI_CR3_TRCE(1));
}

static void sai_stop(struct dai *dai, int direction)
{
	trace_sai("SAI: sai_stop");

	uint32_t xcsr = 0U;

	dai_update_bits(dai, REG_SAI_XCSR(direction),
			REG_SAI_CSR_FRDE, 0);
	dai_update_bits(dai, REG_SAI_XCSR(direction),
			REG_SAI_CSR_XIE_MASK, 0);

	/* Check if the opposite direction is also disabled */
	xcsr = dai_read(dai, REG_SAI_XCSR(!direction));
	if (!(xcsr & REG_SAI_CSR_FRDE)) {
		/* Disable both directions and reset their FIFOs */
		dai_update_bits(dai, REG_SAI_TCSR, REG_SAI_CSR_TERE, 0);
		dai_update_bits(dai, REG_SAI_RCSR, REG_SAI_CSR_TERE, 0);

		/* Software Reset for both Tx and Rx */
		dai_update_bits(dai, REG_SAI_TCSR, REG_SAI_CSR_SR,
				REG_SAI_CSR_SR);
		dai_update_bits(dai, REG_SAI_RCSR, REG_SAI_CSR_SR,
				REG_SAI_CSR_SR);

		/* Clear SR bit to finish the reset */
		dai_update_bits(dai, REG_SAI_TCSR, REG_SAI_CSR_SR, 0U);
		dai_update_bits(dai, REG_SAI_RCSR, REG_SAI_CSR_SR, 0U);
	}
}

static int sai_context_store(struct dai *dai)
{
	return 0;
}

static int sai_context_restore(struct dai *dai)
{
	return 0;
}

static inline int sai_set_config(struct dai *dai,
				 struct sof_ipc_dai_config *config)
{
	trace_sai("SAI: sai_set_config");
	uint32_t val_cr2 = 0, val_cr4 = 0, val_cr5 = 0;
	uint32_t mask_cr2 = 0, mask_cr4 = 0, mask_cr5 = 0;
	/* TODO: this value will be provided by config */
	uint32_t sywd = 32;

	switch (config->format & SOF_DAI_FMT_FORMAT_MASK) {
	case SOF_DAI_FMT_I2S:
		/*
		 * Frame low, 1clk before data, one word length for frame sync,
		 * frame sync starts one serial clock cycle earlier,
		 * that is, together with the last bit of the previous
		 * data word.
		 */
		val_cr2 |= REG_SAI_CR2_BCP;
		val_cr4 |= REG_SAI_CR4_FSE | REG_SAI_CR4_FSP;
		val_cr4 |= REG_SAI_CR4_SYWD(sywd);
		val_cr4 |= REG_SAI_CR4_MF;
		val_cr4 |= REG_SAI_CR4_FSE;
		break;
	case SOF_DAI_FMT_LEFT_J:
		/*
		 * Frame high, one word length for frame sync,
		 * frame sync asserts with the first bit of the frame.
		 */
		val_cr2 |= REG_SAI_CR2_BCP;
		val_cr4 |= REG_SAI_CR4_SYWD(sywd);
		val_cr4 |= REG_SAI_CR4_MF;
		break;
	case SOF_DAI_FMT_DSP_A:
		/*
		 * Frame high, 1clk before data, one bit for frame sync,
		 * frame sync starts one serial clock cycle earlier,
		 * that is, together with the last bit of the previous
		 * data word.
		 */
		val_cr2 |= REG_SAI_CR2_BCP;
		val_cr4 |= REG_SAI_CR4_FSE;
		val_cr4 |= REG_SAI_CR4_SYWD(0U);
		val_cr4 |= REG_SAI_CR4_MF;
		val_cr4 |= REG_SAI_CR4_FSE;
		break;
	case SOF_DAI_FMT_DSP_B:
		/*
		 * Frame high, one bit for frame sync,
		 * frame sync asserts with the first bit of the frame.
		 */
		val_cr2 |= REG_SAI_CR2_BCP;
		val_cr4 |= REG_SAI_CR4_SYWD(0U);
		val_cr4 |= REG_SAI_CR4_MF;
		break;
	case SOF_DAI_FMT_PDM:
		val_cr2 |= REG_SAI_CR2_BCP;
		val_cr4 &= ~REG_SAI_CR4_MF;
		val_cr4 |= REG_SAI_CR4_MF;
		break;
	case SOF_DAI_FMT_RIGHT_J:
		val_cr4 |= REG_SAI_CR4_SYWD(sywd);
		val_cr4 |= REG_SAI_CR4_MF;
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock inversion */
	switch (config->format & SOF_DAI_FMT_INV_MASK) {
	case SOF_DAI_FMT_IB_IF:
		/* Invert both clocks */
		val_cr2 ^= REG_SAI_CR2_BCP;
		val_cr4 ^= REG_SAI_CR4_FSP;
		break;
	case SOF_DAI_FMT_IB_NF:
		/* Invert bit clock */
		val_cr2 ^= REG_SAI_CR2_BCP;
		break;
	case SOF_DAI_FMT_NB_IF:
		/* Invert frame clock */
		val_cr4 ^= REG_SAI_CR4_FSP;
		break;
	case SOF_DAI_FMT_NB_NF:
		/* Nothing to do for both normal cases */
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock master masks */
	switch (config->format & SOF_DAI_FMT_MASTER_MASK) {
	case SOF_DAI_FMT_CBS_CFS:
		break;
	case SOF_DAI_FMT_CBM_CFM:
		val_cr2 |= REG_SAI_CR2_MSEL_MCLK1;
		val_cr2 |= REG_SAI_CR2_BCD_MSTR;
		val_cr4 |= REG_SAI_CR4_FSD_MSTR;
		break;
	case SOF_DAI_FMT_CBS_CFM:
		val_cr4 |= REG_SAI_CR4_FSD_MSTR;
		break;
	case SOF_DAI_FMT_CBM_CFS:
		val_cr2 |= REG_SAI_CR2_BCD_MSTR;
		break;
	default:
		return -EINVAL;
	}

	/* set clock divider as (DIV + 1) * 2 */
	val_cr2 |= SAI_CLOCK_DIV;

	/* TODO: set number of slots from config */
	val_cr4 |= REG_SAI_CR4_FRSZ(SAI_TDM_SLOTS);

	val_cr5 |= REG_SAI_CR5_WNW(sywd) | REG_SAI_CR5_W0W(sywd) |
			REG_SAI_CR5_FBT(sywd);

	mask_cr2  = REG_SAI_CR2_BCP | REG_SAI_CR2_BCD_MSTR |
			REG_SAI_CR2_MSEL_MASK | REG_SAI_CR2_DIV_MASK;

	mask_cr4  = REG_SAI_CR4_MF | REG_SAI_CR4_FSE |
			REG_SAI_CR4_FSP | REG_SAI_CR4_FSD_MSTR |
			REG_SAI_CR4_FRSZ_MASK | REG_SAI_CR4_SYWD_MASK |
			REG_SAI_CR4_MF | REG_SAI_CR4_FSE;

	mask_cr5  = REG_SAI_CR5_WNW_MASK | REG_SAI_CR5_W0W_MASK |
			REG_SAI_CR5_FBT_MASK;

	dai_update_bits(dai, REG_SAI_XCR2(REG_TX_DIR), mask_cr2, val_cr2);
	dai_update_bits(dai, REG_SAI_XCR4(REG_TX_DIR), mask_cr4, val_cr4);
	dai_update_bits(dai, REG_SAI_XCR5(REG_TX_DIR), mask_cr5, val_cr5);
	/* turn on (set to zero) stereo slot */
	dai_update_bits(dai, REG_SAI_XMR(REG_TX_DIR),  REG_SAI_XMR_MASK,
			~(BIT(0) | BIT(1)));

	dai_update_bits(dai, REG_SAI_XCR2(REG_RX_DIR), mask_cr2, val_cr2);
	dai_update_bits(dai, REG_SAI_XCR4(REG_RX_DIR), mask_cr4, val_cr4);
	dai_update_bits(dai, REG_SAI_XCR5(REG_RX_DIR), mask_cr5, val_cr5);
	/* turn on (set to zero) stereo slot */
	dai_update_bits(dai, REG_SAI_XMR(REG_RX_DIR), REG_SAI_XMR_MASK,
			~(BIT(0) | BIT(1)));

	return 0;
}

static int sai_trigger(struct dai *dai, int cmd, int direction)
{
	trace_sai("SAI: sai_trigger");

	switch (cmd) {
	case COMP_TRIGGER_START:
		sai_start(dai, direction);
		break;
	case COMP_TRIGGER_STOP:
		sai_stop(dai, direction);
		break;
	case COMP_TRIGGER_PAUSE:
		sai_stop(dai, direction);
		break;
	case COMP_TRIGGER_RELEASE:
		break;
	case COMP_TRIGGER_SUSPEND:
		break;
	case COMP_TRIGGER_RESUME:
		break;
	default:
		trace_sai_error("SAI: invalid trigger cmd %d", cmd);
		break;
	}
	return 0;
}

static int sai_probe(struct dai *dai)
{
	trace_sai("SAI: sai_probe");

	/* Software Reset for both Tx and Rx */
	dai_update_bits(dai, REG_SAI_TCSR, REG_SAI_CSR_SR, REG_SAI_CSR_SR);
	dai_update_bits(dai, REG_SAI_RCSR, REG_SAI_CSR_SR, REG_SAI_CSR_SR);

	/* Clear SR bit to finish the reset */
	dai_update_bits(dai, REG_SAI_TCSR, REG_SAI_CSR_SR, 0U);
	dai_update_bits(dai, REG_SAI_RCSR, REG_SAI_CSR_SR, 0U);

	/* Reset all Tx register values */
	dai_write(dai, REG_SAI_TCR2, 0U);
	dai_write(dai, REG_SAI_TCR3, 0U);
	dai_write(dai, REG_SAI_TCR4, 0U);
	dai_write(dai, REG_SAI_TCR5, 0U);
	dai_write(dai, REG_SAI_TMR,  0U);

	/* Reset all Rx register values */
	dai_write(dai, REG_SAI_RCR2, 0U);
	dai_write(dai, REG_SAI_RCR3, 0U);
	dai_write(dai, REG_SAI_RCR4, 0U);
	dai_write(dai, REG_SAI_RCR5, 0U);
	dai_write(dai, REG_SAI_RMR,  0U);

	return 0;
}

static int sai_get_handshake(struct dai *dai, int direction, int stream_id)
{
	return dai->plat_data.fifo[direction].handshake;
}

static int sai_get_fifo(struct dai *dai, int direction, int stream_id)
{
	switch (direction) {
	case DAI_DIR_PLAYBACK:
	case DAI_DIR_CAPTURE:
		return dai_fifo(dai, direction); /* stream_id is unused */
	default:
		trace_sai_error("sai_get_fifo(): Invalid direction");
		return -EINVAL;
	}
}

const struct dai_driver sai_driver = {
	.type = SOF_DAI_IMX_SAI,
	.dma_dev = DMA_DEV_SAI,
	.ops = {
		.trigger		= sai_trigger,
		.set_config		= sai_set_config,
		.pm_context_store	= sai_context_store,
		.pm_context_restore	= sai_context_restore,
		.probe			= sai_probe,
		.get_handshake		= sai_get_handshake,
		.get_fifo		= sai_get_fifo,
	},
};
