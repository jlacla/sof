// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2017 Intel Corporation. All rights reserved.
//
// Author: Seppo Ingalsuo <seppo.ingalsuo@linux.intel.com>
//         Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>

#include <sof/audio/buffer.h>
#include <sof/audio/component.h>
#include <sof/audio/eq_fir/fir_config.h>
#include <sof/audio/pipeline.h>
#include <sof/common.h>
#include <sof/debug/panic.h>
#include <sof/drivers/ipc.h>
#include <sof/lib/alloc.h>
#include <sof/lib/cache.h>
#include <sof/list.h>
#include <sof/platform.h>
#include <sof/string.h>
#include <sof/trace/trace.h>
#include <ipc/control.h>
#include <ipc/stream.h>
#include <ipc/topology.h>
#include <kernel/abi.h>
#include <user/eq.h>
#include <user/trace.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#if FIR_GENERIC
#include <sof/audio/eq_fir/fir.h>
#endif

#if FIR_HIFIEP
#include <sof/audio/eq_fir/fir_hifi2ep.h>
#endif

#if FIR_HIFI3
#include <sof/audio/eq_fir/fir_hifi3.h>
#endif

#define trace_eq(__e, ...) trace_event(TRACE_CLASS_EQ_FIR, __e, ##__VA_ARGS__)
#define tracev_eq(__e, ...) tracev_event(TRACE_CLASS_EQ_FIR, __e, ##__VA_ARGS__)
#define trace_eq_error(__e, ...) \
	trace_error(TRACE_CLASS_EQ_FIR, __e, ##__VA_ARGS__)

/* src component private data */
struct comp_data {
	struct fir_state_32x16 fir[PLATFORM_MAX_CHANNELS]; /**< filters state */
	struct sof_eq_fir_config *config; /**< pointer to setup blob */
	enum sof_ipc_frame source_format; /**< source frame format */
	enum sof_ipc_frame sink_format;   /**< sink frame format */
	int32_t *fir_delay;		  /**< pointer to allocated RAM */
	size_t fir_delay_size;		  /**< allocated size */
	void (*eq_fir_func_even)(struct fir_state_32x16 fir[],
				 struct comp_buffer *source,
				 struct comp_buffer *sink,
				 int frames, int nch);
	void (*eq_fir_func)(struct fir_state_32x16 fir[],
			    struct comp_buffer *source,
			    struct comp_buffer *sink,
			    int frames, int nch);
};

/* The optimized FIR functions variants need to be updated into function
 * set_fir_func. The cd->eq_fir_func is a function that can process any
 * number of samples. The cd->eq_fir_func_even is for optimized version
 * that is guaranteed to be called with even samples number.
 */

#if FIR_HIFI3
static inline void set_s16_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_2x_s16_hifi3;
	cd->eq_fir_func = eq_fir_s16_hifi3;
}

static inline void set_s24_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_2x_s24_hifi3;
	cd->eq_fir_func = eq_fir_s24_hifi3;
}

static inline void set_s32_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_2x_s32_hifi3;
	cd->eq_fir_func = eq_fir_s32_hifi3;
}
#elif FIR_HIFIEP
static inline void set_s16_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_2x_s16_hifiep;
	cd->eq_fir_func = eq_fir_s16_hifiep;
}

static inline void set_s24_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_2x_s24_hifiep;
	cd->eq_fir_func = eq_fir_s24_hifiep;
}

static inline void set_s32_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_2x_s32_hifiep;
	cd->eq_fir_func = eq_fir_s32_hifiep;
}
#else
/* FIR_GENERIC */
static inline void set_s16_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_s16;
	cd->eq_fir_func = eq_fir_s16;
}

static inline void set_s24_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_s24;
	cd->eq_fir_func = eq_fir_s24;
}

static inline void set_s32_fir(struct comp_data *cd)
{
	cd->eq_fir_func_even = eq_fir_s32;
	cd->eq_fir_func = eq_fir_s32;
}
#endif

static inline int set_fir_func(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	switch (dev->params.frame_fmt) {
	case SOF_IPC_FRAME_S16_LE:
		trace_eq("set_fir_func(), SOF_IPC_FRAME_S16_LE");
		set_s16_fir(cd);
		break;
	case SOF_IPC_FRAME_S24_4LE:
		trace_eq("set_fir_func(), SOF_IPC_FRAME_S24_4LE");
		set_s24_fir(cd);
		break;
	case SOF_IPC_FRAME_S32_LE:
		trace_eq("set_fir_func(), SOF_IPC_FRAME_S32_LE");
		set_s32_fir(cd);
		break;
	default:
		trace_eq_error("set_fir_func(), invalid frame_fmt");
		return -EINVAL;
	}
	return 0;
}

