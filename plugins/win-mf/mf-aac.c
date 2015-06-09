#include <obs-module.h>

#include "mf-aac-encoder.h"

struct mf_aac_enc {
	obs_encoder_t *obs_enc;
	mf_aac_encoder_t *aac_enc;
};
typedef struct mf_aac_enc mf_aac_enc_t;

static const char *mfaac_getname(void)
{
	return obs_module_text("MFAACEnc");
}

static obs_properties_t *mfaac_properties(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_int(props, "bitrate",
			obs_module_text("Bitrate"), 32, 256, 32);

	return props;
}

static void mfaac_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "bitrate", 128);
}

static void *mfaac_create(obs_data_t *settings, obs_encoder_t *encoder)
{
	mf_aac_enc_t *enc = bzalloc(sizeof(mf_aac_enc_t));
	mf_aac_encoder_t *aac_enc = NULL;

	audio_t *audio = obs_encoder_audio(encoder);

	uint32_t bitrate = (uint32_t)obs_data_get_int(settings, "bitrate");
	uint32_t channels = (uint32_t)audio_output_get_channels(audio);
	uint32_t sample_rate = audio_output_get_sample_rate(audio);
	uint32_t bits_per_sample = 16;

	 aac_enc = mf_aac_encoder_create(bitrate, channels, sample_rate,
			bits_per_sample);
	 if (aac_enc == NULL)
		goto fail;

	if (!mf_aac_encoder_initialize(aac_enc))
		goto fail;

	enc->obs_enc = encoder;
	enc->aac_enc = aac_enc;

	return enc;

fail:
	if (aac_enc != NULL)
		mf_aac_encoder_destroy(aac_enc);
	if (enc != NULL)
		bfree(enc);

	return NULL;
}

static void mfaac_destroy(void *data)
{
	mf_aac_enc_t *enc = data;
	mf_aac_encoder_destroy(enc->aac_enc);
	bfree(enc);
}

static bool mfaac_encode(void *data, struct encoder_frame *frame,
		struct encoder_packet *packet, bool *received_packet)
{
	mf_aac_enc_t *enc = data;
	enum mf_aac_status status;

	uint8_t *output_data;
	uint32_t output_data_length;
	uint64_t output_pts;

	if (!mf_aac_encoder_process_input(enc->aac_enc, frame->data[0],
			frame->linesize[0], frame->pts, &status))
		return false;

	// This shouldn't happen since we drain right after
	// we process input
	if (status == MF_AAC_NOT_ACCEPTING)
		return false;

	if (!mf_aac_encoder_process_output(enc->aac_enc, &output_data,
		&output_data_length, &output_pts, &status))
		return false;

	// Needs more input, not a failure case
	if (status == MF_AAC_NEED_MORE_INPUT)
		return true;

	packet->pts = output_pts;
	packet->dts = output_pts;
	packet->data = output_data;
	packet->size = output_data_length;
	packet->type = OBS_ENCODER_AUDIO;
	packet->timebase_num = 1;
	packet->timebase_den = mf_aac_encoder_sample_rate(enc->aac_enc);

	return *received_packet = true;
}

static bool mfaac_extra_data(void *data, uint8_t **extra_data, size_t *size)
{
	mf_aac_enc_t *enc = data;
	uint32_t length;
	if (mf_aac_encoder_extra_data(enc->aac_enc, extra_data, &length)){
		*size = length;
		return true;
	}
	return false;
}

static void mfaac_audio_info(void *data, struct audio_convert_info *info)
{
	UNUSED_PARAMETER(data);
	info->format = AUDIO_FORMAT_16BIT;
}

static size_t mfaac_frame_size(void *data)
{
	UNUSED_PARAMETER(data);
	return mf_aac_encoder_frame_size();
}

struct obs_encoder_info mf_aac_enc_info = {
	.id = "mf_aac",
	.type = OBS_ENCODER_AUDIO,
	.codec = "AAC",
	.get_name = mfaac_getname,
	.create = mfaac_create,
	.destroy = mfaac_destroy,
	.encode = mfaac_encode,
	.get_frame_size = mfaac_frame_size,
	.get_defaults = mfaac_defaults,
	.get_properties = mfaac_properties,
	.get_extra_data = mfaac_extra_data,
	.get_audio_info = mfaac_audio_info,
};
