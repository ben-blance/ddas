#include "ipc_pipe.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

PipeServer *g_pipe_server = NULL;

// Duplicate group storage (indexed by hash)
typedef struct {
    char filehash[65];
    FileInfo files[MAX_DUPLICATES + 1];  // All files with this hash
    int file_count;
    char last_updated[32];
    BOOL sent_to_client;  // Track if this group has been sent
} DuplicateGroup;

static DuplicateGroup g_duplicate_groups[MAX_HISTORY_ALERTS];
static int g_group_count = 0;
static CRITICAL_SECTION g_groups_lock;
static BOOL g_groups_initialized = FALSE;

// Forward declarations
static DWORD WINAPI pipe_server_thread(LPVOID param);
static BOOL send_message(const char *json_message);
static void handle_client_commands(HANDLE pipe);
static DuplicateGroup* find_or_create_group(const char *filehash);
static void send_duplicate_group(DuplicateGroup *group);

// Get current timestamp in ISO 8601 format
void get_iso8601_timestamp(char *buffer, size_t buffer_size) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buffer, buffer_size, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

// Get file modified time as ISO 8601
void get_file_modified_time(const char *filepath, char *buffer, size_t buffer_size) {
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    if (GetFileAttributesEx(filepath, GetFileExInfoStandard, &file_info)) {
        SYSTEMTIME st;
        FileTimeToSystemTime(&file_info.ftLastWriteTime, &st);
        snprintf(buffer, buffer_size, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    } else {
        strcpy(buffer, "unknown");
    }
}

// Generate unique file index
uint64_t generate_file_index(const char *filepath) {
    BY_HANDLE_FILE_INFORMATION file_info;
    HANDLE hFile = CreateFile(filepath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        // Fallback: use hash of filepath
        uint64_t index = 0;
        for (const char *p = filepath; *p; p++) {
            index = index * 31 + *p;
        }
        return index;
    }
    
    GetFileInformationByHandle(hFile, &file_info);
    CloseHandle(hFile);
    
    // Combine volume serial number, file index high and low
    uint64_t index = ((uint64_t)file_info.dwVolumeSerialNumber << 32) |
                     ((uint64_t)file_info.nFileIndexHigh << 16) |
                     file_info.nFileIndexLow;
    return index;
}

// Find existing group by hash or create new one
static DuplicateGroup* find_or_create_group(const char *filehash) {
    // Search for existing group
    for (int i = 0; i < g_group_count; i++) {
        if (strcmp(g_duplicate_groups[i].filehash, filehash) == 0) {
            return &g_duplicate_groups[i];
        }
    }
    
    // Create new group if we have space
    if (g_group_count < MAX_HISTORY_ALERTS) {
        DuplicateGroup *group = &g_duplicate_groups[g_group_count];
        memset(group, 0, sizeof(DuplicateGroup));
        strncpy(group->filehash, filehash, 64);
        group->filehash[64] = '\0';
        group->file_count = 0;
        group->sent_to_client = FALSE;
        g_group_count++;
        return group;
    }
    
    return NULL;
}

// Send a duplicate group to the GUI
static void send_duplicate_group(DuplicateGroup *group) {
    if (group->file_count < 2) {
        return;  // Not a duplicate group anymore
    }
    
    char message[MAX_MESSAGE_SIZE];
    char *ptr = message;
    int remaining = MAX_MESSAGE_SIZE;
    int written;
    
    // Use first file as trigger
    FileInfo *trigger = &group->files[0];
    
    written = snprintf(ptr, remaining,
        "{\"type\":\"ALERT\",\"event\":\"DUPLICATE_DETECTED\","
        "\"trigger_file\":{"
        "\"filepath\":\"%s\","
        "\"filename\":\"%s\","
        "\"filehash\":\"%s\","
        "\"filesize\":%llu,"
        "\"last_mod\":\"%s\","
        "\"file_index\":%llu"
        "},\"duplicates\":[",
        trigger->filepath,
        trigger->filename,
        trigger->filehash,
        trigger->filesize,
        trigger->last_modified,
        trigger->file_index
    );
    
    ptr += written;
    remaining -= written;
    
    // Add all other files as duplicates
    for (int i = 1; i < group->file_count && remaining > 200; i++) {
        written = snprintf(ptr, remaining,
            "%s{\"filepath\":\"%s\","
            "\"filename\":\"%s\","
            "\"filesize\":%llu,"
            "\"last_mod\":\"%s\","
            "\"file_index\":%llu}",
            (i > 1) ? "," : "",
            group->files[i].filepath,
            group->files[i].filename,
            group->files[i].filesize,
            group->files[i].last_modified,
            group->files[i].file_index
        );
        ptr += written;
        remaining -= written;
    }
    
    written = snprintf(ptr, remaining,
        "],\"timestamp\":\"%s\"}\n",
        group->last_updated
    );
    
    send_message(message);
}

// Initialize pipe server
BOOL init_pipe_server(void) {
    if (g_pipe_server != NULL) {
        return FALSE;
    }
    
    if (!g_groups_initialized) {
        InitializeCriticalSection(&g_groups_lock);
        g_groups_initialized = TRUE;
    }
    
    g_pipe_server = (PipeServer*)malloc(sizeof(PipeServer));
    if (!g_pipe_server) {
        return FALSE;
    }
    
    memset(g_pipe_server, 0, sizeof(PipeServer));
    
    g_pipe_server->stop_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_pipe_server->stop_event) {
        free(g_pipe_server);
        g_pipe_server = NULL;
        return FALSE;
    }
    
    InitializeCriticalSection(&g_pipe_server->lock);
    g_pipe_server->running = TRUE;
    g_pipe_server->client_connected = FALSE;
    g_pipe_server->pipe_handle = INVALID_HANDLE_VALUE;
    
    g_pipe_server->thread_handle = CreateThread(
        NULL, 0, pipe_server_thread, NULL, 0, NULL
    );
    
    if (!g_pipe_server->thread_handle) {
        CloseHandle(g_pipe_server->stop_event);
        DeleteCriticalSection(&g_pipe_server->lock);
        free(g_pipe_server);
        g_pipe_server = NULL;
        return FALSE;
    }
    
    safe_printf("[IPC] Named Pipe server initialized on %s\n", PIPE_NAME);
    return TRUE;
}

