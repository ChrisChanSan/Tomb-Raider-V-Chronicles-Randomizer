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

#include <stdio.h>

/* A savegame's two pool pointers, kept so that a pool that lies elsewhere
 * cannot hand them back as wild ones.
 *
 * The save carries two pointers into the game_malloc pool: a creature's
 * enemy, the last four of the 22 bytes of CREATURE_INFO it writes, and
 * Lara's GeneralPtr. Both were written as their distance from the pool's
 * start, and a load added the start of its own run. That is right for an
 * address in the pool and wrong for anything else, and the start moves at
 * every start of the game: a null enemy came back as the difference of two
 * starts, and the first tick after the load read through it (0xFFFF6000 in
 * GetAITarget).
 *
 * What the two hold, every assignment in game\ counted:
 *   enemy       null, Lara (&items[lara.item_number]) or the creature's own
 *               ai_target (FindAITargetObject, box.cpp) - nothing else;
 *   GeneralPtr  0, an item number cast to a pointer (Lara moving into place
 *               for item n), or an item of items[] while she uses a pulley,
 *               the parallel bars, a cog switch or the crow-dove switch -
 *               read through by lara_as_pulley, lara_as_pbleapoff and
 *               CogSwitchControl.
 *
 * The rule. An address in the pool stays its distance from the pool's start
 * in the field, as before: a build without this rule adds its own start and
 * still finds the item - a tag there would hand it a wild pointer for every
 * enemy. What is no address in the pool gets a value no distance can be: -1
 * for null, and 0x7E in the top byte over the number for an item number (a
 * distance is below MALLOC_SIZE, 20,000,000 or 0x01312D00: its top byte is
 * 0 or 1). Beside the field, where no build reads anything, the rule keeps
 * who it was, by number:
 *   enemy       bits 5 and 6 of the LOT flags byte after the ai_target (the
 *               LOT takes bits 0 to 4): null, Lara or the own ai_target;
 *   GeneralPtr  the save's copy of lara.target, which every load sets to 0:
 *               0x7D, the item's number and the field's low twelve bits, so
 *               that a copy another save left there never counts.
 * A load builds the pointer from that number and THIS run's items[] or the
 * creature, wherever the pool lies and however it is laid out. A save
 * without it - every save before this rule - is read by distance: an item
 * exactly there, or a creature slot's ai_target exactly there, else null.
 * A creature's enemy is only ever its own ai_target, and the slots are dealt
 * anew at a load, so an ai_target found that way is the creature's own.
 *
 * No engine header: plain numbers in, plain numbers out, so ModernTest holds
 * the rule; game\savegame.cpp fills in the numbers. */

#define SAVE_PTR_NULL		(-1L)		// null, or nothing the rule can name
#define SAVE_PTR_TAG_MASK	0xFF000000UL	// the top byte (a distance's: 0, 1)
#define SAVE_PTR_TAG_NUM	0x7E000000UL	// GeneralPtr holds an item number
#define SAVE_PTR_TAG_ITEM	0x7D000000UL	// the target copy names an item
#define SAVE_PTR_NUM_MASK	0x00FFFFFFUL	// the number under the tag
#define SAVE_PTR_ITEM_SHIFT	12		// the item's number in the copy
#define SAVE_PTR_ITEM_MAX	0xFFFUL		// twelve bits of it
#define SAVE_PTR_CHECK		0xFFFUL		// the field's bits the copy repeats
#define SAVE_PTR_KIND_SHIFT	5		// the LOT flags byte, bits 5 and 6
#define SAVE_PTR_KIND_MASK	0x60

// Who a creature's enemy was, in the LOT flags byte. 0 is what every older
// save holds there: no kind, read by distance.
enum SavePtrKind
{
	SAVE_PTR_KIND_NONE,
	SAVE_PTR_KIND_NULL,
	SAVE_PTR_KIND_LARA,
	SAVE_PTR_KIND_AI
};

