/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#include "Picture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincodec.h>

#include <new>

// Only CoCreateInstance is called by name; COM itself loads the codec DLL
// (windowscodecs.dll) for CLSID_WICImagingFactory at run time, so the EXE's
// import table does not change and windowscodecs.lib is not needed.
#pragma comment(lib, "ole32.lib")

namespace
{
    template <class T> void release(T*& p)
    {
        if (p) { p->Release(); p = nullptr; }
    }

    // The largest side a Direct3D 11 texture may have; it also keeps
    // width * height * 4 far below what a size_t and a UINT can hold.
    const UINT kMaxSide = 16384;
}

bool loadPictureRgba(const std::wstring& path,
                     std::vector<unsigned char>& rgba,
                     int& width, int& height)
{
    IWICImagingFactory*    factory = nullptr;
    IWICBitmapDecoder*     decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter*   converter = nullptr;
    std::vector<unsigned char> pixels;
    UINT w = 0, h = 0;

    // One chain of steps; the first failure skips the rest, and every
    // interface obtained so far is released below on every path.
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr,
                GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
    if (SUCCEEDED(hr))
        hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    // Whatever the file holds (JPEG is 24-bit BGR), the converter hands
    // out byte order R, G, B, A with an opaque alpha where none was stored.
    if (SUCCEEDED(hr))
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        hr = converter->GetSize(&w, &h);
    if (SUCCEEDED(hr) && (w == 0 || h == 0 || w > kMaxSide || h > kMaxSide))
        hr = E_FAIL;
    if (SUCCEEDED(hr))
    {
        try { pixels.resize(size_t(w) * h * 4); }
        catch (const std::bad_alloc&) { hr = E_OUTOFMEMORY; }
    }
    if (SUCCEEDED(hr))
        hr = converter->CopyPixels(nullptr, w * 4, UINT(pixels.size()),
                                   pixels.data());

    release(converter);
    release(frame);
    release(decoder);
    release(factory);

    if (FAILED(hr)) return false;
    rgba.swap(pixels);
    width = int(w);
    height = int(h);
    return true;
}
