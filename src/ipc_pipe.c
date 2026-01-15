#include "ipc_pipe.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

PipeServer *g_pipe_server = NULL;

// Forward declarations
static DWORD WINAPI pipe_server_thread(LPVOID param);
static BOOL send_message(const char *json_message);
static void handle_client_commands(HANDLE pipe);

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
    HANDLE hFile = CreateFile(filepath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
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

// Initialize pipe server
BOOL init_pipe_server(void) {
    if (g_pipe_server != NULL) {
        return FALSE; // Already initialized
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
    
    // Create pipe server thread
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
    
    // Wait for thread to finish
    if (g_pipe_server->thread_handle) {
        WaitForSingleObject(g_pipe_server->thread_handle, 2000);
        CloseHandle(g_pipe_server->thread_handle);
    }
    
    CloseHandle(g_pipe_server->stop_event);
    DeleteCriticalSection(&g_pipe_server->lock);
    free(g_pipe_server);
    g_pipe_server = NULL;
    
    safe_printf("[IPC] Pipe server shut down\n");
}

// Pipe server thread - handles client connections
static DWORD WINAPI pipe_server_thread(LPVOID param) {
    (void)param;
    
    while (g_pipe_server->running) {
        // Create named pipe
        HANDLE pipe = CreateNamedPipe(
            PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            1,  // Max instances (only 1 GUI client at a time)
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
        
        // Wait for client connection with timeout
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
                // Stop event signaled
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
            
            // Handle client commands in this thread
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
            break; // Client disconnected
        }
        
        buffer[bytes_read] = '\0';
        
        // Parse JSON command (simple parsing for now)
        safe_printf("[IPC] Received command: %s\n", buffer);
        
        // TODO: Implement command handling (DELETE_FILES, etc.)
        // For now, just acknowledge receipt
        const char *response = "{\"type\":\"RESPONSE\",\"status\":\"OK\",\"message\":\"Command received\"}\n";
        DWORD bytes_written;
        WriteFile(pipe, response, strlen(response), &bytes_written, NULL);
    }
}

// Send message to connected GUI client
static BOOL send_message(const char *json_message) {
    if (!g_pipe_server || !g_pipe_server->client_connected) {
        return FALSE; // No client connected
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
    
    FlushFileBuffers(g_pipe_server->pipe_handle);
    
    LeaveCriticalSection(&g_pipe_server->lock);
    
    if (!success) {
        safe_printf("[IPC] Failed to send message: %lu\n", GetLastError());
        g_pipe_server->client_connected = FALSE;
    }
    
    return success;
}

// Send duplicate detected alert
BOOL send_alert_duplicate_detected(
    const FileInfo *trigger_file,
    const FileInfo *duplicates,
    int duplicate_count,
    const char *timestamp
) {
    char message[MAX_MESSAGE_SIZE];
    char *ptr = message;
    int remaining = MAX_MESSAGE_SIZE;
    int written;
    
    // Build JSON message
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
        trigger_file->filepath,
        trigger_file->filename,
        trigger_file->filehash,
        trigger_file->filesize,
        trigger_file->last_modified,
        trigger_file->file_index
    );
    
    ptr += written;
    remaining -= written;
    
    // Add duplicate files
    for (int i = 0; i < duplicate_count && remaining > 200; i++) {
        written = snprintf(ptr, remaining,
            "%s{\"filepath\":\"%s\","
            "\"filename\":\"%s\","
            "\"filesize\":%llu,"
            "\"last_mod\":\"%s\","
            "\"file_index\":%llu}",
            (i > 0) ? "," : "",
            duplicates[i].filepath,
            duplicates[i].filename,
            duplicates[i].filesize,
            duplicates[i].last_modified,
            duplicates[i].file_index
        );
        ptr += written;
        remaining -= written;
    }
    
    written = snprintf(ptr, remaining,
        "],\"timestamp\":\"%s\"}\n",
        timestamp
    );
    
    return send_message(message);
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