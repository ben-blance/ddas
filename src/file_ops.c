//file_ops.c
#include "file_ops.h"
#include "hash_table.h"
#include "empty_files.h"
#include "utils.h"
#include "blake3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>

int is_file_empty(const char *filepath) {
    HANDLE hFile = CreateFile(
        filepath,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize)) {
        CloseHandle(hFile);
        return -1;
    }
    
    CloseHandle(hFile);
    return (fileSize.QuadPart == 0) ? 1 : 0;
}

int should_ignore_file(const char *filename) {
    char lower_name[MAX_PATH];
    strncpy(lower_name, filename, MAX_PATH);
    
    for (int i = 0; lower_name[i]; i++) {
        lower_name[i] = tolower(lower_name[i]);
    }
    
    const char *ignore_patterns[] = {
        "~$", ".tmp", ".temp", "~", ".swp", ".swo", ".bak",
        ".crdownload", ".part", ".download", "thumbs.db",
        "desktop.ini", ".ds_store", NULL
    };
    
    if (strncmp(lower_name, "~$", 2) == 0) {
        return 1;
    }
    
    for (int i = 0; ignore_patterns[i] != NULL; i++) {
        if (strstr(lower_name, ignore_patterns[i]) != NULL) {
            return 1;
        }
    }
    
    return 0;
}

int hash_file(const char *filepath, char *hex_output) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        return -1;
    }
    
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    
    unsigned char *buffer = malloc(BUFFER_SIZE);
    size_t bytes_read;
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        blake3_hasher_update(&hasher, buffer, bytes_read);
    }
    
    unsigned char hash[HASH_SIZE];
    blake3_hasher_finalize(&hasher, hash, HASH_SIZE);
    
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_output + (i * 2), "%02x", hash[i]);
    }
    hex_output[HASH_SIZE * 2] = '\0';
    
    free(buffer);
    fclose(file);
    return 0;
}

void process_file(const char *full_path, const char *action) {
    int empty_check = is_file_empty(full_path);
    if (empty_check == 1) {
        safe_printf("[%s] %s (0 bytes - skipped)\n", action, full_path);
        add_empty_file(full_path);
        return;
    } else if (empty_check == -1) {
        safe_printf("[ERROR] Cannot access: %s\n", full_path);
        return;
    }
    
    char hash[HASH_SIZE * 2 + 1];
    if (hash_file(full_path, hash) == 0) {
        safe_printf("[%s] %s\n", action, full_path);
        
        if (check_for_duplicate(g_hash_table, hash, full_path)) {
            print_duplicates_for_file(g_hash_table, hash, full_path);
        }
        
        add_file_hash(g_hash_table, hash, full_path);
    } else {
        safe_printf("[ERROR] Failed to hash: %s\n", full_path);
    }
}