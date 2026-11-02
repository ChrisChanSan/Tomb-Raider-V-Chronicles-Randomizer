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

#include <stdint.h>
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
 * The rule. No distance into the pool goes into a save: it names another
 * place in another layout - a 64-bit build's ITEM_INFO is wider - and a
 * save of one game state would differ between the builds. Every field gets
 * a value no distance can be (a distance is below MALLOC_SIZE, 20,000,000
 * or 0x01312D00: its top byte is 0 or 1):
 *   -1          null, and anything that is no item and no ai_target - an
 *               address outside the pool or between its records; SAVELOG
 *               says kind=outside for such a pointer;
 *   0x7D...     an item of items[], any item, by its number;
 *   0x7B...     a creature's ai_target, by the number of the creature's
 *               item - a load takes the slot that item holds in its run;
 *   0x7E...     GeneralPtr's item number (a number, not an address).
 * Beside the field, where no build reads anything, the rule keeps who it
 * was for the builds before this closure, which read the field as a
 * distance:
 *   enemy       bits 5 and 6 of the LOT flags byte after the ai_target (the
 *               LOT takes bits 0 to 4): null, Lara or the own ai_target;
 *   GeneralPtr  the save's copy of lara.target, which every load sets to 0:
 *               0x7D, the item's number and the field's low twelve bits, so
 *               that a copy another save left there never counts.
 * A load builds the pointer from the number and THIS run's items[] or
 * slots, wherever the pool lies and however it is laid out. A field that
 * is a distance comes from a build before this closure and is read the old
 * way (how=legacy_*): by its kind, or by the copy, or at the distance - an
 * item exactly there, or a creature slot's ai_target exactly there, else
 * null. Every assignment in game\ gives a creature as its enemy null, Lara
 * or its own ai_target, so an ai_target found that way is the creature's
 * own.
 *
 * No engine header: plain numbers in, plain numbers out, so ModernTest holds
 * the rule; game\savegame.cpp fills in the numbers.
 *
 * An address is a SavePtrAddr, 64 bits on every build: a 64-bit build's
 * pool may lie above 4 GB or across a 4 GB line, and cut to 32 bits the
 * pool's own items would lie below its start. Only the fields the save
 * writes stay 32 bits - distances into the pool, numbers and tags. */

#define SAVE_PTR_NULL		(-1L)		// null, or nothing the rule can name
#define SAVE_PTR_TAG_MASK	0xFF000000UL	// the top byte (a distance's: 0, 1)
#define SAVE_PTR_TAG_NUM	0x7E000000UL	// GeneralPtr holds an item number
#define SAVE_PTR_TAG_ITEM	0x7D000000UL	// an item by number; the copy's tag
#define SAVE_PTR_TAG_AI		0x7B000000UL	// an ai_target by its owner's item
#define SAVE_PTR_SLOTS_MAX	32		// slotItem[]'s room, MAX_LOT or more
#define SAVE_PTR_NUM_MASK	0x00FFFFFFUL	// the number under the tag
#define SAVE_PTR_ITEM_SHIFT	12		// the item's number in the copy
#define SAVE_PTR_ITEM_MAX	0xFFFUL		// twelve bits of it
#define SAVE_PTR_CHECK		0xFFFUL		// the field's bits the copy repeats
#define SAVE_PTR_KIND_SHIFT	5		// the LOT flags byte, bits 5 and 6
#define SAVE_PTR_KIND_MASK	0x60

typedef uint64_t SavePtrAddr;	// an address, whole on either build

// Who a creature's enemy was, in the LOT flags byte. 0 is what every older
// save holds there: no kind, read by distance; a newer one for an enemy
// that is neither null, Lara nor the own ai_target - its field says who.
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
	SAVE_PTR_HOW_TAG_ITEM,		// by number: an item of items[]
	SAVE_PTR_HOW_TAG_AI,		// by number: a creature's ai_target
	SAVE_PTR_HOW_TAG_NUM,		// GeneralPtr's item number
	SAVE_PTR_HOW_LEGACY_OK,		// an older save's distance, read
	SAVE_PTR_HOW_LEGACY_NULL	// an older save's distance, on nothing
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
	SavePtrAddr base;		// malloc_buffer
	unsigned long size;		// MALLOC_SIZE
	unsigned long items;		// items[]
	unsigned long itemSize;		// sizeof(ITEM_INFO)
	long maxItems;			// MAX_ITEMS
	unsigned long slots;		// baddie_slots
	unsigned long slotSize;		// sizeof(CREATURE_INFO)
	long maxSlots;			// MAX_LOT
	unsigned long aiOffset;		// offsetof(CREATURE_INFO, ai_target)
	short slotItem[SAVE_PTR_SLOTS_MAX];	// each slot's item_num, or -1
};

