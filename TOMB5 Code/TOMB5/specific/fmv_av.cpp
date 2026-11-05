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
#include "../tomb5/pch.h"

/* The films of the 64-bit build (fmv_av.h says why).
 *
 * LOADING. FmvAvLoad, at LoadBinkStuff's place in WinMain: avutil, avcodec
 * and avformat from the EXE's own folder by their full path, avutil first -
 * each imports the one before it, and LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR lets
 * Windows find that import beside the DLL as well. Every function by name
 * (the 23 of BinkProbe), and each library's major version checked against
 * the headers this file was compiled with: their struct layouts are the
 * DLLs' ABI, and another major is refused. The DLLs are optional - any
 * failure is one FMVLOG line naming the DLL or the function, and the game
 * goes on without films (fmvs_disabled), as the 64-bit build always did.
 *
 * PLAYBACK. FmvAvPlay is PlayFmv's frame as fmv.cpp has it for Bink: the
 * same checks, the same path, App.fmv, the same lazy texture, the same skip
 * rule, S_UpdateInput once per picture shown, the same steps to show one,
 * and the same tail in every way out. The film plays until its video
 * decoder is drained, not for a fixed count of frames.
 *
 * TIMING. The audio is the clock: a float source voice on the game's
 * mastering voice, every decoded audio frame copied into a buffer of its
 * own and queued, decoding kept FMV_AUDIO_AHEAD buffers ahead.
 * SamplesPlayed is the clock; a picture is shown when its time has come,
 * and one the clock has passed by more than a frame is dropped. Without an
 * audio stream, without XAudio2, or once the audio has ended or stalls, the
 * clock is QueryPerformanceCounter, carried on from where the audio was.
 *
 *   FMVLOG av loaded avutil=<v> avcodec=<v> avformat=<v>
 *   FMVLOG av <what failed>                     - no films this session
 *   FMVLOG film=<n> video=<w>x<h> <fmt> fps=<r> audio=<hz>x<ch> <fmt>|none
 *   FMVLOG film=<n> <what went wrong>           - the film ends there
 *   FMVLOG film=<n> clock=wall at <t> (<why>)
 *   FMVLOG film=<n> frames=<shown> dropped=<d> audio_frames=<a>
 *          skipped=<0|1> seconds=<t>            - at the end of every film
 */

#ifdef _WIN64

#pragma warning(push, 0)
// vendored code: its narrowings are not this project's
#pragma warning(disable: 4244)
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/pixdesc.h"
#include "libavutil/samplefmt.h"
}
#pragma warning(pop)

#include "fmv_av.h"
#include "yuv_rgb.h"
#include "dxshell.h"
#include "audio.h"
#include "function_stubs.h"
#include "function_table.h"
#include "winmain.h"
#include "input.h"
#include "dxsound.h"
#include "gamemain.h"
#include "LoadSave.h"
#include "setupdlg.h"

// Audio buffers queued on the voice before decoding stops (~0.35 s).
#define FMV_AUDIO_AHEAD	8
// The ring those buffers live in; XAudio2 takes up to 64 queued.
#define FMV_AUDIO_SLOTS	32
// Decoded pictures waiting for their time.
#define FMV_VIDEO_AHEAD	8
// A clock that does not move for this long is taken as stalled.
#define FMV_STALL_SECONDS	0.5
// The film's texture, as the 32-bit build's (fmv.cpp).
#define FMV_W	640
#define FMV_H	480

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

enum { AV_UTIL, AV_CODEC, AV_FORMAT, AV_LIBS };

static HMODULE av_dll[AV_LIBS];

struct AV_PROC
{
	long lib;
	const char* name;
	void** slot;
};