/* Pass-trough functions to replace FIR core while not configured for
 * response.
 */

static void eq_fir_s16_passthrough(struct fir_state_32x16 fir[],
				   struct comp_buffer *source,
				   struct comp_buffer *sink,
				   int frames, int nch)
{
	int16_t *x;
	int16_t *y;
	int i;
	int n = frames * nch;

	for (i = 0; i < n; i++) {
		x = buffer_read_frag_s16(source, i);
		y = buffer_write_frag_s16(sink, i);
		*y = *x;
	}
}

static void eq_fir_s32_passthrough(struct fir_state_32x16 fir[],
				   struct comp_buffer *source,
				   struct comp_buffer *sink,
				   int frames, int nch)
{
	int32_t *x;
	int32_t *y;
	int i;
	int n = frames * nch;

	for (i = 0; i < n; i++) {
		x = buffer_read_frag_s32(source, i);
		y = buffer_write_frag_s32(sink, i);
		*y = *x;
	}
}

/* Function to select pass-trough depending on PCM format */

static inline int set_pass_func(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	switch (dev->params.frame_fmt) {
	case SOF_IPC_FRAME_S16_LE:
		trace_eq("set_pass_func(), SOF_IPC_FRAME_S16_LE");
		cd->eq_fir_func_even = eq_fir_s16_passthrough;
		cd->eq_fir_func = eq_fir_s16_passthrough;
		break;
	case SOF_IPC_FRAME_S24_4LE:
	case SOF_IPC_FRAME_S32_LE:
		trace_eq("set_pass_func(), SOF_IPC_FRAME_S32_LE");
		cd->eq_fir_func_even = eq_fir_s32_passthrough;
		cd->eq_fir_func = eq_fir_s32_passthrough;
		break;
	default:
		trace_eq_error("set_pass_func() error: "
			       "invalid dev->params.frame_fmt");
		return -EINVAL;
	}
	return 0;
}

/*
 * EQ control code is next. The processing is in fir_ C modules.
 */

static void eq_fir_free_parameters(struct sof_eq_fir_config **config)
{
	rfree(*config);
	*config = NULL;
}

static void eq_fir_free_delaylines(struct comp_data *cd)
{
	struct fir_state_32x16 *fir = cd->fir;
	int i = 0;

	/* Free the common buffer for all EQs and point then
	 * each FIR channel delay line to NULL.
	 */
	rfree(cd->fir_delay);
	cd->fir_delay_size = 0;
	for (i = 0; i < PLATFORM_MAX_CHANNELS; i++)
		fir[i].delay = NULL;
}