// A load's answer: `value` is what the pointer is set to - an address, or
// the number itself for SAVE_PTR_IS_NUMBER; `number` the item's, the
// creature's own or the number, -1 for null.
struct SavePtrDecoded
{
	int how;
	int what;
	long number;
	SavePtrAddr value;
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
inline bool SavePtrInPool(const SavePtrPool* p, SavePtrAddr a,
	unsigned long* off)
{
	if (a < p->base || a - p->base >= p->size)
		return false;

	*off = (unsigned long)(a - p->base);
	return true;
}

/* The item lying exactly at the address `a`, or -1. */
inline long SavePtrItemOf(const SavePtrPool* p, SavePtrAddr a)
{
	unsigned long off;

	if (!a || !SavePtrInPool(p, a, &off))
		return -1;

	return SavePtrItemAt(p, off);
}

/* The item of the creature whose ai_target lies exactly at the address `a`
 * - its slot's item_num - or -1, a free slot's NO_ITEM included. */
inline long SavePtrAiOwnerOf(const SavePtrPool* p, SavePtrAddr a)
{
	unsigned long off;
	long s;

	if (!a || !SavePtrInPool(p, a, &off))
		return -1;

	s = SavePtrSlotAiAt(p, off);

	if (s < 0 || s >= SAVE_PTR_SLOTS_MAX || p->slotItem[s] < 0)
		return -1;

	return p->slotItem[s];
}

/* The address of the ai_target of the slot that item `n` holds in this run,
 * or 0 when no slot holds it. */
inline SavePtrAddr SavePtrAiOfItem(const SavePtrPool* p, long n)
{
	for (long s = 0; s < p->maxSlots && s < SAVE_PTR_SLOTS_MAX; s++)
	{
		if (n >= 0 && p->slotItem[s] == n)
			return p->base + p->slots + (unsigned long)s * p->slotSize +
				p->aiOffset;
	}

	return 0;
}

/* The four bytes a save writes for the enemy at the address `enemy`: an
 * item 0x7D over its number, a creature's ai_target 0x7B over the number
 * of the creature's item, anything else -1. */
inline long SavePtrEnemyField(const SavePtrPool* p, SavePtrAddr enemy)
{
	long k = SavePtrItemOf(p, enemy);

	if (k >= 0)
		return (long)(SAVE_PTR_TAG_ITEM | (unsigned long)k);

	k = SavePtrAiOwnerOf(p, enemy);

	if (k >= 0 && k < p->maxItems)
		return (long)(SAVE_PTR_TAG_AI | (unsigned long)k);

	return SAVE_PTR_NULL;
}

/* Whether a pointer that is not null left the save as -1: an address the
 * rule cannot name, SAVELOG's kind=outside. */
inline bool SavePtrOutside(SavePtrAddr a, long field)
{
	return a && field == SAVE_PTR_NULL;
}

/* Who the enemy is, for the LOT flags byte: `lara` the address of Lara's
 * item, `ownAi` that of the creature's own ai_target. */
inline int SavePtrEnemyKind(SavePtrAddr enemy, SavePtrAddr lara,
	SavePtrAddr ownAi)
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
 * `ownAi` the address of its ai_target in this run. A tagged field is read
 * by its number; a distance, from a build before this closure, by its kind
 * or at the distance. */
inline SavePtrDecoded SavePtrDecodeEnemy(const SavePtrPool* p, long field,
	int kind, long lara, long own, SavePtrAddr ownAi)
{
	SavePtrDecoded d = { SAVE_PTR_HOW_NULL, SAVE_PTR_IS_NULL, -1, 0 };
	const unsigned long off = (unsigned long)field;
	const unsigned long n = off & SAVE_PTR_NUM_MASK;
	long k;

	if ((off & SAVE_PTR_TAG_MASK) == SAVE_PTR_TAG_ITEM)
	{
		if (n < (unsigned long)p->maxItems)
		{
			d.how = SAVE_PTR_HOW_TAG_ITEM;
			d.what = SAVE_PTR_IS_ITEM;
			d.number = (long)n;
			d.value = p->base + p->items + n * p->itemSize;
		}

		return d;
	}

	if ((off & SAVE_PTR_TAG_MASK) == SAVE_PTR_TAG_AI)
	{
		const SavePtrAddr ai = (long)n == own ? ownAi :
			SavePtrAiOfItem(p, (long)n);

		if (ai)
		{
			d.how = SAVE_PTR_HOW_TAG_AI;
			d.what = SAVE_PTR_IS_AI;
			d.number = (long)n;
			d.value = ai;
		}

		return d;
	}

	if (kind == SAVE_PTR_KIND_NULL || field == SAVE_PTR_NULL)
		return d;

	d.how = SAVE_PTR_HOW_LEGACY_NULL;

	if (kind == SAVE_PTR_KIND_LARA)
	{
		if (lara >= 0 && lara < p->maxItems)
		{
			d.how = SAVE_PTR_HOW_LEGACY_OK;
			d.what = SAVE_PTR_IS_ITEM;
			d.number = lara;
			d.value = p->base + p->items + (unsigned long)lara * p->itemSize;
		}

		return d;
	}

	if (kind == SAVE_PTR_KIND_AI)
	{
		d.how = SAVE_PTR_HOW_LEGACY_OK;
		d.what = SAVE_PTR_IS_AI;
		d.number = own;
		d.value = ownAi;
		return d;
	}

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
 * number 0x7E over the number, an item of items[] 0x7D over its number,
 * anything else -1. */
inline long SavePtrGeneralField(const SavePtrPool* p, SavePtrAddr value)
{
	long k;

	if (!value)
		return SAVE_PTR_NULL;

	if (value < (SavePtrAddr)p->maxItems)
		return (long)(SAVE_PTR_TAG_NUM | (unsigned long)value);

	k = SavePtrItemOf(p, value);

	if (k >= 0)
		return (long)(SAVE_PTR_TAG_ITEM | (unsigned long)k);

	return SAVE_PTR_NULL;
}

/* What the save's copy of lara.target carries beside the field: 0x7D, the
 * item's number from bit 12 and the field's low twelve bits - or 0 when
 * GeneralPtr names no item of items[] (the copy then stays as it is). */
inline unsigned long SavePtrGeneralSide(const SavePtrPool* p,
	SavePtrAddr value, long field)
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
 * the save's copy of lara.target. A tagged field is read by its number; a
 * distance, from a build before this closure, by the item number in the
 * copy, or at the distance. */
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

	if ((u & SAVE_PTR_TAG_MASK) == SAVE_PTR_TAG_ITEM)
	{
		k = u & SAVE_PTR_NUM_MASK;

		if (k < (unsigned long)p->maxItems)
		{
			d.how = SAVE_PTR_HOW_TAG_ITEM;
			d.what = SAVE_PTR_IS_ITEM;
			d.number = (long)k;
			d.value = p->base + p->items + k * p->itemSize;
		}

		return d;
	}

	k = (side >> SAVE_PTR_ITEM_SHIFT) & SAVE_PTR_ITEM_MAX;

	if ((side & SAVE_PTR_TAG_MASK) == SAVE_PTR_TAG_ITEM &&
		(side & SAVE_PTR_CHECK) == (u & SAVE_PTR_CHECK) &&
		k < (unsigned long)p->maxItems)
	{
		d.how = SAVE_PTR_HOW_LEGACY_OK;
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
		(unsigned long)p->base, p->items, p->slots);
}

/* Lara's mesh pointers. A save wrote each as its distance from mesh_base,
 * where ProcessMeshData (drawroom.cpp) starts turning the level's meshes
 * into MESH_DATA. That distance holds only where MESH_DATA is laid out as
 * the saving build laid it - 70 bytes on a 32-bit build, 94 on a 64-bit
 * one - and names another place in the other build. A save now writes the
 * mesh's number in mesh_vtxbuf, the table ProcessMeshData fills once per
 * level and nothing changes after, under a tag of its own; a distance's
 * top byte is 0 or 1, as above. A load takes the mesh of that number in
 * its own table. A field without the tag is a distance, read as before:
 * exact on the build that wrote it, and a 64-bit build refuses a save
 * file that holds one (sgRestoreGame). */
#define SAVE_PTR_TAG_MESH	0x7C000000UL	// a mesh's number in mesh_vtxbuf

/* The four bytes for one mesh pointer: `k` its number in mesh_vtxbuf, or
 * -1 when it is in none - then `dist`, its distance from mesh_base. */
inline unsigned long SavePtrMeshField(long k, unsigned long dist)
{
	if (k >= 0 && (unsigned long)k <= SAVE_PTR_NUM_MASK)
		return SAVE_PTR_TAG_MESH | (unsigned long)k;

	return dist;
}

/* The mesh number in `field`, `count` meshes in the level: -1 for a
 * distance, -2 for a tagged number the level does not have. */
inline long SavePtrMeshNumber(unsigned long field, long count)
{
	if ((field & SAVE_PTR_TAG_MASK) != SAVE_PTR_TAG_MESH)
		return -1;

	if ((long)(field & SAVE_PTR_NUM_MASK) >= count)
		return -2;

	return (long)(field & SAVE_PTR_NUM_MASK);
}
