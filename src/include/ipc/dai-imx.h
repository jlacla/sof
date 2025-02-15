/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 2019 NXP
 *
 * Author: Daniel Baluta <daniel.baluta@nxp.com>
 */

#ifndef __IPC_DAI_IMX_H__
#define __IPC_DAI_IMX_H__

#include <ipc/header.h>
#include <stdint.h>

/* ESAI Configuration Request - SOF_IPC_DAI_ESAI_CONFIG */
struct sof_ipc_dai_esai_params {
	struct sof_ipc_hdr hdr;

	/* MCLK */
	uint16_t reserved1;
	uint16_t mclk_id;
	uint32_t mclk_rate; /* MCLK frequency in Hz */
	uint32_t mclk_direction;

	/* TDM */
	uint32_t tdm_slots;
	uint32_t rx_slots;
	uint32_t tx_slots;
	uint16_t tdm_slot_width;
	uint16_t reserved2;	/* alignment */

} __attribute__((packed));

#endif /* __IPC_DAI_IMX_H__ */
