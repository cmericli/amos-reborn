/*
 * hal.c — Hardware Abstraction Layer dispatcher
 *
 * Routes AMOS commands to either the classic (Amiga-accurate) or
 * modern (host-native) backend based on display_mode.
 */

#include "amos.h"

/* For Phase 1, the HAL is a thin pass-through.
 * Classic and modern modes share the same implementation.
 * The HAL split will be implemented when we add
 * planar framebuffer emulation (classic) vs RGBA (modern). */
