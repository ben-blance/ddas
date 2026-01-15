# Modular Makefile for DDAS - Duplicate Detection & Alert System
CC = gcc
CFLAGS = -O3 -std=c11 -Iinclude -D_WIN32_WINNT=0x0600 -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512
GUI_CFLAGS = -O2 -std=c11 -D_WIN32_WINNT=0x0600
BLAKE_DIR = blake
SRC_DIR = src
GUI_DIR = gui
BUILD_DIR = build

# Target executables
ENGINE_TARGET = ddas_engine.exe
GUI_TARGET = ddas_gui.exe

# Engine source files
ENGINE_MAIN_SRCS = $(SRC_DIR)/main.c \
                   $(SRC_DIR)/utils.c \
                   $(SRC_DIR)/hash_table.c \
                   $(SRC_DIR)/empty_files.c \
                   $(SRC_DIR)/file_ops.c \
                   $(SRC_DIR)/scanner.c \
                   $(SRC_DIR)/monitor.c \
                   $(SRC_DIR)/ipc_pipe.c

BLAKE_SRCS = $(BLAKE_DIR)/blake3.c \
             $(BLAKE_DIR)/blake3_dispatch.c \
             $(BLAKE_DIR)/blake3_portable.c

ENGINE_SRCS = $(ENGINE_MAIN_SRCS) $(BLAKE_SRCS)
ENGINE_OBJS = $(ENGINE_SRCS:.c=.o)

# GUI source files
GUI_SRCS = $(GUI_DIR)/gui_tray.c
GUI_OBJS = $(GUI_SRCS:.c=.o)

# GUI libraries
GUI_LIBS = -mwindows -lshell32 -lcomctl32 -luser32 -lgdi32

.PHONY: all clean engine gui run-engine run-gui run-both test help structure install stop

# Default target - build both engine and GUI
all: engine gui
	@echo.
	@echo ========================================
	@echo DDAS Build Complete!
	@echo ========================================
	@echo Engine: $(ENGINE_TARGET)
	@echo GUI:    $(GUI_TARGET)
	@echo.
	@echo To run: mingw32-make run-both
	@echo.

# Build engine only
engine: $(ENGINE_TARGET)

$(ENGINE_TARGET): $(ENGINE_OBJS)
	@echo.
	@echo Linking $(ENGINE_TARGET)...
	$(CC) $(CFLAGS) -o $@ $^
	@echo Detection Engine built successfully!

# Build GUI only
gui: $(GUI_TARGET)

$(GUI_TARGET): $(GUI_OBJS)
	@echo.
	@echo Linking $(GUI_TARGET)...
	$(CC) $(GUI_CFLAGS) -o $@ $^ $(GUI_LIBS)
	@echo GUI Application built successfully!

# Compile engine source files
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c
	@echo Compiling $<...
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile BLAKE3 files
$(BLAKE_DIR)/%.o: $(BLAKE_DIR)/%.c
	@echo Compiling $<...
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile GUI files
$(GUI_DIR)/%.o: $(GUI_DIR)/%.c
	@echo Compiling $<...
	$(CC) $(GUI_CFLAGS) -c -o $@ $<

# Clean build artifacts
clean:
	@echo Cleaning build artifacts...
	@if exist $(ENGINE_TARGET) del /Q $(ENGINE_TARGET) 2>nul
	@if exist $(GUI_TARGET) del /Q $(GUI_TARGET) 2>nul
	@if exist $(SRC_DIR)\*.o del /Q $(SRC_DIR)\*.o 2>nul
	@if exist $(BLAKE_DIR)\*.o del /Q $(BLAKE_DIR)\*.o 2>nul
	@if exist $(GUI_DIR)\*.o del /Q $(GUI_DIR)\*.o 2>nul
	@if exist start_ddas.bat del /Q start_ddas.bat 2>nul
	@if exist stop_ddas.bat del /Q stop_ddas.bat 2>nul
	@echo Clean complete!

# Run engine only (scan + watch mode)
run-engine: $(ENGINE_TARGET)
	@echo.
	@echo ========================================
	@echo Starting Detection Engine
	@echo ========================================
	@echo Monitoring: C:\Users\Sahil\Documents\testfolder
	@echo Mode: Watch (Ctrl+C to stop)
	@echo.
	@.\$(ENGINE_TARGET) C:\Users\Sahil\Documents\testfolder --watch

# Run GUI only
run-gui: $(GUI_TARGET)
	@echo.
	@echo ========================================
	@echo Starting GUI Application
	@echo ========================================
	@echo Check system tray for icon
	@echo.
	@start $(GUI_TARGET)

# Run both engine and GUI together
run-both: all create-start-script
	@echo.
	@echo ========================================
	@echo Starting DDAS System
	@echo ========================================
	@.\start_ddas.bat
	@echo.

# Test mode (scan only, no watch)
test: $(ENGINE_TARGET)
	@echo.
	@echo ========================================
	@echo Running Test Scan (no watch mode)
	@echo ========================================
	@.\$(ENGINE_TARGET) C:\Users\Sahil\Documents\testfolder

