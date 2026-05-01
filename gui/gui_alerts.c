// gui_alerts.c - Alert data management: storage, JSON parsing, group operations
#include "gui_common.h"

// ----------------------------------------------------------------
// Global definitions (owned here)
// ----------------------------------------------------------------
DuplicateAlert   g_alerts[MAX_ALERTS];
int              g_alert_count        = 0;
int              g_current_alert_index = 0;
CRITICAL_SECTION g_alert_lock;

EmptyFileEntry   g_empty_entries[MAX_EMPTY_FILES];
int              g_empty_count = 0;
int              g_view_mode   = VIEW_MODE_DUPLICATES;
volatile BOOL    g_scanning    = FALSE;

// ----------------------------------------------------------------
// File utilities
// ----------------------------------------------------------------

BOOL FileExists(const char *filepath) {
    DWORD attrs = GetFileAttributes(filepath);
    return (attrs != INVALID_FILE_ATTRIBUTES);
}

void format_file_size(unsigned long long size, char *buffer, size_t buf_size) {
    if (size < 1024) {
        snprintf(buffer, buf_size, "%llu bytes", size);
    } else if (size < 1024 * 1024) {
        snprintf(buffer, buf_size, "%.2f KB", size / 1024.0);
    } else if (size < 1024 * 1024 * 1024) {
        snprintf(buffer, buf_size, "%.2f MB", size / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buf_size, "%.2f GB", size / (1024.0 * 1024.0 * 1024.0));
    }
}

// ----------------------------------------------------------------
// Group queries
// ----------------------------------------------------------------

int CountRemainingFiles(DuplicateAlert *alert) {
    int count = 0;
    if (FileExists(alert->trigger_file.filepath)) count++;
    for (int i = 0; i < alert->duplicate_count; i++) {
        if (FileExists(alert->duplicates[i].filepath)) count++;
    }
    return count;
}

// direction: 1 = next, -1 = previous
int FindNextValidGroup(int current_index, int direction) {
    int index = current_index;
    int steps = 0;

    while (steps < g_alert_count) {
        index += direction;
        steps++;

        if (index < 0 || index >= g_alert_count)
            return current_index;

        if (CountRemainingFiles(&g_alerts[index]) >= 2)
            return index;
    }
    return current_index;
}

int find_alert_by_hash(const char *filehash) {
    for (int i = 0; i < g_alert_count; i++) {
        if (strcmp(g_alerts[i].filehash, filehash) == 0)
            return i;
    }
    return -1;
}

// ----------------------------------------------------------------
// JSON parsing
// ----------------------------------------------------------------

