#include <obs-module.h>

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>
#undef WIN32_MEAN_AND_LEAN

#include <Shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <Mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <comdef.h>

#include <util/windows/ComPtr.hpp>

#include <memory>

#include "mf-aac-encoder.h"

#define blog(level, format, ...) blog(level, "MFAAC: " format, ##__VA_ARGS__)
#define blog_com(msg, hr) blog(LOG_ERROR, msg " failed,  %S (0x%08lx)", \
		_com_error(hr).ErrorMessage(), hr)
#define HRC(r) \
	if(FAILED(hr = (r))) { \
		blog_com(#r, hr); \
		goto fail; \
	}

static const UINT32 VALID_BITRATES[] =
{
	96,  // 120,
	128, // 160,
	160, // 200,
	192  // 240
};

static const UINT32 VALID_CHANNELS [] =
{
	1,
	2
};

static const UINT32 VALID_BITS_PER_SAMPLE [] = {
	16
};

static const UINT32 VALID_SAMPLERATES [] = {
	44100,
	48000
};

template <size_t N>
bool IsValid(const UINT32(&validValues)[N], UINT32 value)
{
	for (int i = 0; i < N; i++) {
		if (validValues[i] == value)
			return true;
	}
	return false;
};

namespace {
	class MFAACEncoder
	{
	public:
		MFAACEncoder(UINT32 bitrate, UINT32 channels,
				UINT32 sampleRate, UINT32 bitsPerSample)
		: bitrate(bitrate), channels(channels),
				sampleRate(sampleRate),
				bitsPerSample(bitsPerSample) {}

		MFAACEncoder& operator=(MFAACEncoder const&) = delete;

		bool Initialize();
		bool ProcessInput(UINT8 *data, UINT32 dataLength,
			UINT64 pts, enum mf_aac_status *status);
		bool ProcessOutput(UINT8 **data, UINT32 *dataLength,
			UINT64 *pts, enum mf_aac_status *status);
		bool ExtraData(UINT8 **extraData, UINT32 *extraDataLength);

		UINT32 Bitrate() { return bitrate; }
		UINT32 Channels() { return channels; }
		UINT32 SampleRate() { return sampleRate; }
		UINT32 BitsPerSample() { return bitsPerSample; }

	private:
		void InitializeExtraData();
		HRESULT CreateMediaTypes(ComPtr<IMFMediaType> &inputType,
				ComPtr<IMFMediaType> &outputType);
		HRESULT EnsureCapacity(ComPtr<IMFSample> &sample, DWORD length);

	private:
		const UINT32 bitrate;
		const UINT32 channels;
		const UINT32 sampleRate;
		const UINT32 bitsPerSample;

		ComPtr<IMFTransform> transform;
		ComPtr<IMFSample> outputSample;
		std::unique_ptr<BYTE> packetBuffer;
		DWORD packetBufferLength;
		UINT8 extraData[5];
	};
};


HRESULT MFAACEncoder::CreateMediaTypes(ComPtr<IMFMediaType> &i,
		ComPtr<IMFMediaType> &o)
{
	HRESULT hr;
	HRC(MFCreateMediaType(&i));
	HRC(MFCreateMediaType(&o));

	HRC(i->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	HRC(i->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
	HRC(i->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample));
	HRC(i->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate));
	HRC(i->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels));

	HRC(o->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	HRC(o->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC));
	HRC(o->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, bitsPerSample));
	HRC(o->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate));
	HRC(o->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, channels));
	HRC(o->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
		(bitrate * 1000) / 8));

	return S_OK;
fail:
	return hr;
}

void MFAACEncoder::InitializeExtraData()
{
	UINT16 *extraData16 = (UINT16 *)extraData;
	UINT16 profile = 2; //Low Complexity
#define SWAPU16(x) (x>>8) | (x<<8)
	// Profile
	// XXXX X... .... ....
	*extraData16 = profile << 11;
	// Sample Index (3=48, 4=44.1)
	// .... .XXX X... ....
	*extraData16 |= sampleRate == 48000 ? 3 : 4 << 7;
	// Channels
	// .... .... .XXX X...
	*extraData16 |= channels << 3;
	*extraData16 = SWAPU16(*extraData16);

	// Extensions
	extraData16++;
	*extraData16 = 0x2b7 << 5;
	// Profile
	*extraData16 |= profile;
	*extraData16 = SWAPU16(*extraData16);

	extraData[4] = 0;
#undef SWAPU16
}