// How a load read a pointer - SAVELOG's how=.
enum SavePtrHow
{
	SAVE_PTR_HOW_NULL,		// -1, or the kind said null
	SAVE_PTR_HOW_TAG_ITEM,		// by number: Lara, or GeneralPtr's item
	SAVE_PTR_HOW_TAG_AI,		// by number: the creature's own ai_target
	SAVE_PTR_HOW_TAG_NUM,		// GeneralPtr's item number
	SAVE_PTR_HOW_LEGACY_OK,		// by distance, on an item or an ai_target
	SAVE_PTR_HOW_LEGACY_NULL	// by distance, on nothing: null
};

// What the pointer is after the load.
enum SavePtrWhat
{
	SAVE_PTR_IS_NULL,
	SAVE_PTR_IS_ITEM,
	SAVE_PTR_IS_AI,
	SAVE_PTR_IS_NUMBER
};

// The pool as the rule reads it; every distance is from `base`.
struct SavePtrPool
{
	unsigned long base;		// malloc_buffer
	unsigned long size;		// MALLOC_SIZE
	unsigned long items;		// items[]
	unsigned long itemSize;		// sizeof(ITEM_INFO)
	long maxItems;			// MAX_ITEMS
	unsigned long slots;		// baddie_slots
	unsigned long slotSize;		// sizeof(CREATURE_INFO)
	long maxSlots;			// MAX_LOT
	unsigned long aiOffset;		// offsetof(CREATURE_INFO, ai_target)
};

// A load's answer: `value` is what the pointer is set to - an address, or
// the number itself for SAVE_PTR_IS_NUMBER; `number` the item's, the
// creature's own or the number, -1 for null.
struct SavePtrDecoded
{
	int how;
	int what;
	long number;
	unsigned long value;
};

/* The item lying exactly at the distance `off`, or -1. */
inline long SavePtrItemAt(const SavePtrPool* p, unsigned long off)
{
	if (off < p->items || !p->itemSize || p->maxItems <= 0)
		return -1;

	const unsigned long d = off - p->items;

	if (d % p->itemSize || d / p->itemSize >= (unsigned long)p->maxItems)
		return -1;

	return (long)(d / p->itemSize);
}

/* The creature slot whose ai_target lies exactly at `off`, or -1. */
inline long SavePtrSlotAiAt(const SavePtrPool* p, unsigned long off)
{
	if (off < p->slots || !p->slotSize || p->maxSlots <= 0)
		return -1;

	unsigned long d = off - p->slots;

	if (d < p->aiOffset)
		return -1;

	d -= p->aiOffset;

	if (d % p->slotSize || d / p->slotSize >= (unsigned long)p->maxSlots)
		return -1;

	return (long)(d / p->slotSize);
}

/* Whether the address `a` lies in the pool; its distance into `off`. */
inline bool SavePtrInPool(const SavePtrPool* p, unsigned long a,
	unsigned long* off)
{
	if (a < p->base || a - p->base >= p->size)
		return false;

	*off = a - p->base;
	return true;
}

/* The four bytes a save writes for the enemy at the address `enemy`: an
 * item's or a creature slot's ai_target its distance, anything else -1. */
inline long SavePtrEnemyField(const SavePtrPool* p, unsigned long enemy)
{
	unsigned long off;

	if (!enemy || !SavePtrInPool(p, enemy, &off))
		return SAVE_PTR_NULL;

	if (SavePtrItemAt(p, off) >= 0 || SavePtrSlotAiAt(p, off) >= 0)
		return (long)off;

	return SAVE_PTR_NULL;
}

/* Who the enemy is, for the LOT flags byte: `lara` the address of Lara's
 * item, `ownAi` that of the creature's own ai_target. */
inline int SavePtrEnemyKind(unsigned long enemy, unsigned long lara,
	unsigned long ownAi)
{
	if (!enemy)
		return SAVE_PTR_KIND_NULL;

	if (enemy == lara)
		return SAVE_PTR_KIND_LARA;

	if (enemy == ownAi)
		return SAVE_PTR_KIND_AI;

	return SAVE_PTR_KIND_NONE;
}