#define PROC(lib, name) { lib, #name, (void**)&p_##name }
static const AV_PROC av_procs[] =
{
	PROC(AV_UTIL, avutil_version),
	PROC(AV_UTIL, av_frame_alloc),
	PROC(AV_UTIL, av_frame_free),
	PROC(AV_UTIL, av_frame_unref),
	PROC(AV_UTIL, av_get_pix_fmt_name),
	PROC(AV_UTIL, av_get_sample_fmt_name),
	PROC(AV_UTIL, av_strerror),
	PROC(AV_CODEC, avcodec_version),
	PROC(AV_CODEC, av_packet_alloc),
	PROC(AV_CODEC, av_packet_free),
	PROC(AV_CODEC, av_packet_unref),
	PROC(AV_CODEC, avcodec_find_decoder),
	PROC(AV_CODEC, avcodec_alloc_context3),
	PROC(AV_CODEC, avcodec_free_context),
	PROC(AV_CODEC, avcodec_parameters_to_context),
	PROC(AV_CODEC, avcodec_open2),
	PROC(AV_CODEC, avcodec_send_packet),
	PROC(AV_CODEC, avcodec_receive_frame),
	PROC(AV_FORMAT, avformat_version),
	PROC(AV_FORMAT, avformat_open_input),
	PROC(AV_FORMAT, avformat_find_stream_info),
	PROC(AV_FORMAT, avformat_close_input),
	PROC(AV_FORMAT, av_read_frame),
};
#undef PROC

static LPDIRECT3DTEXTURE9 FmvTexture;

static void FmvAvUnload()
{
	for (long i = AV_LIBS - 1; i >= 0; i--)
	{
		if (av_dll[i])
			FreeLibrary(av_dll[i]);

		av_dll[i] = 0;
	}

	for (size_t i = 0; i < sizeof(av_procs) / sizeof(av_procs[0]); i++)
		*av_procs[i].slot = 0;
}

// "61.20.100" from a library's version word.
static void FmvAvVersion(unsigned v, char* out, size_t size)
{
	snprintf(out, size, "%u.%u.%u", v >> 16, (v >> 8) & 255, v & 255);
}

