#pragma once

#  ifdef __GNUC__
#    ifndef __has_warning
#      define __has_warning(x) 0
#    endif
#    pragma GCC diagnostic push
#    if __has_warning("-Wreserved-macro-identifier")
#      pragma GCC diagnostic ignored "-Wreserved-macro-identifier"
#    endif
#    if __has_warning("-Wsign-conversion")
#      pragma GCC diagnostic ignored "-Wsign-conversion"
#    endif
#    if __has_warning("-Wmissing-noreturn")
#      pragma GCC diagnostic ignored "-Wmissing-noreturn"
#    endif
#  endif // __GNUC__
#include "threads/threads.h"
#  ifdef __GNUC__
#    pragma GCC diagnostic pop
#  endif // __GNUC__
