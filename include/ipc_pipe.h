#ifndef IPC_PIPE_H
#define IPC_PIPE_H

#include <windows.h>
#include <stdint.h>

#define PIPE_NAME "\\\\.\\pipe\\ddas_ipc"
#define PIPE_BUFFER_SIZE 65536
#define MAX_MESSAGE_SIZE 32768
#define MAX_DUPLICATES 100
#define MAX_HISTORY_ALERTS 100

// Message types
typedef enum {
    MSG_TYPE_ALERT = 1,
    MSG_TYPE_COMMAND = 2,
    MSG_TYPE_RESPONSE = 3,
    MSG_TYPE_PING = 4,
    MSG_TYPE_PONG = 5
} MessageType;

// Alert event types
typedef enum {
    ALERT_DUPLICATE_DETECTED = 1,
    ALERT_DUPLICATE_GROUP_UPDATED = 2,
    ALERT_SCAN_COMPLETE = 3,
    ALERT_ERROR = 4
} AlertEvent;

// Command action types
typedef enum {
    CMD_DELETE_FILES = 1,
    CMD_QUARANTINE_FILES = 2,
    CMD_GET_STATUS = 3,
    CMD_STOP_MONITORING = 4
} CommandAction;

// Delete modes
typedef enum {
    DELETE_MODE_QUARANTINE = 1,
    DELETE_MODE_PERMANENT = 2
} DeleteMode;

// File info structure
typedef struct {
    char filepath[MAX_PATH];
    char filename[MAX_PATH];
    char filehash[65];  // BLAKE3 hash (32 bytes = 64 hex chars + null)
    uint64_t filesize;
    char last_modified[32];  // ISO 8601 format
    uint64_t file_index;     // Unique file identifier
} FileInfo;

// Duplicate alert structure (for history storage)
typedef struct {
    FileInfo trigger_file;
    FileInfo duplicates[MAX_DUPLICATES];
    int duplicate_count;
    char timestamp[32];
} DuplicateAlert;

// IPC message structures
typedef struct {
    MessageType type;
    uint32_t payload_size;
    char payload[MAX_MESSAGE_SIZE];
} IPCMessage;

// Pipe server structure
typedef struct {
    HANDLE pipe_handle;
    HANDLE thread_handle;
    HANDLE stop_event;
    CRITICAL_SECTION lock;
    volatile BOOL running;
    volatile BOOL client_connected;
} PipeServer;

// Global pipe server instance
extern PipeServer *g_pipe_server;

// Initialize pipe server
BOOL init_pipe_server(void);

// Shutdown pipe server
void shutdown_pipe_server(void);

// Send alert message (JSON format)
BOOL send_alert_duplicate_detected(
    const FileInfo *trigger_file,
    const FileInfo *duplicates,
    int duplicate_count,
    const char *timestamp
);

// Send scan complete alert
BOOL send_alert_scan_complete(int total_files, int duplicate_groups, const char *timestamp);

// Send error alert
BOOL send_alert_error(const char *error_message, const char *timestamp);

// Send all stored alerts to newly connected client
BOOL send_alert_history_to_client(void);

// Helper: Get current ISO 8601 timestamp
void get_iso8601_timestamp(char *buffer, size_t buffer_size);

// Helper: Get file modified time as ISO 8601
void get_file_modified_time(const char *filepath, char *buffer, size_t buffer_size);

// Helper: Generate unique file index (based on creation time and inode)
uint64_t generate_file_index(const char *filepath);

#endif // IPC_PIPE_H