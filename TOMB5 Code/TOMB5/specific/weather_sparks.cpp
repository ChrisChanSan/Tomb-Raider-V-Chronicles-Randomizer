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
#include <string.h>
#include "weather_sparks.h"
#include "../global/types.h"
#include "3dmath.h"
#include "function_stubs.h"		// GetRandomDraw - the draw's generator
#include "specificfx.h"			// S_DrawOneSpark, S_DrawOneRipple
#include "../game/objects.h"
#include "../game/interpolate.h"	// InterpIsTweenFrame, InterpParticleAlpha

/* The rain's splashes and ripples.
 *
 * DoRain simulates on the frame that carries the tick, around that frame's
 * camera and with the draw's generator, so how many drops land - and where
 * - hangs on the frame timing. What they left used to go into the logic's
 * own arrays: each splash took a slot of the spark array through the
 * logic's search for a free spark and moved its cursor, each ripple a slot
 * of ripples[], and both drew from the logic's generator. The draws were
 * given back at the end of the frame at the display's rate; the
 * slots and the cursor were not, so the next spark the logic made - a link
 * of a chained explosion - landed in another slot from one run to the next,
 * and whether it aged in the same pass of UpdateSparks moved its fire by a
 * tick. At thirty, one exact frame per tick, it never varied.
 *
 * Here they are the weather's: arrays of the same structs, so the same
 * lines draw them (S_DrawOneSpark, S_DrawOneRipple), made with the draw's
 * generator over the same ranges, stepped by DoRain once on each frame it
 * simulates - every frame at thirty, once per tick at the display's rate,
 * as the drops themselves. Nothing in the logic reads them.
 *
 * The steps are the logic's steps cut to what a splash and a rain ripple
 * are: UpdateSparks (game\effect2.cpp) for a spark as TriggerSmallSplash
 * sets it up, UpdateSplashes' ripple loop for SetupRipple's flags 0. */

static SPARKS weather_splash[WEATHER_SPARKS];
static RIPPLE_STRUCT weather_ripple[WEATHER_RIPPLES];
static long weather_next = 0;
static long weather_peak = 0;
static long weather_ripple_live = 0;
static long weather_ripple_peak = 0;

static_assert((WEATHER_SPARKS & (WEATHER_SPARKS - 1)) == 0,
	"the cursor wraps by a mask");

/* The slot for a splash: the first one off from the cursor on, else the
 * one with the least life left - the logic's two searches for a free spark
 * (game\effect2.cpp), without the lights and the no-kill flag no splash
 * carries. */
static long WeatherFreeSpark()
{
	long free, min_life;

	free = weather_next;

	for (long lp = 0; lp < WEATHER_SPARKS; lp++)
	{
		if (!weather_splash[free].On)
		{
			weather_next = (free + 1) & (WEATHER_SPARKS - 1);
			return free;
		}

		free = (free + 1) & (WEATHER_SPARKS - 1);
	}

	free = 0;
	min_life = 4095;

	for (long lp = 0; lp < WEATHER_SPARKS; lp++)
	{
		if (weather_splash[lp].Life < min_life)
		{
			free = lp;
			min_life = weather_splash[lp].Life;
		}
	}

	weather_next = (free + 1) & (WEATHER_SPARKS - 1);
	return free;
}

/* TriggerSmallSplash (game\tomb4fx.cpp) for one spark: its fields, its
 * three draws in their order and over their ranges, from the draw's
 * generator. The slot is cleared first, so nothing of a previous spark is
 * left in it: the logic's slot kept the fields the splash does not set -
 * the sizes, the scalar, the turn, the room - from whoever had it before,
 * and the logic's search set only the sprite, the chain and the light. None of
 * them is read for a splash: without SF_DEF it is drawn as a line, the
 * sizes go to the sprite and the fire test only. Its colour is not set
 * here either; the step below writes it before the first draw, as
 * UpdateSparks did before S_DrawSparks first drew a splash at thirty. */