static int eq_fir_setup(struct comp_data *cd, int nch)
{
	struct fir_state_32x16 *fir = cd->fir;
	struct sof_eq_fir_config *config = cd->config;
	struct sof_eq_fir_coef_data *lookup[SOF_EQ_FIR_MAX_RESPONSES];
	struct sof_eq_fir_coef_data *eq;
	int32_t *fir_delay;
	int16_t *coef_data;
	int16_t *assign_response;
	int resp;
	int i;
	int j;
	size_t s;
	size_t size_sum = 0;

	trace_eq("eq_fir_setup(), "
		 "channels_in_config = %u, number_of_responses = %u",
		 config->channels_in_config, config->number_of_responses);

	/* Sanity checks */
	if (nch > PLATFORM_MAX_CHANNELS ||
	    config->channels_in_config > PLATFORM_MAX_CHANNELS ||
	    !config->channels_in_config) {
		trace_eq_error("eq_fir_setup() error: "
			       "invalid channels_in_config");
		return -EINVAL;
	}
	if (config->number_of_responses > SOF_EQ_FIR_MAX_RESPONSES) {
		trace_eq_error("eq_fir_setup() error: number_of_responses > "
			       "SOF_EQ_FIR_MAX_RESPONSES");
		return -EINVAL;
	}

	/* Collect index of respose start positions in all_coefficients[]  */
	j = 0;
	assign_response = &config->data[0];
	coef_data = &config->data[config->channels_in_config];
	for (i = 0; i < SOF_EQ_FIR_MAX_RESPONSES; i++) {
		if (i < config->number_of_responses) {
			trace_eq("eq_fir_setup(), "
				 "index of respose start position = %u", j);
			eq = (struct sof_eq_fir_coef_data *)&coef_data[j];
			lookup[i] = eq;
			j += SOF_EQ_FIR_COEF_NHEADER + coef_data[j];
		} else {
			lookup[i] = NULL;
		}
	}

	/* Initialize 1st phase */
	for (i = 0; i < nch; i++) {
		/* Check for not reading past blob response to channel assign
		 * map. If the blob has smaller channel map then apply for
		 * additional channels the response that was used for the first
		 * channel. This allows to use mono blobs to setup multi
		 * channel equalization without stopping to an error.
		 */
		if (i < config->channels_in_config)
			resp = assign_response[i];
		else
			resp = assign_response[0];

		if (resp < 0) {
			/* Initialize EQ channel to bypass and continue with
			 * next channel response.
			 */
			fir_reset(&fir[i]);
			continue;
		}

		if (resp >= config->number_of_responses)
			return -EINVAL;

		/* Initialize EQ coefficients. */
		eq = lookup[resp];
		s = fir_init_coef(&fir[i], eq);
		if (s > 0)
			size_sum += s;
		else
			return -EINVAL;

		trace_eq("eq_fir_setup(), "
			 "ch = %d initialized to response = %d", i, resp);
	}

	/* If all channels were set to bypass there's no need to
	 * allocate delay. Just return with success.
	 */
	cd->fir_delay = NULL;
	cd->fir_delay_size = size_sum;
	if (!size_sum)
		return 0;

	/* Allocate all FIR channels data in a big chunk and clear it */
	cd->fir_delay = rballoc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, size_sum);
	if (!cd->fir_delay) {
		trace_eq_error("eq_fir_setup() error: alloc failed, size = %u",
			       size_sum);
		return -ENOMEM;
	}

	/* Initialize 2nd phase to set EQ delay lines pointers */
	fir_delay = cd->fir_delay;
	for (i = 0; i < nch; i++) {
		resp = assign_response[i];
		if (resp >= 0)
			fir_init_delay(&fir[i], &fir_delay);
	}

	return 0;
}

static int eq_fir_switch_store(struct fir_state_32x16 fir[],
			       struct sof_eq_fir_config *config,
			       uint32_t ch, int32_t response)
{
	/* Copy assign response from update. The EQ is initialized later
	 * when all channels have been updated.
	 */
	if (!config || ch >= config->channels_in_config)
		return -EINVAL;

	config->data[ch] = response;

	return 0;
}

/*
 * End of algorithm code. Next the standard component methods.
 */

static struct comp_dev *eq_fir_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct comp_data *cd;
	struct sof_ipc_comp_process *fir;
	struct sof_ipc_comp_process *ipc_fir
		= (struct sof_ipc_comp_process *)comp;
	size_t bs = ipc_fir->size;
	int i;

	trace_eq("eq_fir_new()");

	if (IPC_IS_COMP_SIZE_INVALID(ipc_fir)) {
		IPC_COMP_SIZE_ERROR_TRACE(TRACE_CLASS_EQ_FIR, ipc_fir);
		return NULL;
	}

	/* Check first before proceeding with dev and cd that coefficients
	 * blob size is sane.
	 */
	if (bs > SOF_EQ_FIR_MAX_SIZE) {
		trace_eq_error("eq_fir_new() error: coefficients blob size = "
			       "%u > SOF_EQ_FIR_MAX_SIZE", bs);
		return NULL;
	}

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		      COMP_SIZE(struct sof_ipc_comp_process));
	if (!dev)
		return NULL;

	fir = (struct sof_ipc_comp_process *)&dev->comp;
	assert(!memcpy_s(fir, sizeof(*fir), ipc_fir,
			 sizeof(struct sof_ipc_comp_process)));

	cd = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*cd));
	if (!cd) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, cd);

	cd->eq_fir_func_even = eq_fir_s32_passthrough;
	cd->eq_fir_func = eq_fir_s32_passthrough;
	cd->config = NULL;

	/* Allocate and make a copy of the coefficients blob and reset FIR. If
	 * the EQ is configured later in run-time the size is zero.
	 */
	if (bs) {
		cd->config = rballoc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, bs);
		if (!cd->config) {
			rfree(dev);
			rfree(cd);
			return NULL;
		}

		assert(!memcpy_s(cd->config, bs, ipc_fir->data, bs));
	}

	for (i = 0; i < PLATFORM_MAX_CHANNELS; i++)
		fir_reset(&cd->fir[i]);

	dev->state = COMP_STATE_READY;
	return dev;
}

