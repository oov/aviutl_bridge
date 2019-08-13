#include "ods.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include <windows.h>

void ODS(const char *fmt, ...)
{
    char s[1024], f[1024];
    va_list p;
    va_start(p, fmt);
    f[0] = '\0';
    strcat_s(f, 1024, "bridge: ");
    strcat_s(f, 1024, fmt);
    vsprintf_s(s, 1024, f, p);
    OutputDebugStringA(s);
    va_end(p);
}