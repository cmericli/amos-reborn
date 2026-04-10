/*
 * iff_commands.c — IFF/ILBM command wrappers for AMOS executor
 *
 * Provides the executor-facing function for "Load Iff" command.
 * Will be wired into executor.c after all agents complete.
 */

#include "amos.h"
#include <string.h>

/*
 * Execute the "Load Iff" AMOS command.
 *
 * Syntax: Load Iff "filename.iff",screen_id
 *
 * Loads an IFF/ILBM file into the specified screen.
 * If screen_id is -1, uses the current screen.
 */
void amos_exec_load_iff(amos_state_t *state, const char *filename, int screen_id)
{
    if (!state || !filename) return;

    int target = (screen_id >= 0) ? screen_id : state->current_screen;

    int result;
    if (target == state->current_screen) {
        result = amos_load_iff(state, filename);
    } else {
        result = amos_load_iff_to_screen(state, filename, target);
    }

    if (result != 0) {
        state->error_code = 78;  /* File not found / load error */
    }
}
