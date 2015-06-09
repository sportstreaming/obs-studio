#pragma once

#define WIN32_MEAN_AND_LEAN
#include <Windows.h>
#undef WIN32_MEAN_AND_LEAN

#include <mfapi.h>
#include <mfidl.h>

#include <stdint.h>
#include <vector>

#include <util/windows/ComPtr.hpp>

namespace MFAAC
{

enum Status {
	FAILURE,
	SUCCESS,
	NOT_ACCEPTING,
	NEED_MORE_INPUT
};

class Encoder
{
public:
	Encoder(UINT32 bitrate, UINT32 channels,
			UINT32 sampleRate, UINT32 bitsPerSample)
	: bitrate(bitrate), channels(channels),
			sampleRate(sampleRate),
			bitsPerSample(bitsPerSample) {}

	Encoder& operator=(Encoder const&) = delete;

	bool Initialize();
	bool ProcessInput(UINT8 *data, UINT32 dataLength,
			UINT64 pts, MFAAC::Status *status);
	bool ProcessOutput(UINT8 **data, UINT32 *dataLength,
			UINT64 *pts, MFAAC::Status *status);
	bool ExtraData(UINT8 **extraData, UINT32 *extraDataLength);

	UINT32 Bitrate() { return bitrate; }
	UINT32 Channels() { return channels; }
	UINT32 SampleRate() { return sampleRate; }
	UINT32 BitsPerSample() { return bitsPerSample; }

	static const UINT32 FrameSize = 1024;

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
	std::vector<BYTE> packetBuffer;
	UINT8 extraData[5];
};

}
