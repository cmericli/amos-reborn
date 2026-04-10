/*
 * bank_commands.c — Bank command wrappers for AMOS executor
 *
 * Provides the executor-facing function for "Load" command
 * when loading .abk sprite/icon bank files.
 */

#include "amos.h"
#include <string.h>
#include <ctype.h>

/*
 * Check if a filename has a specific extension (case-insensitive).
 */
static bool has_extension(const char *filename, const char *ext)
{
    size_t flen = strlen(filename);
    size_t elen = strlen(ext);
    if (flen < elen) return false;

    const char *fext = filename + flen - elen;
    for (size_t i = 0; i < elen; i++) {
        if (tolower((unsigned char)fext[i]) != tolower((unsigned char)ext[i]))
            return false;
    }
    return true;
}

/*
 * Execute the "Load" AMOS command for bank files.
 *
 * Syntax: Load "filename.abk",bank_num
 *
 * Detects .abk extension and routes to the bank loader.
 * bank_num is the AMOS bank number (1-based):
 *   Bank 1 = sprites (default)
 *   Bank 2 = icons
 *
 * If bank_num is -1, auto-detect from file magic.
 */
void amos_exec_load_bank(amos_state_t *state, const char *filename, int bank_num)
{
    if (!state || !filename) return;

    (void)bank_num;  /* Bank number is auto-detected from file magic for now */

    int result;

    if (has_extension(filename, ".abk")) {
        result = amos_load_sprite_bank(state, filename);
    } else {
        /* Try loading anyway — magic-based detection will catch it */
        result = amos_load_sprite_bank(state, filename);
    }

    if (result != 0) {
        state->error_code = 78;  /* File not found / load error */
    }
}
