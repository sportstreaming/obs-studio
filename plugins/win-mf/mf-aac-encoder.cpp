#include <obs-module.h>

#include "mf-aac-encoder.hpp"

#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <comdef.h>

#define blog_mf(level, format, ...) blog(level, "MFAAC: " format, ##__VA_ARGS__)
#define blog_com(msg, hr) blog_mf(LOG_ERROR, msg " failed,  %S (0x%08lx)", \
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

static const UINT32 VALID_CHANNELS[] =
{
	1,
	2
};

static const UINT32 VALID_BITS_PER_SAMPLE[] = {
	16
};

static const UINT32 VALID_SAMPLERATES[] = {
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

HRESULT MFAAC::Encoder::CreateMediaTypes(ComPtr<IMFMediaType> &i,
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

void MFAAC::Encoder::InitializeExtraData()
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

bool MFAAC::Encoder::Initialize()
{
	HRESULT hr;

	ComPtr<IMFTransform> transform_;
	ComPtr<IMFMediaType> inputType, outputType;

	if (!IsValid(VALID_BITRATES, bitrate)) {
		blog_mf(LOG_WARNING, "invalid bitrate (kbps) '%d'", bitrate);
		return false;
	}
	if (!IsValid(VALID_CHANNELS, channels)) {
		blog_mf(LOG_WARNING, "invalid channel count '%d", channels);
		return false;
	}
	if (!IsValid(VALID_SAMPLERATES, sampleRate)) {
		blog_mf(LOG_WARNING, "invalid sample rate (hz) '%d", 
				sampleRate);
		return false;
	}
	if (!IsValid(VALID_BITS_PER_SAMPLE, bitsPerSample)) {
		blog_mf(LOG_WARNING, "invalid bits-per-sample (bits) '%d'",
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

	blog_mf(LOG_INFO, "encoder 'mf_aac' created\n"
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

HRESULT MFAAC::Encoder::EnsureCapacity(ComPtr<IMFSample> &sample, DWORD length)
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

	packetBuffer.reserve(length);

	return S_OK;

fail:
	return hr;
}

bool MFAAC::Encoder::ProcessInput(UINT8 *data, UINT32 data_length,
		UINT64 pts, MFAAC::Status *status)
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
		*status = MFAAC::NOT_ACCEPTING;
		return true;
	} else if (FAILED(hr)) {
		blog_com("process input", hr);
		return false;
	}

	*status = MFAAC::SUCCESS;
	return true;

fail:
	*status = MFAAC::FAILURE;
	return false;
}

bool MFAAC::Encoder::ProcessOutput(UINT8 **data, UINT32 *dataLength,
		UINT64 *pts, MFAAC::Status *status)
{
	HRESULT hr;

	DWORD outputFlags, outputStatus;
	MFT_OUTPUT_STREAM_INFO outputInfo = {0};
	MFT_OUTPUT_DATA_BUFFER output = {0};
	ComPtr<IMFMediaBuffer> outputBuffer;
	BYTE *bufferData;
	DWORD bufferLength;
	INT64 samplePts;

	HRC(transform->GetOutputStatus(&outputFlags));
	if (outputFlags != MFT_OUTPUT_STATUS_SAMPLE_READY) {
		*status = MFAAC::NEED_MORE_INPUT;
		return true;
	}

	HRC(transform->GetOutputStreamInfo(0, &outputInfo));
	EnsureCapacity(outputSample, outputInfo.cbSize);

	output.pSample = outputSample.Get();

	hr = transform->ProcessOutput(0, 1, &output, &outputStatus);
	if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
		*status = MFAAC::NEED_MORE_INPUT;
		return true;
	} else if (FAILED(hr)) {
		blog_com("process output", hr);
		return false;
	}

	HRC(outputSample->GetBufferByIndex(0, &outputBuffer));

	HRC(outputBuffer->Lock(&bufferData, NULL, &bufferLength));
	packetBuffer.assign(bufferData, bufferData + bufferLength);
	HRC(outputBuffer->Unlock());

	HRC(outputSample->GetSampleTime(&samplePts));

	*pts = samplePts * 100;
	*data = &packetBuffer[0];
	*dataLength = bufferLength;
	*status = MFAAC::SUCCESS;
	return true;

fail:
	*status = MFAAC::FAILURE;
	return false;
}

bool MFAAC::Encoder::ExtraData(UINT8 **extraData_, UINT32 *extraDataLength)
{
	*extraData_ = extraData;
	*extraDataLength = sizeof(extraData);
	return true;
}
