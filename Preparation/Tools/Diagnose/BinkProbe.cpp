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

/*
 * BinkProbe - decode a Bink film with the minimal FFmpeg of vendor\ffmpeg-min.
 *
 * The 64-bit engine cannot use the game's binkw32.dll, which exists only as
 * a 32-bit library. Its films are to come from FFmpeg's Bink decoders
 * instead, built as three DLLs (avutil, avcodec, avformat) with nothing in
 * them but Bink and the file protocol (vendor\ffmpeg-min\build-min.sh).
 *
 * This tool proves that path without the game. It links NO import library:
 * the three DLLs are loaded at run time with LoadLibraryW from the folder
 * given as the first argument, and every function is resolved by name -
 * the way the engine will load them. Then it decodes every packet of the
 * film and prints what the engine will have to know:
 *
 *   - each stream: codec, size, pixel format, frame rate, time base;
 *     sample rate, channels, sample format
 *   - the frame count per stream, the linesize of the first video frame
 *   - the MD5 of the first and the last video frame over the Y, U and V
 *     planes in that order, row by row, width bytes per row, no padding -
 *     the bytes the framemd5 muxer of ffmpeg.exe hashes for yuv420p
 *   - the total audio sample count and the audio frame sizes
 *   - the wall time of the whole decode and its multiple of real time
 *
 * The DLLs are loaded avutil first, then avcodec, then avformat: each one
 * imports the one before it, and Windows looks for an imported DLL among
 * the modules already loaded before it searches any folder. Loaded in the
 * other order, avformat's import of avcodec is searched for beside the EXE
 * and on PATH, not beside avformat - -wrongorder shows that.
 *
 * Usage:
 *   BinkProbe <dll folder> <film.bik>          report
 *   BinkProbe <dll folder> <film.bik> -frames  report, then one line per
 *        decoded frame: "v <n> <md5>" (planes as above) and
 *        "a <n> <samples> <md5 s16> <md5 f32>" (the frame as interleaved
 *        signed 16 bit, converted as ffmpeg.exe's pcm_s16le output
 *        converts it, and as interleaved 32-bit float), for a comparison
 *        of EVERY frame with ffmpeg -f framemd5
 *   BinkProbe <dll folder> -wrongorder         load avformat first
 *   BinkProbe <dll folder> <film.bik> -bmp <n> <out.bmp>
 *        report, and video frame n (from 0) written as a 24-bit BMP
 *        through the engine's own conversion, specific\yuv_rgb.cpp
 *        (BT.601, limited range) - the picture the 64-bit build shows
 *        for it - with the conversion's three test pixels printed. The
 *        only file the tool ever writes.
 *
 * Build: bbp.bat (x64 only - the DLLs are 64-bit).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/pixdesc.h"
#include "libavutil/samplefmt.h"
}

// The engine's conversion of the films' pictures, compiled beside this.
#include "../../../TOMB5 Code/TOMB5/specific/yuv_rgb.h"

#pragma comment(lib, "bcrypt.lib")

/* One function pointer per name, typed by the header's own declaration. */
#define FN(name) static decltype(&name) p_##name
FN(avutil_version);
FN(avcodec_version);
FN(avformat_version);
FN(av_frame_alloc);
FN(av_frame_free);
FN(av_frame_unref);
FN(av_get_pix_fmt_name);
FN(av_get_sample_fmt_name);
FN(av_strerror);
FN(av_packet_alloc);
FN(av_packet_free);
FN(av_packet_unref);
FN(avcodec_find_decoder);
FN(avcodec_alloc_context3);
FN(avcodec_free_context);
FN(avcodec_parameters_to_context);
FN(avcodec_open2);
FN(avcodec_send_packet);
FN(avcodec_receive_frame);
FN(avformat_open_input);
FN(avformat_find_stream_info);
FN(avformat_close_input);
FN(av_read_frame);
#undef FN

static int missing = 0;

static void *Resolve(HMODULE m, const char *dll, const char *name)
{
	void *p = (void *)GetProcAddress(m, name);
	if (!p)
	{
		printf("MISSING %s!%s\n", dll, name);
		missing++;
	}
	return p;
}

#define GET(mod, dll, name) \
	p_##name = (decltype(p_##name))Resolve(mod, dll, #name)

