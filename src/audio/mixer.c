// SPDX-License-Identifier: BSD-3-Clause
//
// Copyright(c) 2016 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//         Keyon Jie <yang.jie@linux.intel.com>

#include <sof/audio/buffer.h>
#include <sof/audio/component.h>
#include <sof/audio/format.h>
#include <sof/audio/mixer.h>
#include <sof/audio/pipeline.h>
#include <sof/common.h>
#include <sof/debug/panic.h>
#include <sof/drivers/ipc.h>
#include <sof/lib/alloc.h>
#include <sof/lib/cache.h>
#include <sof/list.h>
#include <sof/math/numbers.h>
#include <sof/platform.h>
#include <sof/string.h>
#include <sof/trace/trace.h>
#include <sof/ut.h>
#include <ipc/stream.h>
#include <ipc/topology.h>
#include <user/trace.h>
#include <stddef.h>
#include <stdint.h>

#define trace_mixer(__e, ...) \
	trace_event(TRACE_CLASS_MIXER, __e, ##__VA_ARGS__)
#define tracev_mixer(__e, ...) \
	tracev_event(TRACE_CLASS_MIXER, __e, ##__VA_ARGS__)
#define trace_mixer_error(__e, ...) \
	trace_error(TRACE_CLASS_MIXER, __e, ##__VA_ARGS__)

/* mixer component private data */
struct mixer_data {
	void (*mix_func)(struct comp_dev *dev, struct comp_buffer *sink,
		struct comp_buffer **sources, uint32_t count, uint32_t frames);
};

/* Mix n 16 bit PCM source streams to one sink stream */
static void mix_n_s16(struct comp_dev *dev, struct comp_buffer *sink,
		      struct comp_buffer **sources, uint32_t num_sources,
		      uint32_t frames)
{
	int16_t *src;
	int16_t *dest;
	int32_t val;
	int i;
	int j;
	int channel;
	uint32_t frag = 0;

	for (i = 0; i < frames; i++) {
		for (channel = 0; channel < dev->params.channels; channel++) {
			val = 0;

			for (j = 0; j < num_sources; j++) {
				src = buffer_read_frag_s16(sources[j], frag);
				val += *src;
			}

			dest = buffer_write_frag_s16(sink, frag);

			/* Saturate to 16 bits */
			*dest = sat_int16(val);

			frag++;
		}
	}
}

/* Mix n 32 bit PCM source streams to one sink stream */
static void mix_n_s32(struct comp_dev *dev, struct comp_buffer *sink,
		      struct comp_buffer **sources, uint32_t num_sources,
		      uint32_t frames)
{
	int32_t *src;
	int32_t *dest;
	int64_t val;
	int i;
	int j;
	int channel;
	uint32_t frag = 0;

	for (i = 0; i < frames; i++) {
		for (channel = 0; channel < dev->params.channels; channel++) {
			val = 0;

			for (j = 0; j < num_sources; j++) {
				src = buffer_read_frag_s32(sources[j], frag);
				val += *src;
			}

			dest = buffer_write_frag_s32(sink, frag);

			/* Saturate to 32 bits */
			*dest = sat_int32(val);

			frag++;
		}
	}
}

static struct comp_dev *mixer_new(struct sof_ipc_comp *comp)
{
	struct comp_dev *dev;
	struct sof_ipc_comp_mixer *mixer;
	struct sof_ipc_comp_mixer *ipc_mixer =
		(struct sof_ipc_comp_mixer *)comp;
	struct mixer_data *md;

	trace_mixer("mixer_new()");

	if (IPC_IS_COMP_SIZE_INVALID(ipc_mixer)) {
		IPC_COMP_SIZE_ERROR_TRACE(TRACE_CLASS_MIXER, ipc_mixer);
		return NULL;
	}

	dev = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM,
		COMP_SIZE(struct sof_ipc_comp_mixer));
	if (!dev)
		return NULL;

	mixer = (struct sof_ipc_comp_mixer *)&dev->comp;

	assert(!memcpy_s(mixer, sizeof(*mixer), ipc_mixer,
	    sizeof(struct sof_ipc_comp_mixer)));

	md = rzalloc(RZONE_RUNTIME, SOF_MEM_CAPS_RAM, sizeof(*md));
	if (!md) {
		rfree(dev);
		return NULL;
	}

	comp_set_drvdata(dev, md);
	dev->state = COMP_STATE_READY;
	return dev;
}

static void mixer_free(struct comp_dev *dev)
{
	struct mixer_data *md = comp_get_drvdata(dev);

	trace_mixer("mixer_free()");

	rfree(md);
	rfree(dev);
}

/* set component audio stream parameters */
static int mixer_params(struct comp_dev *dev)
{
	struct sof_ipc_comp_config *config = COMP_GET_CONFIG(dev);
	uint32_t period_bytes;
	int ret;

	trace_mixer("mixer_params()");

	/* calculate period size based on config */
	period_bytes = dev->frames * comp_frame_bytes(dev);
	if (period_bytes == 0) {
		trace_mixer_error("mixer_params() error: period_bytes = 0");
		return -EINVAL;
	}

	/* set downstream buffer size */
	ret = comp_set_sink_buffer(dev, period_bytes,
				   config->periods_sink);
	if (ret < 0) {
		trace_mixer_error("mixer_params() error: "
				  "comp_set_sink_buffer() failed");
		return ret;
	}

	return 0;
}

static int mixer_source_status_count(struct comp_dev *mixer, uint32_t status)
{
	struct comp_buffer *source;
	struct list_item *blist;
	int count = 0;

	/* count source with state == status */
	list_for_item(blist, &mixer->bsource_list) {
		source = container_of(blist, struct comp_buffer, sink_list);
		if (source->source->state == status)
			count++;
	}

	return count;
}

static inline int mixer_sink_status(struct comp_dev *mixer)
{
	struct comp_buffer *sink;

	sink = list_first_item(&mixer->bsink_list, struct comp_buffer,
			       source_list);
	return sink->sink->state;
}

/* used to pass standard and bespoke commands (with data) to component */
static int mixer_trigger(struct comp_dev *dev, int cmd)
{
	int ret;

	trace_mixer("mixer_trigger()");

	ret = comp_set_state(dev, cmd);
	if (ret < 0)
		return ret;

	if (ret == COMP_STATUS_STATE_ALREADY_SET)
		return PPL_STATUS_PATH_STOP;

	switch (cmd) {
	case COMP_TRIGGER_START:
	case COMP_TRIGGER_RELEASE:
		if (mixer_sink_status(dev) == COMP_STATE_ACTIVE)
			return 1; /* no need to go downstream */
		break;
	case COMP_TRIGGER_PAUSE:
	case COMP_TRIGGER_STOP:
		if (mixer_source_status_count(dev, COMP_STATE_ACTIVE) > 0) {
			dev->state = COMP_STATE_ACTIVE;
			return 1; /* no need to go downstream */
		}
		break;
	default:
		break;
	}

	return 0; /* send cmd downstream */
}

/*
 * Mix N source PCM streams to one sink PCM stream. Frames copied is constant.
 */
static int mixer_copy(struct comp_dev *dev)
{
	struct mixer_data *md = comp_get_drvdata(dev);
	struct comp_buffer *sink;
	struct comp_buffer *sources[PLATFORM_MAX_STREAMS];
	struct comp_buffer *source;
	struct list_item *blist;
	int32_t i = 0;
	int32_t num_mix_sources = 0;
	uint32_t frames = INT32_MAX;
	uint32_t source_bytes;
	uint32_t sink_bytes;

	tracev_mixer("mixer_copy()");

	sink = list_first_item(&dev->bsink_list, struct comp_buffer,
			       source_list);

	/* calculate the highest runtime component status
	 * between input streams
	 */
	list_for_item(blist, &dev->bsource_list) {
		source = container_of(blist, struct comp_buffer, sink_list);

		/* only mix the sources with the same state with mixer */
		if (source->source->state == dev->state)
			sources[num_mix_sources++] = source;

		/* too many sources ? */
		if (num_mix_sources == PLATFORM_MAX_STREAMS - 1)
			return 0;
	}

	/* don't have any work if all sources are inactive */
	if (num_mix_sources == 0)
		return 0;

	/* check for underruns */
	for (i = 0; i < num_mix_sources; i++)
		frames = MIN(frames, comp_avail_frames(sources[i], sink));

	/* Every source has the same format, so calculate bytes based
	 * on the first one.
	 */
	source_bytes = frames * comp_frame_bytes(sources[0]->source);
	sink_bytes = frames * comp_frame_bytes(sink->sink);

	tracev_mixer("mixer_copy(), source_bytes = 0x%x, sink_bytes = 0x%x",
		     source_bytes, sink_bytes);

	/* mix streams */
	md->mix_func(dev, sink, sources, i, frames);

	/* update source buffer pointers */
	for (i = --num_mix_sources; i >= 0; i--)
		comp_update_buffer_consume(sources[i], source_bytes);

	/* update sink buffer pointer */
	comp_update_buffer_produce(sink, sink_bytes);

	return 0;
}

static int mixer_reset(struct comp_dev *dev)
{
	struct list_item *blist;
	struct comp_buffer *source;

	trace_mixer("mixer_reset()");

	list_for_item(blist, &dev->bsource_list) {
		source = container_of(blist, struct comp_buffer, sink_list);
		/* only mix the sources with the same state with mixer*/
		if (source->source->state > COMP_STATE_READY)
			/* should not reset the downstream components */
			return 1;
	}

	comp_set_state(dev, COMP_TRIGGER_RESET);
	return 0;
}

/*
 * Prepare the mixer. The mixer may already be running at this point with other
 * sources. Make sure we only prepare the "prepared" source streams and not
 * the active or inactive sources.
 *
 * We should also make sure that we propagate the prepare call to downstream
 * if downstream is not currently active.
 */
static int mixer_prepare(struct comp_dev *dev)
{
	struct mixer_data *md = comp_get_drvdata(dev);
	struct list_item *blist;
	struct comp_buffer *source;
	int downstream = 0;
	int ret;

	trace_mixer("mixer_prepare()");

	/* does mixer already have active source streams ? */
	if (dev->state != COMP_STATE_ACTIVE) {
		/* currently inactive so setup mixer */
		md->mix_func = dev->params.frame_fmt == SOF_IPC_FRAME_S16_LE ?
			mix_n_s16 : mix_n_s32;

		ret = comp_set_state(dev, COMP_TRIGGER_PREPARE);
		if (ret < 0)
			return ret;

		if (ret == COMP_STATUS_STATE_ALREADY_SET)
			return PPL_STATUS_PATH_STOP;
	}

	/* check each mixer source state */
	list_for_item(blist, &dev->bsource_list) {
		source = container_of(blist, struct comp_buffer, sink_list);

		/* only prepare downstream if we have no active sources */
		if (source->source->state == COMP_STATE_PAUSED ||
		    source->source->state == COMP_STATE_ACTIVE) {
			downstream = 1;
		}
	}

	/* prepare downstream */
	return downstream;
}

static void mixer_cache(struct comp_dev *dev, int cmd)
{
	struct mixer_data *md;

	switch (cmd) {
	case CACHE_WRITEBACK_INV:
		trace_mixer("mixer_cache(), CACHE_WRITEBACK_INV");

		md = comp_get_drvdata(dev);

		dcache_writeback_invalidate_region(md, sizeof(*md));
		dcache_writeback_invalidate_region(dev, sizeof(*dev));
		break;

	case CACHE_INVALIDATE:
		trace_mixer("mixer_cache(), CACHE_INVALIDATE");

		dcache_invalidate_region(dev, sizeof(*dev));

		md = comp_get_drvdata(dev);
		dcache_invalidate_region(md, sizeof(*md));
		break;
	}
}

struct comp_driver comp_mixer = {
	.type	= SOF_COMP_MIXER,
	.ops	= {
		.new		= mixer_new,
		.free		= mixer_free,
		.params		= mixer_params,
		.prepare	= mixer_prepare,
		.trigger	= mixer_trigger,
		.copy		= mixer_copy,
		.reset		= mixer_reset,
		.cache		= mixer_cache,
	},
};

UT_STATIC void sys_comp_mixer_init(void)
{
	comp_register(&comp_mixer);
}

DECLARE_MODULE(sys_comp_mixer_init);
