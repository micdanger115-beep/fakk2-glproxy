#include "proxy.h"
#include "log.h"

#include <stdarg.h>
#include <stdio.h>

static HANDLE log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_lock;
static DWORD log_start;

void log_open(void)
{
    wchar_t path[MAX_PATH];

    if (log_file != INVALID_HANDLE_VALUE)
        return;
    InitializeCriticalSection(&log_lock);
    proxy_path_in_dll_dir(L"fakk2_glproxy.log", path, MAX_PATH);
    log_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    log_start = GetTickCount();
}

void log_close(void)
{
    if (log_file == INVALID_HANDLE_VALUE)
        return;
    EnterCriticalSection(&log_lock);
    CloseHandle(log_file);
    log_file = INVALID_HANDLE_VALUE;
    LeaveCriticalSection(&log_lock);
    DeleteCriticalSection(&log_lock);
}

void log_printf(const char *fmt, ...)
{
    char line[1024];
    va_list ap;
    int n;
    DWORD written;

    if (log_file == INVALID_HANDLE_VALUE)
        return;

    n = snprintf(line, sizeof line, "[%8lu] ", (unsigned long)(GetTickCount() - log_start));
    va_start(ap, fmt);
    n += vsnprintf(line + n, sizeof line - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof line - 3)
        n = (int)sizeof line - 3;
    line[n++] = '\r';
    line[n++] = '\n';

    EnterCriticalSection(&log_lock);
    WriteFile(log_file, line, (DWORD)n, &written, NULL);
    LeaveCriticalSection(&log_lock);
}
