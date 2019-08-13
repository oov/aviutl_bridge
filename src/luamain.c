#include <lua.h>
#include <lauxlib.h>
#include <windows.h>

#include "bridge_public.h"

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
        struct bridge *__stdcall (*GetBridgeAPI)(void) = (void *)GetProcAddress(g_auf, "GetBridgeAPI");
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
        if ((mflen > 0 && mf[0] == 'r') || (mflen > 1 && mf[1] == 'r'))
        {
            mode |= MEM_MODE_READ;
        }
        if ((mflen > 0 && mf[0] == 'w') || (mflen > 1 && mf[1] == 'w'))
        {
            mode |= MEM_MODE_WRITE;
        }
        if (mode) {
            struct call_mem m;
            lua_getglobal(L, "obj");
            lua_getfield(L, -1, "getpixeldata");
            lua_call(L, 0, 3);
            m.buf = (void *)lua_topointer(L, -3);
            m.width = lua_tointeger(L, -2);
            m.height = lua_tointeger(L, -1);
            m.mode = mode;
            lua_pop(L, 2);
            int32_t rlen;
            void *r;
            int err = g_bridge_api->call(exe_path, buf, buflen, &m, &r, &rlen);
            if (err != ECALL_OK)
            {
                return bridge_call_error(L, err);
            }
            if (m.mode & MEM_MODE_WRITE)
            {
                lua_getfield(L, -2, "putpixeldata");
                lua_pushvalue(L, -2);
                lua_call(L, 1, 0);
            }
            lua_pushlstring(L, r, rlen);
            return 1;
        }
    }

    int32_t rlen;
    void *r;
    int err = g_bridge_api->call(exe_path, buf, buflen, NULL, &r, &rlen);
    if (err != ECALL_OK)
    {
        return bridge_call_error(L, err);
    }
    lua_pushlstring(L, r, rlen);
    return 1;
}

static struct luaL_Reg fntable[] = {
    {"call", bridge_call},
    {NULL, NULL},
};

EXTERN_C int __declspec(dllexport) luaopen_bridge(lua_State *L)
{
    luaL_register(L, "bridge", fntable);
    return 1;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
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
