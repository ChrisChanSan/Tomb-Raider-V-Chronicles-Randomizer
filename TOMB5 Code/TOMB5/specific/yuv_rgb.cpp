/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "yuv_rgb.h"

// The sum back to 0..255: shifted with rounding, then clamped.
static inline uint32_t YuvRgbClamp(int32_t sum)
{
	const int32_t c = (sum + 32768) >> 16;

	return (uint32_t)(c < 0 ? 0 : (c > 255 ? 255 : c));
}

void YuvRgbPixel(int y, int u, int v, uint8_t* r, uint8_t* g, uint8_t* b)
{
	const int32_t c = YUV_RGB_Y * (y - 16);
	const int32_t d = u - 128;
	const int32_t e = v - 128;

	*r = (uint8_t)YuvRgbClamp(c + YUV_RGB_RV * e);
	*g = (uint8_t)YuvRgbClamp(c - YUV_RGB_GU * d - YUV_RGB_GV * e);
	*b = (uint8_t)YuvRgbClamp(c + YUV_RGB_BU * d);
}

void YuvRgbRow(const uint8_t* y, const uint8_t* u, const uint8_t* v,
	uint32_t* out, int width)
{
	for (int x = 0; x < width; x++)
	{
		const int32_t c = YUV_RGB_Y * (y[x] - 16);
		const int32_t d = u[x >> 1] - 128;
		const int32_t e = v[x >> 1] - 128;

		out[x] = 0xFF000000u |
			(YuvRgbClamp(c + YUV_RGB_RV * e) << 16) |
			(YuvRgbClamp(c - YUV_RGB_GU * d - YUV_RGB_GV * e) << 8) |
			YuvRgbClamp(c + YUV_RGB_BU * d);
	}
}

void YuvRgbImage(const uint8_t* const plane[3], const int linesize[3],
	int width, int height, void* dst, int pitch)
{
	uint8_t* row = (uint8_t*)dst;

	for (int y = 0; y < height; y++, row += pitch)
		YuvRgbRow(plane[0] + (intptr_t)y * linesize[0],
			plane[1] + (intptr_t)(y >> 1) * linesize[1],
			plane[2] + (intptr_t)(y >> 1) * linesize[2],
			(uint32_t*)row, width);
}
