//hash_table.h
#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <windows.h>
#include <stddef.h>

#define HASH_SIZE 32

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

// Global hash table
extern HashTable *g_hash_table;

// Create hash table
HashTable* create_hash_table(size_t size);

// Add file hash to table
void add_file_hash(HashTable *table, const char *hash, const char *filepath);

// Remove file from table
void remove_file_from_table(HashTable *table, const char *filepath);

// Check if hash exists (for duplicate detection)
int check_for_duplicate(HashTable *table, const char *hash, const char *new_filepath);

// Print duplicates for a specific file
void print_duplicates_for_file(HashTable *table, const char *hash, const char *new_filepath);

// Find and report all duplicates
void find_duplicates(HashTable *table);

// Free hash table
void free_hash_table(HashTable *table);

#endif // HASH_TABLE_H