static void eq_fir_free(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_eq("eq_fir_free()");

	eq_fir_free_delaylines(cd);
	eq_fir_free_parameters(&cd->config);

	rfree(cd);
	rfree(dev);
}

/* set component audio stream parameters */
static int eq_fir_params(struct comp_dev *dev)
{
	trace_eq("eq_fir_params()");

	/* All configuration work is postponed to prepare(). */
	return 0;
}

static int fir_cmd_get_data(struct comp_dev *dev,
			    struct sof_ipc_ctrl_data *cdata, int max_size)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	unsigned char *dst, *src;
	size_t offset;
	size_t bs;
	int ret = 0;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_BINARY:
		trace_eq("fir_cmd_get_data(), SOF_CTRL_CMD_BINARY");

		max_size -= sizeof(struct sof_ipc_ctrl_data) +
			sizeof(struct sof_abi_hdr);

		/* Copy back to user space */
		if (cd->config) {
			src = (unsigned char *)cd->config;
			dst = (unsigned char *)cdata->data->data;
			bs = cd->config->size;
			cdata->elems_remaining = 0;
			offset = 0;
			if (bs > max_size) {
				bs = (cdata->msg_index + 1) * max_size > bs ?
					bs - cdata->msg_index * max_size :
					max_size;
				offset = cdata->msg_index * max_size;
				cdata->elems_remaining = cd->config->size -
					offset;
			}
			cdata->num_elems = bs;
			trace_eq("fir_cmd_get_data(), blob size %zu "
				 "msg index %u max size %u offset %zu", bs,
				 cdata->msg_index, max_size, offset);
			assert(!memcpy_s(dst, ((struct sof_abi_hdr *)
					 (cdata->data))->size, src + offset,
					 bs));
			cdata->data->abi = SOF_ABI_VERSION;
			cdata->data->size = bs;
		} else {
			trace_eq_error("fir_cmd_get_data() error:"
				       "invalid cd->config");
			ret = -EINVAL;
		}
		break;
	default:
		trace_eq_error("fir_cmd_get_data() error: invalid cdata->cmd");
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int fir_cmd_set_data(struct comp_dev *dev,
			    struct sof_ipc_ctrl_data *cdata)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct sof_ipc_ctrl_value_comp *compv;
	unsigned char *dst, *src;
	uint32_t offset;
	int i;
	int ret = 0;

	switch (cdata->cmd) {
	case SOF_CTRL_CMD_ENUM:
		trace_eq("fir_cmd_set_data(), SOF_CTRL_CMD_ENUM");
		compv = (struct sof_ipc_ctrl_value_comp *)cdata->data->data;
		if (cdata->index == SOF_EQ_FIR_IDX_SWITCH) {
			for (i = 0; i < (int)cdata->num_elems; i++) {
				trace_eq("fir_cmd_set_data(), "
					 "SOF_EQ_FIR_IDX_SWITCH, "
					 "compv index = %u, svalue = %u",
					 compv[i].index, compv[i].svalue);
				ret = eq_fir_switch_store(cd->fir,
							  cd->config,
							  compv[i].index,
							  compv[i].svalue);
				if (ret < 0) {
					trace_eq_error("fir_cmd_set_data() "
						       "error: "
						       "eq_fir_switch_store() "
						       "failed");
					return -EINVAL;
				}
			}
		} else {
			trace_eq_error("fir_cmd_set_data() error: "
				       "invalid cdata->index = %u",
					cdata->index);
			return -EINVAL;
		}
		break;
	case SOF_CTRL_CMD_BINARY:
		trace_eq("fir_cmd_set_data(), SOF_CTRL_CMD_BINARY");

		if (dev->state != COMP_STATE_READY) {
			/* It is a valid request but currently this is not
			 * supported during playback/capture. The driver will
			 * re-send data in next resume when idle and the new
			 * EQ configuration will be used when playback/capture
			 * starts.
			 */
			trace_eq_error("fir_cmd_set_data() error: "
				       "driver is busy");
			return -EBUSY;
		}

		/* Copy new config, find size from header */
		trace_eq("fir_cmd_set_data(): blob size: %u msg_index %u",
			 cdata->num_elems + cdata->elems_remaining,
			 cdata->msg_index);
		if (cdata->num_elems + cdata->elems_remaining >
		    SOF_EQ_FIR_MAX_SIZE)
			return -EINVAL;

		if (cdata->msg_index == 0) {
			/* Check and free old config */
			eq_fir_free_parameters(&cd->config);

			/* Allocate buffer for copy of the blob. */
			cd->config = rballoc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
					     cdata->num_elems +
					     cdata->elems_remaining);

			if (!cd->config) {
				trace_eq_error("fir_cmd_set_data() error: "
					       "buffer allocation failed");
				return -EINVAL;
			}
			offset = 0;
		} else {
			offset = cd->config->size - cdata->elems_remaining -
				cdata->num_elems;
		}

		dst = (unsigned char *)cd->config;
		src = (unsigned char *)cdata->data->data;

		/* Just copy the configuration. The EQ will be initialized in
		 * prepare().
		 */
		assert(!memcpy_s(dst + offset, cdata->num_elems +
				 cdata->elems_remaining - offset, src,
				 cdata->num_elems));

		/* we can check data when elems_remaining == 0 */
		break;
	default:
		trace_eq_error("fir_cmd_set_data() error: invalid cdata->cmd");
		ret = -EINVAL;
		break;
	}

	return ret;
}

