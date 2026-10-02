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

/* Which entries of the engine's own item light lists give an HD object no
 * light at all, and so are not handed to the model shader.
 *
 * This header includes no engine header on purpose, like hd_model_cut.h:
 * Preparation\Tools\Diagnose\ModernTest.cpp includes it and asserts on the
 * predicate below without a device or a level. fill_lights in
 * hd_object.cpp is the engine's one caller.
 *
 * The engine fades a light out in eight calls of FadeLightList
 * (specific\lighting.cpp): CreateLightList sets the step to the colour times
 * -0.125, FadeLightList adds it eight times and clears Active only when red,
 * green and blue are all at or below 0 after a step. Eight float additions
 * of an eighth need not land on 0. Of the 2732 sun, point and spot lights in
 * the rooms of the game's level files, 1503 end that fade with a channel a
 * hair above 0 and keep Active with nothing left to give, and of the 255
 * byte-grid colours n/255, 139 do; the largest remainder of either is
 * 8.94e-8 of the 0..1 scale. An entry of the previous list stays like that
 * until the next room change; one of the current list that is out of range
 * clears on its second fade, eight calls later.
 *
 * Such an entry lights nothing, but counted it makes the object's list
 * non-empty, and the model vertex shader takes the eye as the lamp of the
 * sheen only for an empty list (hd_model.hlsl, the sheen). In a room
 * entered from a room whose light was on the object, the sheen is then
 * gone, until a room change out of a room without such a light or a level
 * load, which builds the lists afresh.
 *
 * The bound is compared in the engine's own 0..1 colour scale, channel by
 * channel: an entry is dark when all three lie below it, so a negative
 * remainder is dark as well. 1e-5 lies a factor of 112 above the largest
 * remainder, 8.94e-8, and below every colour a step of a fade leaves on a
 * light that can add half a unit of 0..255 to a vertex (0.5 / 255): the
 * first step of its fade-in is 2.45e-4, a factor of 24 above, and a light
 * of 1/255 has 4.9e-4 there. Even a fade-out that begins one step into a
 * fade-in, a sixty-fourth of the colour, stays above it from 1/255 up
 * (6.1e-5). The dimmest light in the level files that is not black has a
 * brightest channel of 0.0624, a first step of 7.8e-3. Over the same
 * lights and colours, every state a room change can catch a fade in - its
 * full colour and every step of a fade-in and of a fade-out - ends either
 * cleared or below the bound.
 *
 * What an entry left out would have added to a vertex is under
 * HD_OBJLIGHT_DARK_BOUND * 255 times the light's own factor: 0.0051 of a
 * unit of 0..255 for a sun in the inventory (factor 2), 0.0026 for a point
 * or a spot, 0.0019 for a sun in the world (0.75). */
#define HD_OBJLIGHT_DARK_BOUND	1e-5f

/* Does an engine light of this colour (0..1 per channel, as PCLIGHT holds
 * it) give an object nothing? A NaN compares false and counts as light. */
inline bool HDObjLightIsDark(float r, float g, float b)
{
	return r < HD_OBJLIGHT_DARK_BOUND && g < HD_OBJLIGHT_DARK_BOUND &&
		b < HD_OBJLIGHT_DARK_BOUND;
}
