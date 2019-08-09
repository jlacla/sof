// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright 2019 NXP
//
// Author: Daniel Baluta <daniel.baluta@nxp.com>

#include <sof/common.h>
#include <sof/drivers/esai.h>
#include <sof/lib/dai.h>
#include <sof/lib/memory.h>
#include <ipc/dai.h>
#include <platform/drivers/fsl_esai.h>
#include <platform/drivers/irqsteer.h>

#define SOF_IMX_IRQ(_spi, _irq_num)	SOF_ID_IRQ(_spi, 0, 0, 0, _irq_num)

static struct dai
	dai_esai_instance[] = {
	[0] = {
		.index = 0,
		.plat_data = {
			.base = ADMA__ESAI0_BASE,
			.irq = SOF_IMX_IRQ(STEER_ESAI0_CTRL_ID,
					   STEERSPI2IRQ(STEER_ESAI0_CTRL_ID)),
		},
		.drv = &esai_driver,
	},
#if IMX8QM
	[1] = {
		.index = 1,
		.plat_data = {
			.base = ADMA__ESAI1_BASE,
			.irq = SOF_IMX_IRQ(STEER_ESAI1_CTRL_ID,
					   STEERSPI2IRQ(STEER_ESAI1_CTRL_ID)),
		},
		.drv = &esai_driver,
	},
#endif
};

static struct dai_type_info dti[] = {
	{
		.type = SOF_DAI_IMX_ESAI,
		.dai_array = dai_esai_instance,
		.num_dais = ARRAY_SIZE(dai_esai_instance)
	},
};

int dai_init(void)
{
	dai_install(dti, ARRAY_SIZE(dti));
	return 0;
}
