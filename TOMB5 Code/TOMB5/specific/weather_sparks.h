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
#pragma once

/* What the rain leaves where a drop lands - a splash of one spark, or a
 * ripple on water - in pools of its own, apart from the logic's spark[] and
 * ripples[] (weather_sparks.cpp). DoRain (specificfx.cpp) is their only
 * user: it makes them, steps them on its simulation half and S_DrawSparks
 * and S_DrawSplashes draw them. ClearFX empties them with the drops. */

/* As many splashes as the logic's spark[] could ever have held of them.
 * The rain measured - four runs of one recording, 3659 to 3753 splashes in
 * 231 ticks - had up to 722 to 752 alive at once, 410 to 423 on average;
 * a full pool gives up its oldest splash, as the logic's array did. */
#define WEATHER_SPARKS	1024

// The logic's own ceiling for the rain's ripples, which shared it.
#define WEATHER_RIPPLES	128

void WeatherSplash(long x, long y, long z);
void WeatherRipple(long x, long y, long z);
void WeatherSparksStep();
void WeatherRipplesStep();
long WeatherSparksDraw();
void WeatherRipplesDraw();
void WeatherSparksClear();

// SPARKLOG level (-log): the most of each alive at once since the clear.
long WeatherSparksPeak();
long WeatherRipplesPeak();
