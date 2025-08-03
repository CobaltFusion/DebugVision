#pragma once

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <dbghelp.h>

#include <print>
#include <iostream>

// rudimentary call stack
inline void PrintStackTrace()
{
    void* stack[100];
    HANDLE process = GetCurrentProcess();
    SymInitialize(process, NULL, TRUE);

    WORD frames = CaptureStackBackTrace(0, 100, stack, NULL);

    SYMBOL_INFO* symbol = (SYMBOL_INFO*)malloc(sizeof(SYMBOL_INFO) + 256);
    symbol->MaxNameLen = 255;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

    IMAGEHLP_LINE64 line;
    DWORD displacement = 0;
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (WORD i = 0; i < frames; ++i)
    {
        DWORD64 address = (DWORD64)(stack[i]);
        if (SymFromAddr(process, address, 0, symbol))
        {
            std::cout << frames - i - 1 << ": " << symbol->Name << " - 0x" << std::hex << symbol->Address;
            if (SymGetLineFromAddr64(process, address, &displacement, &line))
            {
                std::cout << std::dec << " (" << line.FileName << ":" << line.LineNumber << ")";
            }
            else
            {
                std::cout << " (no file information)";
            }
            std::cout << std::dec << "\n";
        }
        else
        {
            std::cout << frames - i - 1 << ": (no symbol information)\n";
        }
    }

    free(symbol);
}

inline LONG WINAPI CrashHandler(EXCEPTION_POINTERS*)
{
    std::cerr << "Crash detected! Printing stack trace:\n";
    PrintStackTrace();
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void InstallCrashHandler()
{
    AddVectoredExceptionHandler(1, CrashHandler);
}
