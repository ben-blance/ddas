/* 
 * File Duplicate Detector using BLAKE3 hashing with Concurrent USN Journal Monitoring
 * Windows version - Thread-safe edition
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <windows.h>
#include <winioctl.h>
#include <sys/stat.h>
#include <wchar.h>
#include <stdarg.h>
#include "blake3.h"

// MinGW compatibility
#ifndef _MSC_VER
    #define _strdup strdup
#endif

#define HASH_SIZE 32
#define BUFFER_SIZE (1024 * 1024)

typedef struct FileHash {
    char hash[HASH_SIZE * 2 + 1];
    char *filepath;
    struct FileHash *next;
} FileHash;

typedef struct HashTable {
    FileHash **buckets;
    size_t size;
    CRITICAL_SECTION lock;
} HashTable;

// Global variables
HashTable *g_hash_table = NULL;
volatile int g_scanning_complete = 0;
volatile int g_stop_monitoring = 0;
char g_monitor_path[MAX_PATH];
CRITICAL_SECTION g_print_lock;

typedef struct EmptyFileList {
    char **files;
    int count;
    int capacity;
    CRITICAL_SECTION lock;
} EmptyFileList;

EmptyFileList g_empty_files;

// Thread-safe printf wrapper
void safe_printf(const char *format, ...) {
    EnterCriticalSection(&g_print_lock);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    fflush(stdout);
    LeaveCriticalSection(&g_print_lock);
}

// Initialize empty files list
void init_empty_files_list() {
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

void print_empty_files() {
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

void free_empty_files_list() {
    for (int i = 0; i < g_empty_files.count; i++) {
        free(g_empty_files.files[i]);
    }
    free(g_empty_files.files);
    DeleteCriticalSection(&g_empty_files.lock);
}

// Check if file is empty (0 bytes)
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

// Hash table functions
HashTable* create_hash_table(size_t size) {
    HashTable *table = malloc(sizeof(HashTable));
    table->size = size;
    table->buckets = calloc(size, sizeof(FileHash*));
    InitializeCriticalSection(&table->lock);
    return table;
}

unsigned int hash_string(const char *str, size_t table_size) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash % table_size;
}

void add_file_hash(HashTable *table, const char *hash, const char *filepath) {
    EnterCriticalSection(&table->lock);
    
    unsigned int index = hash_string(hash, table->size);
    FileHash *new_node = malloc(sizeof(FileHash));
    strcpy(new_node->hash, hash);
    new_node->filepath = _strdup(filepath);
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
    
    LeaveCriticalSection(&table->lock);
}

void remove_file_from_table(HashTable *table, const char *filepath) {
    EnterCriticalSection(&table->lock);
    
    for (size_t i = 0; i < table->size; i++) {
        FileHash *current = table->buckets[i];
        FileHash *prev = NULL;
        
        while (current) {
            if (strcmp(current->filepath, filepath) == 0) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    table->buckets[i] = current->next;
                }
                free(current->filepath);
                free(current);
                LeaveCriticalSection(&table->lock);
                return;
            }
            prev = current;
            current = current->next;
        }
    }
    
    LeaveCriticalSection(&table->lock);
}

// Check if file should be ignored
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

// Compute BLAKE3 hash of a file
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

// Check if hash exists in table
int check_for_duplicate(HashTable *table, const char *hash, const char *new_filepath) {
    EnterCriticalSection(&table->lock);
    
    for (size_t i = 0; i < table->size; i++) {
        FileHash *current = table->buckets[i];
        while (current) {
            if (strcmp(current->hash, hash) == 0 && 
                strcmp(current->filepath, new_filepath) != 0) {
                LeaveCriticalSection(&table->lock);
                return 1;
            }
            current = current->next;
        }
    }
    
    LeaveCriticalSection(&table->lock);
    return 0;
}

void print_duplicates_for_file(HashTable *table, const char *hash, 
                               const char *new_filepath) {
    EnterCriticalSection(&table->lock);
    
    safe_printf("\n[DUPLICATE DETECTED]\n");
    safe_printf("New file: %s\n", new_filepath);
    safe_printf("Matches existing files:\n");
    
    for (size_t i = 0; i < table->size; i++) {
        FileHash *current = table->buckets[i];
        while (current) {
            if (strcmp(current->hash, hash) == 0 && 
                strcmp(current->filepath, new_filepath) != 0) {
                safe_printf(" - %s\n", current->filepath);
            }
            current = current->next;
        }
    }
    safe_printf("\n");
    
    LeaveCriticalSection(&table->lock);
}

// Process a single file
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

// Recursively scan directory
void scan_directory(const char *dir_path, HashTable *table, int *file_count) {
    WIN32_FIND_DATA find_data;
    HANDLE hFind;
    char search_path[MAX_PATH];
    
    snprintf(search_path, MAX_PATH, "%s\\*", dir_path);
    
    hFind = FindFirstFile(search_path, &find_data);
    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }
    
    do {
        if (g_stop_monitoring) break;
        
        if (strcmp(find_data.cFileName, ".") == 0 || 
            strcmp(find_data.cFileName, "..") == 0) {
            continue;
        }
        
        char full_path[MAX_PATH];
        snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, find_data.cFileName);
        
        if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_directory(full_path, table, file_count);
        } else {
            if (should_ignore_file(find_data.cFileName)) {
                safe_printf("[SKIP] %s\n", full_path);
                continue;
            }
            
            process_file(full_path, "SCAN");
            (*file_count)++;
        }
    } while (FindNextFile(hFind, &find_data));
    
    FindClose(hFind);
}

// Find and report duplicates
void find_duplicates(HashTable *table) {
    EnterCriticalSection(&table->lock);
    
    int duplicate_groups = 0;
    char **processed_hashes = malloc(sizeof(char*) * 1000);
    int processed_count = 0;
    
    safe_printf("\n=== DUPLICATE FILES (Initial Scan) ===\n\n");
    
    for (size_t i = 0; i < table->size; i++) {
        FileHash *current = table->buckets[i];
        
        while (current) {
            int already_processed = 0;
            for (int j = 0; j < processed_count; j++) {
                if (strcmp(processed_hashes[j], current->hash) == 0) {
                    already_processed = 1;
                    break;
                }
            }
            
            if (already_processed) {
                current = current->next;
                continue;
            }
            
            int count = 0;
            FileHash *counter = table->buckets[i];
            while (counter) {
                if (strcmp(counter->hash, current->hash) == 0) {
                    count++;
                }
                counter = counter->next;
            }
            
            if (count > 1) {
                duplicate_groups++;
                safe_printf("Duplicate group #%d (hash: %s):\n", 
                       duplicate_groups, current->hash);
                
                FileHash *printer = table->buckets[i];
                while (printer) {
                    if (strcmp(printer->hash, current->hash) == 0) {
                        safe_printf(" - %s\n", printer->filepath);
                    }
                    printer = printer->next;
                }
                safe_printf("\n");
                
                processed_hashes[processed_count] = _strdup(current->hash);
                processed_count++;
            }
            
            current = current->next;
        }
    }
    
    for (int i = 0; i < processed_count; i++) {
        free(processed_hashes[i]);
    }
    free(processed_hashes);
    
    if (duplicate_groups == 0) {
        safe_printf("No duplicates found.\n");
    } else {
        safe_printf("Found %d duplicate groups.\n", duplicate_groups);
    }
    
    LeaveCriticalSection(&table->lock);
}

// Monitor thread function
DWORD WINAPI monitor_thread_func(LPVOID lpParam) {
    const char *dir_path = (const char*)lpParam;
    
    HANDLE hDir = CreateFile(
        dir_path,
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL
    );
    
    if (hDir == INVALID_HANDLE_VALUE) {
        safe_printf("Failed to open directory for monitoring: %s\n", dir_path);
        return 1;
    }
    
    safe_printf("\n=== File System Monitor Started ===\n");
    safe_printf("Watching for changes during scan and after...\n\n");
    
    char buffer[4096];
    DWORD bytes_returned;
    OVERLAPPED overlapped = {0};
    overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    while (!g_stop_monitoring) {
        ResetEvent(overlapped.hEvent);
        
        if (ReadDirectoryChangesW(
            hDir,
            buffer,
            sizeof(buffer),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | 
            FILE_NOTIFY_CHANGE_LAST_WRITE,
            &bytes_returned,
            &overlapped,
            NULL
        )) {
            DWORD waitResult = WaitForSingleObject(overlapped.hEvent, 500);
            
            if (g_stop_monitoring) break;
            
            if (waitResult == WAIT_OBJECT_0) {
                if (!GetOverlappedResult(hDir, &overlapped, &bytes_returned, FALSE)) {
                    continue;
                }
                
                FILE_NOTIFY_INFORMATION *fni = (FILE_NOTIFY_INFORMATION*)buffer;
                
                do {
                    WCHAR filename[MAX_PATH];
                    wcsncpy(filename, fni->FileName, 
                           fni->FileNameLength / sizeof(WCHAR));
                    filename[fni->FileNameLength / sizeof(WCHAR)] = L'\0';
                    
                    char full_path[MAX_PATH];
                    char filename_utf8[MAX_PATH];
                    WideCharToMultiByte(CP_UTF8, 0, filename, -1, 
                                       filename_utf8, MAX_PATH, NULL, NULL);
                    snprintf(full_path, MAX_PATH, "%s\\%s", dir_path, filename_utf8);
                    
                    if (should_ignore_file(filename_utf8)) {
                        break;
                    }
                    
                    switch (fni->Action) {
                        case FILE_ACTION_ADDED: {
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs != INVALID_FILE_ATTRIBUTES && 
                                !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                                Sleep(100);
                                process_file(full_path, "ADDED");
                            }
                            break;
                        }
                        
                        case FILE_ACTION_MODIFIED: {
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs != INVALID_FILE_ATTRIBUTES && 
                                !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                                Sleep(100);
                                safe_printf("[MODIFIED] %s - Reprocessing...\n", full_path);
                                remove_file_from_table(g_hash_table, full_path);
                                remove_empty_file(full_path);
                                process_file(full_path, "MODIFIED");
                            }
                            break;
                        }
                        
                        case FILE_ACTION_REMOVED:
                            safe_printf("[DELETED] %s\n", full_path);
                            remove_file_from_table(g_hash_table, full_path);
                            remove_empty_file(full_path);
                            break;
                            
                        case FILE_ACTION_RENAMED_OLD_NAME:
                            safe_printf("[RENAMED FROM] %s\n", full_path);
                            remove_file_from_table(g_hash_table, full_path);
                            break;
                            
                        case FILE_ACTION_RENAMED_NEW_NAME: {
                            DWORD attrs = GetFileAttributes(full_path);
                            if (attrs != INVALID_FILE_ATTRIBUTES && 
                                !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                                process_file(full_path, "RENAMED TO");
                            }
                            break;
                        }
                    }
                    
                    if (fni->NextEntryOffset == 0) break;
                    fni = (FILE_NOTIFY_INFORMATION*)((char*)fni + fni->NextEntryOffset);
                } while (1);
            }
        }
    }
    
    CloseHandle(overlapped.hEvent);
    CloseHandle(hDir);
    safe_printf("\n=== File System Monitor Stopped ===\n");
    return 0;
}

// Scanner thread function
DWORD WINAPI scanner_thread_func(LPVOID lpParam) {
    int file_count = 0;
    scan_directory(g_monitor_path, g_hash_table, &file_count);
    
    safe_printf("\n=== Initial Scan Complete ===\n");
    safe_printf("Processed %d files.\n", file_count);
    
    g_scanning_complete = 1;
    
    find_duplicates(g_hash_table);
    print_empty_files();
    
    return 0;
}

// Free hash table memory
void free_hash_table(HashTable *table) {
    for (size_t i = 0; i < table->size; i++) {
        FileHash *current = table->buckets[i];
        while (current) {
            FileHash *next = current->next;
            free(current->filepath);
            free(current);
            current = next;
        }
    }
    DeleteCriticalSection(&table->lock);
    free(table->buckets);
    free(table);
}

// Ctrl+C handler
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        safe_printf("\n\nStopping monitoring...\n");
        g_stop_monitoring = 1;
        return TRUE;
    }
    return FALSE;
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s <directory> [--watch]\n", argv[0]);
        printf(" --watch: Continue monitoring after initial scan\n");
        return 1;
    }
    
    const char *directory = argv[1];
    int watch_mode = (argc == 3 && strcmp(argv[2], "--watch") == 0);
    
    strncpy(g_monitor_path, directory, MAX_PATH);
    
    // Initialize print lock
    InitializeCriticalSection(&g_print_lock);
    
    safe_printf("=== File Duplicate Detector with Real-time Monitoring ===\n");
    safe_printf("Directory: %s\n", directory);
    safe_printf("Mode: %s\n\n", watch_mode ? "Scan + Watch" : "Scan Only");
    
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    
    g_hash_table = create_hash_table(10007);
    init_empty_files_list();
    
    HANDLE hMonitorThread = CreateThread(NULL, 0, monitor_thread_func, 
                                         g_monitor_path, 0, NULL);
    if (!hMonitorThread) {
        safe_printf("Failed to create monitor thread\n");
        return 1;
    }
    
    // Give monitor thread time to start
    Sleep(200);
    
    HANDLE hScannerThread = CreateThread(NULL, 0, scanner_thread_func, 
                                         NULL, 0, NULL);
    if (!hScannerThread) {
        safe_printf("Failed to create scanner thread\n");
        g_stop_monitoring = 1;
        WaitForSingleObject(hMonitorThread, INFINITE);
        return 1;
    }
    
    WaitForSingleObject(hScannerThread, INFINITE);
    CloseHandle(hScannerThread);
    
    if (watch_mode) {
        safe_printf("\n=== Continuing to monitor (Press Ctrl+C to stop) ===\n\n");
        WaitForSingleObject(hMonitorThread, INFINITE);
    } else {
        g_stop_monitoring = 1;
        WaitForSingleObject(hMonitorThread, INFINITE);
    }
    
    CloseHandle(hMonitorThread);
    
    free_hash_table(g_hash_table);
    free_empty_files_list();
    DeleteCriticalSection(&g_print_lock);
    
    safe_printf("\nProgram terminated.\n");
    return 0;
}