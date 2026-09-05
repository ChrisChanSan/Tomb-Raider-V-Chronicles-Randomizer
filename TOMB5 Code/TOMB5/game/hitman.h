#pragma once
#include "../global/types.h"

/* THE DAMAGE PANELS, out here since #123 fix round B (B3).
 *
 * The order the panels open in is the cyborg's own, and the HD object path
 * needs it as well: the remaster ships one WHOLE body per damage stage,
 * <MODEL>_<node+1>.TRM, cumulative in exactly this order, and which of the
 * eleven files is the right one depends on which panel was opened LAST.
 * One array, one truth - hd_object.cpp reads this one rather than keep a
 * copy that could drift. */
#define HITMAN_PANELS	11
extern char hitman_nodes[16];

void InitialiseHitman(short item_number);
void HitmanControl(short item_number);
