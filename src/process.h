#pragma once

#include <stdbool.h>
#include <windows.h>

struct process;

struct process *
process_start(wchar_t const *const exe_path, wchar_t const *const envvar_name, wchar_t const *const envvar_value);
void process_finish(struct process *const self);
void process_close_stderr(struct process *const self);
int process_read(struct process *const self, void **const buf, size_t *const len);
int process_write(struct process *const self, void const *const buf, size_t const len);
bool process_isrunning(struct process const *const self);
