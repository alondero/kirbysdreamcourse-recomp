/* External x64 main-thread stack sampler. Build with MinGW:
 * gcc -O2 -Wall -Wextra tools/windows_guest_sampler.c -o local/guest-sampler.exe -ldbghelp -lwinmm
 * Samples are private local diagnostics, never release artifacts.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(_WIN64)
#error This sampler requires Windows x64.
#endif

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: sampler PID FRAME_RVA FIRST LAST OUTPUT TIMEOUT_SECONDS\n");
        return 2;
    }
    DWORD pid = strtoul(argv[1], NULL, 10);
    uintptr_t frame_rva = strtoull(argv[2], NULL, 16);
    int first = atoi(argv[3]), last = atoi(argv[4]);
    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    if (!process) return 3;
    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    MODULEENTRY32 module = {0};
    module.dwSize = sizeof(module);
    if (modules == INVALID_HANDLE_VALUE || !Module32First(modules, &module)) return 4;
    uintptr_t base = (uintptr_t)module.modBaseAddr;
    CloseHandle(modules);

    /* The oldest thread is the host main thread, even if workers already exist. */
    HANDLE threads = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0), main_thread = NULL;
    THREADENTRY32 entry = {0};
    entry.dwSize = sizeof(entry);
    ULONGLONG oldest = UINT64_MAX;
    if (threads != INVALID_HANDLE_VALUE && Thread32First(threads, &entry)) do {
        if (entry.th32OwnerProcessID != pid) continue;
        HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME | THREAD_QUERY_INFORMATION | SYNCHRONIZE,
                                   FALSE, entry.th32ThreadID);
        FILETIME created, exited, kernel, user;
        if (thread && GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            ULONGLONG time = ((ULONGLONG)created.dwHighDateTime << 32) | created.dwLowDateTime;
            if (time < oldest) {
                if (main_thread) CloseHandle(main_thread);
                main_thread = thread;
                oldest = time;
                thread = NULL;
            }
        }
        if (thread) CloseHandle(thread);
    } while (Thread32Next(threads, &entry));
    if (threads != INVALID_HANDLE_VALUE) CloseHandle(threads);
    if (!main_thread || !SymInitialize(process, NULL, TRUE)) return 5;
    FILE *out = fopen(argv[5], "w");
    if (!out) return 6;
    fprintf(out, "frame,stack_rvas\n");
    timeBeginPeriod(1);
    ULONGLONG deadline = GetTickCount64() + 1000ULL * strtoul(argv[6], NULL, 10);
    unsigned samples = 0, failures = 0;
    uint32_t random = 63;
    while (WaitForSingleObject(process, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
        /* Jitter avoids repeatedly sampling the same phase of the frame. */
        random = random * 1664525U + 1013904223U;
        Sleep(1 + random % 3);
        if (SuspendThread(main_thread) == (DWORD)-1) {
            DWORD error = GetLastError();
            if (WaitForSingleObject(main_thread, 20) != WAIT_OBJECT_0) {
                fprintf(stderr, "SuspendThread failed: %lu\n", error);
                failures++;
            }
            break;
        }
        CONTEXT context = {0};
        context.ContextFlags = CONTEXT_FULL;
        int frame = -1;
        DWORD64 addresses[96];
        unsigned depth = 0;
        if (ReadProcessMemory(process, (LPCVOID)(base + frame_rva), &frame, sizeof(frame), NULL) &&
            frame >= first && frame <= last && GetThreadContext(main_thread, &context)) {
            STACKFRAME64 stack = {0};
            stack.AddrPC.Offset = context.Rip;
            stack.AddrStack.Offset = context.Rsp;
            stack.AddrFrame.Offset = context.Rbp;
            stack.AddrPC.Mode = stack.AddrStack.Mode = stack.AddrFrame.Mode = AddrModeFlat;
            addresses[depth++] = context.Rip;
            while (depth < 96 && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, main_thread,
                   &stack, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
                if (!stack.AddrPC.Offset) break;
                if (stack.AddrPC.Offset != addresses[depth - 1]) addresses[depth++] = stack.AddrPC.Offset;
            }
        }
        /* Always resume before I/O; no target locks are acquired by the sampler. */
        if (ResumeThread(main_thread) == (DWORD)-1) {
            DWORD error = GetLastError();
            if (WaitForSingleObject(main_thread, 20) != WAIT_OBJECT_0) {
                fprintf(stderr, "ResumeThread failed: %lu\n", error);
                failures++;
            }
            break;
        }
        if (depth) {
            fprintf(out, "%d,", frame);
            for (unsigned i = 0; i < depth; i++) {
                if (i) fputc(';', out);
                if (addresses[i] >= base && addresses[i] < base + module.modBaseSize)
                    fprintf(out, "%llx", (unsigned long long)(addresses[i] - base));
                else {
                    SYMBOL_INFO_PACKAGE symbol = {0};
                    symbol.si.SizeOfStruct = sizeof(SYMBOL_INFO);
                    symbol.si.MaxNameLen = MAX_SYM_NAME;
                    DWORD64 displacement;
                    if (SymFromAddr(process, addresses[i], &displacement, &symbol.si))
                        fprintf(out, "external:%s", symbol.si.Name);
                    else fputs("external", out);
                }
            }
            fputc('\n', out);
            samples++;
        }
        if (frame > last) break;
    }
    timeEndPeriod(1);
    fclose(out);
    SymCleanup(process);
    CloseHandle(main_thread);
    CloseHandle(process);
    fprintf(stderr, "sampler: samples=%u failures=%u\n", samples, failures);
    return samples && !failures ? 0 : 7;
}