bool MFAACEncoder::Initialize()
{
	HRESULT hr;

	ComPtr<IMFTransform> transform_;
	ComPtr<IMFMediaType> inputType, outputType;

	if (!IsValid(VALID_BITRATES, bitrate)) {
		blog(LOG_WARNING, "invalid bitrate (kbps) '%d'", bitrate);
		return false;
	}
	if (!IsValid(VALID_CHANNELS, channels)) {
		blog(LOG_WARNING, "invalid channel count '%d", channels);
		return false;
	}
	if (!IsValid(VALID_SAMPLERATES, sampleRate)) {
		blog(LOG_WARNING, "invalid sample rate (hz) '%d", sampleRate);
		return false;
	}
	if (!IsValid(VALID_BITS_PER_SAMPLE, bitsPerSample)) {
		blog(LOG_WARNING, "invalid bits-per-sample (bits) '%d'",
				bitsPerSample);
		return false;
	}

	InitializeExtraData();

	HRC(CoCreateInstance(CLSID_AACMFTEncoder, NULL, CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&transform_)));
	HRC(CreateMediaTypes(inputType, outputType));

	HRC(transform_->SetInputType(0, inputType.Get(), 0));
	HRC(transform_->SetOutputType(0, outputType.Get(), 0));

	HRC(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
		NULL));
	HRC(transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM,
		NULL));

	blog(LOG_INFO, "encoder created\n"
			"\tbitrate: %d\n"
			"\tchannels: %d\n"
			"\tsample rate: %d\n"
			"\tbits-per-sample: %d\n",
			bitrate, channels, sampleRate, bitsPerSample);

	transform = transform_;
	return true;

fail:
	return false;
}

static HRESULT CreateEmptySample(ComPtr<IMFSample> &sample,
		ComPtr<IMFMediaBuffer> &buffer, DWORD length)
{
	HRESULT hr;

	HRC(MFCreateSample(&sample));
	HRC(MFCreateMemoryBuffer(length, &buffer));
	HRC(sample->AddBuffer(buffer.Get()));
	return S_OK;

fail:
	return hr;
}

HRESULT MFAACEncoder::EnsureCapacity(ComPtr<IMFSample> &sample, DWORD length)
{
	HRESULT hr;
	ComPtr<IMFMediaBuffer> buffer;
	DWORD currentLength;

	if (!sample) {
		HRC(CreateEmptySample(sample, buffer, length));
	}
	else {
		HRC(sample->GetBufferByIndex(0, &buffer));
	}

	HRC(buffer->GetMaxLength(&currentLength));
	if (currentLength < length) {
		HRC(sample->RemoveAllBuffers());
		HRC(MFCreateMemoryBuffer(length, &buffer));
		HRC(sample->AddBuffer(buffer));
	}
	else {
		buffer->SetCurrentLength(0);
	}

	if (!packetBuffer || packetBufferLength < length) {
		packetBuffer.reset(new BYTE[length]);
	}

	return S_OK;

fail:
	return hr;
}

bool MFAACEncoder::ProcessInput(UINT8 *data, UINT32 data_length,
	UINT64 pts, enum mf_aac_status *status)
{
	HRESULT hr;
	ComPtr<IMFSample> sample;
	ComPtr<IMFMediaBuffer> buffer;
	BYTE *bufferData;
	INT64 samplePts;
	UINT32 samples;
	UINT64 sampleDur;

	HRC(CreateEmptySample(sample, buffer, data_length));

	HRC(buffer->Lock(&bufferData, NULL, NULL));
	memcpy(bufferData, data, data_length);
	HRC(buffer->Unlock());
	HRC(buffer->SetCurrentLength(data_length));

	samples = data_length / channels / (bitsPerSample / 8);
	sampleDur = (UINT64)(((float) sampleRate / channels / samples) * 10000);
	samplePts = pts / 100;

	HRC(sample->SetSampleTime(samplePts));
	HRC(sample->SetSampleDuration(sampleDur));

	hr = transform->ProcessInput(0, sample, 0);
	if (hr == MF_E_NOTACCEPTING) {
		*status = MF_AAC_NOT_ACCEPTING;
		return true;
	} else if (FAILED(hr)) {
		blog_com("process input", hr);
		return false;
	}

	*status = MF_AAC_SUCCESS;
	return true;

fail:
	*status = MF_AAC_FAILURE;
	return false;
}

