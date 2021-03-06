/*
Copyright (c) 2012-2013 Maarten Baert <maarten-baert@hotmail.com>

This file is part of SimpleScreenRecorder.

SimpleScreenRecorder is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

SimpleScreenRecorder is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with SimpleScreenRecorder.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "Global.h"
#include "FastScaler.h"

#include "Logger.h"
#include "DetectCPUFeatures.h"
#include "TempBuffer.h"

#include "FastScaler_Convert.h"
#include "FastScaler_Scale.h"

FastScaler::FastScaler() {

#if SSR_USE_X86_ASM
	CPUFeatures features;
	DetectCPUFeatures(&features);
	m_use_ssse3 = (features.sse && features.sse2 && features.sse3 && features.ssse3);
	m_warn_alignment = true;
#endif

	m_warn_swscale = true;
	m_sws_context = NULL;

}

FastScaler::~FastScaler() {
	if(m_sws_context != NULL) {
		sws_freeContext(m_sws_context);
		m_sws_context = NULL;
	}
}

void FastScaler::Scale(unsigned int in_width, unsigned int in_height, PixelFormat in_format, const uint8_t* const* in_data, const int* in_stride,
					   unsigned int out_width, unsigned int out_height, PixelFormat out_format, uint8_t* const* out_data, const int* out_stride) {

	// faster BGRA scaling
	if(in_format == PIX_FMT_BGRA && out_format == PIX_FMT_BGRA) {
		Scale_BGRA(in_width, in_height, in_data[0], in_stride[0], out_width, out_height, out_data[0], out_stride[0]);
		return;
	}

	// faster BGRA to YUV conversion
	if(in_format == PIX_FMT_BGRA && out_format == PIX_FMT_YUV420P) {
		if(in_width == out_width && in_height == out_height) {
			Convert_BGRA_YUV420(in_width, in_height, in_data[0], in_stride[0], out_data, out_stride);
		} else {
			TempBuffer<uint8_t> scaled;
			int scaled_stride = grow_align16(out_width * 4);
			scaled.alloc(scaled_stride * out_height);
			Scale_BGRA(in_width, in_height, in_data[0], in_stride[0], out_width, out_height, scaled.data(), scaled_stride);
			Convert_BGRA_YUV420(out_width, out_height, scaled.data(), scaled_stride, out_data, out_stride);
		}
		return;
	}

	if(m_warn_swscale) {
		m_warn_swscale = false;
		Logger::LogWarning("[FastScaler::Scale] " + QObject::tr("Warning: Pixel format is not supported (%1 -> %2), using swscale instead. "
																"This is not a problem, but performance will be worse.").arg(in_format).arg(out_format));
	}

	m_sws_context = sws_getCachedContext(m_sws_context,
										 in_width, in_height, in_format,
										 out_width, out_height, out_format,
										 SWS_BILINEAR, NULL, NULL, NULL);
	if(m_sws_context == NULL) {
		Logger::LogError("[FastScaler::Scale] " + QObject::tr("Error: Can't get swscale context!", "Don't translate 'swscale'"));
		throw LibavException();
	}
	sws_scale(m_sws_context, in_data, in_stride, 0, in_height, out_data, out_stride);

}

void FastScaler::Convert_BGRA_YUV420(unsigned int width, unsigned int height, const uint8_t* in_data, int in_stride, uint8_t* const out_data[3], const int out_stride[3]) {
	Q_ASSERT(width % 2 == 0 && height % 2 == 0);

#if SSR_USE_X86_ASM
	if(m_use_ssse3) {
		if((uintptr_t) out_data[0] % 16 == 0 && out_stride[0] % 16 == 0 &&
		   (uintptr_t) out_data[1] % 16 == 0 && out_stride[1] % 16 == 0 &&
		   (uintptr_t) out_data[2] % 16 == 0 && out_stride[2] % 16 == 0) {
			Convert_BGRA_YUV420_SSSE3(width, height, in_data, in_stride, out_data, out_stride);
		} else {
			if(m_warn_alignment) {
				m_warn_alignment = false;
				Logger::LogWarning("[FastScaler::Convert_BGRA_YUV420] " + QObject::tr("Warning: Memory is not properly aligned for SSE, using fallback converter instead. "
																					  "This is not a problem, but performance will be worse.", "Don't translate 'fallback'"));
			}
			Convert_BGRA_YUV420_Fallback(width, height, in_data, in_stride, out_data, out_stride);
		}
		return;
	}
#endif

	Convert_BGRA_YUV420_Fallback(width, height, in_data, in_stride, out_data, out_stride);

}

void FastScaler::Scale_BGRA(unsigned int in_width, unsigned int in_height, const uint8_t* in_data, int in_stride,
							unsigned int out_width, unsigned int out_height, uint8_t* out_data, int out_stride) {

#if SSR_USE_X86_ASM
	if(m_use_ssse3) {
		if((uintptr_t) out_data % 16 == 0 && out_stride % 16 == 0) {
			Scale_BGRA_SSSE3(in_width, in_height, in_data, in_stride, out_width, out_height, out_data, out_stride);
		} else {
			if(m_warn_alignment) {
				m_warn_alignment = false;
				Logger::LogWarning("[FastScaler::Scale_BGRA] " + QObject::tr("Warning: Memory is not properly aligned for SSE, using fallback converter instead. "
																			 "This is not a problem, but performance will be worse.", "Don't translate 'fallback'"));
			}
			Scale_BGRA_Fallback(in_width, in_height, in_data, in_stride, out_width, out_height, out_data, out_stride);
		}
		return;
	}
#endif

	Scale_BGRA_Fallback(in_width, in_height, in_data, in_stride, out_width, out_height, out_data, out_stride);

}
