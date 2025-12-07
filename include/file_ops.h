#ifndef FILE_OPS_H
#define FILE_OPS_H

#define BUFFER_SIZE (1024 * 1024)

// Check if file is empty (0 bytes)
// Returns: 1 if empty, 0 if not empty, -1 on error
int is_file_empty(const char *filepath);

// Check if file should be ignored based on patterns
int should_ignore_file(const char *filename);

// Compute BLAKE3 hash of a file
// Returns: 0 on success, -1 on error
int hash_file(const char *filepath, char *hex_output);

// Process a single file (check, hash, add to table)
void process_file(const char *full_path, const char *action);

#endif // FILE_OPS_H