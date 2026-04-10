/*
 * file_io.c — AMOS BASIC file I/O subsystem
 *
 * Numbered file channels (1-10), directory scanning, and file operations.
 * Uses standard C stdio and POSIX dirent for portability.
 */

#include "amos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fnmatch.h>
#include <errno.h>

/* ── Channel Validation ─────────────────────────────────────────────── */

static bool valid_channel(amos_state_t *state, int channel, const char *op)
{
    if (channel < 1 || channel > 10) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "%s: channel %d out of range (1-10)", op, channel);
        state->error_code = 5;
        return false;
    }
    return true;
}

static bool channel_open(amos_state_t *state, int channel, const char *op)
{
    if (!valid_channel(state, channel, op)) return false;
    if (!state->file_channels[channel]) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "%s: channel %d not open", op, channel);
        state->error_code = 6;
        return false;
    }
    return true;
}

/* ── Open / Close ───────────────────────────────────────────────────── */

void amos_file_open_in(amos_state_t *state, int channel, const char *path)
{
    if (!valid_channel(state, channel, "Open In")) return;

    /* Close if already open */
    if (state->file_channels[channel]) {
        fclose(state->file_channels[channel]);
        state->file_channels[channel] = NULL;
        state->file_channel_mode[channel] = 0;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Open In: cannot open '%s': %s", path, strerror(errno));
        state->error_code = 2;
        return;
    }
    state->file_channels[channel] = f;
    state->file_channel_mode[channel] = 1;
}

void amos_file_open_out(amos_state_t *state, int channel, const char *path)
{
    if (!valid_channel(state, channel, "Open Out")) return;

    if (state->file_channels[channel]) {
        fclose(state->file_channels[channel]);
        state->file_channels[channel] = NULL;
        state->file_channel_mode[channel] = 0;
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Open Out: cannot create '%s': %s", path, strerror(errno));
        state->error_code = 2;
        return;
    }
    state->file_channels[channel] = f;
    state->file_channel_mode[channel] = 2;
}

void amos_file_append(amos_state_t *state, int channel, const char *path)
{
    if (!valid_channel(state, channel, "Append")) return;

    if (state->file_channels[channel]) {
        fclose(state->file_channels[channel]);
        state->file_channels[channel] = NULL;
        state->file_channel_mode[channel] = 0;
    }

    FILE *f = fopen(path, "a");
    if (!f) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Append: cannot open '%s': %s", path, strerror(errno));
        state->error_code = 2;
        return;
    }
    state->file_channels[channel] = f;
    state->file_channel_mode[channel] = 3;
}

void amos_file_close(amos_state_t *state, int channel)
{
    if (!valid_channel(state, channel, "Close")) return;

    if (state->file_channels[channel]) {
        fclose(state->file_channels[channel]);
        state->file_channels[channel] = NULL;
        state->file_channel_mode[channel] = 0;
    }
}

void amos_file_close_all(amos_state_t *state)
{
    for (int i = 1; i <= 10; i++) {
        if (state->file_channels[i]) {
            fclose(state->file_channels[i]);
            state->file_channels[i] = NULL;
            state->file_channel_mode[i] = 0;
        }
    }
    /* Close directory handle if open */
    if (state->dir_handle) {
        closedir((DIR *)state->dir_handle);
        state->dir_handle = NULL;
    }
}

/* ── Reading ────────────────────────────────────────────────────────── */

char *amos_file_input_line(amos_state_t *state, int channel)
{
    if (!channel_open(state, channel, "Line Input#")) return strdup("");

    char buf[1024];
    if (!fgets(buf, sizeof(buf), state->file_channels[channel])) {
        return strdup("");
    }

    /* Strip trailing newline */
    int len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }

    return strdup(buf);
}

int amos_file_input_int(amos_state_t *state, int channel)
{
    if (!channel_open(state, channel, "Input#")) return 0;

    int val = 0;
    if (fscanf(state->file_channels[channel], "%d", &val) != 1) {
        /* Skip separator if present */
        int c = fgetc(state->file_channels[channel]);
        if (c != ',' && c != EOF) {
            ungetc(c, state->file_channels[channel]);
        }
    } else {
        /* Skip trailing comma or newline */
        int c = fgetc(state->file_channels[channel]);
        if (c != ',' && c != '\n' && c != '\r' && c != EOF) {
            ungetc(c, state->file_channels[channel]);
        }
    }
    return val;
}