# Create startup script
create-start-script:
	@echo @echo off > start_ddas.bat
	@echo echo ======================================== >> start_ddas.bat
	@echo echo   DDAS - Duplicate Detection System >> start_ddas.bat
	@echo echo ======================================== >> start_ddas.bat
	@echo echo. >> start_ddas.bat
	@echo echo Starting detection engine... >> start_ddas.bat
	@echo start "DDAS Engine" /MIN $(ENGINE_TARGET) "C:\Users\Sahil\Documents\testfolder" --watch >> start_ddas.bat
	@echo timeout /t 2 /nobreak ^>nul >> start_ddas.bat
	@echo echo Starting GUI application... >> start_ddas.bat
	@echo start "DDAS GUI" $(GUI_TARGET) >> start_ddas.bat
	@echo echo. >> start_ddas.bat
	@echo echo ======================================== >> start_ddas.bat
	@echo echo DDAS is now running! >> start_ddas.bat
	@echo echo - Engine: Monitoring folder >> start_ddas.bat
	@echo echo - GUI: Check system tray >> start_ddas.bat
	@echo echo. >> start_ddas.bat
	@echo echo To stop: mingw32-make stop >> start_ddas.bat
	@echo echo ======================================== >> start_ddas.bat
	@echo pause >> start_ddas.bat

# Create stop script
create-stop-script:
	@echo @echo off > stop_ddas.bat
	@echo echo ======================================== >> stop_ddas.bat
	@echo echo   Stopping DDAS >> stop_ddas.bat
	@echo echo ======================================== >> stop_ddas.bat
	@echo echo. >> stop_ddas.bat
	@echo echo Stopping GUI application... >> stop_ddas.bat
	@echo taskkill /F /IM $(GUI_TARGET) 2^>nul >> stop_ddas.bat
	@echo echo. >> stop_ddas.bat
	@echo echo Stopping detection engine... >> stop_ddas.bat
	@echo taskkill /F /IM $(ENGINE_TARGET) 2^>nul >> stop_ddas.bat
	@echo echo. >> stop_ddas.bat
	@echo echo ======================================== >> stop_ddas.bat
	@echo echo DDAS has been stopped >> stop_ddas.bat
	@echo echo ======================================== >> stop_ddas.bat
	@echo pause >> stop_ddas.bat

# Stop running DDAS
stop: create-stop-script
	@.\stop_ddas.bat

# Install (optional - copy to build directory)
install: all
	@if not exist $(BUILD_DIR) mkdir $(BUILD_DIR)
	@copy $(ENGINE_TARGET) $(BUILD_DIR)\ >nul
	@copy $(GUI_TARGET) $(BUILD_DIR)\ >nul
	@echo.
	@echo ========================================
	@echo Installed to $(BUILD_DIR)
	@echo ========================================

# Show project structure
structure:
	@echo.
	@echo ========================================
	@echo === DDAS Project Structure ===
	@echo ========================================
	@echo.
	@echo include/
	@echo   - utils.h          (Thread-safe utilities)
	@echo   - hash_table.h     (Hash table for duplicates)
	@echo   - empty_files.h    (Empty file tracking)
	@echo   - file_ops.h       (File operations ^& hashing)
	@echo   - scanner.h        (Directory scanning)
	@echo   - monitor.h        (File system monitoring)
	@echo   - ipc_pipe.h       (Named Pipe IPC)
	@echo   - blake3.h         (BLAKE3 hash library)
	@echo.
	@echo src/
	@echo   - main.c           (Engine entry point)
	@echo   - utils.c          (Utilities implementation)
	@echo   - hash_table.c     (Hash table with IPC)
	@echo   - empty_files.c    (Empty files implementation)
	@echo   - file_ops.c       (File operations)
	@echo   - scanner.c        (Scanner implementation)
	@echo   - monitor.c        (Monitor implementation)
	@echo   - ipc_pipe.c       (IPC server implementation)
	@echo.
	@echo gui/
	@echo   - gui_tray.c       (Tray application with alerts)
	@echo.
	@echo blake/
	@echo   - blake3.c, blake3_dispatch.c, blake3_portable.c
	@echo.
	@echo docs/
	@echo   - IPC Message Format.md
	@echo.

# Help message
help:
	@echo.
	@echo ========================================
	@echo DDAS Makefile - Available Targets
	@echo ========================================
	@echo.
	@echo BUILD TARGETS:
	@echo   all              - Build both engine and GUI (default)
	@echo   engine           - Build detection engine only
	@echo   gui              - Build GUI application only
	@echo   clean            - Remove all build artifacts
	@echo   install          - Copy executables to build/ directory
	@echo.
	@echo RUN TARGETS:
	@echo   run-both         - Start both engine and GUI together
	@echo   run-engine       - Start engine only (watch mode)
	@echo   run-gui          - Start GUI only
	@echo   test             - Run engine in scan-only mode
	@echo   stop             - Stop all running DDAS processes
	@echo.
	@echo INFO TARGETS:
	@echo   structure        - Show project structure
	@echo   help             - Show this help message
	@echo.
	@echo ========================================
	@echo QUICK START:
	@echo ========================================
	@echo 1. Build everything:
	@echo    mingw32-make
	@echo.
	@echo 2. Run the system:
	@echo    mingw32-make run-both
	@echo.
	@echo 3. Stop the system:
	@echo    mingw32-make stop
	@echo.
	@echo ========================================
	@echo ADVANCED USAGE:
	@echo ========================================
	@echo Build and run engine only:
	@echo    mingw32-make run-engine
	@echo.
	@echo Build and run GUI only (engine must be running):
	@echo    mingw32-make run-gui
	@echo.
	@echo Test scan without monitoring:
	@echo    mingw32-make test
	@echo.
	@echo Clean and rebuild:
	@echo    mingw32-make clean
	@echo    mingw32-make all
	@echo.
	@echo ========================================
	@echo CUSTOMIZATION:
	@echo ========================================
	@echo To change monitored directory, edit:
	@echo   - run-engine target
	@echo   - create-start-script target
	@echo.
	@echo Current directory: C:\Users\Sahil\Documents\testfolder
	@echo.