//monitor.h
#ifndef MONITOR_H
#define MONITOR_H

#include <windows.h>

// Monitor thread function
DWORD WINAPI monitor_thread_func(LPVOID lpParam);

// Signal monitor thread to stop immediately
void signal_monitor_stop(void);

#endif // MONITOR_H