#ifndef EMPTY_FILES_H
#define EMPTY_FILES_H

#include <windows.h>

typedef struct EmptyFileList {
    char **files;
    int count;
    int capacity;
    CRITICAL_SECTION lock;
} EmptyFileList;

// Global empty files list
extern EmptyFileList g_empty_files;

// Initialize empty files list
void init_empty_files_list(void);

// Add empty file to list
void add_empty_file(const char *filepath);

// Remove empty file from list
void remove_empty_file(const char *filepath);

// Print all empty files
void print_empty_files(void);

// Free empty files list
void free_empty_files_list(void);

#endif // EMPTY_FILES_H