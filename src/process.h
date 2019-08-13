#pragma once

#include <windows.h>
#include <stdbool.h>

struct process;

struct process *process_start(const WCHAR *exe_path, const WCHAR *envvar_name, const WCHAR *envvar_value);
void process_finish(struct process *self);
void process_close_stderr(struct process *self);
int process_read(struct process *self, void **buf, size_t *len);
int process_write(struct process *self, const void *buf, size_t len);
bool process_isrunning(const struct process *self);
