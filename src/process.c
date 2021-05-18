#include "process.h"

#include "threads/threads.h"
#include <windows.h>
#include <stdint.h>

struct queue
{
  mtx_t mtx;
  cnd_t cnd, cnd2;
  void **items;
  int num_items, used;
  int readcur, writecur;
};

static struct queue *queue_init(void **items, int num_items)
{
  int mtx_ret = thrd_error;
  int cnd_ret = thrd_error;
  int cnd2_ret = thrd_error;
  struct queue *const q = malloc(sizeof(struct queue));
  if (!q)
  {
    return NULL;
  }
  q->items = items;
  q->num_items = num_items;
  q->used = 0;
  q->readcur = 0;
  q->writecur = 0;
  mtx_ret = mtx_init(&q->mtx, mtx_plain | mtx_recursive);
  if (mtx_ret != thrd_success)
  {
    goto cleanup;
  }
  cnd_ret = cnd_init(&q->cnd);
  if (cnd_ret != thrd_success)
  {
    goto cleanup;
  }
  cnd2_ret = cnd_init(&q->cnd2);
  if (cnd2_ret != thrd_success)
  {
    goto cleanup;
  }
  return q;
cleanup:
  if (cnd2_ret == thrd_success)
  {
    cnd_destroy(&q->cnd2);
  }
  if (cnd_ret == thrd_success)
  {
    cnd_destroy(&q->cnd);
  }
  if (mtx_ret == thrd_success)
  {
    mtx_destroy(&q->mtx);
  }
  free(q);
  return NULL;
}

void queue_destroy(struct queue *const q)
{
  cnd_destroy(&q->cnd2);
  cnd_destroy(&q->cnd);
  mtx_destroy(&q->mtx);
  free(q);
}

void queue_push(struct queue *const q, void *item)
{
  mtx_lock(&q->mtx);
  while (q->used == q->num_items)
  {
    cnd_wait(&q->cnd2, &q->mtx);
  }
  q->items[q->writecur] = item;
  ++q->used;
  q->writecur = (q->writecur + 1) % q->num_items;
  cnd_signal(&q->cnd);
  mtx_unlock(&q->mtx);
}

void *queue_pop(struct queue *const q)
{
  void *r = NULL;
  mtx_lock(&q->mtx);
  while (q->used == 0)
  {
    cnd_wait(&q->cnd, &q->mtx);
  }
  r = q->items[q->readcur];
  q->items[q->readcur] = NULL;
  --q->used;
  q->readcur = (q->readcur + 1) % q->num_items;
  cnd_signal(&q->cnd2);
  mtx_unlock(&q->mtx);
  return r;
}

void *queue_pop_nowait(struct queue *const q)
{
  void *r = NULL;
  mtx_lock(&q->mtx);
  if (q->used > 0)
  {
    r = q->items[q->readcur];
    q->items[q->readcur] = NULL;
    --q->used;
    q->readcur = (q->readcur + 1) % q->num_items;
    cnd_signal(&q->cnd2);
  }
  mtx_unlock(&q->mtx);
  return r;
}

struct queue_item
{
  void *buf;
  int32_t len;
};

struct process
{
  HANDLE process;
  thrd_t thread;
  struct queue *q;
  struct queue_item *queue_items[2];
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
  lstrcpyW(dest, name);
  dest += name_len;
  *dest++ = L'=';
  len -= name_len + 1;
  lstrcpyW(dest, value);
  dest += value_len + 1;
  len -= value_len + 1;

  memcpy(dest, envstr, len * sizeof(WCHAR));
  FreeEnvironmentStringsW(envstr);
  return newenv;
cleanup:
  FreeEnvironmentStringsW(envstr);
  free(newenv);
  return NULL;
}

static WCHAR *get_working_directory(const WCHAR *exe_path)
{
  int exe_pathlen = lstrlenW(exe_path) + 1;
  WCHAR *path = calloc(exe_pathlen, sizeof(WCHAR));
  if (!path)
  {
    return NULL;
  }
  int pathlen = 0;
  if (*exe_path == L'"')
  {
    ++exe_path;
    while (*exe_path != L'\0' && *exe_path != L'"')
    {
      path[pathlen++] = *exe_path++;
    }
  }
  else
  {
    while (*exe_path != L'\0' && *exe_path != L' ')
    {
      path[pathlen++] = *exe_path++;
    }
  }
  int dirlen = GetFullPathNameW(path, 0, NULL, NULL);
  if (dirlen == 0)
  {
    free(path);
    return NULL;
  }
  WCHAR *dir = calloc(dirlen, sizeof(WCHAR));
  WCHAR *fn = NULL;
  if (GetFullPathNameW(path, dirlen, dir, &fn) == 0 || fn == NULL)
  {
    free(dir);
    free(path);
    return NULL;
  }
  *fn = '\0';
  free(path);
  return dir;
}