bool MFAACEncoder::ProcessOutput(UINT8 **data, UINT32 *dataLength,
		UINT64 *pts, enum mf_aac_status *status)
{
	HRESULT hr;

	DWORD outputFlags, outputStatus;
	MFT_OUTPUT_STREAM_INFO outputInfo = { 0 };
	MFT_OUTPUT_DATA_BUFFER output = { 0 };
	ComPtr<IMFMediaBuffer> outputBuffer;
	BYTE *bufferData;
	DWORD bufferLength;
	INT64 samplePts;

	HRC(transform->GetOutputStatus(&outputFlags));
	if (outputFlags != MFT_OUTPUT_STATUS_SAMPLE_READY) {
		*status = MF_AAC_NEED_MORE_INPUT;
		return true;
	}

	HRC(transform->GetOutputStreamInfo(0, &outputInfo));
	EnsureCapacity(outputSample, outputInfo.cbSize);

	output.pSample = outputSample.Get();

	hr = transform->ProcessOutput(0, 1, &output, &outputStatus);
	if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
		*status = MF_AAC_NEED_MORE_INPUT;
		return true;
	} else if (FAILED(hr)) {
		blog_com("process output", hr);
		return false;
	}

	HRC(outputSample->GetBufferByIndex(0, &outputBuffer));

	HRC(outputBuffer->Lock(&bufferData, NULL, &bufferLength));
	memcpy(packetBuffer.get(), bufferData, bufferLength);
	HRC(outputBuffer->Unlock());

	HRC(outputSample->GetSampleTime(&samplePts));

	*pts = samplePts * 100;
	*data = packetBuffer.get();
	*dataLength = bufferLength;
	*status = MF_AAC_SUCCESS;
	return true;

fail:
	*status = MF_AAC_FAILURE;
	return false;
}

bool MFAACEncoder::ExtraData(UINT8 **extraData_, UINT32 *extraDataLength)
{
	*extraData_ = extraData;
	*extraDataLength = sizeof(extraData);
	return true;
}

struct mf_aac_encoder
{
	MFAACEncoder *encoder;
};

mf_aac_encoder_t *mf_aac_encoder_create(uint32_t bitrate, uint32_t channels,
		uint32_t sample_rate, uint32_t bits_per_sample)
{
	mf_aac_encoder_t *encoder =
			(mf_aac_encoder_t *)bzalloc(sizeof(mf_aac_encoder_t));
	if (!encoder)
		return NULL;

	encoder->encoder = new MFAACEncoder(bitrate, channels, sample_rate,
			bits_per_sample);

	return encoder;
}

void mf_aac_encoder_destroy(mf_aac_encoder_t *encoder)
{
	if (encoder != NULL && encoder->encoder != NULL)
		delete encoder->encoder;
}

bool mf_aac_encoder_initialize(mf_aac_encoder_t *encoder)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->Initialize();
	return false;
}

bool mf_aac_encoder_process_input(mf_aac_encoder_t *encoder,
		uint8_t *data, uint32_t data_length,
		uint64_t pts, enum mf_aac_status *status)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->ProcessInput(data,
				data_length, pts, status);
	return false;
}

bool mf_aac_encoder_process_output(mf_aac_encoder_t *encoder,
		uint8_t **data, uint32_t *data_length,
		uint64_t *pts, enum mf_aac_status *status)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->ProcessOutput(data,
				data_length, pts, status);
	return false;
}

bool mf_aac_encoder_extra_data(mf_aac_encoder_t *encoder,
		uint8_t **extra_data, uint32_t *extra_data_length)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->ExtraData(
				extra_data, extra_data_length);
	return false;
}

int32_t mf_aac_encoder_frame_size()
{
	return 1024;
}

uint32_t mf_aac_encoder_bitrate(mf_aac_encoder_t *encoder)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->Bitrate();
	return 0;
}

uint32_t mf_aac_encoder_channels(mf_aac_encoder_t *encoder)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->Channels();
	return 0;
}

uint32_t mf_aac_encoder_sample_rate(mf_aac_encoder_t *encoder)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->SampleRate();
	return 0;
}

uint32_t mf_aac_encoder_bits_per_sample(mf_aac_encoder_t *encoder)
{
	if (encoder != NULL && encoder->encoder != NULL)
		return encoder->encoder->BitsPerSample();
	return 0;
}
