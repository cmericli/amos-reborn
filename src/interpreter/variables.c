/*
 * variables.c — AMOS variable store
 *
 * Three types: integer (A), float (A#), string (A$).
 * Variables are created on first use. Hash lookup by name.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Determine variable type from name suffix */
static amos_var_type_t var_type_from_name(const char *name)
{
    int len = (int)strlen(name);
    if (len > 0 && name[len - 1] == '$') return VAR_STRING;
    if (len > 0 && name[len - 1] == '#') return VAR_FLOAT;
    return VAR_INTEGER;
}

amos_var_t *amos_var_get(amos_state_t *state, const char *name)
{
    /* Linear scan (fine for < 4096 variables) */
    for (int i = 0; i < state->var_count; i++) {
        if (strcasecmp(state->variables[i].name, name) == 0) {
            return &state->variables[i];
        }
    }
    return NULL;
}

static amos_var_t *var_create(amos_state_t *state, const char *name)
{
    if (state->var_count >= AMOS_MAX_VARIABLES) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Too many variables (max %d)", AMOS_MAX_VARIABLES);
        state->error_code = 1;
        return NULL;
    }

    amos_var_t *var = &state->variables[state->var_count++];
    memset(var, 0, sizeof(*var));
    strncpy(var->name, name, sizeof(var->name) - 1);
    var->type = var_type_from_name(name);

    return var;
}

amos_var_t *amos_var_set_int(amos_state_t *state, const char *name, int32_t val)
{
    amos_var_t *var = amos_var_get(state, name);
    if (!var) var = var_create(state, name);
    if (!var) return NULL;

    var->type = VAR_INTEGER;
    var->ival = val;
    return var;
}

amos_var_t *amos_var_set_float(amos_state_t *state, const char *name, double val)
{
    amos_var_t *var = amos_var_get(state, name);
    if (!var) var = var_create(state, name);
    if (!var) return NULL;

    var->type = VAR_FLOAT;
    var->fval = val;
    return var;
}

amos_var_t *amos_var_set_string(amos_state_t *state, const char *name, const char *val)
{
    amos_var_t *var = amos_var_get(state, name);
    if (!var) var = var_create(state, name);
    if (!var) return NULL;

    /* Free old string if any */
    if (var->type == VAR_STRING && var->sval.data) {
        free(var->sval.data);
    }

    var->type = VAR_STRING;
    var->sval.len = (int)strlen(val);
    var->sval.data = strdup(val);
    return var;
}