/* The kind out of a LOT flags byte as the save holds it. */
inline int SavePtrKindOf(int lflags)
{
	return (lflags & SAVE_PTR_KIND_MASK) >> SAVE_PTR_KIND_SHIFT;
}

/* The enemy a load gives a creature: `field` the save's four bytes, `kind`
 * its two bits, `lara` Lara's item number, `own` the creature's item number,
 * `ownAi` the address of its ai_target in this run. */
inline SavePtrDecoded SavePtrDecodeEnemy(const SavePtrPool* p, long field,
	int kind, long lara, long own, unsigned long ownAi)
{
	SavePtrDecoded d = { SAVE_PTR_HOW_NULL, SAVE_PTR_IS_NULL, -1, 0 };
	const unsigned long off = (unsigned long)field;
	long k;

	if (kind == SAVE_PTR_KIND_NULL)
		return d;

	if (kind == SAVE_PTR_KIND_LARA)
	{
		if (lara >= 0 && lara < p->maxItems)
		{
			d.how = SAVE_PTR_HOW_TAG_ITEM;
			d.what = SAVE_PTR_IS_ITEM;
			d.number = lara;
			d.value = p->base + p->items + (unsigned long)lara * p->itemSize;
		}

		return d;
	}

	if (kind == SAVE_PTR_KIND_AI)
	{
		d.how = SAVE_PTR_HOW_TAG_AI;
		d.what = SAVE_PTR_IS_AI;
		d.number = own;
		d.value = ownAi;
		return d;
	}

	if (field == SAVE_PTR_NULL)
		return d;

	d.how = SAVE_PTR_HOW_LEGACY_NULL;

	if (off >= p->size)
		return d;

	k = SavePtrItemAt(p, off);

	if (k >= 0)
	{
		d.how = SAVE_PTR_HOW_LEGACY_OK;
		d.what = SAVE_PTR_IS_ITEM;
		d.number = k;
		d.value = p->base + off;
	}
	else if (SavePtrSlotAiAt(p, off) >= 0)
	{
		d.how = SAVE_PTR_HOW_LEGACY_OK;
		d.what = SAVE_PTR_IS_AI;
		d.number = own;
		d.value = ownAi;
	}

	return d;
}

/* The four bytes a save writes for GeneralPtr's `value`: 0 is -1, an item
 * number 0x7E000000 with the number, an item of items[] its distance,
 * anything else -1. */
inline long SavePtrGeneralField(const SavePtrPool* p, unsigned long value)
{
	unsigned long off;

	if (!value)
		return SAVE_PTR_NULL;

	if (value < (unsigned long)p->maxItems)
		return (long)(SAVE_PTR_TAG_NUM | value);

	if (SavePtrInPool(p, value, &off) && SavePtrItemAt(p, off) >= 0)
		return (long)off;

	return SAVE_PTR_NULL;
}

/* What the save's copy of lara.target carries beside the field: 0x7D, the
 * item's number from bit 12 and the field's low twelve bits - or 0 when
 * GeneralPtr names no item of items[] (the copy then stays as it is). */
inline unsigned long SavePtrGeneralSide(const SavePtrPool* p,
	unsigned long value, long field)
{
	unsigned long off;
	long k;

	if (!SavePtrInPool(p, value, &off))
		return 0;

	k = SavePtrItemAt(p, off);

	if (k < 0 || (unsigned long)k > SAVE_PTR_ITEM_MAX)
		return 0;

	return SAVE_PTR_TAG_ITEM | ((unsigned long)k << SAVE_PTR_ITEM_SHIFT) |
		((unsigned long)field & SAVE_PTR_CHECK);
}

/* The GeneralPtr a load gives Lara: `field` the save's four bytes, `side`
 * the save's copy of lara.target. */
