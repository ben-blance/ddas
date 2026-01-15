<img width="894" height="1301" alt="ddas drawio" src="https://github.com/user-attachments/assets/5cf93713-2abe-4e8f-8750-c66b00f6aac3" />

# DDAS - Duplicate Detection & Alert System

Real-time file duplicate detection with GUI alerts using Named Pipes IPC.

## Architecture

```
┌─────────────────────────────────────┐
│   Detection Engine (ddas_engine)    │
│   - Scans directory for files       │
│   - Computes BLAKE3 hashes          │
│   - Detects duplicates              │
│   - Monitors file system changes    │
│   - Named Pipe Server (IPC)         │
└──────────────┬──────────────────────┘
               │
        Named Pipe: \\.\pipe\ddas_ipc
               │
┌──────────────┴──────────────────────┐
│      GUI Tray App (ddas_gui)        │
│   - System tray icon                │
│   - Toast notifications             │
│   - Duplicate file report window    │
│   - File management (delete, open)  │
│   - Named Pipe Client (IPC)         │
└─────────────────────────────────────┘
```

## Project Structure

```
DDAS/
├── src/
│   ├── main.c           # Engine entry point
│   ├── hash_table.c     # Hash table with IPC integration
│   ├── file_ops.c       # File operations
│   ├── scanner.c        # Directory scanner
│   ├── monitor.c        # File system monitor
│   ├── empty_files.c    # Empty file tracking
│   ├── utils.c          # Utilities
│   └── ipc_pipe.c       # Named Pipe IPC (NEW)
├── gui/
│   └── gui_tray.c       # Tray GUI application (NEW)
├── include/
│   ├── ipc_pipe.h       # IPC header (NEW)
│   └── [other headers]
├── blake/               # BLAKE3 implementation
├── build/               # Build output
└── Makefile
```

## Setup Instructions

### Step 1: Create GUI Directory

```cmd
mkdir gui
```

### Step 2: Add New Files

1. **Create `include/ipc_pipe.h`** - Copy the IPC header content
2. **Create `src/ipc_pipe.c`** - Copy the IPC implementation
3. **Create `gui/gui_tray.c`** - Copy the GUI application
4. **Update `src/hash_table.c`** - Add IPC integration
5. **Update `src/main.c`** - Initialize IPC server
6. **Update `Makefile`** - Add new build targets

### Step 3: Build

```cmd
make all
```

This builds:
- `build/ddas_engine.exe` - Detection engine
- `build/ddas_gui.exe` - GUI tray application

### Step 4: Install (Optional)

```cmd
make install
```

Copies executables to project root for easier access.

## Usage

### Running the System

**Option 1: Manual Start (Development)**

1. Start the detection engine first:
```cmd
ddas_engine.exe "C:\Users\Sahil\Desktop\test" --watch
```

2. Start the GUI in a separate terminal:
```cmd
ddas_gui.exe
```

**Option 2: Quick Start Script**

Create `start_ddas.bat`:
```batch
@echo off
echo Starting DDAS...
start /B ddas_engine.exe "C:\Users\Sahil\Desktop\test" --watch
timeout /t 2 /nobreak >nul
start ddas_gui.exe
echo DDAS Started!
```

### Command Line Options

**Engine:**
```cmd
ddas_engine.exe <directory>          # Scan once, then exit
ddas_engine.exe <directory> --watch  # Scan and continue monitoring
```

**GUI:**
```cmd
ddas_gui.exe  # No arguments needed
```

### What Happens

1. **Engine starts** and creates Named Pipe server on `\\.\pipe\ddas_ipc`
2. **Engine scans** the specified directory
3. **GUI connects** to the Named Pipe
4. **When duplicate found**:
   - Engine sends JSON alert via pipe
   - GUI shows toast notification
   - User clicks notification
   - Report window opens with details
5. **User actions**:
   - View all duplicate files in a group
   - Open file location in Explorer
   - Delete files (moves to Recycle Bin)

## JSON Message Format

