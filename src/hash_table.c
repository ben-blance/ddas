//hash_table.c
#include "hash_table.h"
#include "ipc_pipe.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HashTable *g_hash_table = NULL;

static unsigned int hash_string(const char *str, size_t table_size) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash % table_size;
}

HashTable* create_hash_table(size_t size) {
    HashTable *table = malloc(sizeof(HashTable));
    table->size = size;
    table->buckets = calloc(size, sizeof(FileHash*));
    InitializeCriticalSection(&table->lock);
    return table;
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

// Helper function to collect all files with same hash
static int collect_duplicates_for_hash(HashTable *table, const char *hash, 
                                       const char *exclude_filepath, 
                                       FileInfo *duplicates, int max_count) {
    int count = 0;
    
    for (size_t i = 0; i < table->size && count < max_count; i++) {
        FileHash *current = table->buckets[i];
        while (current && count < max_count) {
            if (strcmp(current->hash, hash) == 0 && 
                strcmp(current->filepath, exclude_filepath) != 0) {
                
                FileInfo *dup = &duplicates[count];
                strncpy(dup->filepath, current->filepath, MAX_PATH - 1);
                dup->filepath[MAX_PATH - 1] = '\0';
                
                // Extract filename
                const char *filename = strrchr(current->filepath, '\\');
                filename = filename ? filename + 1 : current->filepath;
                strncpy(dup->filename, filename, MAX_PATH - 1);
                dup->filename[MAX_PATH - 1] = '\0';
                
                strncpy(dup->filehash, hash, 64);
                dup->filehash[64] = '\0';
                
                // Get file size
                WIN32_FILE_ATTRIBUTE_DATA file_data;
                if (GetFileAttributesEx(current->filepath, GetFileExInfoStandard, &file_data)) {
                    dup->filesize = ((uint64_t)file_data.nFileSizeHigh << 32) | file_data.nFileSizeLow;
                } else {
                    dup->filesize = 0;
                }
                
                // Get modified time
                get_file_modified_time(current->filepath, dup->last_modified, sizeof(dup->last_modified));
                
                // Generate file index
                dup->file_index = generate_file_index(current->filepath);
                
                count++;
            }
            current = current->next;
        }
    }
    
    return count;
}

int check_for_duplicate(HashTable *table, const char *hash, const char *new_filepath) {
    EnterCriticalSection(&table->lock);
    
    int found = 0;
    for (size_t i = 0; i < table->size; i++) {
        FileHash *current = table->buckets[i];
        while (current) {
            if (strcmp(current->hash, hash) == 0 && 
                strcmp(current->filepath, new_filepath) != 0) {
                found = 1;
                break;
            }
            current = current->next;
        }
        if (found) break;
    }
    
    // If duplicate found, collect all duplicates and send IPC alert
    if (found) {
        FileInfo duplicates[100]; // Max 100 duplicates per alert
        int duplicate_count = collect_duplicates_for_hash(table, hash, new_filepath, duplicates, 100);
        
        if (duplicate_count > 0) {
            // Build FileInfo for trigger file (new file)
            FileInfo trigger;
            strncpy(trigger.filepath, new_filepath, MAX_PATH - 1);
            trigger.filepath[MAX_PATH - 1] = '\0';
            
            const char *filename = strrchr(new_filepath, '\\');
            filename = filename ? filename + 1 : new_filepath;
            strncpy(trigger.filename, filename, MAX_PATH - 1);
            trigger.filename[MAX_PATH - 1] = '\0';
            
            strncpy(trigger.filehash, hash, 64);
            trigger.filehash[64] = '\0';
            
            // Get file size
            WIN32_FILE_ATTRIBUTE_DATA file_data;
            if (GetFileAttributesEx(new_filepath, GetFileExInfoStandard, &file_data)) {
                trigger.filesize = ((uint64_t)file_data.nFileSizeHigh << 32) | file_data.nFileSizeLow;
            } else {
                trigger.filesize = 0;
            }
            
            get_file_modified_time(new_filepath, trigger.last_modified, sizeof(trigger.last_modified));
            trigger.file_index = generate_file_index(new_filepath);
            
            // Get timestamp
            char timestamp[32];
            get_iso8601_timestamp(timestamp, sizeof(timestamp));
            
            // Send alert via IPC (releases lock internally if needed)
            LeaveCriticalSection(&table->lock);
            send_alert_duplicate_detected(&trigger, duplicates, duplicate_count, timestamp);
            EnterCriticalSection(&table->lock);
        }
    }
    
    LeaveCriticalSection(&table->lock);
    return found;
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

void find_duplicates(HashTable *table) {
    EnterCriticalSection(&table->lock);
    
    int duplicate_groups = 0;
    int total_duplicate_files = 0;
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
                total_duplicate_files += count;
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
        safe_printf("Found %d duplicate groups (%d total duplicate files).\n", 
                   duplicate_groups, total_duplicate_files);
    }
    
    LeaveCriticalSection(&table->lock);
    
    // Send scan complete alert via IPC
    char timestamp[32];
    get_iso8601_timestamp(timestamp, sizeof(timestamp));
    send_alert_scan_complete(total_duplicate_files, duplicate_groups, timestamp);
}

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