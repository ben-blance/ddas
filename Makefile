# Modular Makefile for File Duplicate Detector
CC = gcc
CFLAGS = -O3 -std=c11 -Iinclude -D_WIN32_WINNT=0x0600 -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512
BLAKE_DIR = blake
SRC_DIR = src
TARGET = finddupes.exe

# Source files
MAIN_SRCS = $(SRC_DIR)/main.c \
            $(SRC_DIR)/utils.c \
            $(SRC_DIR)/hash_table.c \
            $(SRC_DIR)/empty_files.c \
            $(SRC_DIR)/file_ops.c \
            $(SRC_DIR)/scanner.c \
            $(SRC_DIR)/monitor.c

BLAKE_SRCS = $(BLAKE_DIR)/blake3.c \
             $(BLAKE_DIR)/blake3_dispatch.c \
             $(BLAKE_DIR)/blake3_portable.c

SRCS = $(MAIN_SRCS) $(BLAKE_SRCS)
OBJS = $(SRCS:.c=.o)

.PHONY: all clean run test help structure

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo.
	@echo Linking $(TARGET)...
	$(CC) $(CFLAGS) -o $@ $^
	@echo Build complete!
	@echo.

%.o: %.c
	@echo Compiling $<...
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	@echo Cleaning build artifacts...
	@if exist $(TARGET) del /Q $(TARGET) 2>nul
	@if exist $(SRC_DIR)\*.o del /Q $(SRC_DIR)\*.o 2>nul
	@if exist $(BLAKE_DIR)\*.o del /Q $(BLAKE_DIR)\*.o 2>nul
	@echo Clean complete!

run: $(TARGET)
	@echo.
	@echo Running duplicate detector in watch mode...
	@echo ============================================
	@.\$(TARGET) C:\Users\Sahil\Documents\testfolder --watch

test: $(TARGET)
	@echo.
	@echo Running duplicate detector (scan only)...
	@echo ==========================================
	@.\$(TARGET) C:\Users\Sahil\Documents\testfolder

structure:
	@echo.
	@echo === Project Structure ===
	@echo.
	@echo include/
	@echo   - utils.h          (Thread-safe utilities)
	@echo   - hash_table.h     (Hash table for duplicates)
	@echo   - empty_files.h    (Empty file tracking)
	@echo   - file_ops.h       (File operations ^& hashing)
	@echo   - scanner.h        (Directory scanning)
	@echo   - monitor.h        (File system monitoring)
	@echo   - blake3.h         (BLAKE3 hash library)
	@echo.
	@echo src/
	@echo   - main.c           (Entry point)
	@echo   - utils.c          (Utilities implementation)
	@echo   - hash_table.c     (Hash table implementation)
	@echo   - empty_files.c    (Empty files implementation)
	@echo   - file_ops.c       (File operations implementation)
	@echo   - scanner.c        (Scanner implementation)
	@echo   - monitor.c        (Monitor implementation)
	@echo.
	@echo blake/
	@echo   - blake3.c, blake3_dispatch.c, blake3_portable.c
	@echo.

help:
	@echo.
	@echo Available targets:
	@echo   all       - Build the project (default)
	@echo   clean     - Remove all build artifacts
	@echo   run       - Build and run with --watch mode
	@echo   test      - Build and run scan only (no watch)
	@echo   structure - Show project structure
	@echo   help      - Show this help message
	@echo.
	@echo Usage examples:
	@echo   mingw32-make
	@echo   mingw32-make clean
	@echo   mingw32-make run
	@echo   mingw32-make test
	@echo   mingw32-make structure
	@echo.