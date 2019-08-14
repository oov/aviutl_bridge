#include "process.h"

#include "thread.h"
#include <windows.h>
#include <stdint.h>
#include <strsafe.h>

struct queue_item
{
    void *buf;
    int32_t len;
};

struct process
{
    HANDLE process;
    thread_ptr_t thread;
    thread_queue_t queue;
    struct queue_item queue_items[2];
    void *previous_queue_item;
    HANDLE in_w;
    HANDLE out_r;
    HANDLE err_r;
};

static WCHAR *build_environment_strings(const WCHAR *name, const WCHAR *value)
{
    LPWCH envstr = GetEnvironmentStringsW();
    if (!envstr)
    {
        return NULL;
    }

    const int name_len = lstrlenW(name);
    const int value_len = lstrlenW(value);
    int len = name_len + 1 + value_len + 1;
    LPWCH src = envstr;
    while (*src)
    {
        int l = lstrlenW(src) + 1;
        len += l;
        src += l;
    }

    WCHAR *newenv = calloc(len + 1, sizeof(WCHAR));
    if (!newenv)
    {
        goto cleanup;
    }

    WCHAR *dest = newenv;
    if (StringCchCopyW(dest, len, name) != S_OK)
    {
        goto cleanup;
    }
    dest += name_len;
    *dest++ = L'=';
    len -= name_len + 1;

    if (StringCchCopyW(dest, len, value) != S_OK)
    {
        goto cleanup;
    }
    dest += value_len + 1;
    len -= value_len + 1;

    if (memcpy_s(dest, len * sizeof(WCHAR), envstr, len * sizeof(WCHAR)))
    {
        goto cleanup;
    }
    FreeEnvironmentStringsW(envstr);
    return newenv;
cleanup:
    FreeEnvironmentStringsW(envstr);
    free(newenv);
    return NULL;
}

static WCHAR *get_working_directory(const WCHAR *exe_path) {
    int exe_pathlen = lstrlenW(exe_path) + 1;
    WCHAR *path = calloc(exe_pathlen, sizeof(WCHAR));
    if (!path) {
        return NULL;
    }
    int pathlen = 0;
    if (*exe_path == L'"') {
        ++exe_path;
        while(*exe_path != L'\0' && *exe_path != L'"') {
            path[pathlen++] = *exe_path++;
        }
    } else {
        while(*exe_path != L'\0' && *exe_path != L' ') {
            path[pathlen++] = *exe_path++;
        }
    }
    int dirlen = GetFullPathNameW(path, 0, NULL, NULL);
    if (dirlen == 0) {
        free(path);
        return NULL;
    }
    WCHAR *dir = calloc(dirlen, sizeof(WCHAR));
    WCHAR *fn = NULL;
    if (GetFullPathNameW(path, dirlen, dir, &fn) == 0) {
        free(dir);
        free(path);
        return NULL;
    }
    *fn = '\0';
    free(path);
    return dir;
}

static int read_worker(void *userdata)
{
    struct process *self = userdata;
    while (1)
    {
        int32_t sz;
        DWORD read;
        if (!ReadFile(self->out_r, &sz, sizeof(sz), &read, NULL))
        {
            goto error;
        }
        if (read != sizeof(sz))
        {
            goto error;
        }
        struct queue_item *qi = malloc(sizeof(struct queue_item) + sz);
        qi->buf = qi + 1;
        qi->len = sz;
        if (sz > 0)
        {
            if (!ReadFile(self->out_r, qi->buf, sz, &read, NULL))
            {
                goto error;
            }
            if (read != sz)
            {
                goto error;
            }
        }
        thread_queue_produce(&self->queue, qi, 60000);
    }
    return 0;

    error:
    {
        struct queue_item *qi = malloc(sizeof(struct queue_item));
        qi->buf = NULL;
        qi->len = 0;
        thread_queue_produce(&self->queue, qi, 60000);
    }
    return 1;
}

