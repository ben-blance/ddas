//hash_table.c
#include "hash_table.h"
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