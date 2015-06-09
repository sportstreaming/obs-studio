#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct mf_aac_encoder;
typedef struct mf_aac_encoder mf_aac_encoder_t;

typedef enum mf_aac_status {
	MF_AAC_FAILURE,
	MF_AAC_SUCCESS,
	MF_AAC_NOT_ACCEPTING,
	MF_AAC_NEED_MORE_INPUT
} mf_aac_status;

mf_aac_encoder_t *mf_aac_encoder_create(uint32_t bitrate,
		uint32_t channels, uint32_t sample_rate,
		uint32_t bits_per_sample);
void mf_aac_encoder_destroy(mf_aac_encoder_t *encoder);
bool mf_aac_encoder_initialize(mf_aac_encoder_t *encoder);
bool mf_aac_encoder_process_input(mf_aac_encoder_t *encoder,
		uint8_t *data, uint32_t data_length,
		uint64_t pts, enum mf_aac_status *status);
bool mf_aac_encoder_process_output(mf_aac_encoder_t *encoder,
		uint8_t **data, uint32_t *data_length,
		uint64_t *pts, enum mf_aac_status *status);
bool mf_aac_encoder_extra_data(mf_aac_encoder_t *encoder,
		uint8_t **extra_data, uint32_t *extra_data_length);
int32_t mf_aac_encoder_frame_size();

uint32_t mf_aac_encoder_bitrate(mf_aac_encoder_t *encoder);
uint32_t mf_aac_encoder_channels(mf_aac_encoder_t *encoder);
uint32_t mf_aac_encoder_sample_rate(mf_aac_encoder_t *encoder);
uint32_t mf_aac_encoder_bits_per_sample(mf_aac_encoder_t *encoder);

#ifdef __cplusplus
}
#endif