int process_write(struct process *self, const void *buf, size_t len)
{
    int32_t sz = len;
    DWORD written;
    if (!WriteFile(self->in_w, &sz, sizeof(sz), &written, NULL))
    {
        return 1;
    }
    if (written != sizeof(sz))
    {
        return 2;
    }
    if (len > 0)
    {
        if (!WriteFile(self->in_w, buf, len, &written, NULL))
        {
            return 3;
        }
        if (written != len)
        {
            return 4;
        }
    }
    return 0;
}

int process_read(struct process *self, void **buf, size_t *len)
{
    struct queue_item *qi = thread_queue_consume(&self->queue, 60000);
    if (!qi)
    {
        return 1;
    }
    if (self->previous_queue_item)
    {
        free(self->previous_queue_item);
    }
    self->previous_queue_item = qi;
    if (qi->buf == NULL || qi->len == 0) {
        return 2;
    }
    *buf = qi->buf;
    *len = qi->len;
    return 0;
}

struct process *process_start(const WCHAR *exe_path, const WCHAR *envvar_name, const WCHAR *envvar_value)
{
    HANDLE in_r = INVALID_HANDLE_VALUE;
    HANDLE in_w = INVALID_HANDLE_VALUE;
    HANDLE in_w_tmp = INVALID_HANDLE_VALUE;

    HANDLE out_r = INVALID_HANDLE_VALUE;
    HANDLE out_w = INVALID_HANDLE_VALUE;
    HANDLE out_r_tmp = INVALID_HANDLE_VALUE;

    HANDLE err_r = INVALID_HANDLE_VALUE;
    HANDLE err_w = INVALID_HANDLE_VALUE;
    HANDLE err_r_tmp = INVALID_HANDLE_VALUE;

