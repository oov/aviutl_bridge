#include <lua5.1/lua.h>
#include <lua5.1/lauxlib.h>
#include <windows.h>

#include "bridge_public.h"
#include "ods.h"

HMODULE g_auf = NULL;
struct bridge *g_bridge_api = NULL;

static int bridge_call_error(lua_State *L, int err)
{
  switch (err)
  {
  case ECALL_OK:
    return luaL_error(L, "success");
  case ECALL_NOT_INITIALIZED:
    return luaL_error(L, "bridge library is not initialized yet");
  case ECALL_FAILED_TO_CONVERT_EXE_PATH:
    return luaL_error(L, "failed to convert exe path");
  case ECALL_FAILED_TO_START_PROCESS:
    return luaL_error(L, "failed to start new process");
  case ECALL_FAILED_TO_SEND_COMMAND:
    return luaL_error(L, "could not send command to child process");
  case ECALL_FAILED_TO_RECEIVE_COMMAND:
    return luaL_error(L, "could not receive reply from child process");
  }
  return luaL_error(L, "unexpected error code");
}

static int bridge_call(lua_State *L)
{
  if (!g_bridge_api)
  {
    if (!g_auf)
    {
      g_auf = LoadLibrary("bridge.auf");
      if (!g_auf)
      {
        return luaL_error(L, "could not found bridge.auf");
      }
    }
    typedef struct bridge *(__stdcall * GBAPI)(void);
    GBAPI GetBridgeAPI = (GBAPI)GetProcAddress(g_auf, "GetBridgeAPI");
    //struct bridge*(__stdcall *GetBridgeAPI)(void) = (void*)GetProcAddress(g_auf, "GetBridgeAPI");
    if (!GetBridgeAPI)
    {
      return luaL_error(L, "could not found GetBridgeAPI function in bridge.auf");
    }
    g_bridge_api = GetBridgeAPI();
    if (!g_bridge_api)
    {
      return luaL_error(L, "could not access BridgeAPI");
    }
  }

  const char *exe_path = lua_tostring(L, 1);
  if (!exe_path)
  {
    return luaL_error(L, "invalid exe path");
  }
  size_t buflen;
  const char *buf = lua_tolstring(L, 2, &buflen);

  if (lua_isstring(L, 3))
  {
    size_t mflen;
    const char *mf = lua_tolstring(L, 3, &mflen);
    int32_t mode = 0;
    for (size_t i = 0; i < mflen; ++i)
    {
      switch (mf[i])
      {
      case 'r':
      case 'R':
        mode |= MEM_MODE_READ;
        break;
      case 'w':
      case 'W':
        mode |= MEM_MODE_WRITE;
        break;
      case 'p':
      case 'P':
        mode |= MEM_MODE_DIRECT;
        break;
      }
    }
    if (mode & (MEM_MODE_READ | MEM_MODE_WRITE))
    {
      struct call_mem m;
      m.mode = mode;
      if (mode & MEM_MODE_DIRECT)
      {
        m.buf = (void *)lua_topointer(L, 4);
        m.width = lua_tointeger(L, 5);
        m.height = lua_tointeger(L, 6);
        if (!m.buf || m.width == 0 || m.height == 0)
        {
          return luaL_error(L, "invalid arguments");
        }
      }
      else
      {
        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "getpixeldata");
        lua_call(L, 0, 3);
        m.buf = (void *)lua_topointer(L, -3);
        m.width = lua_tointeger(L, -2);
        m.height = lua_tointeger(L, -1);
        lua_pop(L, 2);
      }
      int32_t rlen;
      void *r;
      int err = g_bridge_api->call(exe_path, buf, buflen, &m, &r, &rlen);
      if (err != ECALL_OK)
      {
        return bridge_call_error(L, err);
      }
      if (m.mode & MEM_MODE_WRITE && !(m.mode & MEM_MODE_DIRECT))
      {
        lua_getfield(L, -2, "putpixeldata");
        lua_pushvalue(L, -2);
        lua_call(L, 1, 0);
      }
      lua_pushlstring(L, r, rlen);
      return 1;
    }
  }

  int32_t rlen = 0;
  void *r = NULL;
  int err = g_bridge_api->call(exe_path, buf, buflen, NULL, &r, &rlen);
  if (err != ECALL_OK)
  {
    return bridge_call_error(L, err);
  }
  lua_pushlstring(L, r, rlen);
  return 1;
}

static uint64_t cyrb64(const uint32_t *src, const size_t len, const uint32_t seed)
{
  uint32_t h1 = 0x91eb9dc7 ^ seed, h2 = 0x41c6ce57 ^ seed;
  for (size_t i = 0; i < len; ++i)
  {
    h1 = (h1 ^ src[i]) * 2654435761;
    h2 = (h2 ^ src[i]) * 1597334677;
  }
  h1 = ((h1 ^ (h1 >> 16)) * 2246822507) ^ ((h2 ^ (h2 >> 13)) * 3266489909);
  h2 = ((h2 ^ (h2 >> 16)) * 2246822507) ^ ((h1 ^ (h1 >> 13)) * 3266489909);
  return (((uint64_t)h2) << 32) | ((uint64_t)h1);
}

static void to_hex(char *dst, uint64_t x)
{
  const char *chars = "0123456789abcdef";
  for (int i = 15; i >= 0; --i)
  {
    dst[i] = chars[x & 0xf];
    x >>= 4;
  }
}

static int bridge_calc_hash(lua_State *L)
{
  const void *p = lua_topointer(L, 1);
  const int w = lua_tointeger(L, 2);
  const int h = lua_tointeger(L, 3);
  char b[16];
  to_hex(b, cyrb64(p, w * h, 0x3fc0b49e));
  lua_pushlstring(L, b, 16);
  return 1;
}

static struct luaL_Reg fntable[] = {
    {"call", bridge_call},
    {"calc_hash", bridge_calc_hash},
    {NULL, NULL},
};

EXTERN_C int __declspec(dllexport) luaopen_bridge(lua_State *L)
{
  luaL_register(L, "bridge", fntable);
  return 1;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
  (void)hinstDLL;
  (void)lpvReserved;
  switch (fdwReason)
  {
  case DLL_PROCESS_ATTACH:
    break;

  case DLL_PROCESS_DETACH:
    if (g_auf)
    {
      FreeLibrary(g_auf);
      g_auf = NULL;
      g_bridge_api = NULL;
    }
    break;

  case DLL_THREAD_ATTACH:
    break;

  case DLL_THREAD_DETACH:
    break;
  }
  return TRUE;
}
