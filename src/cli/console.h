#pragma once

#ifdef _WIN32
#include <windows.h>

#include <iostream>

// The manifest makes everything this process writes UTF-8. A console reads the
// bytes in its own output code page, so it is switched for the run and put back
// afterwards: the setting belongs to the console, which outlives the process.
// A pipe or a file takes the bytes as they are and is left alone.
class ConsoleUtf8 {
public:
    ConsoleUtf8() {
        DWORD mode = 0;
        if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode) ||
            GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode)) {
            previous_ = GetConsoleOutputCP();
            SetConsoleOutputCP(CP_UTF8);
        }
    }
    ~ConsoleUtf8() {
        if (previous_ == 0) return;
        // What is still buffered has to reach the console before its code page
        // changes back.
        std::cout.flush();
        std::cerr.flush();
        SetConsoleOutputCP(previous_);
    }

private:
    UINT previous_ = 0;
};
#endif
