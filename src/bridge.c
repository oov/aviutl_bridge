#include "bridge.h"

#include "hashmap.h"
#include "threads/threads.h"

#include "process.h"
#include "ver.h"
#include "ods.h"

#include <stdint.h>
#include <stdbool.h>

struct hash_map_value
{
  struct process *value;
};

WCHAR g_mapped_file_name[32];
HANDLE g_mapped_file = NULL;
int g_bufsize = 0;
void *g_view = NULL;
struct hashmap_s g_process_map = { 0 };
mtx_t g_mutex;

static BOOL bridge_init(FILTER *fp)
{
  wsprintfW(g_mapped_file_name, L"aviutl_bridge_fmo_%08x", GetCurrentProcessId());
  if (hashmap_create(2, &g_process_map) != 0) {
    return FALSE;
  }
  mtx_init(&g_mutex, mtx_plain | mtx_recursive);

  SYS_INFO si;
  if (!fp->exfunc->get_sys_info(NULL, &si))
  {
    return FALSE;
  }
  const int w = max(si.max_w, 1280);
  const int h = max(si.max_h, 720);
  const int header_size = sizeof(struct share_mem_header);
  const int body_size = w * h * sizeof(PIXEL_YC);
  HANDLE mapped_file = CreateFileMappingW(
      INVALID_HANDLE_VALUE,
      NULL, PAGE_READWRITE,
      0,
      header_size + body_size,
      g_mapped_file_name);
  if (!mapped_file)
  {
    return FALSE;
  }

  void *view = MapViewOfFile(mapped_file, FILE_MAP_WRITE, 0, 0, 0);
  if (!view)
  {
    CloseHandle(mapped_file);
    return FALSE;
  }

  g_mapped_file = mapped_file;
  g_view = view;
  g_bufsize = header_size + body_size;
  struct share_mem_header *v = view;
  v->header_size = header_size;
  v->body_size = body_size;
  v->version = 1;
  v->width = w;
  v->height = h;
  return TRUE;
}

static int delete_all_callback(void* const context, void* const value) {
  (void)context;
  struct hash_map_value *hmv = value;
  process_finish(hmv->value);
  free(value);
  return 1;
}

static BOOL bridge_exit(FILTER *fp)
{
  (void)fp;
  mtx_lock(&g_mutex);
  hashmap_iterate(&g_process_map, delete_all_callback, NULL);
  hashmap_destroy(&g_process_map);
  if (g_view)
  {
    UnmapViewOfFile(g_view);
    g_view = NULL;
  }
  if (g_mapped_file)
  {
    CloseHandle(g_mapped_file);
    g_mapped_file = NULL;
  }
  mtx_destroy(&g_mutex);
  return TRUE;
}

static int bridge_call_core(const char *exe_path, const void *buf, int32_t len, struct call_mem *mem, void **r, int32_t *rlen)
{
  if (g_bufsize == 0 || !g_view)
  {
    return ECALL_NOT_INITIALIZED;
  }
  const size_t exe_path_len = lstrlenA(exe_path);
  struct hash_map_value *hmv = hashmap_get(&g_process_map, exe_path, exe_path_len);
  if (hmv)
  {
    if (!process_isrunning(hmv->value))
    {
      // It seems process is already dead
      process_finish(hmv->value);
      hashmap_remove(&g_process_map, exe_path, exe_path_len);
      free(hmv);
      hmv = NULL;
    }
  }
  if (!hmv)
  {
    hmv = malloc(sizeof(struct hash_map_value) + exe_path_len);
    int buflen = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, exe_path, exe_path_len, NULL, 0);
    WCHAR *wpath = malloc(sizeof(WCHAR) * (buflen+1));
    if (!wpath)
    {
      free(hmv);
      return ECALL_FAILED_TO_CONVERT_EXE_PATH;
    }
    if (MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, exe_path, exe_path_len, wpath, buflen) == 0)
    {
      free(hmv);
      free(wpath);
      return ECALL_FAILED_TO_CONVERT_EXE_PATH;
    }
    wpath[buflen] = '\0';
    struct process *p = process_start(wpath, L"BRIDGE_FMO", g_mapped_file_name);
    if (!p)
    {
      free(hmv);
      free(wpath);
      return ECALL_FAILED_TO_START_PROCESS;
    }
    free(wpath);
    process_close_stderr(p);
    hmv->value = p;
    char *key = (char*)(hmv + 1);
    memcpy(key, exe_path, exe_path_len);
    if (hashmap_put(&g_process_map, key, exe_path_len, hmv) != 0) {
      process_finish(p);
      free(hmv);
      return ECALL_FAILED_TO_START_PROCESS;
    }
  }
  if (mem)
  {
    struct share_mem_header *v = g_view;
    v->width = mem->width;
    v->height = mem->height;
    if (mem->mode & MEM_MODE_READ)
    {
      memcpy(v + 1, mem->buf, mem->width * 4 * mem->height);
    }
  }
  if (process_write(hmv->value, buf, len) != 0)
  {
    return ECALL_FAILED_TO_SEND_COMMAND;
  }
  void *rbuf;
  size_t rbuflen;
  if (process_read(hmv->value, &rbuf, &rbuflen) != 0)
  {
    return ECALL_FAILED_TO_RECEIVE_COMMAND;
  }
  if (mem && mem->mode & MEM_MODE_WRITE)
  {
    struct share_mem_header *v = g_view;
    memcpy(mem->buf, v + 1, mem->width * 4 * mem->height);
  }
  *r = rbuf;
  *rlen = rbuflen;
  return ECALL_OK;
}

static int bridge_call(const char *exe_path, const void *buf, int32_t len, struct call_mem *mem, void **r, int32_t *rlen)
{
  mtx_lock(&g_mutex);
  int ret = bridge_call_core(exe_path, buf, len, mem, r, rlen);
  mtx_unlock(&g_mutex);
  return ret;
}

#define BRIDGE_NAME "\x83\x75\x83\x8A\x83\x62\x83\x57"
FILTER_DLL bridge_filter = {
    FILTER_FLAG_ALWAYS_ACTIVE | FILTER_FLAG_EX_INFORMATION | FILTER_FLAG_NO_CONFIG | FILTER_FLAG_RADIO_BUTTON,
    0,
    0,
    BRIDGE_NAME,
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    NULL,
    NULL,
    NULL,
    bridge_init,
    bridge_exit,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    BRIDGE_NAME " " VERSION,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    {0, 0},
};

struct bridge bridge_api = {0, bridge_call};