/* The DLL of that name in the folder: "avutil-*.dll" -> full path. */
static std::wstring FindDll(const std::wstring &dir, const wchar_t *stem,
	std::string &shortName)
{
	std::wstring pat = dir + L"\\" + stem + L"-*.dll";
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW(pat.c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
		return L"";
	FindClose(h);
	char buf[MAX_PATH];
	WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, buf, sizeof(buf), 0, 0);
	shortName = buf;
	return dir + L"\\" + fd.cFileName;
}

static HMODULE Load(const std::wstring &path, const std::string &name)
{
	HMODULE m = LoadLibraryW(path.c_str());
	if (!m)
		printf("LoadLibraryW(%s) failed, error %lu\n", name.c_str(),
			GetLastError());
	else
		printf("loaded %s\n", name.c_str());
	return m;
}

/* MD5 through Windows' own CNG - independent of the DLLs under test. */
struct Md5
{
	BCRYPT_ALG_HANDLE alg = 0;
	BCRYPT_HASH_HANDLE h = 0;
	Md5()
	{
		BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, 0, 0);
		BCryptCreateHash(alg, &h, 0, 0, 0, 0, 0);
	}
	~Md5()
	{
		BCryptDestroyHash(h);
		BCryptCloseAlgorithmProvider(alg, 0);
	}
	void Add(const void *p, size_t n)
	{
		BCryptHashData(h, (PUCHAR)p, (ULONG)n, 0);
	}
	std::string Hex()
	{
		unsigned char d[16];
		BCryptFinishHash(h, d, 16, 0);
		char s[33];
		for (int i = 0; i < 16; i++)
			snprintf(s + i * 2, 3, "%02x", d[i]);
		return s;
	}
};

/* Y, U, V (and A, if any) row by row, only the visible bytes of a row. */
static std::string FrameMd5(const AVFrame *f)
{
	Md5 md5;
	int planes = f->format == AV_PIX_FMT_YUVA420P ? 4 : 3;
	for (int p = 0; p < planes; p++)
	{
		bool chroma = p == 1 || p == 2;
		int w = chroma ? (f->width + 1) >> 1 : f->width;
		int h = chroma ? (f->height + 1) >> 1 : f->height;
		for (int y = 0; y < h; y++)
			md5.Add(f->data[p] + (size_t)y * f->linesize[p], w);
	}
	return md5.Hex();
}

/*
 * The audio frame as ffmpeg.exe's framemd5 sees it, two ways: as interleaved
 * signed 16 bit (its default audio codec pcm_s16le; the float conversion is
 * libswresample's own rule, clip(lrintf(x * 32768))) and, for a float
 * decoder, as the interleaved 32-bit floats themselves (-c:a pcm_f32le).
 * Returns "<s16 md5> <f32 md5>"; the second is "-" for 16-bit output.
 */
static std::string AudioMd5(const AVFrame *f, int channels)
{
	size_t n = (size_t)f->nb_samples * channels;
	bool flt = f->format == AV_SAMPLE_FMT_FLTP
		|| f->format == AV_SAMPLE_FMT_FLT;
	std::vector<float> fl(flt ? n : 0);
	std::vector<int16_t> out(n);
	for (int i = 0; i < f->nb_samples; i++)
		for (int c = 0; c < channels; c++)
		{
			size_t k = (size_t)i * channels + c;
			int v = 0;
			switch (f->format)
			{
			case AV_SAMPLE_FMT_FLTP:
				fl[k] = ((const float *)f->extended_data[c])[i];
				break;
			case AV_SAMPLE_FMT_FLT:
				fl[k] = ((const float *)f->data[0])[k];
				break;
			case AV_SAMPLE_FMT_S16P:
				v = ((const int16_t *)f->extended_data[c])[i];
				break;
			case AV_SAMPLE_FMT_S16:
				v = ((const int16_t *)f->data[0])[k];
				break;
			}
			if (flt)
				v = lrintf(fl[k] * 32768.0f);
			if (v > 32767) v = 32767;
			if (v < -32768) v = -32768;
			out[k] = (int16_t)v;
		}
	Md5 m16;
	m16.Add(out.data(), out.size() * sizeof(int16_t));
	std::string r = m16.Hex() + " ";
	if (!flt)
		return r + "-";
	Md5 m32;
	m32.Add(fl.data(), fl.size() * sizeof(float));
	return r + m32.Hex();
}

static const char *Err(int e)
{
	static char buf[128];
	p_av_strerror(e, buf, sizeof(buf));
	return buf;
}