static BOOL read(HANDLE h, void *buf, DWORD sz)
{
  char *b = buf;
  for (DWORD read; sz > 0; b += read, sz -= read)
  {
    if (!ReadFile(h, b, sz, &read, NULL))
    {
      return FALSE;
    }
  }
  return TRUE;
}

static BOOL write(HANDLE h, const void *buf, DWORD sz)
{
  char *b = (void *)buf;
  for (DWORD written; sz > 0; b += written, sz -= written)
  {
    if (!WriteFile(h, b, sz, &written, NULL))
    {
      return FALSE;
    }
  }
  return TRUE;
}

static int read_worker(void *userdata)
{
  struct process *self = userdata;
  while (1)
  {
    int32_t sz;
    if (!read(self->out_r, &sz, sizeof(sz)))
    {
      goto error;
    }
    struct queue_item *qi = malloc(sizeof(struct queue_item) + sz);
    qi->len = sz;
    if (sz)
    {
      qi->buf = qi + 1;
      if (!read(self->out_r, qi->buf, sz))
      {
        free(qi);
        goto error;
      }
    }
    else
    {
      qi->buf = NULL;
    }
    queue_push(self->q, qi);
  }
  return 0;

error:
{
  struct queue_item *qi = malloc(sizeof(struct queue_item));
  qi->buf = NULL;
  qi->len = -1;
  queue_push(self->q, qi);
}
  return 1;
}

int process_write(struct process *self, const void *buf, size_t len)
{
  int32_t sz = len;
  if (!write(self->in_w, &sz, sizeof(sz)))
  {
    return 1;
  }
  if (!write(self->in_w, buf, len))
  {
    return 3;
  }
  return 0;
}

int process_read(struct process *self, void **buf, size_t *len)
{
  struct queue_item *qi = queue_pop(self->q);
  if (!qi)
  {
    return 1;
  }
  if (self->previous_queue_item)
  {
    free(self->previous_queue_item);
  }
  self->previous_queue_item = qi;
  if (qi->buf == NULL && qi->len == -1)
  {
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
  int pathlen = lstrlenW(exe_path) + 1;
  path = calloc(pathlen, sizeof(WCHAR));
  if (!path)
  {
    goto cleanup;
  }
  wsprintfW(path, L"%s", exe_path);

  dir = get_working_directory(exe_path);
  if (!dir)
  {
    goto cleanup;
  }

  PROCESS_INFORMATION pi = {INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, 0, 0};
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

  r->q = queue_init((void **)r->queue_items, 2);
  if (!r->q)
  {
    CloseHandle(pi.hProcess);
    pi.hProcess = INVALID_HANDLE_VALUE;
    free(r);
    goto cleanup;
  }
  if (thrd_create(&r->thread, read_worker, r) != thrd_success)
  {
    CloseHandle(pi.hProcess);
    pi.hProcess = INVALID_HANDLE_VALUE;
    queue_destroy(r->q);
    free(r);
    goto cleanup;
  }
  return r;

cleanup:
  if (dir)
  {
    free(dir);
    dir = NULL;
  }
  if (path)
  {
    free(path);
    path = NULL;
  }
  if (env)
  {
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

  // FIXME: we cannot use thrd_join on DLL_PROCESS_DETACH bacause hangs.
  // thrd_join(self->thread, NULL);
  // struct queue_item *qi = NULL;
  // while ((qi = queue_pop_nowait(self->q)))
  // {
  //   free(qi);
  // }
  thrd_detach(self->thread);
  struct queue_item *qi = NULL;
  while ((qi = queue_pop(self->q)))
  {
    if (qi->buf == NULL && qi->len == -1) {
      free(qi);
      break;
    }
    free(qi);
  }

  if (self->previous_queue_item)
  {
    free(self->previous_queue_item);
    self->previous_queue_item = NULL;
  }
  queue_destroy(self->q);
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
