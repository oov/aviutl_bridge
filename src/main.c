#include <windows.h>
#include "aviutl.h"
#include "bridge.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"
#define THREAD_IMPLEMENTATION
#include "thread.h"

FILTER_DLL *filter_list[] = {&bridge_filter, NULL};

EXTERN_C FILTER_DLL __declspec(dllexport) * *__stdcall GetFilterTableList(void)
{
    return (FILTER_DLL **)&filter_list;
}

EXTERN_C struct bridge __declspec(dllexport) * __stdcall GetBridgeAPI(void)
{
    return &bridge_api;
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
        break;

    case DLL_THREAD_ATTACH:
        break;

    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