struct StreamState
{
	AVCodecContext *ctx = 0;
	int type = -1;
	long long frames = 0;
	long long samples = 0;
	int minNb = 1 << 30, maxNb = 0;
	std::string firstMd5, lastMd5;
	int linesize[3] = {0, 0, 0};
};

static bool g_frames = false;
static long long g_bmpIndex = -1;
static const wchar_t *g_bmpPath = 0;

/* The frame through yuv_rgb.cpp into a 24-bit BMP, bottom-up, rows padded
 * to four bytes. */
static void WriteBmp(const AVFrame *f, const wchar_t *path)
{
	if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVA420P)
	{
		printf("bmp: picture format %d is not yuv420p\n", f->format);
		return;
	}
	const int w = f->width, h = f->height;
	std::vector<uint32_t> px((size_t)w * h);
	YuvRgbImage(f->data, f->linesize, w, h, px.data(), w * 4);

	const int row = (w * 3 + 3) & ~3;
	BITMAPFILEHEADER fh = {};
	BITMAPINFOHEADER ih = {};
	fh.bfType = 0x4D42;
	fh.bfOffBits = sizeof(fh) + sizeof(ih);
	fh.bfSize = fh.bfOffBits + (DWORD)row * h;
	ih.biSize = sizeof(ih);
	ih.biWidth = w;
	ih.biHeight = h;
	ih.biPlanes = 1;
	ih.biBitCount = 24;
	ih.biCompression = BI_RGB;
	ih.biSizeImage = (DWORD)row * h;

	FILE *o = 0;
	if (_wfopen_s(&o, path, L"wb") || !o)
	{
		printf("bmp: cannot write the file\n");
		return;
	}
	fwrite(&fh, sizeof(fh), 1, o);
	fwrite(&ih, sizeof(ih), 1, o);
	std::vector<uint8_t> line(row, 0);
	for (int y = h - 1; y >= 0; y--)
	{
		for (int x = 0; x < w; x++)
		{
			uint32_t c = px[(size_t)y * w + x];
			line[x * 3 + 0] = (uint8_t)c;
			line[x * 3 + 1] = (uint8_t)(c >> 8);
			line[x * 3 + 2] = (uint8_t)(c >> 16);
		}
		fwrite(line.data(), 1, row, o);
	}
	fclose(o);
	char name[MAX_PATH * 3];
	WideCharToMultiByte(CP_UTF8, 0, path, -1, name, sizeof(name), 0, 0);
	printf("bmp: frame %lld, %dx%d, %d bytes -> %s\n", g_bmpIndex, w, h,
		(int)fh.bfSize, name);
}

// The conversion's three test pixels: black, white and red.
static void PrintTestPixels()
{
	static const int t[3][3] = { { 16, 128, 128 }, { 235, 128, 128 },
		{ 81, 90, 240 } };
	for (int i = 0; i < 3; i++)
	{
		uint8_t r, g, b;
		YuvRgbPixel(t[i][0], t[i][1], t[i][2], &r, &g, &b);
		printf("pixel Y=%d U=%d V=%d -> R=%d G=%d B=%d\n", t[i][0],
			t[i][1], t[i][2], r, g, b);
	}
}

static void TakeFrame(StreamState &s, AVFrame *f)
{
	if (s.type == AVMEDIA_TYPE_VIDEO)
	{
		std::string m = FrameMd5(f);
		if (s.frames == 0)
		{
			s.firstMd5 = m;
			for (int i = 0; i < 3; i++)
				s.linesize[i] = f->linesize[i];
		}
		s.lastMd5 = m;
		if (s.frames == g_bmpIndex)
			WriteBmp(f, g_bmpPath);
		if (g_frames)
			printf("v %lld %s\n", s.frames, m.c_str());
	}
	else if (s.type == AVMEDIA_TYPE_AUDIO)
	{
		s.samples += f->nb_samples;
		if (f->nb_samples < s.minNb) s.minNb = f->nb_samples;
		if (f->nb_samples > s.maxNb) s.maxNb = f->nb_samples;
		if (g_frames)
			printf("a %lld %d %s\n", s.frames, f->nb_samples,
				AudioMd5(f, s.ctx->ch_layout.nb_channels).c_str());
	}
	s.frames++;
}