// Shutdown pipe server
void shutdown_pipe_server(void) {
    if (!g_pipe_server) {
        return;
    }
    
    safe_printf("[IPC] Shutting down pipe server...\n");
    
    EnterCriticalSection(&g_pipe_server->lock);
    g_pipe_server->running = FALSE;
    SetEvent(g_pipe_server->stop_event);
    
    if (g_pipe_server->pipe_handle != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(g_pipe_server->pipe_handle);
        CloseHandle(g_pipe_server->pipe_handle);
    }
    LeaveCriticalSection(&g_pipe_server->lock);
    
    if (g_pipe_server->thread_handle) {
        WaitForSingleObject(g_pipe_server->thread_handle, 2000);
        CloseHandle(g_pipe_server->thread_handle);
    }
    
    CloseHandle(g_pipe_server->stop_event);
    DeleteCriticalSection(&g_pipe_server->lock);
    free(g_pipe_server);
    g_pipe_server = NULL;
    
    if (g_groups_initialized) {
        DeleteCriticalSection(&g_groups_lock);
        g_groups_initialized = FALSE;
        g_group_count = 0;
    }
    
    safe_printf("[IPC] Pipe server shut down\n");
}

// Send all duplicate groups to newly connected client
BOOL send_alert_history_to_client(void) {
    if (!g_pipe_server || !g_pipe_server->client_connected) {
        return FALSE;
    }
    
    EnterCriticalSection(&g_groups_lock);
    
    safe_printf("[IPC] Sending %d duplicate groups to client...\n", g_group_count);
    
    for (int i = 0; i < g_group_count; i++) {
        DuplicateGroup *group = &g_duplicate_groups[i];
        
        if (group->file_count >= 2 && !group->sent_to_client) {
            LeaveCriticalSection(&g_groups_lock);
            send_duplicate_group(group);
            Sleep(50);
            EnterCriticalSection(&g_groups_lock);
            
            group->sent_to_client = TRUE;
        }
    }
    
    LeaveCriticalSection(&g_groups_lock);
    
    safe_printf("[IPC] Finished sending duplicate groups\n");
    return TRUE;
}

