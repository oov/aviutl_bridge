#pragma once

#include <stdbool.h>
#include <windows.h>

struct process;

struct process *
process_start(wchar_t const *const exe_path, wchar_t const *const envvar_name, wchar_t const *const envvar_value);
void process_finish(struct process *self);
void process_close_stderr(struct process *self);
int process_read(struct process *self, void **buf, size_t *len);
int process_write(struct process *self, const void *buf, size_t len);
bool process_isrunning(const struct process *self);