/* Every frame the decoder holds now; EAGAIN and EOF end the round. */
static int Receive(StreamState &s, AVFrame *f)
{
	for (;;)
	{
		int r = p_avcodec_receive_frame(s.ctx, f);
		if (r == AVERROR(EAGAIN) || r == AVERROR_EOF)
			return 0;
		if (r < 0)
		{
			printf("receive_frame: %s\n", Err(r));
			return r;
		}
		TakeFrame(s, f);
		p_av_frame_unref(f);
	}
}

static void PrintVersion(const char *what, unsigned v)
{
	printf("%s %u.%u.%u\n", what, v >> 16, (v >> 8) & 255, v & 255);
}

int wmain(int argc, wchar_t **argv)
{
	if (argc < 3)
	{
		printf("usage: BinkProbe <dll folder> <film.bik> [-frames]\n"
			"       BinkProbe <dll folder> <film.bik> -bmp <n> <out.bmp>\n"
			"       BinkProbe <dll folder> -wrongorder\n");
		return 2;
	}
	std::wstring dir = argv[1];
	std::string nU, nC, nF;
	std::wstring pU = FindDll(dir, L"avutil", nU);
	std::wstring pC = FindDll(dir, L"avcodec", nC);
	std::wstring pF = FindDll(dir, L"avformat", nF);
	if (pU.empty() || pC.empty() || pF.empty())
	{
		printf("avutil-*.dll, avcodec-*.dll or avformat-*.dll missing\n");
		return 1;
	}

	if (!wcscmp(argv[2], L"-wrongorder"))
	{
		/* avformat first: its imports are not found beside it. */
		HMODULE f = Load(pF, nF);
		return f ? 0 : 1;
	}
	if (argc > 3 && !wcscmp(argv[3], L"-frames"))
		g_frames = true;
	if (argc > 5 && !wcscmp(argv[3], L"-bmp"))
	{
		g_bmpIndex = _wtoi64(argv[4]);
		g_bmpPath = argv[5];
		PrintTestPixels();
	}

	HMODULE mU = Load(pU, nU);
	HMODULE mC = mU ? Load(pC, nC) : 0;
	HMODULE mF = mC ? Load(pF, nF) : 0;
	if (!mF)
		return 1;

	GET(mU, "avutil", avutil_version);
	GET(mU, "avutil", av_frame_alloc);
	GET(mU, "avutil", av_frame_free);
	GET(mU, "avutil", av_frame_unref);
	GET(mU, "avutil", av_get_pix_fmt_name);
	GET(mU, "avutil", av_get_sample_fmt_name);
	GET(mU, "avutil", av_strerror);
	GET(mC, "avcodec", avcodec_version);
	GET(mC, "avcodec", av_packet_alloc);
	GET(mC, "avcodec", av_packet_free);
	GET(mC, "avcodec", av_packet_unref);
	GET(mC, "avcodec", avcodec_find_decoder);
	GET(mC, "avcodec", avcodec_alloc_context3);
	GET(mC, "avcodec", avcodec_free_context);
	GET(mC, "avcodec", avcodec_parameters_to_context);
	GET(mC, "avcodec", avcodec_open2);
	GET(mC, "avcodec", avcodec_send_packet);
	GET(mC, "avcodec", avcodec_receive_frame);
	GET(mF, "avformat", avformat_version);
	GET(mF, "avformat", avformat_open_input);
	GET(mF, "avformat", avformat_find_stream_info);
	GET(mF, "avformat", avformat_close_input);
	GET(mF, "avformat", av_read_frame);
	if (missing)
		return 1;
	printf("resolved 23 functions by name\n");
	PrintVersion("avutil", p_avutil_version());
	PrintVersion("avcodec", p_avcodec_version());
	PrintVersion("avformat", p_avformat_version());

	char film[MAX_PATH * 3];
	WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, film, sizeof(film), 0, 0);
	printf("film %s\n", film);

	LARGE_INTEGER freq, t0, t1;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&t0);

	AVFormatContext *fmt = 0;
	int r = p_avformat_open_input(&fmt, film, 0, 0);
	if (r < 0)
	{
		printf("open_input: %s\n", Err(r));
		return 1;
	}
	r = p_avformat_find_stream_info(fmt, 0);
	if (r < 0)
	{
		printf("find_stream_info: %s\n", Err(r));
		return 1;
	}
	printf("container %s, %u streams\n", fmt->iformat->name,
		fmt->nb_streams);

	std::vector<StreamState> st(fmt->nb_streams);
	double fps = 0;
	for (unsigned i = 0; i < fmt->nb_streams; i++)
	{
		AVStream *s = fmt->streams[i];
		AVCodecParameters *par = s->codecpar;
		const AVCodec *dec = p_avcodec_find_decoder(par->codec_id);
		if (!dec)
		{
			printf("stream %u: no decoder for codec id %d\n", i,
				par->codec_id);
			continue;
		}
		AVCodecContext *ctx = p_avcodec_alloc_context3(dec);
		p_avcodec_parameters_to_context(ctx, par);
		ctx->pkt_timebase = s->time_base;
		r = p_avcodec_open2(ctx, dec, 0);
		if (r < 0)
		{
			printf("stream %u: open2: %s\n", i, Err(r));
			return 1;
		}
		st[i].ctx = ctx;
		st[i].type = par->codec_type;
		if (par->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			AVRational fr = s->avg_frame_rate.num ? s->avg_frame_rate
				: s->r_frame_rate;
			fps = fr.den ? (double)fr.num / fr.den : 0;
			const char *pf = p_av_get_pix_fmt_name(ctx->pix_fmt);
			printf("stream %u: video %s, codec_tag %.4s, %dx%d, %s, "
				"%d/%d fps (%.3f), time_base %d/%d\n", i, dec->name,
				(const char *)&par->codec_tag, ctx->width, ctx->height,
				pf ? pf : "?", fr.num, fr.den, fps, s->time_base.num,
				s->time_base.den);
		}
		else if (par->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			const char *sf = p_av_get_sample_fmt_name(ctx->sample_fmt);
			printf("stream %u: audio %s, %d Hz, %d channels, %s, "
				"frame_size %d, time_base %d/%d\n", i, dec->name,
				ctx->sample_rate, ctx->ch_layout.nb_channels,
				sf ? sf : "?", ctx->frame_size, s->time_base.num,
				s->time_base.den);
		}
	}

	AVPacket *pkt = p_av_packet_alloc();
	AVFrame *frm = p_av_frame_alloc();
	long long packets = 0;
	for (;;)
	{
		r = p_av_read_frame(fmt, pkt);
		if (r == AVERROR_EOF)
			break;
		if (r < 0)
		{
			printf("read_frame: %s\n", Err(r));
			break;
		}
		packets++;
		StreamState &s = st[pkt->stream_index];
		if (s.ctx)
		{
			int e = p_avcodec_send_packet(s.ctx, pkt);
			if (e < 0)
				printf("send_packet (stream %d): %s\n", pkt->stream_index,
					Err(e));
			else
				Receive(s, frm);
		}
		p_av_packet_unref(pkt);
	}
	/* Drain: a NULL packet, then every frame the decoder still holds. */
	for (auto &s : st)
		if (s.ctx)
		{
			p_avcodec_send_packet(s.ctx, 0);
			Receive(s, frm);
		}

	QueryPerformanceCounter(&t1);
	double wall = (double)(t1.QuadPart - t0.QuadPart) / freq.QuadPart;

	printf("packets %lld\n", packets);
	double seconds = 0;
	for (unsigned i = 0; i < st.size(); i++)
	{
		StreamState &s = st[i];
		if (s.type == AVMEDIA_TYPE_VIDEO)
		{
			printf("stream %u: %lld video frames, linesize %d/%d/%d\n", i,
				s.frames, s.linesize[0], s.linesize[1], s.linesize[2]);
			printf("stream %u: first %s\n", i, s.firstMd5.c_str());
			printf("stream %u: last  %s\n", i, s.lastMd5.c_str());
			if (fps > 0)
				seconds = s.frames / fps;
		}
		else if (s.type == AVMEDIA_TYPE_AUDIO)
			printf("stream %u: %lld audio frames, %lld samples, frame "
				"%d..%d samples\n", i, s.frames, s.samples,
				s.frames ? s.minNb : 0, s.maxNb);
	}
	printf("film %.3f s, decode %.3f s wall, %.1fx real time\n", seconds,
		wall, wall > 0 ? seconds / wall : 0);

	p_av_frame_free(&frm);
	p_av_packet_free(&pkt);
	for (auto &s : st)
		if (s.ctx)
			p_avcodec_free_context(&s.ctx);
	p_avformat_close_input(&fmt);
	return 0;
}