void ParseAlertJSON(const char *json) {
    EnterCriticalSection(&g_alert_lock);

    char filehash[65] = {0};

    const char *hash_start = strstr(json, "\"filehash\":\"");
    if (hash_start) {
        hash_start += 12;
        const char *hash_end = strchr(hash_start, '"');
        if (hash_end) {
            size_t len = hash_end - hash_start;
            if (len < 65) {
                strncpy(filehash, hash_start, len);
                filehash[len] = '\0';
            }
        }
    }

    if (strlen(filehash) == 0) {
        LeaveCriticalSection(&g_alert_lock);
        return;
    }

    int alert_index = find_alert_by_hash(filehash);
    DuplicateAlert *alert;

    if (alert_index >= 0) {
        alert = &g_alerts[alert_index];
        memset(alert, 0, sizeof(DuplicateAlert));
        strncpy(alert->filehash, filehash, 64);
    } else {
        if (g_alert_count >= MAX_ALERTS) {
            for (int i = 0; i < MAX_ALERTS - 1; i++)
                g_alerts[i] = g_alerts[i + 1];
            g_alert_count = MAX_ALERTS - 1;
            if (g_current_alert_index > 0) g_current_alert_index--;
        }
        alert = &g_alerts[g_alert_count];
        memset(alert, 0, sizeof(DuplicateAlert));
        strncpy(alert->filehash, filehash, 64);
        g_alert_count++;
        alert_index = g_alert_count - 1;
    }

    // Parse trigger file
    const char *trigger_start = strstr(json, "\"trigger_file\":");
    if (trigger_start) {
        const char *filepath = strstr(trigger_start, "\"filepath\":\"");
        if (filepath) {
            filepath += 12;
            const char *filepath_end = strchr(filepath, '"');
            if (filepath_end) {
                size_t len = filepath_end - filepath;
                if (len < MAX_PATH) {
                    strncpy(alert->trigger_file.filepath, filepath, len);
                    alert->trigger_file.filepath[len] = '\0';
                    const char *fn = strrchr(alert->trigger_file.filepath, '\\');
                    fn = fn ? fn + 1 : alert->trigger_file.filepath;
                    strncpy(alert->trigger_file.filename, fn, MAX_PATH - 1);
                }
            }
        }

        const char *fh = strstr(trigger_start, "\"filehash\":\"");
        if (fh && fh < strstr(trigger_start, "},")) {
            fh += 12;
            const char *he = strchr(fh, '"');
            if (he) {
                size_t len = he - fh;
                if (len < 65) {
                    strncpy(alert->trigger_file.filehash, fh, len);
                    alert->trigger_file.filehash[len] = '\0';
                }
            }
        }

        const char *fs = strstr(trigger_start, "\"filesize\":");
        if (fs && fs < strstr(trigger_start, "},"))
            sscanf(fs + 11, "%llu", &alert->trigger_file.filesize);

        const char *lm = strstr(trigger_start, "\"last_mod\":\"");
        if (lm && lm < strstr(trigger_start, "},")) {
            lm += 12;
            const char *me = strchr(lm, '"');
            if (me) {
                size_t len = me - lm;
                if (len < 32) {
                    strncpy(alert->trigger_file.last_modified, lm, len);
                    alert->trigger_file.last_modified[len] = '\0';
                }
            }
        }
    }

    // Parse duplicates array
    const char *dup_start = strstr(json, "\"duplicates\":[");
    if (dup_start) {
        const char *dup = dup_start + 14;
        int count = 0;

        while (count < MAX_DUPLICATES) {
            dup = strstr(dup, "{\"filepath\":\"");
            if (!dup) break;

            const char *array_end = strstr(dup_start, "],\"timestamp\"");
            if (!array_end || dup > array_end) break;

            dup += 13;
            const char *dup_end = strchr(dup, '"');
            if (dup_end) {
                size_t len = dup_end - dup;
                if (len < MAX_PATH) {
                    strncpy(alert->duplicates[count].filepath, dup, len);
                    alert->duplicates[count].filepath[len] = '\0';
                    const char *fn = strrchr(alert->duplicates[count].filepath, '\\');
                    fn = fn ? fn + 1 : alert->duplicates[count].filepath;
                    strncpy(alert->duplicates[count].filename, fn, MAX_PATH - 1);
                }
            }

            const char *nb = strchr(dup, '}');
            if (!nb) break;

            const char *ss = strstr(dup, "\"filesize\":");
            if (ss && ss < nb)
                sscanf(ss + 11, "%llu", &alert->duplicates[count].filesize);

            const char *lm = strstr(dup, "\"last_mod\":\"");
            if (lm && lm < nb) {
                lm += 12;
                const char *me = strchr(lm, '"');
                if (me && me < nb) {
                    size_t len = me - lm;
                    if (len < 32) {
                        strncpy(alert->duplicates[count].last_modified, lm, len);
                        alert->duplicates[count].last_modified[len] = '\0';
                    }
                }
            }

            count++;
            dup = nb + 1;
        }
        alert->duplicate_count = count;
    }

    // Parse timestamp
    const char *ts = strstr(json, "\"timestamp\":\"");
    if (ts) {
        ts += 13;
        const char *te = strchr(ts, '"');
        if (te) {
            size_t len = te - ts;
            if (len < 32) {
                strncpy(alert->timestamp, ts, len);
                alert->timestamp[len] = '\0';
            }
        }
    }

    alert->files_remaining = CountRemainingFiles(alert);
    g_current_alert_index  = alert_index;

    LeaveCriticalSection(&g_alert_lock);
}

// ----------------------------------------------------------------
// Group mutations
// ----------------------------------------------------------------

