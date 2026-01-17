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
# DDAS Quick Start Guide

## 🚀 Getting Started (3 Steps)

### Step 1: Build Everything
```cmd
mingw32-make
```

You should see:
```
Compiling src/main.c...
Compiling src/ipc_pipe.c...
...
Detection Engine built successfully!
GUI Application built successfully!

========================================
DDAS Build Complete!
========================================
Engine: ddas_engine.exe
GUI:    ddas_gui.exe
```

### Step 2: Run the System
```cmd
mingw32-make run-both
```

This will:
1. ✅ Start the detection engine (minimized console window)
2. ✅ Wait 2 seconds for engine to initialize
3. ✅ Start the GUI (system tray icon appears)

### Step 3: Test It!

**Create a duplicate file:**
```cmd
cd C:\Users\Sahil\Documents\testfolder
echo Hello World > file1.txt
copy file1.txt file2.txt
```

**What happens:**
1. 🔍 Engine detects the duplicate
2. 💬 Toast notification appears: "Duplicate found: file2.txt"
3. 🖱️ Click the notification
4. 📊 Report window opens showing both files

---

## 🎯 Common Commands

| Command | What it does |
|---------|--------------|
| `mingw32-make` | Build everything |
| `mingw32-make run-both` | Start engine + GUI |
| `mingw32-make stop` | Stop everything |
| `mingw32-make clean` | Clean build files |
| `mingw32-make help` | Show all commands |

---

## 🛠️ Individual Components

### Run Engine Only (for testing)
```cmd
mingw32-make run-engine
```
- Console stays open
- Shows all file operations
- Press Ctrl+C to stop

### Run GUI Only (engine must be running first!)
```cmd
# In terminal 1:
mingw32-make run-engine

# In terminal 2:
mingw32-make run-gui
```

---

## 🔧 Troubleshooting

### Problem: "GUI not connecting"
**Symptoms:**
- No notifications appearing
- Tray icon present but inactive

**Solution:**
1. Make sure engine is running first
2. Check engine console for: `[IPC] GUI client connected`
3. Restart in correct order: engine → GUI

```cmd
mingw32-make stop
mingw32-make run-both
```

### Problem: "Pipe already in use"
**Symptoms:**
- Engine fails to start
- Error about pipe creation

**Solution:**
Kill any existing processes:
```cmd
mingw32-make stop
```

Or manually:
```cmd
taskkill /F /IM ddas_engine.exe
taskkill /F /IM ddas_gui.exe
```

### Problem: No tray icon visible
**Solution:**
- Check Windows notification area (bottom-right)
- Click the up arrow (^) to show hidden icons
- Right-click taskbar → Taskbar settings → Turn on all system icons

### Problem: Build errors
**Check:**
1. All files are in place:
   ```
   src/ipc_pipe.c
   include/ipc_pipe.h
   gui/gui_tray.c
   ```

2. Updated `hash_table.c` with IPC integration

3. Clean and rebuild:
   ```cmd
   mingw32-make clean
   mingw32-make
   ```

---

## 📁 Changing Monitored Directory

Edit the Makefile, find these lines:

```makefile
# Line ~97 (in run-engine target)
@.\$(ENGINE_TARGET) C:\Users\Sahil\Documents\testfolder --watch

# Line ~117 (in create-start-script target)
@echo start "DDAS Engine" /MIN $(ENGINE_TARGET) "C:\Users\Sahil\Documents\testfolder" --watch >> start_ddas.bat
```

Change `C:\Users\Sahil\Documents\testfolder` to your desired path.

---

## 🎨 Using the GUI

### Tray Icon Menu
**Right-click the tray icon:**
- **Show Last Alert** → Opens report window for last duplicate
- **About** → Shows version info
- **Exit** → Closes GUI (engine keeps running)

### Report Window
**Displays:**
- Trigger file (the new file that caused the alert)
- All duplicate files in the group
- File details: size, modified date

**Actions:**
- **Open File Location** → Opens Explorer at file location
- **Delete Selected** → Moves file to Recycle Bin (safe delete)
- **Close** → Closes window

---

## 📊 Console Output Examples

### Engine Starting:
```
=== File Duplicate Detector with Real-time Monitoring ===
Directory: C:\Users\Sahil\Documents\testfolder
Mode: Scan + Watch

[IPC] Named Pipe server initialized on \\.\pipe\ddas_ipc
[IPC] Waiting for GUI client to connect...

=== File System Monitor Started ===
Watching for changes during scan and after...

[SCAN] C:\Users\Sahil\Documents\testfolder\file1.txt
[SCAN] C:\Users\Sahil\Documents\testfolder\file2.txt

[DUPLICATE DETECTED]
New file: C:\Users\Sahil\Documents\testfolder\file2.txt
Matches existing files:
 - C:\Users\Sahil\Documents\testfolder\file1.txt
```

### GUI Connecting:
```
[IPC] GUI client connected
```

### Duplicate Alert Sent:
```
[DUPLICATE DETECTED]
New file: C:\...\file2.txt
Matches existing files:
 - C:\...\file1.txt
```

---

## 🎯 Test Scenarios

### Scenario 1: Copy Files
```cmd
cd C:\Users\Sahil\Documents\testfolder
echo Test > original.txt
copy original.txt copy1.txt
copy original.txt copy2.txt
```
**Expected:** 2 alerts (for copy1.txt and copy2.txt)

### Scenario 2: Create Directory with Duplicates
```cmd
mkdir subdir
copy original.txt subdir\duplicate.txt
```
**Expected:** 1 alert for subdir\duplicate.txt

### Scenario 3: Modify File
```cmd
echo Modified >> original.txt
```
**Expected:** File reprocessed, old duplicates removed, new hash added

---

## 🔄 Daily Workflow

### Morning: Start System
```cmd
mingw32-make run-both
```

### Work: Monitor happens automatically
- Save files as normal
- Get alerts when duplicates appear
- Review duplicates periodically

### Evening: Stop System
```cmd
mingw32-make stop
```

Or: Right-click tray icon → Exit (stops GUI, engine keeps running)

---

## 📈 Next Steps

1. **Test the basic functionality** with the commands above
2. **Watch the console** to understand the detection flow
3. **Try the GUI features** (notifications, report window, delete)
4. **Experiment with different file scenarios**

Once you're comfortable, you can extend the system with:
- Database integration (SQLite)
- DELETE_FILES command implementation
- Quarantine directory
- Service mode for auto-start

---

## 💡 Pro Tips

1. **Keep console window visible** during development to see IPC messages
2. **Test GUI reconnection** by closing and reopening it
3. **Use different test directories** to avoid confusion
4. **Check Windows Event Viewer** if pipe issues occur
5. **Monitor with Process Explorer** to see pipe connections

---

## 📞 Quick Reference Card

```
BUILD:    mingw32-make
RUN:      mingw32-make run-both
STOP:     mingw32-make stop
CLEAN:    mingw32-make clean
HELP:     mingw32-make help
```

**File Locations:**
- Engine: `ddas_engine.exe`
- GUI: `ddas_gui.exe`
- Pipe: `\\.\pipe\ddas_ipc`
- Test Dir: `C:\Users\Sahil\Documents\testfolder`

---


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

Happy duplicate detecting! 🎉