inline SavePtrDecoded SavePtrDecodeGeneral(const SavePtrPool* p, long field,
	unsigned long side)
{
	SavePtrDecoded d = { SAVE_PTR_HOW_NULL, SAVE_PTR_IS_NULL, -1, 0 };
	const unsigned long u = (unsigned long)field;
	unsigned long k;

	if (field == SAVE_PTR_NULL)
		return d;

	if ((u & SAVE_PTR_TAG_MASK) == SAVE_PTR_TAG_NUM &&
		(u & SAVE_PTR_NUM_MASK) < (unsigned long)p->maxItems)
	{
		d.how = SAVE_PTR_HOW_TAG_NUM;
		d.what = SAVE_PTR_IS_NUMBER;
		d.number = (long)(u & SAVE_PTR_NUM_MASK);
		d.value = u & SAVE_PTR_NUM_MASK;
		return d;
	}

	k = (side >> SAVE_PTR_ITEM_SHIFT) & SAVE_PTR_ITEM_MAX;

	if ((side & SAVE_PTR_TAG_MASK) == SAVE_PTR_TAG_ITEM &&
		(side & SAVE_PTR_CHECK) == (u & SAVE_PTR_CHECK) &&
		k < (unsigned long)p->maxItems)
	{
		d.how = SAVE_PTR_HOW_TAG_ITEM;
		d.what = SAVE_PTR_IS_ITEM;
		d.number = (long)k;
		d.value = p->base + p->items + k * p->itemSize;
		return d;
	}

	d.how = SAVE_PTR_HOW_LEGACY_NULL;

	if (u < p->size && SavePtrItemAt(p, u) >= 0)
	{
		d.how = SAVE_PTR_HOW_LEGACY_OK;
		d.what = SAVE_PTR_IS_ITEM;
		d.number = SavePtrItemAt(p, u);
		d.value = p->base + u;
	}

	return d;
}

/* What a build without this rule makes of a field: its run's base plus the
 * field, wrapped as an address wraps. */
inline unsigned long SavePtrOldDecode(long field, unsigned long base)
{
	return base + (unsigned long)field;
}

inline const char* SavePtrHowName(int how)
{
	static const char* const names[] = { "null", "tag_item", "tag_ai",
		"tag_num", "legacy_ok", "legacy_null" };

	return how >= 0 && how < 6 ? names[how] : "?";
}

inline const char* SavePtrKindName(int kind)
{
	static const char* const names[] = { "none", "null", "lara", "ai" };

	return kind >= 0 && kind < 4 ? names[kind] : "?";
}

/* "null", "item n", "ai n" or "number n". */
inline void SavePtrWhatText(char* out, size_t size, const SavePtrDecoded* d)
{
	static const char* const names[] = { "null", "item", "ai", "number" };

	if (d->what <= SAVE_PTR_IS_NULL || d->what > SAVE_PTR_IS_NUMBER)
		snprintf(out, size, "null");
	else
		snprintf(out, size, "%s %ld", names[d->what], d->number);
}

/* SAVELOG at a load: a creature's enemy... */
inline void SavePtrEnemyLine(char* out, size_t size, long item, long field,
	const SavePtrDecoded* d)
{
	char what[32];

	SavePtrWhatText(what, sizeof(what), d);
	snprintf(out, size, "SAVELOG item=%ld stored=0x%08lX how=%s enemy=%s",
		item, (unsigned long)field, SavePtrHowName(d->how), what);
}

/* ...and GeneralPtr, with the copy beside it. */
inline void SavePtrGeneralLine(char* out, size_t size, long field,
	unsigned long side, const SavePtrDecoded* d)
{
	char what[32];

	SavePtrWhatText(what, sizeof(what), d);
	snprintf(out, size,
		"SAVELOG general stored=0x%08lX side=0x%08lX how=%s general=%s",
		(unsigned long)field, side, SavePtrHowName(d->how), what);
}

/* SAVELOG's pool line at a save or a load (`when`): two logs show whether
 * the pool moved and whether its layout did. */
inline void SavePtrPoolLine(char* out, size_t size, const char* when,
	const SavePtrPool* p)
{
	snprintf(out, size,
		"SAVELOG %s pool base=0x%08lX items=+0x%lX slots=+0x%lX", when,
		p->base, p->items, p->slots);
}
