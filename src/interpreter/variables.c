/*
 * variables.c — AMOS variable store with procedure scoping
 *
 * Three types: integer (A), float (A#), string (A$).
 * Variables are created on first use.
 *
 * Scoping: When inside a Procedure (proc_scope_top > 0), variables
 * are local by default. Only names listed in the Shared declaration
 * resolve to the caller's scope. On End Proc, locals are discarded.
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

/* Check if a name is in the current scope's Shared list */
static bool is_shared(amos_state_t *state, const char *name)
{
    if (state->proc_scope_top <= 0) return false;
    proc_scope_t *scope = &state->proc_scopes[state->proc_scope_top - 1];
    for (int i = 0; i < scope->shared_count; i++) {
        if (strcasecmp(scope->shared_names[i], name) == 0)
            return true;
    }
    return false;
}

amos_var_t *amos_var_get(amos_state_t *state, const char *name)
{
    if (state->proc_scope_top > 0) {
        proc_scope_t *scope = &state->proc_scopes[state->proc_scope_top - 1];
        int local_base = scope->saved_var_count;

        /* Search local scope first (vars from watermark to var_count) */
        for (int i = local_base; i < state->var_count; i++) {
            if (strcasecmp(state->variables[i].name, name) == 0)
                return &state->variables[i];
        }

        /* If Shared, search global scope (vars below watermark) */
        if (is_shared(state, name)) {
            for (int i = 0; i < local_base; i++) {
                if (strcasecmp(state->variables[i].name, name) == 0)
                    return &state->variables[i];
            }
        }

        /* Not found in local or shared — return NULL (will be created locally) */
        return NULL;
    }

    /* Global scope: linear scan all variables */
    for (int i = 0; i < state->var_count; i++) {
        if (strcasecmp(state->variables[i].name, name) == 0)
            return &state->variables[i];
    }
    return NULL;
}

static amos_var_t *var_create(amos_state_t *state, const char *name)
{
    /* If inside a procedure and name is Shared, create in global scope */
    if (state->proc_scope_top > 0 && is_shared(state, name)) {
        proc_scope_t *scope = &state->proc_scopes[state->proc_scope_top - 1];
        int local_base = scope->saved_var_count;

        /* Check if already exists in global scope */
        for (int i = 0; i < local_base; i++) {
            if (strcasecmp(state->variables[i].name, name) == 0)
                return &state->variables[i];
        }

        /* Need to insert into global scope — shift locals up by 1 */
        if (state->var_count >= AMOS_MAX_VARIABLES) {
            snprintf(state->error_msg, sizeof(state->error_msg),
                     "Too many variables (max %d)", AMOS_MAX_VARIABLES);
            state->error_code = 1;
            return NULL;
        }

        /* Move local vars up to make room at local_base */
        int local_count = state->var_count - local_base;
        if (local_count > 0) {
            memmove(&state->variables[local_base + 1],
                    &state->variables[local_base],
                    local_count * sizeof(amos_var_t));
        }

        /* Insert new global var at local_base */
        amos_var_t *var = &state->variables[local_base];
        memset(var, 0, sizeof(*var));
        strncpy(var->name, name, sizeof(var->name) - 1);
        var->type = var_type_from_name(name);
        state->var_count++;
        scope->saved_var_count++;  /* watermark moves up */
        return var;
    }

    /* Normal creation: append at end */
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

/* ── Procedure Scope Management ──────────────────────────────────── */

void amos_proc_scope_push(amos_state_t *state)
{
    if (state->proc_scope_top >= AMOS_MAX_PROC_DEPTH) return;
    proc_scope_t *scope = &state->proc_scopes[state->proc_scope_top++];
    scope->saved_var_count = state->var_count;
    scope->shared_count = 0;
}

void amos_proc_scope_pop(amos_state_t *state)
{
    if (state->proc_scope_top <= 0) return;
    proc_scope_t *scope = &state->proc_scopes[--state->proc_scope_top];

    /* Free string variables in local scope */
    for (int i = scope->saved_var_count; i < state->var_count; i++) {
        if (state->variables[i].type == VAR_STRING && state->variables[i].sval.data) {
            free(state->variables[i].sval.data);
            state->variables[i].sval.data = NULL;
        }
    }

    /* Discard local variables */
    state->var_count = scope->saved_var_count;
}

void amos_proc_scope_add_shared(amos_state_t *state, const char *name)
{
    if (state->proc_scope_top <= 0) return;
    proc_scope_t *scope = &state->proc_scopes[state->proc_scope_top - 1];
    if (scope->shared_count >= AMOS_MAX_SHARED_VARS) return;

    /* Don't add duplicates */
    for (int i = 0; i < scope->shared_count; i++) {
        if (strcasecmp(scope->shared_names[i], name) == 0) return;
    }

    strncpy(scope->shared_names[scope->shared_count], name,
            sizeof(scope->shared_names[0]) - 1);
    scope->shared_names[scope->shared_count][sizeof(scope->shared_names[0]) - 1] = '\0';
    scope->shared_count++;
}