void CompactAlerts(void) {
    int write = 0;
    for (int read = 0; read < g_alert_count; read++) {
        if (CountRemainingFiles(&g_alerts[read]) >= 2) {
            if (write != read) g_alerts[write] = g_alerts[read];
            write++;
        }
    }
    g_alert_count = write;

    if (g_alert_count == 0)
        g_current_alert_index = 0;
    else if (g_current_alert_index >= g_alert_count)
        g_current_alert_index = g_alert_count - 1;
}

void PromoteDuplicate(DuplicateAlert *alert) {
    int pi = -1;
    for (int i = 0; i < alert->duplicate_count; i++) {
        if (FileExists(alert->duplicates[i].filepath)) { pi = i; break; }
    }
    if (pi == -1) return;

    alert->trigger_file = alert->duplicates[pi];
    strncpy(alert->trigger_file.filehash, alert->filehash, 64);
    alert->trigger_file.filehash[64] = '\0';

    for (int i = pi; i < alert->duplicate_count - 1; i++)
        alert->duplicates[i] = alert->duplicates[i + 1];
    alert->duplicate_count--;
    memset(&alert->duplicates[alert->duplicate_count], 0, sizeof(FileInfo));
}

void RemoveAlertAt(int index) {
    for (int i = index; i < g_alert_count - 1; i++)
        g_alerts[i] = g_alerts[i + 1];
    memset(&g_alerts[g_alert_count - 1], 0, sizeof(DuplicateAlert));
    g_alert_count--;

    if (g_alert_count == 0)
        g_current_alert_index = 0;
    else if (g_current_alert_index >= g_alert_count)
        g_current_alert_index = g_alert_count - 1;
}

// ----------------------------------------------------------------
// Empty file list operations
// ----------------------------------------------------------------

void RemoveEmptyEntry(const char *filepath) {
    EnterCriticalSection(&g_alert_lock);
    for (int i = 0; i < g_empty_count; i++) {
        if (strcmp(g_empty_entries[i].filepath, filepath) == 0) {
            for (int j = i; j < g_empty_count - 1; j++)
                g_empty_entries[j] = g_empty_entries[j + 1];
            memset(&g_empty_entries[g_empty_count - 1], 0, sizeof(EmptyFileEntry));
            g_empty_count--;
            break;
        }
    }
    LeaveCriticalSection(&g_alert_lock);
}

static void extract_json_string(const char *json, const char *key,
                                char *out, size_t outsz) {
    out[0] = '\0';
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char *s = strstr(json, pat);
    if (!s) return;
    s += strlen(pat);
    const char *e = strchr(s, '"');
    if (!e) return;
    size_t len = (size_t)(e - s);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, s, len);
    out[len] = '\0';
}

void ParseEmptyFileJSON(const char *json) {
    char filepath[MAX_PATH] = {0};
    extract_json_string(json, "filepath", filepath, sizeof(filepath));
    if (filepath[0] == '\0') return;

    char last_mod[32] = {0};
    extract_json_string(json, "last_mod", last_mod, sizeof(last_mod));

    unsigned long long filesize = 0;
    const char *fs = strstr(json, "\"filesize\":");
    if (fs) sscanf(fs + 11, "%llu", &filesize);

    EnterCriticalSection(&g_alert_lock);

    // De-dup
    for (int i = 0; i < g_empty_count; i++) {
        if (strcmp(g_empty_entries[i].filepath, filepath) == 0) {
            g_empty_entries[i].filesize = filesize;
            strncpy(g_empty_entries[i].last_modified, last_mod, 31);
            g_empty_entries[i].last_modified[31] = '\0';
            LeaveCriticalSection(&g_alert_lock);
            return;
        }
    }

    if (g_empty_count >= MAX_EMPTY_FILES) {
        // drop oldest
        for (int i = 0; i < MAX_EMPTY_FILES - 1; i++)
            g_empty_entries[i] = g_empty_entries[i + 1];
        g_empty_count = MAX_EMPTY_FILES - 1;
    }

    EmptyFileEntry *e = &g_empty_entries[g_empty_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->filepath, filepath, MAX_PATH - 1);
    e->filesize = filesize;
    strncpy(e->last_modified, last_mod, 31);

    LeaveCriticalSection(&g_alert_lock);
}