### ALERT: Duplicate Detected
```json
{
  "type": "ALERT",
  "event": "DUPLICATE_DETECTED",
  "trigger_file": {
    "filepath": "C:\\Users\\sahil\\Downloads\\file.txt",
    "filename": "file.txt",
    "filehash": "a3f4...64hex",
    "filesize": 12345,
    "last_mod": "2026-01-10T12:34:56Z",
    "file_index": 987654321
  },
  "duplicates": [
    {
      "filepath": "D:\\Backup\\file_copy.txt",
      "filename": "file_copy.txt",
      "filesize": 12345,
      "last_mod": "2026-01-09T09:00:00Z",
      "file_index": 111222333
    }
  ],
  "timestamp": "2026-01-10T12:34:57Z"
}
```

### ALERT: Scan Complete
```json
{
  "type": "ALERT",
  "event": "SCAN_COMPLETE",
  "total_files": 150,
  "duplicate_groups": 5,
  "timestamp": "2026-01-10T12:35:00Z"
}
```

## Features

### Detection Engine
- ✅ BLAKE3 cryptographic hashing
- ✅ Recursive directory scanning
- ✅ Real-time file system monitoring
- ✅ Empty file detection
- ✅ Named Pipe IPC server
- ✅ Thread-safe hash table
- ✅ Pattern-based file ignoring

### GUI Application
- ✅ System tray icon
- ✅ Toast notifications
- ✅ Duplicate file report window
- ✅ File list with details (path, size, modified date)
- ✅ Open file location in Explorer
- ✅ Delete files (Recycle Bin)
- ✅ Named Pipe IPC client
- ✅ Auto-reconnect on disconnect

## Testing

### Test Scenario 1: Initial Scan with Duplicates

1. Create test directory with duplicates:
```cmd
mkdir C:\test_duplicates
echo Hello World > C:\test_duplicates\file1.txt
copy C:\test_duplicates\file1.txt C:\test_duplicates\file2.txt
```

2. Start engine and GUI
3. Observe:
   - Console shows scan progress
   - GUI notification appears
   - Report window shows duplicate group

### Test Scenario 2: Real-time Detection

1. With engine and GUI running
2. Copy a file into monitored directory
3. Observe instant notification

### Test Scenario 3: Reconnection

1. Start engine first
2. Start GUI → connects successfully
3. Close GUI
4. Restart GUI → reconnects automatically

## Troubleshooting

### GUI Can't Connect
- **Symptom**: No notifications appearing
- **Fix**: Ensure engine is running first
- **Check**: Engine console should show `[IPC] Waiting for GUI client to connect...`

### Pipe Already in Use
- **Symptom**: Engine fails with "pipe in use"
- **Fix**: Kill existing engine process:
```cmd
taskkill /F /IM ddas_engine.exe
```

### No Tray Icon
- **Symptom**: GUI starts but no tray icon
- **Fix**: Check Windows notification area settings
- Enable "Always show all icons in notification area"

## Next Steps / Future Enhancements

### Phase 2: Command Support
- Implement DELETE_FILES command
- Add quarantine directory
- File deletion confirmation

### Phase 3: Database
- SQLite integration for persistent state
- File history tracking
- Duplicate group management

### Phase 4: Advanced GUI
- Settings window
- Scan progress indicator
- Multiple monitor directory support
- Filtering and search in report window

### Phase 5: Service Mode
- Run engine as Windows service
- Auto-start with system
- Service control from GUI

## Development Notes

### Building in Debug Mode
```cmd
make clean
set CFLAGS=-Wall -Wextra -g -Iinclude -Iblake
make all
```

### IPC Message Flow
1. Engine detects duplicate in `check_for_duplicate()`
2. Calls `send_alert_duplicate_detected()`
3. Builds JSON message
4. Writes to named pipe via `send_message()`
5. GUI's `PipeReaderThread()` receives message
6. Parses JSON with `ParseAlertJSON()`
7. Shows notification with `ShowTrayNotification()`

### Adding New Message Types
1. Add enum in `ipc_pipe.h`
2. Create sender function in `ipc_pipe.c`
3. Add parser case in `gui_tray.c::PipeReaderThread()`

## License

MIT License - Feel free to modify and distribute.

## Author

Developed for real-time duplicate file detection and user notification.