bool FmvAvLoad()
{
	static const struct { const char* stem; int major; } lib[AV_LIBS] =
	{
		{ "avutil", LIBAVUTIL_VERSION_MAJOR },
		{ "avcodec", LIBAVCODEC_VERSION_MAJOR },
		{ "avformat", LIBAVFORMAT_VERSION_MAJOR },
	};
	wchar_t dir[MAX_PATH];
	wchar_t path[MAX_PATH + 32];
	char file[32];
	unsigned ver[AV_LIBS];
	char text[AV_LIBS][16];
	DWORD n;

	// The EXE's own folder, never the current directory or the PATH.
	n = GetModuleFileNameW(0, dir, MAX_PATH);

	if (!n || n >= MAX_PATH)
	{
		Log("FMVLOG av no EXE folder, error %lu", GetLastError());
		fmvs_disabled = 1;
		return 0;
	}

	for (wchar_t* p = dir + n; p > dir; p--)
	{
		if (p[-1] == L'\\' || p[-1] == L'/')
		{
			*p = 0;
			break;
		}
	}

	for (long i = 0; i < AV_LIBS; i++)
	{
		snprintf(file, sizeof(file), "%s-%d.dll", lib[i].stem, lib[i].major);
		swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls%hs", dir, file);
		av_dll[i] = LoadLibraryExW(path, 0,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

		if (!av_dll[i])
		{
			Log("FMVLOG av missing %s, error %lu - films off", file,
				GetLastError());
			FmvAvUnload();
			fmvs_disabled = 1;
			return 0;
		}
	}

	for (size_t i = 0; i < sizeof(av_procs) / sizeof(av_procs[0]); i++)
	{
		*av_procs[i].slot = (void*)GetProcAddress(av_dll[av_procs[i].lib],
			av_procs[i].name);

		if (!*av_procs[i].slot)
		{
			Log("FMVLOG av missing %s-%d.dll!%s - films off",
				lib[av_procs[i].lib].stem, lib[av_procs[i].lib].major,
				av_procs[i].name);
			FmvAvUnload();
			fmvs_disabled = 1;
			return 0;
		}
	}

	ver[AV_UTIL] = p_avutil_version();
	ver[AV_CODEC] = p_avcodec_version();
	ver[AV_FORMAT] = p_avformat_version();

	for (long i = 0; i < AV_LIBS; i++)
	{
		FmvAvVersion(ver[i], text[i], sizeof(text[i]));

		if ((int)(ver[i] >> 16) != lib[i].major)
		{
			Log("FMVLOG av refused %s %s, built for major %d - films off",
				lib[i].stem, text[i], lib[i].major);
			FmvAvUnload();
			fmvs_disabled = 1;
			return 0;
		}
	}

	Log("FMVLOG av loaded avutil=%s avcodec=%s avformat=%s", text[AV_UTIL],
		text[AV_CODEC], text[AV_FORMAT]);
	return 1;
}

/* The device is going away; the texture goes with it (fmv.cpp,
 * FmvDeviceLost, says why). FmvAvPlay makes it again on the next film. */
void FmvAvDeviceLost()
{
	if (!FmvTexture)
		return;

	Log("Released %s @ %x - RefCnt = %d", "Film Texture", LogAddr(FmvTexture),
		FmvTexture->Release());
	FmvTexture = 0;
}

void FmvAvFree()
{
	if (FmvTexture)
	{
		FmvTexture->Release();
		FmvTexture = 0;
	}

	FmvAvUnload();
}

static const char* FmvAvError(int e)
{
	static char buf[128];

	if (p_av_strerror(e, buf, sizeof(buf)) < 0)
		snprintf(buf, sizeof(buf), "error %d", e);

	return buf;
}

/* --------------------------------------------------------- one film -- */

struct FMV_FILM
{
	long num;
	AVFormatContext* fmt;
	AVCodecContext* vctx;
	AVCodecContext* actx;
	int vidx, aidx;
	AVPacket* pkt;
	AVFrame* afrm;

	// The pictures decoded ahead, in order, with their times in seconds.
	AVFrame* vq[FMV_VIDEO_AHEAD];
	double vpts[FMV_VIDEO_AHEAD];
	long vhead, vcount;
	double vtb;			// the video stream's time base, in seconds
	double fdur;		// one picture's duration, in seconds
	long decoded;		// pictures decoded, the time of one without pts

	long demux_end;		// the file read to its end, the decoders told
	long video_end;		// the video decoder drained
	long audio_end;		// the audio decoder drained
	long failed;		// a read or decode error: the film ends

	// The audio: the voice, the ring of its buffers.
	IXAudio2SourceVoice* voice;
	long channels;
	long rate;
	float* slot[FMV_AUDIO_SLOTS];
	size_t slot_cap[FMV_AUDIO_SLOTS];
	UINT64 submitted;	// buffers ever submitted; the ring's write index
	long audio_frames;	// audio frames played (submitted)
	double audio_start;	// the first audio frame's time, in seconds
	long audio_start_set;
	long started;		// the voice runs

	// The clock: the voice's SamplesPlayed, or the QPC once `wall` is set.
	long wall;
	double wall_base;
	LONGLONG wall_t0;
	LONGLONG freq;
	UINT64 last_played;
	LONGLONG last_advance;
	LONGLONG t_start;

	long shown, dropped;
};

static LONGLONG FmvQpc()
{
	LARGE_INTEGER t;

	QueryPerformanceCounter(&t);
	return t.QuadPart;
}

/* From here on the clock is the QPC, carried on from `t`. The voice is
 * stopped and emptied: what it still holds would no longer be in time. */
static void FmvWallFrom(FMV_FILM* f, double t, const char* why)
{
	if (f->wall)
		return;

	f->wall = 1;
	f->wall_base = t;
	f->wall_t0 = FmvQpc();

	if (f->voice && f->started)
	{
		f->voice->Stop(0);
		f->voice->FlushSourceBuffers();
	}

	Log("FMVLOG film=%ld clock=wall at %.3f (%s)", f->num, t, why);
}

static double FmvClock(FMV_FILM* f)
{
	const LONGLONG now = FmvQpc();
	XAUDIO2_VOICE_STATE st;
	double t;

	if (!f->wall && f->voice && f->started)
	{
		f->voice->GetState(&st, 0);
		t = f->audio_start + (double)st.SamplesPlayed / f->rate;

		if (st.SamplesPlayed != f->last_played)
		{
			f->last_played = st.SamplesPlayed;
			f->last_advance = now;
		}
		else if (!st.BuffersQueued && f->audio_end)
			FmvWallFrom(f, t, "audio ended");
		else if ((double)(now - f->last_advance) / f->freq >
			FMV_STALL_SECONDS)
			FmvWallFrom(f, t, "audio stalled");

		if (!f->wall)
			return t;
	}

	if (!f->wall)
		FmvWallFrom(f, 0, "no audio");

	return f->wall_base + (double)(now - f->wall_t0) / f->freq;
}

static void FmvStartVoice(FMV_FILM* f)
{
	if (f->started || !f->voice || f->wall)
		return;

	if (FAILED(f->voice->Start(0)))
	{
		Log("FMVLOG film=%ld the voice did not start", f->num);
		FmvWallFrom(f, f->audio_start, "no voice");
		return;
	}

	f->started = 1;
	f->last_played = 0;
	f->last_advance = FmvQpc();
}

static UINT32 FmvQueued(FMV_FILM* f)
{
	XAUDIO2_VOICE_STATE st;

	if (!f->voice || f->wall)
		return 0;

	f->voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
	return st.BuffersQueued;
}

/* One decoded audio frame into the next buffer of the ring, interleaved
 * 32-bit float, and onto the voice. The AVFrame is the decoder's and is
 * reused at once, so the samples are copied. */
static void FmvTakeAudio(FMV_FILM* f, const AVFrame* a)
{
	if (!f->voice || f->wall || a->nb_samples <= 0)
		return;

	if (!f->audio_start_set)
	{
		f->audio_start_set = 1;

		if (a->pts != AV_NOPTS_VALUE)
			f->audio_start = a->pts * (double)f->actx->pkt_timebase.num /
				f->actx->pkt_timebase.den;
	}

	// A free buffer: the voice plays them in order, so one is free once
	// fewer than the whole ring are still queued.
	while (FmvQueued(f) >= FMV_AUDIO_SLOTS)
	{
		FmvStartVoice(f);

		if (MainThread.ended)
			return;

		Sleep(1);
		FmvClock(f);

		if (f->wall)
			return;
	}

	const long ch = f->channels;
	const size_t n = (size_t)a->nb_samples * ch;
	const long k = (long)(f->submitted % FMV_AUDIO_SLOTS);

	if (f->slot_cap[k] < n)
	{
		float* p = (float*)realloc(f->slot[k], n * sizeof(float));

		if (!p)
			return;

		f->slot[k] = p;
		f->slot_cap[k] = n;
	}

	float* out = f->slot[k];

	if (a->format == AV_SAMPLE_FMT_FLT)
		memcpy(out, a->data[0], n * sizeof(float));
	else
	{
		for (long c = 0; c < ch; c++)
		{
			const float* in = (const float*)a->extended_data[c];

			for (int i = 0; i < a->nb_samples; i++)
				out[(size_t)i * ch + c] = in[i];
		}
	}

	XAUDIO2_BUFFER b;

	memset(&b, 0, sizeof(b));
	b.AudioBytes = (UINT32)(n * sizeof(float));
	b.pAudioData = (const BYTE*)out;

	if (FAILED(f->voice->SubmitSourceBuffer(&b, 0)))
	{
		FmvWallFrom(f, FmvClock(f), "submit failed");
		return;
	}

	f->submitted++;
	f->audio_frames++;
}

// Every audio frame the decoder holds now.
static void FmvReceiveAudio(FMV_FILM* f)
{
	for (;;)
	{
		const int r = p_avcodec_receive_frame(f->actx, f->afrm);

		if (r == AVERROR(EAGAIN))
			return;

		if (r == AVERROR_EOF)
		{
			f->audio_end = 1;
			return;
		}

		if (r < 0)
		{
			Log("FMVLOG film=%ld audio decode: %s", f->num, FmvAvError(r));
			f->failed = 1;
			return;
		}

		if (f->afrm->format != f->actx->sample_fmt ||
			f->afrm->ch_layout.nb_channels != f->channels)
		{
			Log("FMVLOG film=%ld an audio frame changed its form", f->num);
			f->failed = 1;
			p_av_frame_unref(f->afrm);
			return;
		}

		FmvTakeAudio(f, f->afrm);
		p_av_frame_unref(f->afrm);
	}
}

// Every picture the decoder holds now, as far as the queue has room.
static void FmvReceiveVideo(FMV_FILM* f)
{
	while (f->vcount < FMV_VIDEO_AHEAD && !f->video_end && !f->failed)
	{
		const long k = (f->vhead + f->vcount) % FMV_VIDEO_AHEAD;
		AVFrame* v = f->vq[k];
		const int r = p_avcodec_receive_frame(f->vctx, v);

		if (r == AVERROR(EAGAIN))
		{
			// Told its end, the decoder has nothing more to give.
			if (f->demux_end)
				f->video_end = 1;

			return;
		}

		if (r == AVERROR_EOF)
		{
			f->video_end = 1;
			return;
		}

		if (r < 0)
		{
			Log("FMVLOG film=%ld video decode: %s", f->num, FmvAvError(r));
			f->failed = 1;
			return;
		}

		if (v->format != AV_PIX_FMT_YUV420P &&
			v->format != AV_PIX_FMT_YUVA420P)
		{
			const char* name = p_av_get_pix_fmt_name((AVPixelFormat)v->format);

			Log("FMVLOG film=%ld refused: picture format %s", f->num,
				name ? name : "?");
			f->failed = 1;
			p_av_frame_unref(v);
			return;
		}

		const int64_t ts = v->best_effort_timestamp != AV_NOPTS_VALUE ?
			v->best_effort_timestamp : v->pts;

		f->vpts[k] = ts != AV_NOPTS_VALUE ? ts * f->vtb :
			f->decoded * f->fdur;
		f->decoded++;
		f->vcount++;
	}
}

/* One packet from the file to its decoder. At the file's end - or at a
 * read error, which ends the film as a short file would - both decoders
 * are told, and what they still hold is taken. */
static void FmvReadPacket(FMV_FILM* f)
{
	int r = p_av_read_frame(f->fmt, f->pkt);

	if (r < 0)
	{
		if (r != AVERROR_EOF)
			Log("FMVLOG film=%ld read: %s", f->num, FmvAvError(r));

		f->demux_end = 1;
		p_avcodec_send_packet(f->vctx, 0);

		if (f->actx)
		{
			p_avcodec_send_packet(f->actx, 0);
			FmvReceiveAudio(f);
		}

		f->audio_end = 1;
		return;
	}

	if (f->pkt->stream_index == f->vidx)
	{
		r = p_avcodec_send_packet(f->vctx, f->pkt);

		if (r < 0)
		{
			Log("FMVLOG film=%ld video packet: %s", f->num, FmvAvError(r));
			f->failed = 1;
		}
	}
	else if (f->actx && f->pkt->stream_index == f->aidx)
	{
		r = p_avcodec_send_packet(f->actx, f->pkt);

		if (r < 0)
		{
			Log("FMVLOG film=%ld audio packet: %s", f->num, FmvAvError(r));
			f->failed = 1;
		}
		else
			FmvReceiveAudio(f);
	}

	p_av_packet_unref(f->pkt);
}

/* Decode ahead: until a picture waits and the voice holds FMV_AUDIO_AHEAD
 * buffers - never further, and never more than FMV_VIDEO_AHEAD pictures. */
static void FmvFill(FMV_FILM* f)
{
	for (;;)
	{
		FmvReceiveVideo(f);

		if (f->failed || f->video_end || f->demux_end ||
			f->vcount >= FMV_VIDEO_AHEAD || MainThread.ended)
			return;

		const long want_audio = f->actx && !f->wall &&
			FmvQueued(f) < FMV_AUDIO_AHEAD;

		if (f->vcount && !want_audio)
			return;

		FmvReadPacket(f);
	}
}

static void FmvPop(FMV_FILM* f)
{
	p_av_frame_unref(f->vq[f->vhead]);
	f->vhead = (f->vhead + 1) % FMV_VIDEO_AHEAD;
	f->vcount--;
}

// The next picture in order, decoded if need be; 0 at the end or an error.
static AVFrame* FmvNext(FMV_FILM* f, double* pts)
{
	if (!f->vcount)
		FmvFill(f);

	if (!f->vcount || f->failed)
		return 0;

	*pts = f->vpts[f->vhead];
	return f->vq[f->vhead];
}

static void FmvDrawQuad()
{
	D3DTLBUMPVERTEX v[6];
	float w, h;

	w = (float)App.dx.dwRenderWidth;
	h = (float)App.dx.dwRenderHeight;
	memset(v, 0, sizeof(v));

	v[0].sx = 0; v[0].sy = 0; v[0].tu = 0; v[0].tv = 0;
	v[1].sx = w; v[1].sy = 0; v[1].tu = 1; v[1].tv = 0;
	v[2].sx = w; v[2].sy = h; v[2].tu = 1; v[2].tv = 1;
	v[3].sx = 0; v[3].sy = 0; v[3].tu = 0; v[3].tv = 0;
	v[4].sx = w; v[4].sy = h; v[4].tu = 1; v[4].tv = 1;
	v[5].sx = 0; v[5].sy = h; v[5].tu = 0; v[5].tv = 1;

	for (int i = 0; i < 6; i++)
	{
		v[i].sz = 0.5F;
		v[i].rhw = 1.0F;
		v[i].color = 0xFFFFFFFF;
		v[i].specular = 0xFF000000;
	}

	App.dx.lpD3DDevice->SetTexture(0, FmvTexture);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHATESTENABLE, 0);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ZENABLE, 0);
	HWDrawPrimitive(D3DPT_TRIANGLELIST, v, 6);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ZENABLE, 1);
}

