#pragma once

#include <stdbool.h>
#include <stdint.h>

struct share_mem_header {
  uint32_t header_size;
  uint32_t body_size;
  uint32_t version;
  uint32_t width;
  uint32_t height;
};

enum ECALL {
  ECALL_OK,
  ECALL_NOT_INITIALIZED,
  ECALL_FAILED_TO_CONVERT_EXE_PATH,
  ECALL_FAILED_TO_START_PROCESS,
  ECALL_FAILED_TO_SEND_COMMAND,
  ECALL_FAILED_TO_RECEIVE_COMMAND,
};

enum mem_mode {
  MEM_MODE_READ = 1,
  MEM_MODE_WRITE = 2,
  MEM_MODE_DIRECT = 4,
};

struct call_mem {
  void *buf;
  int32_t mode;
  int32_t width;
  int32_t height;
};

bool bridge_init(int32_t const max_width, int32_t const max_height);
int bridge_call(char const *const exe_path,
                void const *const buf,
                int32_t const len,
                struct call_mem *const mem,
                void **const r,
                int32_t *const rlen);
bool bridge_exit(void);
