#include "debug.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>

void diagLog(const wchar_t* fmt, ...) {
    static bool enabled = [] {
        wchar_t buf[8];
        return GetEnvironmentVariableW(L"MOC_DEBUG", buf, 8) > 0;
    }();
    if (!enabled) return;
    wchar_t path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"moc-diag.txt");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") == 0 && f) {
        va_list ap;
        va_start(ap, fmt);
        vfwprintf(f, fmt, ap);
        va_end(ap);
        fwprintf(f, L"\n");
        fclose(f);
    }
}