/* ShowBinkFrame's steps (fmv.cpp), the conversion of yuv_rgb.cpp in place
 * of BinkCopyToBuffer. A picture larger than the texture is cut to it. */
static void FmvShowPicture(const AVFrame* pic)
{
	D3DLOCKED_RECT rect;

	if (!FmvTexture)
		return;

	if (FAILED(FmvTexture->LockRect(0, &rect, 0, 0)))
		return;

	YuvRgbImage(pic->data, pic->linesize,
		pic->width < FMV_W ? pic->width : FMV_W,
		pic->height < FMV_H ? pic->height : FMV_H, rect.pBits, rect.Pitch);
	FmvTexture->UnlockRect(0);

	_BeginScene();
	App.dx.lpD3DDevice->Clear(0, 0, D3DCLEAR_TARGET, 0, 1.0F, 0);
	FmvDrawQuad();
	_EndScene();
	DXShowFrame();
}

// The decoder of one stream, opened; 0 with a log line if it cannot be.
static AVCodecContext* FmvOpenDecoder(FMV_FILM* f, int idx)
{
	AVStream* s = f->fmt->streams[idx];
	const AVCodec* dec = p_avcodec_find_decoder(s->codecpar->codec_id);
	AVCodecContext* ctx;
	int r;

	if (!dec)
	{
		Log("FMVLOG film=%ld stream %d: no decoder for codec %d", f->num, idx,
			(int)s->codecpar->codec_id);
		return 0;
	}

	ctx = p_avcodec_alloc_context3(dec);

	if (!ctx)
		return 0;

	r = p_avcodec_parameters_to_context(ctx, s->codecpar);
	ctx->pkt_timebase = s->time_base;

	if (r >= 0)
		r = p_avcodec_open2(ctx, dec, 0);

	if (r < 0)
	{
		Log("FMVLOG film=%ld stream %d: %s", f->num, idx, FmvAvError(r));
		p_avcodec_free_context(&ctx);
		return 0;
	}

	return ctx;
}