// Pipe server thread
static DWORD WINAPI pipe_server_thread(LPVOID param) {
    (void)param;
    
    while (g_pipe_server->running) {
        HANDLE pipe = CreateNamedPipe(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,
            PIPE_BUFFER_SIZE,
            PIPE_BUFFER_SIZE,
            0,
            NULL
        );
        
        if (pipe == INVALID_HANDLE_VALUE) {
            safe_printf("[IPC] Failed to create named pipe: %lu\n", GetLastError());
            Sleep(1000);
            continue;
        }
        
        EnterCriticalSection(&g_pipe_server->lock);
        g_pipe_server->pipe_handle = pipe;
        LeaveCriticalSection(&g_pipe_server->lock);
        
        safe_printf("[IPC] Waiting for GUI client to connect...\n");
        
        OVERLAPPED overlapped = {0};
        overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        
        BOOL connected = ConnectNamedPipe(pipe, &overlapped);
        DWORD error = GetLastError();
        
        if (!connected && error == ERROR_IO_PENDING) {
            HANDLE handles[2] = { overlapped.hEvent, g_pipe_server->stop_event };
            DWORD wait_result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
            
            if (wait_result == WAIT_OBJECT_0) {
                connected = TRUE;
            } else {
                CancelIo(pipe);
                CloseHandle(overlapped.hEvent);
                CloseHandle(pipe);
                break;
            }
        } else if (!connected && error == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        
        CloseHandle(overlapped.hEvent);
        
        if (connected) {
            safe_printf("[IPC] GUI client connected\n");
            g_pipe_server->client_connected = TRUE;
            
            send_alert_history_to_client();
            
            handle_client_commands(pipe);
            
            g_pipe_server->client_connected = FALSE;
            safe_printf("[IPC] GUI client disconnected\n");
        }
        
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        
        EnterCriticalSection(&g_pipe_server->lock);
        g_pipe_server->pipe_handle = INVALID_HANDLE_VALUE;
        LeaveCriticalSection(&g_pipe_server->lock);
    }
    
    return 0;
}

// Handle incoming commands from GUI client
static void handle_client_commands(HANDLE pipe) {
    char buffer[PIPE_BUFFER_SIZE];
    DWORD bytes_read;
    
    while (g_pipe_server->running && g_pipe_server->client_connected) {
        BOOL success = ReadFile(pipe, buffer, sizeof(buffer) - 1, &bytes_read, NULL);
        
        if (!success || bytes_read == 0) {
            break;
        }
        
        buffer[bytes_read] = '\0';
        safe_printf("[IPC] Received command: %s\n", buffer);
        
        const char *response = "{\"type\":\"RESPONSE\",\"status\":\"OK\",\"message\":\"Command received\"}\n";
        DWORD bytes_written;
        WriteFile(pipe, response, strlen(response), &bytes_written, NULL);
    }
}

// Send message to connected GUI client
static BOOL send_message(const char *json_message) {
    if (!g_pipe_server || !g_pipe_server->client_connected) {
        return FALSE;
    }
    
    EnterCriticalSection(&g_pipe_server->lock);
    
    if (g_pipe_server->pipe_handle == INVALID_HANDLE_VALUE) {
        LeaveCriticalSection(&g_pipe_server->lock);
        return FALSE;
    }
    
    DWORD bytes_written;
    BOOL success = WriteFile(
        g_pipe_server->pipe_handle,
        json_message,
        strlen(json_message),
        &bytes_written,
        NULL
    );
    
    if (success) {
        FlushFileBuffers(g_pipe_server->pipe_handle);
    }
    
    LeaveCriticalSection(&g_pipe_server->lock);
    
    if (!success) {
        safe_printf("[IPC] Failed to send message: %lu\n", GetLastError());
        g_pipe_server->client_connected = FALSE;
    }
    
    return success;
}

// Send duplicate detected alert - now updates groups
BOOL send_alert_duplicate_detected(
    const FileInfo *trigger_file,
    const FileInfo *duplicates,
    int duplicate_count,
    const char *timestamp
) {
    if (!g_groups_initialized) {
        InitializeCriticalSection(&g_groups_lock);
        g_groups_initialized = TRUE;
    }
    
    EnterCriticalSection(&g_groups_lock);
    
    // Find or create group for this hash
    DuplicateGroup *group = find_or_create_group(trigger_file->filehash);
    
    if (!group) {
        LeaveCriticalSection(&g_groups_lock);
        safe_printf("[IPC] Failed to create duplicate group (limit reached)\n");
        return FALSE;
    }
    
    // For initial scan, clear the group first if it's being rebuilt
    // (This handles the case where find_duplicates sends all files at once)
    if (duplicate_count > 0 && group->file_count == 0) {
        // This is likely an initial scan group being created
        group->file_count = 0;
    }
    
    // Check if trigger file already exists in group
    BOOL trigger_exists = FALSE;
    for (int i = 0; i < group->file_count; i++) {
        if (strcmp(group->files[i].filepath, trigger_file->filepath) == 0) {
            trigger_exists = TRUE;
            break;
        }
    }
    
    // Add trigger file if not already in group
    if (!trigger_exists && group->file_count < (MAX_DUPLICATES + 1)) {
        memcpy(&group->files[group->file_count], trigger_file, sizeof(FileInfo));
        group->file_count++;
    }
    
    // Add duplicate files that aren't already in group
    for (int i = 0; i < duplicate_count && group->file_count < (MAX_DUPLICATES + 1); i++) {
        BOOL exists = FALSE;
        for (int j = 0; j < group->file_count; j++) {
            if (strcmp(group->files[j].filepath, duplicates[i].filepath) == 0) {
                exists = TRUE;
                break;
            }
        }
        
        if (!exists) {
            memcpy(&group->files[group->file_count], &duplicates[i], sizeof(FileInfo));
            group->file_count++;
        }
    }
    
    // Update timestamp
    strncpy(group->last_updated, timestamp, 31);
    group->last_updated[31] = '\0';
    
    // Only send if we have at least 2 files (actual duplicates)
    if (group->file_count < 2) {
        LeaveCriticalSection(&g_groups_lock);
        return TRUE;
    }
    
    // Track if this was already sent
    BOOL was_sent = group->sent_to_client;
    
    LeaveCriticalSection(&g_groups_lock);
    
    // Send the complete updated group only if client is connected
    if (g_pipe_server && g_pipe_server->client_connected) {
        send_duplicate_group(group);
        
        EnterCriticalSection(&g_groups_lock);
        group->sent_to_client = TRUE;
        LeaveCriticalSection(&g_groups_lock);
        
        if (was_sent) {
            safe_printf("[IPC] Updated duplicate group for hash %.8s... (now %d files)\n", 
                       trigger_file->filehash, group->file_count);
        } else {
            safe_printf("[IPC] Created new duplicate group for hash %.8s... (%d files)\n", 
                       trigger_file->filehash, group->file_count);
        }
    } else {
        // Mark as not sent so it will be sent when client connects
        EnterCriticalSection(&g_groups_lock);
        group->sent_to_client = FALSE;
        LeaveCriticalSection(&g_groups_lock);
    }
    
    return TRUE;
}

// Send scan complete alert
BOOL send_alert_scan_complete(int total_files, int duplicate_groups, const char *timestamp) {
    char message[1024];
    snprintf(message, sizeof(message),
        "{\"type\":\"ALERT\",\"event\":\"SCAN_COMPLETE\","
        "\"total_files\":%d,\"duplicate_groups\":%d,"
        "\"timestamp\":\"%s\"}\n",
        total_files, duplicate_groups, timestamp
    );
    
    return send_message(message);
}

// Send error alert
BOOL send_alert_error(const char *error_message, const char *timestamp) {
    char message[2048];
    snprintf(message, sizeof(message),
        "{\"type\":\"ALERT\",\"event\":\"ERROR\","
        "\"message\":\"%s\",\"timestamp\":\"%s\"}\n",
        error_message, timestamp
    );
    
    return send_message(message);
}