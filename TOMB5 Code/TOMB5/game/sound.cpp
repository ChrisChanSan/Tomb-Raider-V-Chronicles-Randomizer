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
#include "sound.h"
#include "../specific/function_stubs.h"
#include "../specific/dxsound.h"
#include "../specific/3dmath.h"
#include "gameflow.h"
#include "deltapak.h"
#include "camera.h"
#include "control.h"
#include "balance.h"

SAMPLE_INFO* sample_infos;
SoundSlot LaSlot[32];
short* sample_lut;
long sound_active;
long sound_cut_flag = 0;

// Diagnosis: who called the running sound? AnimateItem sets these before
// the PlaySound commands and cleans up afterwards; all other callers leave
// -1 in place. Only for the SFXLOG, no game behavior.
long SfxCommandCaller = -1;
long SfxCommandRoom = -1;
long SfxCommandAnim = -1;
long SfxCommandType = -1;
long SfxCommandRoomFlags = -1;

// Level scenery: set while SoundEffects() works through the level's fixed
// sound sources (gas stoves, fans, televisions). Everything else - enemies,
// Lara, anim commands - runs with 0 and stays untouched.
long SfxSourceAmbient = 0;

// Range of the level scenery in percent of the data value (#114). TR5 knows
// no wall damping: a sound with radius 10 is audible 11 sectors out, right
// through closed rooms. In joby3 a whole story of that piles up. 100 =
// unchanged, as vanilla. Since #108 Balance.txt sets the value, default 65.

void SoundEffectCS(long sfx, PHD_3DPOS* pos, long flags)
{
	long bak;

	bak = GLOBAL_playing_cutseq;
	GLOBAL_playing_cutseq = 0;
	SoundEffect(sfx, pos, flags);
	GLOBAL_playing_cutseq = bak;
}

void SayNo()
{
	long fx;

	fx = SFX_LARA_NO;

	if (Gameflow->Language == 1)
		fx = SFX_LARA_NO_FRENCH;
	else if (Gameflow->Language == 6)
		fx = SFX_LARA_NO_JAPANESE;

	SoundEffect(fx, 0, SFX_ALWAYS);
}

void SOUND_Init()
{
	for (int i = 0; i < 32; i++)
		LaSlot[i].nSampleInfo = -1;

	// Does NOT switch sound on. Whether there is anything to play through is
	// decided by DXDSCreate, which sets sound_active only once XAudio2 and the
	// mastering voice really exist; here the slots are merely cleared.
	//
	// It used to set the flag unconditionally, and that was a crash waiting
	// for a machine without an audio device: XAudio2 fails, g_XAudio2 stays
	// null, SOUND_Init turns sound "on" anyway and the first effect
	// dereferences null. It never showed because the setup dialog forced
	// "Disable" whenever the DirectSound enumeration found no device - a dead
	// list holding up a live engine by accident. The engine answers for itself
	// now, and the list could go.
}

void SOUND_Stop()
{
	if (sound_active)
	{
		S_SoundStopAllSamples();

		for (int i = 0; i < 32; i++)
			LaSlot[i].nSampleInfo = -1;
	}
}

void StopSoundEffect(long sfx)
{
	long lut;

	if (sound_active)
	{
		lut = sample_lut[sfx];

		for (int i = 0; i < 32; i++)
		{
			if (LaSlot[i].nSampleInfo >= lut && LaSlot[i].nSampleInfo < (lut + ((sample_infos[lut].flags >> 2) & 0xF)))
			{
				S_SoundStopSample(i);
				LaSlot[i].nSampleInfo = -1;
			}
		}
	}
}

void GetPanVolume(SoundSlot* slot)
{
	long dx, dy, dz, radius, distance, nPan, nVolume;

	if (slot->distance || slot->pos.x || slot->pos.y || slot->pos.z)
	{
		dx = slot->pos.x - camera.pos.x;
		dy = slot->pos.y - camera.pos.y;
		dz = slot->pos.z - camera.pos.z;
		radius = sample_infos[slot->nSampleInfo].radius << 10;

		if (dx < -radius || dx > radius || dy < -radius || dy > radius || dz < -radius || dz > radius)
		{
			slot->distance = 0;
			slot->nPan = 0;
			slot->nVolume = 0;
		}
		else
		{
			distance = SQUARE(dx) + SQUARE(dy) + SQUARE(dz);

			if (distance <= SQUARE(radius))
			{
				if (distance >= 0x100000)
					distance = phd_sqrt(distance) - 1024;
				else
					distance = 0;

				// The same for running sounds (#126) - see SoundEffect: at
				// point-blank range there is no direction, or a sustained
				// tone flips from left to right on every turn.
				nPan = (FirstPersonActive() && !distance) ? 0 : ((CamRot.y << 4) + phd_atan(dz, dx));
				nVolume = slot->OrigVolume;

				if (distance)
					nVolume = (nVolume * (4096 - (phd_sin((distance << 14) / radius) >> 2))) >> 12;

				if (nVolume > 0)
				{
					if (nVolume > 0x7FFF)
						nVolume = 0x7FFF;

					slot->nVolume = nVolume;
					slot->nPan = nPan;
					slot->distance = distance;
				}
				else
				{
					slot->distance = 0;
					slot->nPan = 0;
					slot->nVolume = 0;
				}
			}
			else
			{
				slot->distance = 0;
				slot->nPan = 0;
				slot->nVolume = 0;
			}
		}
	}
}