/* The float voice for the film's audio; without it the film plays on the
 * QPC clock and silent. */
static void FmvOpenAudio(FMV_FILM* f)
{
	WAVEFORMATEX wfx;
	const char* why = 0;

	f->actx = FmvOpenDecoder(f, f->aidx);

	if (!f->actx)
		return;

	f->channels = f->actx->ch_layout.nb_channels;
	f->rate = f->actx->sample_rate;

	if (f->actx->sample_fmt != AV_SAMPLE_FMT_FLT &&
		f->actx->sample_fmt != AV_SAMPLE_FMT_FLTP)
		why = "not float";
	else if (f->channels < 1 || f->channels > XAUDIO2_MAX_AUDIO_CHANNELS ||
		f->rate < XAUDIO2_MIN_SAMPLE_RATE || f->rate > XAUDIO2_MAX_SAMPLE_RATE)
		why = "out of range";
	else if (!g_XAudio2)
		why = "no XAudio2";

	if (!why)
	{
		memset(&wfx, 0, sizeof(wfx));
		wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		wfx.nChannels = (WORD)f->channels;
		wfx.nSamplesPerSec = (DWORD)f->rate;
		wfx.wBitsPerSample = 32;
		wfx.nBlockAlign = (WORD)(f->channels * 4);
		wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

		if (FAILED(g_XAudio2->CreateSourceVoice(&f->voice, &wfx, 0,
			XAUDIO2_DEFAULT_FREQ_RATIO, 0, 0, 0)))
		{
			f->voice = 0;
			why = "no voice";
		}
	}

	if (why)
	{
		Log("FMVLOG film=%ld audio not played (%s)", f->num, why);
		p_avcodec_free_context(&f->actx);
	}
}

