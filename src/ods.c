#include "ods.h"

#include <stdarg.h>
#include <windows.h>

void ODS(const char *fmt, ...) {
  char s[1024], f[1024];
  va_list p;
  va_start(p, fmt);
  f[0] = '\0';
  lstrcatA(f, "bridge: ");
  lstrcatA(f, fmt);
  wsprintfA(s, f, p);
  OutputDebugStringA(s);
  va_end(p);
}
