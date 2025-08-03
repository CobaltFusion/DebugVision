#pragma once

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <dbghelp.h>

#include <print>
#include <iostream>

inline void PrintStackTrace() {
    void* stack[100];
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);

    WORD frames = CaptureStackBackTrace(0, 100, stack, NULL);
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    for (WORD i = 0; i < frames; ++i) {
        SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
        std::cout << frames - i - 1 << ": " << symbol->Name << " - 0x" << std::hex << symbol->Address << std::dec << "\n";
    }

    free(symbol);
}

inline LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ExceptionInfo) {
    std::cerr << "Crash detected! Printing stack trace:\n";
    PrintStackTrace();
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void InstallCrashHandler()
{
    AddVectoredExceptionHandler(1, CrashHandler);
}