static bool FmvOpen(FMV_FILM* f, const char* path)
{
	int r = p_avformat_open_input(&f->fmt, path, 0, 0);

	if (r < 0)
	{
		Log("FMVLOG film=%ld open %s: %s", f->num, path, FmvAvError(r));
		f->fmt = 0;
		return 0;
	}

	r = p_avformat_find_stream_info(f->fmt, 0);

	if (r < 0)
	{
		Log("FMVLOG film=%ld stream info: %s", f->num, FmvAvError(r));
		return 0;
	}

	f->vidx = -1;
	f->aidx = -1;

	for (unsigned i = 0; i < f->fmt->nb_streams; i++)
	{
		const AVMediaType type = f->fmt->streams[i]->codecpar->codec_type;

		if (type == AVMEDIA_TYPE_VIDEO && f->vidx < 0)
			f->vidx = (int)i;
		else if (type == AVMEDIA_TYPE_AUDIO && f->aidx < 0)
			f->aidx = (int)i;
	}

	if (f->vidx < 0)
	{
		Log("FMVLOG film=%ld no video stream", f->num);
		return 0;
	}

	f->vctx = FmvOpenDecoder(f, f->vidx);

	if (!f->vctx)
		return 0;

	if (f->vctx->pix_fmt != AV_PIX_FMT_YUV420P &&
		f->vctx->pix_fmt != AV_PIX_FMT_YUVA420P)
	{
		const char* name = p_av_get_pix_fmt_name(f->vctx->pix_fmt);

		Log("FMVLOG film=%ld refused: picture format %s", f->num,
			name ? name : "?");
		return 0;
	}

	AVStream* vs = f->fmt->streams[f->vidx];
	const AVRational fr = vs->avg_frame_rate.num ? vs->avg_frame_rate :
		vs->r_frame_rate;

	f->vtb = (double)vs->time_base.num / vs->time_base.den;
	f->fdur = (fr.num && fr.den) ? (double)fr.den / fr.num : f->vtb;

	if (f->aidx >= 0)
		FmvOpenAudio(f);

	f->pkt = p_av_packet_alloc();
	f->afrm = p_av_frame_alloc();

	long frames = 1;

	for (long i = 0; i < FMV_VIDEO_AHEAD; i++)
	{
		f->vq[i] = p_av_frame_alloc();
		frames = frames && f->vq[i];
	}

	if (!f->pkt || !f->afrm || !frames)
	{
		Log("FMVLOG film=%ld out of memory", f->num);
		return 0;
	}

	const char* pf = p_av_get_pix_fmt_name(f->vctx->pix_fmt);
	const char* sf = f->voice ? p_av_get_sample_fmt_name(f->actx->sample_fmt)
		: 0;

	if (f->voice)
		Log("FMVLOG film=%ld video=%dx%d %s fps=%d/%d audio=%ldx%ld %s",
			f->num, f->vctx->width, f->vctx->height, pf ? pf : "?", fr.num,
			fr.den, f->rate, f->channels, sf ? sf : "?");
	else
		Log("FMVLOG film=%ld video=%dx%d %s fps=%d/%d audio=none", f->num,
			f->vctx->width, f->vctx->height, pf ? pf : "?", fr.num, fr.den);

	return 1;
}

