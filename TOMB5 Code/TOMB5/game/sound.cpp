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

// Diagnose: wer hat den laufenden Klang gerufen? AnimateItem setzt beide
// vor den PlaySound-Kommandos und raeumt danach auf; alle anderen Rufer
// lassen -1 stehen. Nur fuers SFXLOG, kein Spielverhalten.
long SfxKommandoRufer = -1;
long SfxKommandoRaum = -1;
long SfxKommandoAnim = -1;
long SfxKommandoTyp = -1;
long SfxKommandoRaumFlags = -1;

// Levelkulisse: gesetzt, solange SoundEffects() die ortsfesten Klangquellen
// des Levels abarbeitet (Gaskocher, Ventilatoren, Fernseher). Alles andere -
// Gegner, Lara, Anim-Kommandos - laeuft mit 0 und bleibt unberuehrt.
long SfxQuelleAmbient = 0;

// Reichweite der Levelkulisse in Prozent des Datenwerts (#114). TR5 kennt
// keine Wanddaempfung: ein Klang mit radius 10 ist 11 Sektoren weit hoerbar,
// quer durch geschlossene Raeume. In joby3 stapelt sich davon eine ganze
// Etage. 100 = unveraendert wie Vanilla. Seit #108 stellt Balance.txt den
// Wert ein, Voreinstellung 65.

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

	sound_active = 1;
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

				// Dasselbe fuer laufende Klaenge (#126) - siehe SoundEffect:
				// aus naechster Naehe gibt es keine Richtung, sonst kippt
				// ein Dauerton bei jeder Drehung von links nach rechts.
				nPan = (EgoKopfWeg() && !distance) ? 0 : ((CamRot.y << 4) + phd_atan(dz, dx));
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

	// Nur die ortsfeste Levelkulisse wird eingehegt - Gegner und Lara
	// behalten ihre Reichweite, damit man Schritte und Rufe weiter so
	// frueh hoert wie vorgesehen.
	if (SfxQuelleAmbient)
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

		// Ego-Sicht (#126): ein Klang, der IM Ohr entsteht, hat keine
		// Richtung. Die Kamera sitzt in Laras Kopf, ihre eigene Waffe also
		// eine Handbreit daneben - phd_atan rechnet aus diesem winzigen
		// Vektor einen vollen Winkel, und weil die Samples mono sind, sprang
		// der Schuss hart von einem Lautsprecher zum anderen (vom Nutzer
		// gehoert). "distance" ist hier schon 0, sobald die Quelle naeher
		// als ein Sektor steht - genau der Fall, den wir mittig legen.
		// Nur in Ego-Sicht: die normale Kamera steht 1536 hinter Lara, dort
		// bleibt alles wie bei Core.
		if (!(info->flags & 0x1000) && !(EgoKopfWeg() && !distance))
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

	// Stapelschutz fuer die Levelkulisse (#114). SoundEffects() ruft jede
	// ortsfeste Quelle JEDEN Frame auf; steht ihr Klang auf Modus 0
	// ("stapelt"), entstehen 30 uebereinanderliegende Stimmen je Sekunde.
	// Das ist nie gewollt, sondern ein Datenfehler - belegt am Gaskocher in
	// joby3: SFX_KITCHEN_HOB_LOOP (220) traegt "LOOP" im Namen, aber Modus 0
	// und keine Zufallsdrossel (242 Abspielvorgaenge in einem Log-Lauf).
	// Solche Quellen werden hier wie Modus 1 behandelt: neu anstossen erst,
	// wenn der vorige Durchlauf zu Ende ist. Einmalklaenge von Gegnern und
	// Lara bleiben unberuehrt, die laufen nicht ueber diesen Weg.
	if (SfxQuelleAmbient && flag == 0)
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

	// Klangprotokoll: was wird WIRKLICH abgespielt?
	//
	// Gebaut, weil beim Compsognathus-Import ein fremder, basslastiger Klang
	// dazwischenfuhr, den keine Messung an den Dateien fand: Modell, Zuordnung,
	// Soundmap, SAMPLE_INFO-Kette und Wave-Daten waren alle nachweislich
	// richtig. Wer den Verursacher nicht in den Daten findet, muss ihn zur
	// Laufzeit sehen.
	//
	// Eine Zeile je Abspielvorgang mit allem, was zur Zuordnung noetig ist:
	// die gerufene SFX-Nummer, der Eintrag der Soundmap, die tatsaechliche
	// Sample-Nummer samt Variantenzahl und die fertige Lautstaerke. Die Zeile
	// ist bewusst durchsuchbar aufgebaut ("SFXLOG"), damit sie sich aus dem
	// uebrigen Protokoll herausfiltern laesst.
	// Der Rufer wird von AnimateItem (control.cpp) vor dem Aufruf gesetzt -
	// kommt der Klang nicht aus einem Anim-Kommando, steht dort -1 und die
	// Zeile bleibt wie gehabt. Gebaut fuer die Frage "WER hat das gespielt?"
	// (Wasserschritt-Fall 2026-08-19: nur die Nummer im Log, der Verursacher
	// musste ueber die Anim-Saetze rueckgerechnet werden).
	if (SfxKommandoRufer >= 0)
		Log("SFXLOG sfx=%d map=%d sample=%d von %d Varianten vol=%d pitch=%d %s obj=%d raum=%d anim=%d typ=0x%X rflags=0x%X",
			sfx, lut, sample, rnd, volume, pitch, flag == 3 ? "Schleife" : "einmal",
			SfxKommandoRufer, SfxKommandoRaum, SfxKommandoAnim, SfxKommandoTyp, SfxKommandoRaumFlags);
	else
		Log("SFXLOG sfx=%d map=%d sample=%d von %d Varianten vol=%d pitch=%d %s",
			sfx, lut, sample, rnd, volume, pitch, flag == 3 ? "Schleife" : "einmal");

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