    WCHAR *env = NULL, *path = NULL, *dir = NULL;

    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), 0, TRUE};
    if (!CreatePipe(&in_r, &in_w_tmp, &sa, 0))
    {
        goto cleanup;
    }
    if (!CreatePipe(&out_r_tmp, &out_w, &sa, 0))
    {
        goto cleanup;
    }
    if (!CreatePipe(&err_r_tmp, &err_w, &sa, 0))
    {
        goto cleanup;
    }

    HANDLE curproc = GetCurrentProcess();
    if (!DuplicateHandle(curproc, in_w_tmp, curproc, &in_w, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        goto cleanup;
    }
    if (!DuplicateHandle(curproc, out_r_tmp, curproc, &out_r, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        goto cleanup;
    }
    if (!DuplicateHandle(curproc, err_r_tmp, curproc, &err_r, 0, FALSE, DUPLICATE_SAME_ACCESS))
    {
        goto cleanup;
    }

    CloseHandle(in_w_tmp);
    in_w_tmp = INVALID_HANDLE_VALUE;
    CloseHandle(out_r_tmp);
    out_r_tmp = INVALID_HANDLE_VALUE;
    CloseHandle(err_r_tmp);
    err_r_tmp = INVALID_HANDLE_VALUE;

    env = build_environment_strings(envvar_name, envvar_value);
    if (!env)
    {
        goto cleanup;
    }

    // have to copy this buffer because CreateProcessW may modify path string.
    int pathlen = lstrlenW(exe_path)+1;
    path = calloc(pathlen, sizeof(WCHAR));
    if (!path) {
        goto cleanup;
    }
    StringCchPrintfW(path, pathlen, L"%s", exe_path);

    dir = get_working_directory(exe_path);
    if (!dir) {
        goto cleanup;
    }

    PROCESS_INFORMATION pi = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    STARTUPINFOW si = {0};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = err_w;
    if (!CreateProcessW(0, path, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, env, dir, &si, &pi))
    {
        goto cleanup;
    }
    free(dir);
    dir = NULL;
    free(path);
    path = NULL;
    free(env);
    env = NULL;
    CloseHandle(pi.hThread);
    pi.hThread = INVALID_HANDLE_VALUE;
    CloseHandle(in_r);
    in_r = INVALID_HANDLE_VALUE;
    CloseHandle(err_w);
    err_w = INVALID_HANDLE_VALUE;
    CloseHandle(out_w);
    out_w = INVALID_HANDLE_VALUE;

    struct process *r = calloc(1, sizeof(struct process));
    if (!r)
    {
        CloseHandle(pi.hProcess);
        pi.hProcess = INVALID_HANDLE_VALUE;
        goto cleanup;
    }
    r->process = pi.hProcess;
    r->in_w = in_w;
    r->out_r = out_r;
    r->err_r = err_r;

    thread_queue_init(&r->queue, 2, (void **)r->queue_items, 0);

    thread_ptr_t thread = thread_create(read_worker, r, NULL, THREAD_STACK_SIZE_DEFAULT);
    if (!thread)
    {
        CloseHandle(pi.hProcess);
        pi.hProcess = INVALID_HANDLE_VALUE;
        free(r);
        goto cleanup;
    }
    r->thread = thread;
    return r;

cleanup:
    if (dir) {
        free(dir);
        dir = NULL;
    }
    if (path) {
        free(path);
        path = NULL;
    }
    if (env) {
        free(env);
        env = NULL;
    }

    if (pi.hThread != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pi.hThread);
        pi.hThread = INVALID_HANDLE_VALUE;
    }
    if (pi.hProcess != INVALID_HANDLE_VALUE)
    {
        CloseHandle(pi.hProcess);
        pi.hProcess = INVALID_HANDLE_VALUE;
    }

    if (err_r != INVALID_HANDLE_VALUE)
    {
        CloseHandle(err_r);
        err_r = INVALID_HANDLE_VALUE;
    }
    if (err_w != INVALID_HANDLE_VALUE)
    {
        CloseHandle(err_w);
        err_w = INVALID_HANDLE_VALUE;
    }
    if (err_r_tmp != INVALID_HANDLE_VALUE)
    {
        CloseHandle(err_r_tmp);
        err_r_tmp = INVALID_HANDLE_VALUE;
    }
    if (out_r != INVALID_HANDLE_VALUE)
    {
        CloseHandle(out_r);
        out_r = INVALID_HANDLE_VALUE;
    }
    if (out_w != INVALID_HANDLE_VALUE)
    {
        CloseHandle(out_w);
        out_w = INVALID_HANDLE_VALUE;
    }
    if (out_r_tmp != INVALID_HANDLE_VALUE)
    {
        CloseHandle(out_r_tmp);
        out_r_tmp = INVALID_HANDLE_VALUE;
    }
    if (in_r != INVALID_HANDLE_VALUE)
    {
        CloseHandle(in_r);
        in_r = INVALID_HANDLE_VALUE;
    }
    if (in_w != INVALID_HANDLE_VALUE)
    {
        CloseHandle(in_w);
        in_w = INVALID_HANDLE_VALUE;
    }
    if (in_w_tmp != INVALID_HANDLE_VALUE)
    {
        CloseHandle(in_w_tmp);
        in_w_tmp = INVALID_HANDLE_VALUE;
    }
    return NULL;
}

void process_finish(struct process *self)
{
    if (self->in_w != INVALID_HANDLE_VALUE)
    {
        CloseHandle(self->in_w);
        self->in_w = INVALID_HANDLE_VALUE;
    }
    if (self->out_r != INVALID_HANDLE_VALUE)
    {
        CloseHandle(self->out_r);
        self->out_r = INVALID_HANDLE_VALUE;
    }
    if (self->err_r != INVALID_HANDLE_VALUE)
    {
        CloseHandle(self->err_r);
        self->err_r = INVALID_HANDLE_VALUE;
    }
    if (self->process != INVALID_HANDLE_VALUE)
    {
        CloseHandle(self->process);
        self->process = INVALID_HANDLE_VALUE;
    }
    thread_join(self->thread);
    if (self->previous_queue_item)
    {
        free(self->previous_queue_item);
        self->previous_queue_item = NULL;
    }
    while (thread_queue_count(&self->queue))
    {
        free(thread_queue_consume(&self->queue, THREAD_QUEUE_WAIT_INFINITE));
    }
    thread_queue_term(&self->queue);
    free(self);
}

void process_close_stderr(struct process *self)
{
    CloseHandle(self->err_r);
    self->err_r = INVALID_HANDLE_VALUE;
}

bool process_isrunning(const struct process *self)
{
    return WaitForSingleObject(self->process, 0) == WAIT_TIMEOUT;
}