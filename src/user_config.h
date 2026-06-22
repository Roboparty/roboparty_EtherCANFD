// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 wentywenty

/* this file for eeprom emulation component */

#ifndef _USER_CONFIG_H
#define _USER_CONFIG_H

#ifndef CONFIG_CHERRYUSB_DEVICE
#include "ecat_def.h"
#elif !defined(ESC_EEPROM_SIZE)
#define ESC_EEPROM_SIZE (0x800)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define E2P_DEBUG_LEVEL        E2P_DEBUG_LEVEL_WARN /* if use E2P_DEBUG_LEVEL_INFO, excessive printf */
#define EEPROM_MAX_VAR_CNT     (ESC_EEPROM_SIZE)

#ifdef __cplusplus
}
#endif

#endif