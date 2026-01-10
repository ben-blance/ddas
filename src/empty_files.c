//empty_files.c
#include "empty_files.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

EmptyFileList g_empty_files;

void init_empty_files_list(void) {
    g_empty_files.capacity = 1000;
    g_empty_files.count = 0;
    g_empty_files.files = malloc(sizeof(char*) * g_empty_files.capacity);
    InitializeCriticalSection(&g_empty_files.lock);
}

void add_empty_file(const char *filepath) {
    EnterCriticalSection(&g_empty_files.lock);
    if (g_empty_files.count >= g_empty_files.capacity) {
        g_empty_files.capacity *= 2;
        g_empty_files.files = realloc(g_empty_files.files, 
                                      sizeof(char*) * g_empty_files.capacity);
    }
    g_empty_files.files[g_empty_files.count++] = _strdup(filepath);
    LeaveCriticalSection(&g_empty_files.lock);
}

void remove_empty_file(const char *filepath) {
    EnterCriticalSection(&g_empty_files.lock);
    for (int i = 0; i < g_empty_files.count; i++) {
        if (strcmp(g_empty_files.files[i], filepath) == 0) {
            free(g_empty_files.files[i]);
            for (int j = i; j < g_empty_files.count - 1; j++) {
                g_empty_files.files[j] = g_empty_files.files[j + 1];
            }
            g_empty_files.count--;
            break;
        }
    }
    LeaveCriticalSection(&g_empty_files.lock);
}

void print_empty_files(void) {
    EnterCriticalSection(&g_empty_files.lock);
    if (g_empty_files.count > 0) {
        safe_printf("\n=== EMPTY FILES (0 bytes) ===\n\n");
        for (int i = 0; i < g_empty_files.count; i++) {
            safe_printf(" - %s\n", g_empty_files.files[i]);
        }
        safe_printf("\nTotal empty files: %d\n", g_empty_files.count);
    }
    LeaveCriticalSection(&g_empty_files.lock);
}

void free_empty_files_list(void) {
    for (int i = 0; i < g_empty_files.count; i++) {
        free(g_empty_files.files[i]);
    }
    free(g_empty_files.files);
    DeleteCriticalSection(&g_empty_files.lock);
}