char *amos_file_input_str(amos_state_t *state, int channel)
{
    if (!channel_open(state, channel, "Input#")) return strdup("");

    /* Read until comma or newline */
    char buf[1024];
    int pos = 0;
    bool in_quotes = false;

    while (pos < (int)sizeof(buf) - 1) {
        int c = fgetc(state->file_channels[channel]);
        if (c == EOF) break;

        if (c == '"') {
            in_quotes = !in_quotes;
            continue;
        }

        if (!in_quotes && (c == ',' || c == '\n')) break;
        if (!in_quotes && c == '\r') continue;

        buf[pos++] = (char)c;
    }
    buf[pos] = '\0';

    return strdup(buf);
}

int amos_file_eof(amos_state_t *state, int channel)
{
    if (!valid_channel(state, channel, "Eof")) return -1;
    if (!state->file_channels[channel]) return -1;

    int c = fgetc(state->file_channels[channel]);
    if (c == EOF) return -1;  /* AMOS true = -1 */
    ungetc(c, state->file_channels[channel]);
    return 0;  /* not at EOF */
}

/* ── Writing ────────────────────────────────────────────────────────── */

void amos_file_print(amos_state_t *state, int channel, const char *text)
{
    if (!channel_open(state, channel, "Print#")) return;

    if (state->file_channel_mode[channel] != 2 &&
        state->file_channel_mode[channel] != 3) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Print#: channel %d not open for output", channel);
        state->error_code = 7;
        return;
    }

    fprintf(state->file_channels[channel], "%s\n", text);
    fflush(state->file_channels[channel]);
}

/* ── Directory Scanning ─────────────────────────────────────────────── */

char *amos_dir_first(amos_state_t *state, const char *pattern)
{
    /* Close any previous scan */
    if (state->dir_handle) {
        closedir((DIR *)state->dir_handle);
        state->dir_handle = NULL;
    }

    /* Extract directory path and filename pattern */
    char dir_path[256] = ".";
    const char *file_pattern = pattern;

    /* Find last slash */
    const char *last_slash = strrchr(pattern, '/');
    if (last_slash) {
        int dir_len = (int)(last_slash - pattern);
        if (dir_len > 0 && dir_len < (int)sizeof(dir_path)) {
            memcpy(dir_path, pattern, dir_len);
            dir_path[dir_len] = '\0';
        }
        file_pattern = last_slash + 1;
    }

    strncpy(state->dir_pattern, file_pattern, sizeof(state->dir_pattern) - 1);
    state->dir_pattern[sizeof(state->dir_pattern) - 1] = '\0';

    DIR *d = opendir(dir_path);
    if (!d) {
        return strdup("");
    }
    state->dir_handle = d;

    /* Return first matching entry */
    return amos_dir_next(state);
}

char *amos_dir_next(amos_state_t *state)
{
    if (!state->dir_handle) return strdup("");

    DIR *d = (DIR *)state->dir_handle;
    struct dirent *entry;

    while ((entry = readdir(d)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        /* Match against pattern (use fnmatch for glob matching) */
        if (state->dir_pattern[0] == '\0' ||
            fnmatch(state->dir_pattern, entry->d_name, 0) == 0) {
            return strdup(entry->d_name);
        }
    }

    /* No more matches */
    closedir(d);
    state->dir_handle = NULL;
    return strdup("");
}

/* ── File Information ───────────────────────────────────────────────── */

int amos_file_exist(amos_state_t *state, const char *path)
{
    (void)state;
    struct stat st;
    if (stat(path, &st) == 0)
        return -1;  /* AMOS true */
    return 0;
}

int amos_file_length(amos_state_t *state, const char *path)
{
    (void)state;
    struct stat st;
    if (stat(path, &st) == 0)
        return (int)st.st_size;
    return 0;
}

/* ── File Operations ────────────────────────────────────────────────── */

void amos_file_kill(amos_state_t *state, const char *path)
{
    if (remove(path) != 0) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Kill: cannot delete '%s': %s", path, strerror(errno));
        state->error_code = 2;
    }
}

void amos_file_rename(amos_state_t *state, const char *old_name, const char *new_name)
{
    if (rename(old_name, new_name) != 0) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Rename: cannot rename '%s' to '%s': %s",
                 old_name, new_name, strerror(errno));
        state->error_code = 2;
    }
}

void amos_file_mkdir(amos_state_t *state, const char *path)
{
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        snprintf(state->error_msg, sizeof(state->error_msg),
                 "Mkdir: cannot create '%s': %s", path, strerror(errno));
        state->error_code = 2;
    }
}