/* Everything of one film is let go, the voice first: XAudio2 reads the
 * buffers until DestroyVoice returns. */
static void FmvClose(FMV_FILM* f)
{
	if (f->voice)
	{
		f->voice->Stop(0);
		f->voice->FlushSourceBuffers();
		f->voice->DestroyVoice();
		f->voice = 0;
	}

	for (long i = 0; i < FMV_AUDIO_SLOTS; i++)
		free(f->slot[i]);

	for (long i = 0; i < FMV_VIDEO_AHEAD; i++)
	{
		if (f->vq[i])
			p_av_frame_free(&f->vq[i]);
	}

	if (f->afrm)
		p_av_frame_free(&f->afrm);

	if (f->pkt)
		p_av_packet_free(&f->pkt);

	if (f->vctx)
		p_avcodec_free_context(&f->vctx);

	if (f->actx)
		p_avcodec_free_context(&f->actx);

	if (f->fmt)
		p_avformat_close_input(&f->fmt);
}

/* The film from its first picture to its end, a skip or an error. Every
 * way out returns here to FmvAvPlay, which runs the tail. */
static void FmvRun(FMV_FILM* f, const char* path)
{
	LARGE_INTEGER freq;
	double pts = 0, now;
	long end = 0;

	QueryPerformanceFrequency(&freq);
	f->freq = freq.QuadPart;

	if (!FmvOpen(f, path))
		f->failed = 1;
	else
	{
		// Ahead before the clock starts: a picture and the audio queue.
		FmvFill(f);
		FmvStartVoice(f);
		f->t_start = FmvQpc();
		S_UpdateInput();

		// Skip only on the edge: the key that skipped the previous video
		// (Eidos logo) is usually still held when the next one (intro)
		// starts - with a plain level check the intro skipped along with
		// it. Only a press that comes after a release counts; on entry the
		// key counts as held.
		long skipHeld = 1;

		for (;;)
		{
			const AVFrame* pic = FmvNext(f, &pts);

			if (!pic)
			{
				end = 1;
				break;
			}

			// IN_SELECT added so Cross/Enter also skips FMVs, not just Escape
			// (IN_OPTION) or Draw Weapon (IN_DRAW) - matches Cross already being
			// the confirm button everywhere else in the menus.
			const long skipNow = (input & (IN_OPTION | IN_DRAW | IN_SELECT)) != 0;

			if ((skipNow && !skipHeld) || MainThread.ended)
				break;

			skipHeld = skipNow;

			// Late by more than a picture: dropped, the next one decoded.
			now = FmvClock(f);

			if (now - pts > f->fdur)
			{
				f->dropped++;
				FmvPop(f);
				continue;
			}

			// Early: its time is waited for, the voice kept fed. One due
			// more than a second ahead (a broken time stamp) is not.
			while (now < pts && pts - now < 1.0 && !MainThread.ended)
			{
				FmvFill(f);
				Sleep(1);
				now = FmvClock(f);
			}

			FmvShowPicture(pic);
			FmvPop(f);
			f->shown++;
			S_UpdateInput();
		}

		// At the end the queued audio plays out; a skip stops it at once.
		while (end && !f->failed && FmvQueued(f) && !MainThread.ended)
		{
			Sleep(1);
			FmvClock(f);
		}
	}

	Log("FMVLOG film=%ld frames=%ld dropped=%ld audio_frames=%ld skipped=%d "
		"seconds=%.2f", f->num, f->shown, f->dropped, f->audio_frames,
		(!end && !f->failed && !MainThread.ended) ? 1 : 0,
		f->t_start ? (double)(FmvQpc() - f->t_start) / f->freq : 0.0);
	FmvClose(f);
}

long FmvAvPlay(long num)
{
	char name[80];
	char path[80];
	FMV_FILM film;

	if (MainThread.ended)
		return 0;

	S_CDStop();

	if (fmvs_disabled)
		return 0;

	sprintf(name, "movie\\fmv%01d.bik", num);
	memset(path, 0, sizeof(path));
	strcat(path, name);
	Log("PlayFMV %s", path);
	App.fmv = 1;

	// The texture is made lazily, on the device of this film (fmv.cpp).
	if (!FmvTexture)
		App.dx.lpD3DDevice->CreateTexture(FMV_W, FMV_H, 1, 0, D3DFMT_X8R8G8B8,
			D3DPOOL_MANAGED, &FmvTexture, 0);

	memset(&film, 0, sizeof(film));
	film.num = num;
	FmvRun(&film, path);

	DXChangeOutputFormat(sfx_frequencies[SoundQuality], 1);
	HWInitialise();
	ClearSurfaces();
	App.fmv = 0;
	return 0;
}

#endif