/* used to pass standard and bespoke commands (with data) to component */
static int eq_fir_cmd(struct comp_dev *dev, int cmd, void *data,
		      int max_data_size)
{
	struct sof_ipc_ctrl_data *cdata = data;
	int ret = 0;

	trace_eq("eq_fir_cmd()");

	switch (cmd) {
	case COMP_CMD_SET_DATA:
		ret = fir_cmd_set_data(dev, cdata);
		break;
	case COMP_CMD_GET_DATA:
		ret = fir_cmd_get_data(dev, cdata, max_data_size);
		break;
	case COMP_CMD_SET_VALUE:
		trace_eq("eq_fir_cmd(), COMP_CMD_SET_VALUE");
		break;
	case COMP_CMD_GET_VALUE:
		trace_eq("eq_fir_cmd(), COMP_CMD_GET_VALUE");
		break;
	default:
		trace_eq_error("eq_fir_cmd() error: invalid command");
		ret = -EINVAL;
	}

	return ret;
}

static int eq_fir_trigger(struct comp_dev *dev, int cmd)
{
	trace_eq("eq_fir_trigger()");

	return comp_set_state(dev, cmd);
}

/* copy and process stream data from source to sink buffers */
static int eq_fir_copy(struct comp_dev *dev)
{
	struct comp_copy_limits cl;
	struct comp_data *cd = comp_get_drvdata(dev);
	struct fir_state_32x16 *fir = cd->fir;
	int nch = dev->params.channels;

	tracev_comp("eq_fir_copy()");

	/* Get source, sink, number of frames etc. to process. */
	comp_get_copy_limits(dev, &cl);

	/* Check if number of frames to process if it is odd. The
	 * optimized FIR function to process even number of frames
	 * is lower load than generic version. In that case process
	 * one frame with generic FIR and the rest with even frames
	 * number FIR version.
	 */
	if (cl.frames & 0x1) {
		cl.frames--;
		cl.source_bytes -= cl.source_frame_bytes;
		cl.sink_bytes -= cl.sink_frame_bytes;

		/* Run EQ for one frame and update pointers */
		cd->eq_fir_func(fir, cl.source, cl.sink, 1, nch);
		comp_update_buffer_consume(cl.source, cl.source_frame_bytes);
		comp_update_buffer_produce(cl.sink, cl.sink_frame_bytes);
	}

	if (cl.frames > 1) {
		/* Run EQ function */
		cd->eq_fir_func_even(fir, cl.source, cl.sink, cl.frames, nch);

		/* calc new free and available */
		comp_update_buffer_consume(cl.source, cl.source_bytes);
		comp_update_buffer_produce(cl.sink, cl.sink_bytes);
	}

	return 0;
}