long SoundEffect(long sfx, PHD_3DPOS* pos, long flags)
{
	SAMPLE_INFO* info;
	PHD_3DPOS pos2;
	long lut, radius, pan, dx, dy, dz, distance, volume, OrigVolume, pitch, rnd, sample, flag, vol, slot;

	if (GLOBAL_playing_cutseq)
	{
		if (!sound_cut_flag)
			return sound_cut_flag;
	}

	if (sfx == SFX_LARA_NO)
	{
		switch (Gameflow->Language)
		{
		case 1:
			sfx = SFX_LARA_NO_FRENCH;
			break;

		case 2:
		case 3:
		case 4:
			sfx = SFX_LARA_NO;
			break;

		case 6:
			sfx = SFX_LARA_NO_JAPANESE;
			break;
		}
	}

	if (!sound_active || !(flags & 2) && (flags & 1) != (room[camera.pos.room_number].flags & 1))
		return 0;

	lut = sample_lut[sfx];

	if (lut == -1)
	{
		Log("Non present sample:%d", sfx);
		sample_lut[sfx] = -2;
		return 0;
	}

	if (lut == -2)
		return 0;

	info = &sample_infos[lut];

	if (info->randomness)
	{
		if ((GetRandomDraw() & 0xFF) > (uchar)info->randomness)
			return 0;
	}

	radius = (info->radius + 1) << 10;

	// Only the fixed level scenery is reined in - enemies and Lara keep
	// their range, so steps and calls are still heard as early as
	// intended.
	if (SfxSourceAmbient)
		radius = radius * balance.sceneryRange / 100;

	pan = 0;

	if (pos)
	{
		dx = pos->x_pos - camera.pos.x;
		dy = pos->y_pos - camera.pos.y;
		dz = pos->z_pos - camera.pos.z;

		if (dx < -radius)
			return 0;

		if (dx > radius)
			return 0;

		if (dy < -radius)
			return 0;

		if (dy > radius)
			return 0;

		if (dz < -radius)
			return 0;

		if (dz > radius)
			return 0;

		distance = SQUARE(dx) + SQUARE(dy) + SQUARE(dz);

		if (distance > SQUARE(radius))
			return 0;

		if (distance >= 0x100000)
			distance = phd_sqrt(distance) - 1024;
		else
			distance = 0;

		// First person (#126): a sound born IN the ear has no direction.
		// The camera sits in Lara's head, her own weapon thus a hand's
		// width beside it - phd_atan computes a full angle from this tiny
		// vector, and because the samples are mono, the shot jumped hard
		// from one speaker to the other (heard by the user). "distance" is
		// already 0 here once the source stands closer than one sector -
		// exactly the case we center. First person only: the normal camera
		// stands 1536 behind Lara, there everything stays as Core had it.
		if (!(info->flags & 0x1000) && !(FirstPersonActive() && !distance))
			pan = (CamRot.y << 4) + phd_atan(dz, dx);
	}
	else
	{
		distance = 0;
		pos2.x_pos = 0;
		pos2.y_pos = 0;
		pos2.z_pos = 0;
		pos = &pos2;
	}

	volume = info->volume << 6;

	if (flags & SFX_SETVOL)
		volume = (volume * ((flags >> 8) & 0x1F)) >> 5;

	if (info->flags & 0x4000)
		volume -= GetRandomDraw() << 12 >> 15;

	OrigVolume = volume;

	if (distance)
		volume = (volume * (4096 - (phd_sin((distance << 14) / radius) >> 2))) >> 12;

	if (volume <= 0)
		return 0;

	if (volume > 0x7FFF)
		volume = 0x7FFF;

	if (flags & SFX_SETPITCH)
		pitch = (flags >> 8) & 0xFFFF00;
	else
		pitch = 0x10000;

	pitch += info->pitch << 9;

	if (info->flags & 0x2000)
		pitch += ((6000 * GetRandomDraw()) >> 14) - 6000;

	if (info->number < 0)
		return 0;

	rnd = (info->flags >> 2) & 0xF;

	if (rnd == 1)
		sample = info->number;
	else
		sample = info->number + ((rnd * GetRandomDraw()) >> 15);

	flag = info->flags & 3;

	// Stacking guard for the level scenery (#114). SoundEffects() calls
	// every fixed source EVERY frame; if its sound stands on mode 0
	// ("stacks"), 30 overlapping voices per second arise. That is never
	// intended but a data error - proven at the gas stove in joby3:
	// SFX_KITCHEN_HOB_LOOP (220) carries "LOOP" in its name but mode 0 and
	// no random throttle (242 plays in one log run). Such sources are
	// treated like mode 1 here: restart only once the previous pass has
	// finished. One-shot sounds of enemies and Lara stay untouched, they do
	// not run through this path.
	if (SfxSourceAmbient && flag == 0)
		flag = 1;

	switch (flag)
	{
	case 1:

		for (int i = 0; i < 32; i++)
		{
			if (LaSlot[i].nSampleInfo == lut)
			{
				if (S_SoundSampleIsPlaying(i))
					return 0;

				LaSlot[i].nSampleInfo = -1;
			}
		}

		break;

	case 2:

		for (int i = 0; i < 32; i++)
		{
			if (LaSlot[i].nSampleInfo == lut)
			{
				S_SoundStopSample(i);
				LaSlot[i].nSampleInfo = -1;
				break;
			}
		}

		break;

	case 3:

		for (int i = 0; i < 32; i++)
		{
			if (LaSlot[i].nSampleInfo == lut)
			{
				if (volume > LaSlot[i].nVolume)
				{
					LaSlot[i].OrigVolume = OrigVolume;
					LaSlot[i].nVolume = volume;
					LaSlot[i].nPan = pan;
					LaSlot[i].nPitch = pitch;
					LaSlot[i].distance = distance;
					LaSlot[i].pos.x = pos->x_pos;
					LaSlot[i].pos.y = pos->y_pos;
					LaSlot[i].pos.z = pos->z_pos;
					return 1;
				}

				return 0;
			}
		}

		break;
	}

	// Sound log: what is REALLY being played?
	//
	// Built because a foreign, bass-heavy sound cut in during the
	// Compsognathus import that no measurement on the files could find:
	// model, mapping, soundmap, SAMPLE_INFO chain and wave data were all
	// provably right. Whoever cannot find the culprit in the data must see
	// it at runtime.
	//
	// One line per play with everything needed for attribution: the called
	// SFX number, the soundmap entry, the actual sample number with its
	// variant count and the final volume. The line is deliberately built to
	// be searchable ("SFXLOG"), so it can be filtered out of the rest of
	// the log.
	// The caller is set by AnimateItem (control.cpp) before the call - if
	// the sound does not come from an anim command, -1 stands there and the
	// line stays as before. Built for the question "WHO played that?"
	// (water-step case 2026-08-19: only the number in the log, the culprit
	// had to be back-computed through the anim sets).
	if (SfxCommandCaller >= 0)
		Log("SFXLOG sfx=%d map=%d sample=%d of %d variants vol=%d pitch=%d %s obj=%d room=%d anim=%d type=0x%X rflags=0x%X",
			sfx, lut, sample, rnd, volume, pitch, flag == 3 ? "loop" : "once",
			SfxCommandCaller, SfxCommandRoom, SfxCommandAnim, SfxCommandType, SfxCommandRoomFlags);
	else
		Log("SFXLOG sfx=%d map=%d sample=%d of %d variants vol=%d pitch=%d %s",
			sfx, lut, sample, rnd, volume, pitch, flag == 3 ? "loop" : "once");

	if (flag == 3)
		dx = S_SoundPlaySampleLooped(sample, (ushort)volume, pitch, (short)pan);
	else
		dx = S_SoundPlaySample(sample, (ushort)volume, pitch, (short)pan);
	
	if (dx >= 0)
	{
		LaSlot[dx].OrigVolume = OrigVolume;
		LaSlot[dx].nVolume = volume;
		LaSlot[dx].nPan = pan;
		LaSlot[dx].nPitch = pitch;
		LaSlot[dx].nSampleInfo = lut;
		LaSlot[dx].distance = distance;
		LaSlot[dx].pos.x = pos->x_pos;
		LaSlot[dx].pos.y = pos->y_pos;
		LaSlot[dx].pos.z = pos->z_pos;
		return 1;
	}

	if (dx == -1)
	{
		vol = 0x8000000;
		slot = -1;

		for (int i = 0; i < 32; i++)
		{
			if ((LaSlot[i].nSampleInfo >= 0) && (LaSlot[i].nVolume <= vol))
			{
				vol = LaSlot[i].nVolume;
				slot = i;
			}
		}

		if (volume > vol)
		{
			S_SoundStopSample(slot);
			LaSlot[slot].nSampleInfo = -1;

			if (flag == 3)
				dx = S_SoundPlaySampleLooped(sample, (ushort)volume, pitch, (short)pan);
			else
				dx = S_SoundPlaySample(sample, (ushort)volume, pitch, (short)pan);

			if (dx >= 0)
			{
				LaSlot[dx].OrigVolume = OrigVolume;
				LaSlot[dx].nVolume = volume;
				LaSlot[dx].nPan = pan;
				LaSlot[dx].nPitch = pitch;
				LaSlot[dx].nSampleInfo = lut;
				LaSlot[dx].distance = distance;
				LaSlot[dx].pos.x = pos->x_pos;
				LaSlot[dx].pos.y = pos->y_pos;
				LaSlot[dx].pos.z = pos->z_pos;
				return 1;
			}
		}

		return 0;
	}

	if (sample >= 0)
		Log("Can't play SFX %d", sfx);

	info->number = -1;
	return 0;
}