void WeatherSplash(long x, long y, long z)
{
	SPARKS* sptr;
	short ang;

	sptr = &weather_splash[WeatherFreeSpark()];
	memset(sptr, 0, sizeof(SPARKS));
	sptr->Dynamic = -1;
	sptr->Def = (uchar)objects[DEFAULT_SPRITES].mesh_index;
	sptr->On = 1;
	sptr->sR = 64;
	sptr->sG = 64;
	sptr->sB = 64;
	sptr->dR = 32;
	sptr->dG = 32;
	sptr->dB = 32;
	sptr->ColFadeSpeed = 4;
	sptr->FadeToBlack = 8;
	sptr->Life = 24;
	sptr->sLife = 24;
	sptr->TransType = 2;
	ang = GetRandomDraw() & 0xFFF;
	sptr->Xvel = -rcossin_tbl[ang << 1] >> 5;
	sptr->Yvel = -640 - (GetRandomDraw() & 0xFF);
	sptr->Zvel = rcossin_tbl[(ang << 1) + 1] >> 5;
	sptr->x = x + (sptr->Xvel >> 3);
	sptr->y = y - (sptr->Yvel >> 5);
	sptr->z = z + (sptr->Zvel >> 3);
	sptr->Friction = 5;
	sptr->Flags = SF_NONE;
	sptr->MaxYvel = 0;
	sptr->Gravity = (GetRandomDraw() & 0xF) + 64;
}

/* SetupRipple (game\effect2.cpp) as DoRain called it - size 3, flags 0 -
 * with its one draw from the draw's generator. Like SetupRipple it takes
 * the first free slot and makes nothing when there is none. */
void WeatherRipple(long x, long y, long z)
{
	RIPPLE_STRUCT* ripple;
	long num;

	num = 0;

	while (weather_ripple[num].flags & 1)
	{
		num++;

		if (num >= WEATHER_RIPPLES)
			return;
	}

	ripple = &weather_ripple[num];
	ripple->flags = 1;
	ripple->size = 3;
	ripple->life = (GetRandomDraw() & 0xF) + 48;
	ripple->init = 1;
	ripple->x = x;
	ripple->y = y;
	ripple->z = z;

	weather_ripple_live++;

	if (weather_ripple_live > weather_ripple_peak)
		weather_ripple_peak = weather_ripple_live;
}

/* UpdateSparks' pass for a splash, line for line: the life, the colour
 * from start to end over ColFadeSpeed, the fade to black under
 * FadeToBlack with its end below 8, gravity, the friction of the low four
 * bits, the move. What it leaves out a splash never takes: Friction's high
 * bits, MaxYvel and SF_OUTSIDE's wind are 0, SF_UNWATER, SF_ROTATE, the
 * chain (extras), the fire and damage test and the light (Dynamic -1) are
 * not set, and the size the pass also walks stays 0 between sizes of 0. */
void WeatherSparksStep()
{
	SPARKS* sptr;
	long fade, live;

	live = 0;

	for (int i = 0; i < WEATHER_SPARKS; i++)
	{
		sptr = &weather_splash[i];

		if (!sptr->On)
			continue;

		sptr->Life--;

		if (!sptr->Life)
		{
			sptr->On = 0;
			continue;
		}

		if (sptr->sLife - sptr->Life < sptr->ColFadeSpeed)
		{
			fade = ((sptr->sLife - sptr->Life) << 16) / sptr->ColFadeSpeed;
			sptr->R = uchar(sptr->sR + ((fade * (sptr->dR - sptr->sR)) >> 16));
			sptr->G = uchar(sptr->sG + ((fade * (sptr->dG - sptr->sG)) >> 16));
			sptr->B = uchar(sptr->sB + ((fade * (sptr->dB - sptr->sB)) >> 16));
		}
		else if (sptr->Life < sptr->FadeToBlack)
		{
			fade = ((sptr->Life - sptr->FadeToBlack) << 16) /
				sptr->FadeToBlack + 0x10000;
			sptr->R = uchar((sptr->dR * fade) >> 16);
			sptr->G = uchar((sptr->dG * fade) >> 16);
			sptr->B = uchar((sptr->dB * fade) >> 16);

			if (sptr->R < 8 && sptr->G < 8 && sptr->B < 8)
			{
				sptr->On = 0;
				continue;
			}
		}
		else
		{
			sptr->R = sptr->dR;
			sptr->G = sptr->dG;
			sptr->B = sptr->dB;
		}

		sptr->Yvel += sptr->Gravity;

		if (sptr->Friction & 0xF)
		{
			sptr->Xvel -= sptr->Xvel >> (sptr->Friction & 0xF);
			sptr->Zvel -= sptr->Zvel >> (sptr->Friction & 0xF);
		}

		sptr->x += sptr->Xvel >> 5;
		sptr->y += sptr->Yvel >> 5;
		sptr->z += sptr->Zvel >> 5;
		live++;
	}

	if (live > weather_peak)
		weather_peak = live;
}

