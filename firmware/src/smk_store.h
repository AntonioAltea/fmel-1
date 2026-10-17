/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* MEL's flash data on the M-Vave SMK-37 (BOARD_SMK37; platform.c includes this instead of its FM-1 lines).
 * On the FM-1 the settings sit at 0xFC000: on the SMK that is the protected USR area (stock resources, kept for the
 * way back to stock), so they move into the store window FL_DATA 0x9D000..0xBAFFF (the stock VM):
 *   0x9D000          settings (the parameters and the pattern), the same sector format as on the FM-1
 *   0xAF000          owner tag "SMK37DATA" + MEL_DATA_FAMILY: another firmware's data here is cleared, never parsed
 * The tapes are in the app image (firmware/gen/mel_tapes.h, XIP), never in the store. The rest of FL_DATA and all of
 * USRFLASH (0xC2000..0xEAFFF, the stock key / knob mapping) stay untouched. Calibration (0xC1000), BT pairing, USR and
 * key_mac are never in reach: FL_RANGE_OK below is the app's allow-list, and the RAM flash driver also refuses every
 * protected range on its own (FL_GUARD, fm1_flash_map.h). Macros and compile-time checks only (host tests include it). */
#pragma once
#include <stdint.h>

#define SETTINGS_AT 0x0009D000u                 /* one 4 KiB sector */
#define OWNER_AT 0x000AF000u                    /* the store's owner tag (the kit's place for it) */
#ifndef MEL_DATA_FAMILY
#define MEL_DATA_FAMILY "FMEL1"                 /* (tools/build.py sets it: SMK_DATA_FAMILY) */
#endif
#define FL_RANGE_OK(off, n) (FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_IN(off, n, SETTINGS_AT, SETTINGS_AT + 0x1000u) || \
                             FL_IN(off, n, OWNER_AT, OWNER_AT + 0x1000u))
#include "fm1_flash_map.h"

#if !BOARD_SMK37
#error "smk_store.h is the SMK-37 layout (BOARD_SMK37)"
#endif
_Static_assert(SETTINGS_AT >= FL_DATA_LO && SETTINGS_AT + 0x1000u <= OWNER_AT, "SMK store: settings outside FL_DATA");
_Static_assert(OWNER_AT + 0x1000u <= FL_DATA_HI, "SMK store: the owner tag outside FL_DATA");
_Static_assert(FL_WIN_SAFE(SETTINGS_AT, OWNER_AT + 0x1000u), "SMK store: overlaps a protected range");
_Static_assert(OWNER_AT + 0x1000u <= FL_OTA_LO && FL_OTA_HI <= FL_CAL_LO, "SMK store: below the update area and BTIF");
_Static_assert(OWNER_AT + 0x1000u <= 0xC2000u, "SMK store: never in USRFLASH (the stock mapping)");
_Static_assert(sizeof MEL_DATA_FAMILY - 1u >= 1u && sizeof MEL_DATA_FAMILY - 1u <= 7u, "SMK store: family 1..7 characters");

/* the 16-byte owner tag: "SMK37DATA" + the family, zero padded (a 7-letter family fills it, no NUL) */
static void smk_owner_tag(uint8_t tag[16])
{
    static const char FAM[] = MEL_DATA_FAMILY;
    uint32_t i;
    for (i = 0; i < 16u; i++)
        tag[i] = i < 9u ? (uint8_t)"SMK37DATA"[i] : i - 9u < sizeof FAM - 1u ? (uint8_t)FAM[i - 9u] : 0u;
}
