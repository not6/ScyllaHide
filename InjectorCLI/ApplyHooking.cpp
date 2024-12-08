#include <Scylla/Logger.h>
#include <Scylla/OsInfo.h>
#include <Scylla/PebHider.h>
#include "Scylla/VersionPatch.h"

#include "ApplyHooking.h"
#include "DynamicMapping.h"
#include "RemoteHook.h"

#define STR(x) #x
#define HOOK(name) { \
    hdd->d##name = (t_##name)DetourCreateRemote(hProcess, "" STR(name) "", (void*)_##name, Hooked##name, true, &hdd->name##BackupSize); \
    if (hdd->d##name == nullptr) { return false; } }
#define HOOK_NATIVE(name) { \
    hdd->d##name = (t_##name)DetourCreateRemoteNative(hProcess, "" STR(name) "", (void*)_##name, Hooked##name, true, &hdd->name##BackupSize); \
    if (hdd->d##name == nullptr) { return false; } }
#define HOOK_NATIVE_NOTRAMP(name) DetourCreateRemoteNative(hProcess, "" STR(name) "", (void*)_##name, Hooked##name, false, &hdd->name##BackupSize)
#define FREE_HOOK(name) FreeMemory(hProcess, (void*)hdd->d##name); hdd->d##name = 0
#define RESTORE_JMP(name) RestoreJumper(hProcess, (void*)_##name, (void*)hdd->d##name, hdd->name##BackupSize)

extern scl::Logger g_log;

void * HookedNativeCallInternal = 0;
void * NativeCallContinue = 0;
int countNativeHooks = 0;
HOOK_NATIVE_CALL32 * HookNative = 0;
bool onceNativeCallContinue = false;
bool fatalFindSyscallIndexFailure = false;
bool fatalAlreadyHookedFailure = false;

#ifndef _WIN64
extern BYTE KiFastSystemCallBackup[20];
extern BYTE KiFastSystemCallWow64Backup[7];
extern DWORD KiFastSystemCallAddress;
extern DWORD KiFastSystemCallWow64Address;
#endif

HMODULE hKernel = 0;
HMODULE hKernelbase = 0;
HMODULE hNtdll = 0;
HMODULE hUser = 0;

t_KiUserExceptionDispatcher _KiUserExceptionDispatcher = 0;
t_OutputDebugStringA _OutputDebugStringA = 0;
t_GetTickCount _GetTickCount = 0;
t_GetTickCount64 _GetTickCount64 = 0;
t_GetLocalTime _GetLocalTime = 0;
t_GetSystemTime _GetSystemTime = 0;

t_NtUserBlockInput _NtUserBlockInput = 0;
t_NtUserBuildHwndList _NtUserBuildHwndList = 0;
t_NtUserFindWindowEx _NtUserFindWindowEx = 0;
t_NtUserQueryWindow _NtUserQueryWindow = 0;
t_NtUserGetForegroundWindow _NtUserGetForegroundWindow = 0;

t_NtSetInformationThread _NtSetInformationThread = 0;
t_NtQuerySystemInformation _NtQuerySystemInformation = 0;
t_NtQueryInformationThread _NtQueryInformationThread = 0;
t_NtQueryInformationProcess _NtQueryInformationProcess = 0;
t_NtSetInformationProcess _NtSetInformationProcess = 0;
t_NtQueryObject _NtQueryObject = 0;
t_NtYieldExecution _NtYieldExecution = 0;
t_NtGetContextThread _NtGetContextThread = 0;
t_NtSetContextThread _NtSetContextThread = 0;
t_NtContinue _NtContinue = 0;
t_NtClose _NtClose = 0;
t_NtDuplicateObject _NtDuplicateObject = 0;
t_NtSetDebugFilterState _NtSetDebugFilterState = 0;
t_NtCreateThread _NtCreateThread = 0;
t_NtCreateThreadEx _NtCreateThreadEx = 0;
t_NtQuerySystemTime _NtQuerySystemTime = 0;
t_NtQueryPerformanceCounter _NtQueryPerformanceCounter = 0;
t_NtResumeThread _NtResumeThread = 0;
t_NtOpenFile _NtOpenFile = 0;
t_NtCreateSection _NtCreateSection = 0;
t_NtMapViewOfSection _NtMapViewOfSection = 0;

bool ApplyNtdllHook(HOOK_DLL_DATA * hdd, HANDLE hProcess, BYTE * dllMemory, DWORD_PTR imageBase)
{
    hNtdll = GetModuleHandleW(L"ntdll.dll");

#ifndef _WIN64
    countNativeHooks = 0;
    onceNativeCallContinue = false;
    HookNative = hdd->HookNative;
#endif

    void * HookedNtSetInformationThread = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtSetInformationThread") + imageBase);
    void * HookedNtQuerySystemInformation = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtQuerySystemInformation") + imageBase);
    void * HookedNtQueryInformationThread = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtQueryInformationThread") + imageBase);
    void * HookedNtQueryInformationProcess = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtQueryInformationProcess") + imageBase);
    void * HookedNtSetInformationProcess = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtSetInformationProcess") + imageBase);
    void * HookedNtQueryObject = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtQueryObject") + imageBase);
    void * HookedNtYieldExecution = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtYieldExecution") + imageBase);
    void * HookedNtGetContextThread = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtGetContextThread") + imageBase);
    void * HookedNtSetContextThread = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtSetContextThread") + imageBase);
    void * HookedKiUserExceptionDispatcher = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedKiUserExceptionDispatcher") + imageBase);
    void * HookedNtContinue = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtContinue") + imageBase);
    void * HookedNtClose = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtClose") + imageBase);
    void * HookedNtDuplicateObject = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtDuplicateObject") + imageBase);
    void * HookedNtSetDebugFilterState = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtSetDebugFilterState") + imageBase);
    void * HookedNtCreateThread = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtCreateThread") + imageBase);
    void * HookedNtCreateThreadEx = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtCreateThreadEx") + imageBase);
    void * HookedNtQuerySystemTime = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtQuerySystemTime") + imageBase);
    void * HookedNtQueryPerformanceCounter = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtQueryPerformanceCounter") + imageBase);
    void * HookedNtResumeThread = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtResumeThread") + imageBase);
    void * HookedNtOpenFile = (void*)(GetDllFunctionAddressRVA(dllMemory, "HookedNtOpenFile") + imageBase);
    void * HookedNtCreateSection = (void*)(GetDllFunctionAddressRVA(dllMemory, "HookedNtCreateSection") + imageBase);
    void * HookedNtMapViewOfSection = (void*)(GetDllFunctionAddressRVA(dllMemory, "HookedNtMapViewOfSection") + imageBase);

    HookedNativeCallInternal = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNativeCallInternal") + imageBase);

    _NtSetInformationThread = (t_NtSetInformationThread)GetProcAddress(hNtdll, "NtSetInformationThread");
    _NtQuerySystemInformation = (t_NtQuerySystemInformation)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    _NtQueryInformationThread = (t_NtQueryInformationThread)GetProcAddress(hNtdll, "NtQueryInformationThread");
    _NtQueryInformationProcess = (t_NtQueryInformationProcess)GetProcAddress(hNtdll, "NtQueryInformationProcess");
    _NtSetInformationProcess = (t_NtSetInformationProcess)GetProcAddress(hNtdll, "NtSetInformationProcess");
    _NtQueryObject = (t_NtQueryObject)GetProcAddress(hNtdll, "NtQueryObject");
    _NtYieldExecution = (t_NtYieldExecution)GetProcAddress(hNtdll, "NtYieldExecution");
    _NtGetContextThread = (t_NtGetContextThread)GetProcAddress(hNtdll, "NtGetContextThread");
    _NtSetContextThread = (t_NtSetContextThread)GetProcAddress(hNtdll, "NtSetContextThread");
    _KiUserExceptionDispatcher = (t_KiUserExceptionDispatcher)GetProcAddress(hNtdll, "KiUserExceptionDispatcher");
    _NtContinue = (t_NtContinue)GetProcAddress(hNtdll, "NtContinue");
    _NtClose = (t_NtClose)GetProcAddress(hNtdll, "NtClose");
    _NtDuplicateObject = (t_NtDuplicateObject)GetProcAddress(hNtdll, "NtDuplicateObject");
    _NtSetDebugFilterState = (t_NtSetDebugFilterState)GetProcAddress(hNtdll, "NtSetDebugFilterState");
    _NtCreateThread = (t_NtCreateThread)GetProcAddress(hNtdll, "NtCreateThread");
    _NtCreateThreadEx = (t_NtCreateThreadEx)GetProcAddress(hNtdll, "NtCreateThreadEx");
    _NtQuerySystemTime = (t_NtQuerySystemTime)GetProcAddress(hNtdll, "NtQuerySystemTime");
    _NtQueryPerformanceCounter = (t_NtQueryPerformanceCounter)GetProcAddress(hNtdll, "NtQueryPerformanceCounter");
    _NtResumeThread = (t_NtResumeThread)GetProcAddress(hNtdll, "NtResumeThread");
    _NtOpenFile = (t_NtOpenFile)GetProcAddress(hNtdll, "NtOpenFile");
    _NtCreateSection = (t_NtCreateSection)GetProcAddress(hNtdll, "NtCreateSection");
    _NtMapViewOfSection = (t_NtMapViewOfSection)GetProcAddress(hNtdll, "NtMapViewOfSection");

    g_log.LogDebug(L"ApplyNtdllHook -> _NtSetInformationThread %p _NtQuerySystemInformation %p _NtQueryInformationThread %p _NtQueryInformationProcess %p _NtSetInformationProcess %p _NtQueryObject %p",
        _NtSetInformationThread,
        _NtQuerySystemInformation,
        _NtQueryInformationThread,
        _NtQueryInformationProcess,
        _NtSetInformationProcess,
        _NtQueryObject);
    g_log.LogDebug(L"ApplyNtdllHook -> _NtYieldExecution %p _NtGetContextThread %p _NtSetContextThread %p _KiUserExceptionDispatcher %p _NtContinue %p",
        _NtYieldExecution,
        _NtGetContextThread,
        _NtSetContextThread,
        _KiUserExceptionDispatcher,
        _NtContinue);
    g_log.LogDebug(L"ApplyNtdllHook -> _NtClose %p _NtDuplicateObject %p _NtSetDebugFilterState %p _NtCreateThread %p _NtCreateThreadEx %p _NtQuerySystemTime %p _NtQueryPerformanceCounter %p _NtResumeThread %p",
        _NtClose,
        _NtDuplicateObject,
        _NtSetDebugFilterState,
        _NtCreateThread,
        _NtCreateThreadEx,
        _NtQuerySystemTime,
        _NtQueryPerformanceCounter,
        _NtResumeThread);
    g_log.LogDebug(L"ApplyNtdllHook -> _NtOpenFile %p _NtCreateSection %p _NtMapViewOfSection %p",
        _NtOpenFile,
        _NtCreateSection,
        _NtMapViewOfSection);

    if (hdd->EnableNtSetInformationThreadHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtSetInformationThread");
        HOOK_NATIVE(NtSetInformationThread);
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtQueryInformationThread");
        HOOK_NATIVE(NtQueryInformationThread);
    }
    if (hdd->EnableNtQuerySystemInformationHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtQuerySystemInformation");
        HOOK_NATIVE(NtQuerySystemInformation);
    }
    if (hdd->EnableNtQueryInformationProcessHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtQueryInformationProcess");
        HOOK_NATIVE(NtQueryInformationProcess);
    }
    if (hdd->EnableNtSetInformationProcessHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtSetInformationProcess");
        HOOK_NATIVE(NtSetInformationProcess);
    }

    if (hdd->EnableNtQueryObjectHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtQueryObject");
        HOOK_NATIVE(NtQueryObject);
    }
    if (hdd->EnableNtYieldExecutionHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtYieldExecution");
        HOOK_NATIVE(NtYieldExecution);
    }
    if (hdd->EnableNtGetContextThreadHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtGetContextThread");
        HOOK_NATIVE(NtGetContextThread);
    }
    if (hdd->EnableNtSetContextThreadHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtSetContextThread");
        HOOK_NATIVE(NtSetContextThread);
    }

    if (hdd->EnableNtCloseHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtClose");
        HOOK_NATIVE(NtClose);
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtDuplicateObject");
        HOOK_NATIVE(NtDuplicateObject);
    }
    if (hdd->EnablePreventThreadCreation == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtCreateThread");
        HOOK_NATIVE(NtCreateThread);
    }

    if (((hdd->EnablePreventThreadCreation == TRUE) || (hdd->EnableNtCreateThreadExHook == TRUE)) && _NtCreateThreadEx != 0)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtCreateThreadEx");
        HOOK_NATIVE(NtCreateThreadEx);
    }

    if (hdd->EnableNtSetDebugFilterStateHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtSetDebugFilterState");
        HOOK_NATIVE_NOTRAMP(NtSetDebugFilterState);
    }

    if (hdd->EnableKiUserExceptionDispatcherHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking KiUserExceptionDispatcher");
#ifdef _WIN64
        // The x86_64 version of this function currently contains relative offset instructions
        // which will cause problems with the naive trampoline generation currently in use.
        // Therefore, let us apply some manual patching instead.
        PVOID address = (PVOID)_KiUserExceptionDispatcher;
        const bool startsWithCld = ((UINT8*)address)[0] == 0xFC; // true on Vista and later
        if ((startsWithCld && *(PUINT32)address != 0x058B48FC) ||
            (!startsWithCld && (*(PUINT32)address & 0xFFFFFF) != 0x058B48))
        {
            g_log.LogError(L"ApplyNtdllHook -> KiUserExceptionDispatcher pattern mismatch 0x%lx", *(PUINT32)address);
        }
        else
        {
            // This function currently has a nine byte NOP before it, probably for hot patching?
            // There is also some alignment space. Let's borrow this to write our trampoline.
            uint8_t trampoline[] =
            {
                0xFF, 0x15, 0x0F, 0x00, 0x00, 0x00,         // call qword ptr[+15]
                0xFC,                                       // cld
                0x48, 0x8B, 0x05, 0x22, 0xA4, 0x0D, 0x00,   // mov rax, qword ptr:[<Wow64PrepareForException>]
                0x48, 0x85, 0xC0,                           // test rax,rax
                0xEB, 0x0B                                  // jmp <next real instruction>
            };

            // Deal with XP/2003
            if (!startsWithCld)
            {
                trampoline[6] = 0x90;                       // cld -> nop
                trampoline[18] -= 0x1;                      // <next real instruction> -= 1
            }

            // update RVA of Wow64PrepareForException
            UINT32 rvaWow64PrepareForException;
            ReadProcessMemory(hProcess, (LPCVOID)(((UINT_PTR)address) + (startsWithCld ? 4 : 3)), (PVOID)&rvaWow64PrepareForException,
                sizeof(rvaWow64PrepareForException), nullptr);

            // instruction is moved up 12/13 bytes. update trampoline
            rvaWow64PrepareForException += (startsWithCld ? 13 : 12);
            memcpy(&trampoline[10], &rvaWow64PrepareForException, sizeof(rvaWow64PrepareForException));

            uint8_t hook[] =
            {
                0xEB, 0xEB,     // jmp -21
                0xFE, 0xED, 0xFA, 0xCE, 0xDE, 0xAD, 0xBE, 0xEF,
            };

            // insert hook into payload
            memcpy(&hook[2], &HookedKiUserExceptionDispatcher, sizeof(PVOID));

            // for most hooks the following fields are for the trampoline. this works for them because
            // the trampoline is an identical copy of what was at the start of the function. since this
            // is not the case for us, we must preserve the original bytes in memory we deliberately set
            // aside for this purpose.
            PVOID backup_location = VirtualAllocEx(hProcess, nullptr, sizeof(hook), MEM_COMMIT,
                PAGE_READWRITE);

            hdd->dKiUserExceptionDispatcher = (decltype(hdd->dKiUserExceptionDispatcher))(backup_location);
            hdd->KiUserExceptionDispatcherBackupSize = sizeof(hook);

            // backup start of function
            uint8_t backup_prologue[sizeof(hook)];
            ReadProcessMemory(hProcess, address, backup_prologue, sizeof(backup_prologue), nullptr);
            WriteProcessMemory(hProcess, backup_location, backup_prologue, sizeof(backup_prologue), nullptr);

            // install trampoline
            PVOID trampoline_location = (PVOID)(((UINT_PTR)address) - sizeof(trampoline));
            WriteProcessMemory(hProcess, trampoline_location, trampoline, sizeof(trampoline), nullptr);

            // install hook
            WriteProcessMemory(hProcess, address, hook, sizeof(hook), nullptr);
        }
#else
        HOOK(KiUserExceptionDispatcher);
#endif
    }
    if (hdd->EnableNtContinueHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtContinue");
        HOOK_NATIVE(NtContinue);
    }

    if (hdd->EnableNtQuerySystemTimeHook == TRUE && _NtQuerySystemTime != 0)
    {
#ifdef _WIN64
        ULONG_PTR address = (ULONG_PTR)_NtQuerySystemTime;
        if (*(PUCHAR)address == 0xE9) // jmp rel32
        {
            g_log.LogDebug(L"ApplyNtdllHook -> Finding jmp to RtlQuerySystemTime at NtQuerySystemTime");
            LONG relativeOffset = *(PLONG)(address + 1);
            _NtQuerySystemTime = (t_NtQuerySystemTime)(address + relativeOffset + 5);
        }
#endif
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtQuerySystemTime at %p", _NtQuerySystemTime);
        HOOK_NATIVE(NtQuerySystemTime);
    }
    if (hdd->EnableNtQueryPerformanceCounterHook == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtQueryPerformanceCounter");
        HOOK_NATIVE(NtQueryPerformanceCounter);
    }

    if (hdd->EnableMalwareRunPeUnpacker == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtResumeThread for RUNPE UNPACKER");
        HOOK_NATIVE(NtResumeThread);
    }

    if (hdd->EnablePebOsBuildNumber == TRUE)
    {
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtOpenFile");
        HOOK_NATIVE(NtOpenFile);
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtCreateSection");
        HOOK_NATIVE(NtCreateSection);
        g_log.LogDebug(L"ApplyNtdllHook -> Hooking NtMapViewOfSection");
        HOOK_NATIVE(NtMapViewOfSection);
    }

    hdd->isNtdllHooked = TRUE;

    return true;
}

bool ApplyKernel32Hook(HOOK_DLL_DATA * hdd, HANDLE hProcess, BYTE * dllMemory, DWORD_PTR imageBase)
{
    hKernel = GetModuleHandleW(L"kernel32.dll");
    hKernelbase = GetModuleHandleW(L"kernelbase.dll");

    if (GetModuleBaseRemote(hProcess, L"kernel32.dll") == nullptr ||
        (hKernelbase != nullptr && GetModuleBaseRemote(hProcess, L"kernelbase.dll") == nullptr))
    {
        hdd->isKernel32Hooked = FALSE;
        return true;
    }

    void * HookedOutputDebugStringA = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedOutputDebugStringA") + imageBase);
    void * HookedGetTickCount = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedGetTickCount") + imageBase);
    void * HookedGetTickCount64 = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedGetTickCount64") + imageBase);
    void * HookedGetLocalTime = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedGetLocalTime") + imageBase);
    void * HookedGetSystemTime = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedGetSystemTime") + imageBase);

    HMODULE hCurrent = hKernel;
    if (hKernelbase)
    {
        hCurrent = hKernelbase;
        g_log.LogDebug(L"ApplyKernel32Hook -> Using Kernelbase %p instead of kernel32 %p", hKernelbase, hKernel);
    }

    _GetTickCount = (t_GetTickCount)GetProcAddress(hCurrent, "GetTickCount");
    _GetTickCount64 = (t_GetTickCount64)GetProcAddress(hCurrent, "GetTickCount64");
    _GetLocalTime = (t_GetLocalTime)GetProcAddress(hCurrent, "GetLocalTime");
    _GetSystemTime = (t_GetSystemTime)GetProcAddress(hCurrent, "GetSystemTime");

    _OutputDebugStringA = (t_OutputDebugStringA)GetProcAddress(hCurrent, "OutputDebugStringA");

    g_log.LogDebug(L"ApplyKernel32Hook -> _GetTickCount %p _GetTickCount64 %p _GetLocalTime %p _GetSystemTime %p _OutputDebugStringA %p",
        _GetTickCount,
        _GetTickCount64,
        _GetLocalTime,
        _GetSystemTime,
        _OutputDebugStringA);

    if (hdd->EnableGetTickCountHook == TRUE)
    {
        g_log.LogDebug(L"ApplyKernel32Hook -> Hooking GetTickCount");
        HOOK(GetTickCount);
    }
    if (hdd->EnableGetTickCount64Hook == TRUE && _GetTickCount64 != 0)
    {
        g_log.LogDebug(L"ApplyKernel32Hook -> Hooking GetTickCount64");
        HOOK(GetTickCount64);
    }
    if (hdd->EnableGetLocalTimeHook == TRUE)
    {
        g_log.LogDebug(L"ApplyKernel32Hook -> Hooking GetLocalTime");
        HOOK(GetLocalTime);
    }
    if (hdd->EnableGetSystemTimeHook == TRUE)
    {
        g_log.LogDebug(L"ApplyKernel32Hook -> Hooking GetSystemTime");
        HOOK(GetSystemTime);
    }
    if (hdd->EnableOutputDebugStringHook == TRUE)
    {
        g_log.LogDebug(L"ApplyKernel32Hook -> Hooking OutputDebugStringA");
        HOOK(OutputDebugStringA);
    }
    hdd->isKernel32Hooked = TRUE;

    return true;
}

bool ApplyUserHook(HOOK_DLL_DATA * hdd, HANDLE hProcess, BYTE * dllMemory, DWORD_PTR imageBase)
{
    if (GetModuleBaseRemote(hProcess, L"user32.dll") == nullptr &&
        GetModuleBaseRemote(hProcess, L"win32u.dll") == nullptr)
    {
        hdd->isUserDllHooked = FALSE;
        return true;
    }

    void * HookedNtUserBlockInput = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtUserBlockInput") + imageBase);
    void * HookedNtUserFindWindowEx = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtUserFindWindowEx") + imageBase);
    void * HookedNtUserBuildHwndList = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtUserBuildHwndList") + imageBase);
    void * HookedNtUserBuildHwndList_Eight = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtUserBuildHwndList_Eight") + imageBase);
    void * HookedNtUserQueryWindow = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtUserQueryWindow") + imageBase);
    void * HookedNtUserGetForegroundWindow = (void *)(GetDllFunctionAddressRVA(dllMemory, "HookedNtUserGetForegroundWindow") + imageBase);

    g_log.LogDebug(L"ApplyUserHook -> HookedNtUserBlockInput %p HookedNtUserFindWindowEx %p HookedNtUserBuildHwndList %p HookedNtUserBuildHwndList_Eight %p HookedNtUserQueryWindow %p HookedNtUserGetForegroundWindow %p",
        HookedNtUserBlockInput,
        HookedNtUserFindWindowEx,
        HookedNtUserBuildHwndList,
        HookedNtUserBuildHwndList_Eight,
        HookedNtUserQueryWindow,
        HookedNtUserGetForegroundWindow);

    _NtUserBlockInput = (t_NtUserBlockInput)hdd->NtUserBlockInputVA;
    _NtUserFindWindowEx = (t_NtUserFindWindowEx)hdd->NtUserFindWindowExVA;
    _NtUserBuildHwndList = (t_NtUserBuildHwndList)hdd->NtUserBuildHwndListVA;
    _NtUserQueryWindow = (t_NtUserQueryWindow)hdd->NtUserQueryWindowVA;
    _NtUserGetForegroundWindow = (t_NtUserGetForegroundWindow)hdd->NtUserGetForegroundWindowVA;

    hdd->NtUserQueryWindow = _NtUserQueryWindow;
    hdd->NtUserGetClassName = (t_NtUserGetClassName)hdd->NtUserGetClassNameVA;
    hdd->NtUserInternalGetWindowText = (t_NtUserInternalGetWindowText)hdd->NtUserInternalGetWindowTextVA;
    hdd->NtUserGetThreadState = (t_NtUserGetThreadState)hdd->NtUserGetThreadStateVA;

    g_log.LogDebug(L"ApplyUserHook -> _NtUserBlockInput %p _NtUserFindWindowEx %p _NtUserBuildHwndList %p _NtUserQueryWindow %p _NtUserGetForegroundWindow %p",
        _NtUserBlockInput,
        _NtUserFindWindowEx,
        _NtUserBuildHwndList,
        _NtUserQueryWindow,
        _NtUserGetForegroundWindow);

    if (hdd->EnableNtUserBlockInputHook)
    {
        g_log.LogDebug(L"ApplyUserHook -> Hooking NtUserBlockInput");
        HOOK_NATIVE(NtUserBlockInput);
    }
    if (hdd->EnableNtUserFindWindowExHook)
    {
        g_log.LogDebug(L"ApplyUserHook -> Hooking NtUserFindWindowEx");
        HOOK_NATIVE(NtUserFindWindowEx);
    }
    if (hdd->EnableNtUserBuildHwndListHook)
    {
        g_log.LogDebug(L"ApplyUserHook -> Hooking NtUserBuildHwndList");
        //HOOK_NATIVE(NtUserBuildHwndList); // Not possible here because Windows >= 8 uses a different function export
        hdd->dNtUserBuildHwndList = (t_NtUserBuildHwndList)DetourCreateRemoteNative(hProcess, "NtUserBuildHwndList", (PVOID)_NtUserBuildHwndList,
            (scl::GetWindowsVersion() <= scl::OS_WIN_7 ? HookedNtUserBuildHwndList : HookedNtUserBuildHwndList_Eight),
            true, &hdd->NtUserBuildHwndListBackupSize);
        if (hdd->dNtUserBuildHwndList == nullptr)
            return false;
    }
    if (hdd->EnableNtUserQueryWindowHook)
    {
        g_log.LogDebug(L"ApplyUserHook -> Hooking NtUserQueryWindow");
        HOOK_NATIVE(NtUserQueryWindow);
    }
    if (hdd->EnableNtUserGetForegroundWindowHook)
    {
        g_log.LogDebug(L"ApplyUserHook -> Hooking NtUserGetForegroundWindow");
        HOOK_NATIVE(NtUserGetForegroundWindow);
    }

    hdd->isUserDllHooked = TRUE;

    return true;
}

void ApplyPEBPatch(HANDLE hProcess, DWORD flags)
{
    auto peb = scl::GetPeb(hProcess);
    if (!peb) {
        g_log.LogError(L"Failed to read PEB from remote process");
    }
    else
    {
        if (flags & PEB_PATCH_BeingDebugged)
            peb->BeingDebugged = FALSE;
        if (flags & PEB_PATCH_NtGlobalFlag)
            peb->NtGlobalFlag &= ~0x70;

        if (flags & PEB_PATCH_ProcessParameters) {
            if (!scl::PebPatchProcessParameters(peb.get(), hProcess))
                g_log.LogError(L"Failed to patch PEB!ProcessParameters");
        }

        if (flags & PEB_PATCH_HeapFlags)
        {
            if (!scl::PebPatchHeapFlags(peb.get(), hProcess))
                g_log.LogError(L"Failed to patch flags in PEB!ProcessHeaps");
        }

        if (flags & PEB_PATCH_OsBuildNumber)
        {
            peb->OSBuildNumber = FAKE_VERSION;
        }

        if (!scl::SetPeb(hProcess, peb.get()))
            g_log.LogError(L"Failed to write PEB to remote process");

    }

#ifndef _WIN64
    if (!scl::IsWow64Process(hProcess))
        return;

    auto peb64 = scl::Wow64GetPeb64(hProcess);
    if (!peb64) {
        g_log.LogError(L"Failed to read PEB64 from remote process");
    }
    else
    {
        if (flags & PEB_PATCH_BeingDebugged)
            peb64->BeingDebugged = FALSE;
        if (flags & PEB_PATCH_NtGlobalFlag)
            peb64->NtGlobalFlag &= ~0x70;

        if (flags & PEB_PATCH_ProcessParameters) {
            if (!scl::Wow64Peb64PatchProcessParameters(peb64.get(), hProcess))
                g_log.LogError(L"Failed to patch PEB64!ProcessParameters");
        }

        if (flags & PEB_PATCH_HeapFlags)
        {
            if (!scl::Wow64Peb64PatchHeapFlags(peb64.get(), hProcess))
                g_log.LogError(L"Failed to patch flags in PEB64!ProcessHeaps");
        }

        if (flags & PEB_PATCH_OsBuildNumber)
        {
            peb64->OSBuildNumber = FAKE_VERSION;
        }

        if (!scl::Wow64SetPeb64(hProcess, peb64.get()))
            g_log.LogError(L"Failed to write PEB64 to remote process");
    }
#endif
}

void ApplyNtdllVersionPatch(HANDLE hProcess)
{
    // This will get the 32 bit ntdll if we are on Wow64, which is fine.
    // Note that this relies on the addresses of DLLs in \KnownDlls[32] to be the same for all processes
    const PVOID Ntdll = GetModuleHandleW(L"ntdll.dll");
    ApplyNtdllVersionPatch(hProcess, Ntdll);
}

void RestoreMemory(HANDLE hProcess, DWORD_PTR address, void * buffer, int bufferSize)
{
    DWORD protect = 0;
    if (address && buffer && bufferSize)
    {
        if (VirtualProtectEx(hProcess, (void *)address, bufferSize, PAGE_EXECUTE_READWRITE, &protect))
        {
            WriteProcessMemory(hProcess, (void *)address, buffer, bufferSize, 0);

            VirtualProtectEx(hProcess, (void *)address, bufferSize, protect, &protect);
        }
    }

}

void RestoreJumper(HANDLE hProcess, void* address, void * backupAddress, DWORD backupSize)
{
    if (address && backupAddress && backupSize)
    {
        void * backup = malloc(backupSize);
        if (backup)
        {
            if (ReadProcessMemory(hProcess, backupAddress, backup, backupSize, 0))
            {
                RestoreMemory(hProcess, (DWORD_PTR)address, backup, backupSize);
            }

            free(backup);
        }
    }
}

void FreeMemory(HANDLE hProcess, void * buffer)
{
    if (hProcess && buffer)
    {
        VirtualFreeEx(hProcess, buffer, 0, MEM_RELEASE);
    }
}

void RestoreNtdllHooks(HOOK_DLL_DATA * hdd, HANDLE hProcess)
{
#ifndef _WIN64
    if (scl::IsWow64Process(hProcess))
    {
        RestoreMemory(hProcess, KiFastSystemCallWow64Address, KiFastSystemCallWow64Backup, sizeof(KiFastSystemCallWow64Backup));
    }
    else
    {
        if (KiFastSystemCallAddress != 0)
        {
            RestoreMemory(hProcess, KiFastSystemCallAddress, KiFastSystemCallBackup, sizeof(KiFastSystemCallBackup));
        }
        else
        {
            RESTORE_JMP(NtClose);
            RESTORE_JMP(NtDuplicateObject);
            RESTORE_JMP(NtContinue);
            RESTORE_JMP(NtCreateThreadEx);
            RESTORE_JMP(NtCreateThread);
            RESTORE_JMP(NtSetContextThread);
            RESTORE_JMP(NtGetContextThread);
            RESTORE_JMP(NtYieldExecution);
            RESTORE_JMP(NtQueryObject);
            RESTORE_JMP(NtSetInformationProcess);
            RESTORE_JMP(NtQueryInformationThread);
            RESTORE_JMP(NtQueryInformationProcess);
            RESTORE_JMP(NtQuerySystemInformation);
            RESTORE_JMP(NtSetInformationThread);
            RESTORE_JMP(NtOpenFile);
            RESTORE_JMP(NtCreateSection);
            RESTORE_JMP(NtMapViewOfSection);
        }
    }
#else
    RESTORE_JMP(NtClose);
    RESTORE_JMP(NtDuplicateObject);
    RESTORE_JMP(NtContinue);
    RESTORE_JMP(NtCreateThreadEx);
    RESTORE_JMP(NtCreateThread);
    RESTORE_JMP(NtSetContextThread);
    RESTORE_JMP(NtGetContextThread);
    RESTORE_JMP(NtYieldExecution);
    RESTORE_JMP(NtQueryObject);
    RESTORE_JMP(NtSetInformationProcess);
    RESTORE_JMP(NtQueryInformationThread);
    RESTORE_JMP(NtQueryInformationProcess);
    RESTORE_JMP(NtQuerySystemInformation);
    RESTORE_JMP(NtSetInformationThread);
    RESTORE_JMP(NtOpenFile);
    RESTORE_JMP(NtCreateSection);
    RESTORE_JMP(NtMapViewOfSection);
#endif

    FREE_HOOK(NtClose);
    FREE_HOOK(NtDuplicateObject);
    FREE_HOOK(NtContinue);
    FREE_HOOK(NtCreateThreadEx);
    FREE_HOOK(NtCreateThread);
    FREE_HOOK(NtSetContextThread);
    FREE_HOOK(NtGetContextThread);
    FREE_HOOK(NtYieldExecution);
    FREE_HOOK(NtQueryObject);
    FREE_HOOK(NtSetInformationProcess);
    FREE_HOOK(NtQueryInformationThread);
    FREE_HOOK(NtQueryInformationProcess);
    FREE_HOOK(NtQuerySystemInformation);
    FREE_HOOK(NtSetInformationThread);
    FREE_HOOK(NtOpenFile);
    FREE_HOOK(NtCreateSection);
    FREE_HOOK(NtMapViewOfSection);


    RESTORE_JMP(KiUserExceptionDispatcher);
    FREE_HOOK(KiUserExceptionDispatcher);


    hdd->isNtdllHooked = FALSE;
}

void RestoreKernel32Hooks(HOOK_DLL_DATA * hdd, HANDLE hProcess)
{
    RESTORE_JMP(OutputDebugStringA);
    RESTORE_JMP(GetTickCount);
    RESTORE_JMP(GetTickCount64);
    RESTORE_JMP(GetLocalTime);
    RESTORE_JMP(GetSystemTime);

    FREE_HOOK(OutputDebugStringA);
    FREE_HOOK(GetTickCount);
    FREE_HOOK(GetTickCount64);
    FREE_HOOK(GetLocalTime);
    FREE_HOOK(GetSystemTime);

    hdd->isKernel32Hooked = FALSE;
}

void RestoreUserHooks(HOOK_DLL_DATA * hdd, HANDLE hProcess)
{
#ifndef _WIN64
    if (!scl::IsWow64Process(hProcess) && KiFastSystemCallAddress == 0)
    {
        RESTORE_JMP(NtUserBlockInput);
        RESTORE_JMP(NtUserFindWindowEx);
        RESTORE_JMP(NtUserBuildHwndList);
        RESTORE_JMP(NtUserQueryWindow);
        RESTORE_JMP(NtUserGetForegroundWindow);
    }
#else
    RESTORE_JMP(NtUserBlockInput);
    RESTORE_JMP(NtUserFindWindowEx);
    RESTORE_JMP(NtUserBuildHwndList);
    RESTORE_JMP(NtUserQueryWindow);
    RESTORE_JMP(NtUserGetForegroundWindow);
#endif

    FREE_HOOK(NtUserBlockInput);
    FREE_HOOK(NtUserFindWindowEx);
    FREE_HOOK(NtUserBuildHwndList);
    FREE_HOOK(NtUserQueryWindow);
    FREE_HOOK(NtUserGetForegroundWindow);

    hdd->isUserDllHooked = FALSE;
}

void RestoreHooks(HOOK_DLL_DATA * hdd, HANDLE hProcess)
{
    if (hdd->isNtdllHooked)
    {
        RestoreNtdllHooks(hdd, hProcess);
    }

    if (hdd->isKernel32Hooked)
    {
        RestoreKernel32Hooks(hdd, hProcess);
    }

    if (hdd->isUserDllHooked)
    {
        RestoreUserHooks(hdd, hProcess);
    }

    FreeMemory(hProcess, hdd->hDllImage);
    hdd->hDllImage = 0;
}

bool ApplyHook(HOOK_DLL_DATA * hdd, HANDLE hProcess, BYTE * dllMemory, DWORD_PTR imageBase)
{
    bool success = true;
    hdd->hDllImage = (HMODULE)imageBase;

    if (!hdd->isNtdllHooked)
    {
        success = success && ApplyNtdllHook(hdd, hProcess, dllMemory, imageBase);
    }
    if (!hdd->isKernel32Hooked)
    {
        success = success && ApplyKernel32Hook(hdd, hProcess, dllMemory, imageBase);
    }
    if (!hdd->isUserDllHooked)
    {
        success = success && ApplyUserHook(hdd, hProcess, dllMemory, imageBase);
    }

#ifndef _WIN64
    hdd->NativeCallContinue = NativeCallContinue;
#endif

    return success;
}