// UpdateSplashes' ripple loop (game\effect2.cpp), unchanged.
void WeatherRipplesStep()
{
	RIPPLE_STRUCT* ripple;

	weather_ripple_live = 0;

	for (int i = 0; i < WEATHER_RIPPLES; i++)
	{
		ripple = &weather_ripple[i];

		if (!(ripple->flags & 1))
			continue;

		if (ripple->size < 252)
		{
			if (ripple->flags & 2)
				ripple->size += 2;
			else
				ripple->size += 4;
		}

		if (ripple->init)
		{
			if (ripple->init < ripple->life)
			{
				if (ripple->flags & 2)
					ripple->init += 8;
				else
					ripple->init += 4;

				if (ripple->init >= ripple->life)
					ripple->init = 0;
			}
		}
		else
		{
			ripple->life -= 3;

			if (ripple->life > 250)
				ripple->flags = 0;
		}

		if (ripple->flags & 1)
			weather_ripple_live++;
	}
}

/* The splashes through S_DrawSparks' own lines, inside its matrix; the
 * count of those trailed goes to its INTERP_PART_SPARK.
 *
 * The trail. The logic's sparks are stepped by the tick before a frame
 * draws them; these are stepped by DoRain, which runs after S_DrawSparks
 * on the frame that carries the tick. So that frame draws them as they
 * stand, not yet stepped - the full instant, a blend of 1 - and the frames
 * after it trail them back by the step DoRain has just taken, as the
 * logic's are trailed by the tick's. At thirty and on fps_mode 1's own
 * tick frame the blend is 1 anyway; at the display's rate the carrying
 * frame's blend can be below 1, and trailing there would walk them back a
 * step and forward again on every tick.
 *
 * A splash is drawn as a line (no SF_DEF), so the sprite's far-plane end
 * in S_DrawDrawSparksNEW is never reached for one; it would not matter if
 * it were - nothing in the logic reads these. */
long WeatherSparksDraw()
{
	SPARKS* sptr;
	long smallest_size, moved;
	const float pa = InterpIsTweenFrame() ? InterpParticleAlpha() : 1.0F;

	smallest_size = 0;
	moved = 0;

	for (int i = 0; i < WEATHER_SPARKS; i++)
	{
		sptr = &weather_splash[i];

		if (sptr->On)
			moved += S_DrawOneSpark(sptr, sptr->x, sptr->y, sptr->z, pa,
				&smallest_size);
	}

	return moved;
}

// The ripples through S_DrawSplashes' own lines.
void WeatherRipplesDraw()
{
	for (int i = 0; i < WEATHER_RIPPLES; i++)
	{
		if (weather_ripple[i].flags & 1)
			S_DrawOneRipple(&weather_ripple[i]);
	}
}

// Everything off, at the level's start (ClearFX) and nowhere else.
void WeatherSparksClear()
{
	memset(weather_splash, 0, sizeof(weather_splash));
	memset(weather_ripple, 0, sizeof(weather_ripple));
	weather_next = 0;
	weather_peak = 0;
	weather_ripple_live = 0;
	weather_ripple_peak = 0;
}

long WeatherSparksPeak()
{
	return weather_peak;
}

long WeatherRipplesPeak()
{
	return weather_ripple_peak;
}
