# DDAS IPC Message Format Reference

## Overview

All messages are JSON objects sent as newline-delimited text over the Named Pipe `\\.\pipe\ddas_ipc`.

## Message Types

### 1. ALERT - Duplicate Detected

**Direction**: Engine → GUI  
**When**: A new file is added/detected that matches an existing file's hash

```json
{
  "type": "ALERT",
  "event": "DUPLICATE_DETECTED",
  "trigger_file": {
    "filepath": "C:\\Users\\sahil\\Downloads\\document.pdf",
    "filename": "document.pdf",
    "filehash": "a3f41c7b8e9d4f5a6b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a",
    "filesize": 1048576,
    "last_mod": "2026-01-14T10:30:45.123Z",
    "file_index": 987654321098765
  },
  "duplicates": [
    {
      "filepath": "D:\\Backup\\old_document.pdf",
      "filename": "old_document.pdf",
      "filesize": 1048576,
      "last_mod": "2025-12-01T08:15:00.000Z",
      "file_index": 123456789012345
    },
    {
      "filepath": "E:\\Archive\\doc_copy.pdf",
      "filename": "doc_copy.pdf",
      "filesize": 1048576,
      "last_mod": "2025-11-15T14:22:30.500Z",
      "file_index": 456789012345678
    }
  ],
  "timestamp": "2026-01-14T10:30:46.234Z"
}
```

**Fields**:
- `type`: Always "ALERT"
- `event`: "DUPLICATE_DETECTED"
- `trigger_file`: The newly detected file that triggered the alert
  - `filepath`: Full absolute path (Windows format with escaped backslashes)
  - `filename`: Just the filename extracted from path
  - `filehash`: 64-character hex string (BLAKE3 hash)
  - `filesize`: Size in bytes (unsigned 64-bit integer)
  - `last_mod`: ISO 8601 timestamp with milliseconds
  - `file_index`: Unique file identifier based on volume serial + file index
- `duplicates`: Array of existing files with same hash
  - Each entry has same structure as trigger_file (except no `filehash` field)
- `timestamp`: When the alert was generated (ISO 8601)

---

### 2. ALERT - Scan Complete

**Direction**: Engine → GUI  
**When**: Initial directory scan has finished

```json
{
  "type": "ALERT",
  "event": "SCAN_COMPLETE",
  "total_files": 2847,
  "duplicate_groups": 42,
  "timestamp": "2026-01-14T10:32:15.678Z"
}
```

**Fields**:
- `type`: Always "ALERT"
- `event`: "SCAN_COMPLETE"
- `total_files`: Total number of duplicate files found (not unique files)
- `duplicate_groups`: Number of unique duplicate groups
- `timestamp`: When scan completed

---

### 3. ALERT - Error

**Direction**: Engine → GUI  
**When**: An error occurs during operation

```json
{
  "type": "ALERT",
  "event": "ERROR",
  "message": "Failed to access directory: C:\\Protected\\Folder",
  "timestamp": "2026-01-14T10:35:22.100Z"
}
```

**Fields**:
- `type`: Always "ALERT"
- `event`: "ERROR"
- `message`: Human-readable error description
- `timestamp`: When error occurred

---

### 4. COMMAND - Delete Files (Future)

**Direction**: GUI → Engine  
**When**: User wants to delete selected duplicate files

```json
{
  "type": "COMMAND",
  "action": "DELETE_FILES",
  "request_id": "req-1a2b3c4d-5e6f-7a8b-9c0d-1e2f3a4b5c6d",
  "mode": "quarantine",
  "files": [
    {
      "filepath": "D:\\Backup\\old_document.pdf",
      "file_index": 123456789012345,
      "expected_hash": "a3f41c7b8e9d4f5a6b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a"
    },
    {
      "filepath": "E:\\Archive\\doc_copy.pdf",
      "file_index": 456789012345678,
      "expected_hash": "a3f41c7b8e9d4f5a6b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a"
    }
  ]
}
```

**Fields**:
- `type`: Always "COMMAND"
- `action`: "DELETE_FILES"
- `request_id`: UUID for idempotency and response matching
- `mode`: "quarantine" (move to quarantine folder) or "permanent" (direct delete)
- `files`: Array of files to delete
  - `filepath`: Full path to file
  - `file_index`: File identifier (for verification)
  - `expected_hash`: Hash for safety verification before deletion

---

### 5. RESPONSE - Command Result (Future)

**Direction**: Engine → GUI  
**When**: Engine completes a command from GUI

```json
{
  "type": "RESPONSE",
  "request_id": "req-1a2b3c4d-5e6f-7a8b-9c0d-1e2f3a4b5c6d",
  "status": "OK",
  "results": [
    {
      "filepath": "D:\\Backup\\old_document.pdf",
      "result": "QUARANTINED",
      "quarantine_path": "C:\\DDAS\\Quarantine\\20260114_123456_old_document.pdf"
    },
    {
      "filepath": "E:\\Archive\\doc_copy.pdf",
      "result": "FAILED",
      "error": "ACCESS_DENIED",
      "error_message": "Insufficient permissions to delete file"
    }
  ],
  "timestamp": "2026-01-14T10:36:10.500Z"
}
```

**Fields**:
- `type`: Always "RESPONSE"
- `request_id`: Matches the command's request_id
- `status`: "OK" (some operations succeeded) or "ERROR" (all failed)
- `results`: Array with per-file results
  - `filepath`: The file that was processed
  - `result`: "QUARANTINED", "DELETED", "FAILED", or "SKIPPED"
  - `quarantine_path`: (if quarantined) New location of file
  - `error`: Error code if failed
  - `error_message`: Human-readable error description
- `timestamp`: When response was generated

---

## Data Types

| Field | Type | Format | Example |
|-------|------|--------|---------|
| `filepath` | string | Windows path with escaped backslashes | `"C:\\Users\\name\\file.txt"` |
| `filename` | string | Just filename | `"file.txt"` |
| `filehash` | string | 64 hex chars (BLAKE3) | `"a3f4...3f4a"` |
| `filesize` | number | Unsigned 64-bit integer | `1048576` |
| `last_mod` | string | ISO 8601 with milliseconds | `"2026-01-14T10:30:45.123Z"` |
| `file_index` | number | Unsigned 64-bit integer | `987654321098765` |
| `timestamp` | string | ISO 8601 with milliseconds | `"2026-01-14T10:30:46.234Z"` |
| `request_id` | string | UUID v4 format | `"req-1a2b3c4d-5e6f-..."` |

---

## Error Codes

| Code | Description |
|------|-------------|
| `ACCESS_DENIED` | Insufficient permissions |
| `FILE_NOT_FOUND` | File no longer exists |
| `HASH_MISMATCH` | File