static int eq_fir_prepare(struct comp_dev *dev)
{
	struct comp_data *cd = comp_get_drvdata(dev);
	struct sof_ipc_comp_config *config = COMP_GET_CONFIG(dev);
	struct comp_buffer *sourceb;
	struct comp_buffer *sinkb;
	uint32_t sink_period_bytes;
	int ret;

	trace_eq("eq_fir_prepare()");

	ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
	if (ret < 0)
		return ret;

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
		return PPL_STATUS_PATH_STOP;

	/* EQ component will only ever have 1 source and 1 sink buffer. */
	sourceb = list_first_item(&dev->bsource_list,
				  struct comp_buffer, sink_list);
	sinkb = list_first_item(&dev->bsink_list,
				struct comp_buffer, source_list);

	/* get source data format */
	cd->source_format = comp_frame_fmt(sourceb->source);

	/* get sink data format and period bytes */
	cd->sink_format = comp_frame_fmt(sinkb->sink);
	sink_period_bytes = comp_period_bytes(sinkb->sink, dev->frames);

	/* Rewrite params format for this component to match the host side. */
	if (dev->params.direction == SOF_IPC_STREAM_PLAYBACK)
		dev->params.frame_fmt = cd->source_format;
	else
		dev->params.frame_fmt = cd->sink_format;

	/* set downstream buffer size */
	ret = comp_set_sink_buffer(dev, sink_period_bytes,
				   config->periods_sink);
	if (ret < 0) {
		trace_eq_error("eq_fir_prepare() error: "
			       "comp_set_sink_buffer() failed");
		goto err;
	}

	/* Initialize EQ */
	if (cd->config) {
		ret = eq_fir_setup(cd, dev->params.channels);
		if (ret < 0) {
			trace_eq_error("eq_fir_prepare() error: "
				       "eq_fir_setup failed.");
			goto err;
		}

		ret = set_fir_func(dev);
		return ret;
	}

	ret = set_pass_func(dev);
	return ret;

err:
	comp_set_state(dev, COMP_TRIGGER_RESET);
	return ret;
}

static int eq_fir_reset(struct comp_dev *dev)
{
	int i;
	struct comp_data *cd = comp_get_drvdata(dev);

	trace_eq("eq_fir_reset()");

	eq_fir_free_delaylines(cd);

	cd->eq_fir_func_even = eq_fir_s32_passthrough;
	cd->eq_fir_func = eq_fir_s32_passthrough;
	for (i = 0; i < PLATFORM_MAX_CHANNELS; i++)
		fir_reset(&cd->fir[i]);

	comp_set_state(dev, COMP_TRIGGER_RESET);
	return 0;
}

static void eq_fir_cache(struct comp_dev *dev, int cmd)
{
	struct comp_data *cd;

	switch (cmd) {
	case CACHE_WRITEBACK_INV:
		trace_eq("eq_fir_cache(), CACHE_WRITEBACK_INV");

		cd = comp_get_drvdata(dev);
		if (cd->config)
			dcache_writeback_invalidate_region(cd->config,
							   cd->config->size);
		if (cd->fir_delay)
			dcache_writeback_invalidate_region(cd->fir_delay,
							   cd->fir_delay_size);

		dcache_writeback_invalidate_region(cd, sizeof(*cd));
		dcache_writeback_invalidate_region(dev, sizeof(*dev));
		break;

	case CACHE_INVALIDATE:
		trace_eq("eq_fir_cache(), CACHE_INVALIDATE");

		dcache_invalidate_region(dev, sizeof(*dev));

		/* Note: The component data need to be retrieved after
		 * the dev data has been invalidated.
		 */
		cd = comp_get_drvdata(dev);
		dcache_invalidate_region(cd, sizeof(*cd));

		if (cd->fir_delay)
			dcache_invalidate_region(cd->fir_delay,
						 cd->fir_delay_size);
		if (cd->config)
			dcache_invalidate_region(cd->config,
						 cd->config->size);

		break;
	}
}

struct comp_driver comp_eq_fir = {
	.type = SOF_COMP_EQ_FIR,
	.ops = {
		.new = eq_fir_new,
		.free = eq_fir_free,
		.params = eq_fir_params,
		.cmd = eq_fir_cmd,
		.trigger = eq_fir_trigger,
		.copy = eq_fir_copy,
		.prepare = eq_fir_prepare,
		.reset = eq_fir_reset,
		.cache = eq_fir_cache,
	},
};

static void sys_comp_eq_fir_init(void)
{
	comp_register(&comp_eq_fir);
}

DECLARE_MODULE(sys_comp_eq_fir_init);
