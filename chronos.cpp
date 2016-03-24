/* chronos - a crude substitution for POSIX `time` command line utility
   on Windows.
   Aggregates and reports user and kernel times for process and its children.
   Attempts to mimic output format used on Linux

Copyright (c) 2016, Grigory Rechistov
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdint>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#include <windows.h>

/* Get a human-readable description of the last error.
   Kinda like POSIX's strerror() */
std::wstring GetLastErrorDescription() {
    DWORD errcode = GetLastError();
    if (errcode == 0) return std::wstring();

    LPWSTR buf = NULL;
    size_t bufSize = FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | 
                                   FORMAT_MESSAGE_IGNORE_INSERTS |
                                    FORMAT_MESSAGE_FROM_SYSTEM, 
                                    NULL, errcode,
                                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                    (LPWSTR)&buf, 0, NULL);
    std::wstring message(buf, bufSize);
    LocalFree(buf);
    return message;
}

void UsageAndExit(wchar_t *argv[]) {
    std::wcerr <<
        "chronos - report wallclock, user and system times of process\n"
        "Copyright (c) 2016, Grigory Rechistov\n\n" 
        "Usage: " << argv[0] << " [-v] [-o file] [--] program [options]\n"
        "\n"
        "Run program and report its resources usage\n"
        "   --verbose, -v          produce results in verbose format\n"
        "   --output, -o filename  write result to filename instead of stdout\n"
        "   program                program name to start\n"
        "   options                the program's own arguments\n" << std::endl;
    exit(1);
}

/* Discovered command line options */
struct CliParams {
    bool verbose; /* true if verbose output */
    std::wstring outputFileName; /* file name to write results, or empty string */
    std::wstring cmdLine; /* The rest of command line options combined in a string */
};

/* Returns true on success, false if parsing failed */
/* BUG: may not handle quoted arguments and spaces in them as a whole */
bool ParseArgv(int argc, wchar_t *argv[], CliParams &result) {
    assert(argc >= 1);
    argv++;
    argc--;
    std::vector<std::wstring> arguments(argc);
    /* We start counting from 1 to omit program name */
    for (int i = 0; i < argc; i++) {
        arguments[i] = std::wstring(argv[i]);
    }

    int argNo = 0;
    bool consumeNextPositionalArgument = false;
    std::wstring posArg(L"");

    while (argNo < argc) {
        std::wstring &curWord = arguments[argNo];
        if (consumeNextPositionalArgument) {
            posArg = curWord;
            consumeNextPositionalArgument = false;
            argNo++;
            continue;
        }

        if (!curWord.compare(L"--")) {
            /* Optional separator of flags and positional arguments */
            argNo++; /* skip the "--" itself */
            break;
        }
        /* Look for matches for supported options */
        if (curWord.find(L"-o") == 0) {
            curWord = curWord.substr(2); /* remove the '-o' part */
            if (curWord.empty()) { /* must be the next word */
                consumeNextPositionalArgument = true;
            } else { /* argument is attached to the flag */
                posArg = curWord;
            }
        } else if (curWord.find(L"--output") == 0) {
            curWord = curWord.substr(8);
            if (curWord.empty()) {
                consumeNextPositionalArgument = true;
            } else {
                posArg = curWord;
            }
        } else if (!curWord.compare(L"-v")
                || !curWord.compare(L"--verbose")) {
            result.verbose = true;
        } else if (!curWord.compare(L"-h")
                || !curWord.compare(L"--help")) {
            /* Help asked */
            return false;
        } else if (curWord.find(L"-") == 0) { /* Unknown option */
            std::wcerr << "Unknown option " << curWord << std::endl;
            return false;
            break;
        } else { /* Non positional arguments have started */
            break;
        }
        argNo++;
    }

    if (!posArg.compare(L"--")) {
        std::wcerr << "Missing positional argument" << std::endl;
        return false;
    }
    if (posArg.length() != 0) {
        result.outputFileName = posArg;
    }

    /* Check if there is at least one positional parameter left */
    if (argNo == argc) {
        std::wcerr << "Missing program name" << std::endl;
        return false;
    }
    result.cmdLine = arguments[argNo];
    /* Concatenate with the rest of arguments */
    for (int i = argNo + 1; i < argc; i++) {
        result.cmdLine += L" " + arguments[i];
    }

    return true;
}

int wmain(int argc, wchar_t* argv[]) {
    const double timeUnit = 1.0e-7; /* 100 nanoseconds time resolution unit */
    int ret = 0;
    /* Parse command line arguments */
    CliParams params = { 0 };
    if (!ParseArgv(argc, argv, params)) {
        UsageAndExit(argv);
    }

    //std::wcout << "DEBUG: Output file " << 
    //    (params.outputFileName.length() == 0 ? L"Nothing": params.outputFileName.c_str())
    //    << ", verbose " << params.verbose << std::endl;

    /* Prepare to start application */
    STARTUPINFO startUp;
    GetStartupInfo(&startUp);

    /* Start program in paused state */
    PROCESS_INFORMATION procInfo;
    if (!CreateProcess(NULL, const_cast<LPWSTR>(params.cmdLine.c_str()), NULL, NULL, TRUE,
        CREATE_SUSPENDED | NORMAL_PRIORITY_CLASS, NULL, NULL, &startUp, &procInfo)) {
        std::wcerr << L"Unable to start the process: "
            << GetLastErrorDescription() << std::endl;
        return 127;
    }

    HANDLE hProcess = procInfo.hProcess;

    /* Create job object and attach the process to it */
    HANDLE hJob = CreateJobObject(NULL, NULL); // XXX no security attributes passed
    assert(hJob != NULL);
    ret = AssignProcessToJobObject(hJob, hProcess);
    assert(ret);

    /* Now run the process and allow it to spawn children */
    ResumeThread(procInfo.hThread);

    /* Block until the process terminates */
    if (WaitForSingleObject(hProcess, INFINITE) != WAIT_OBJECT_0) {
        std::wcerr << L"Failed waiting for process termination: " 
            << GetLastErrorDescription() << std::endl;
        return 127;
    }

    DWORD exitCode = 0;
    ret = GetExitCodeProcess(hProcess, &exitCode);
    assert(ret);

    /* Calculate wallclock time in hundreds of nanoseconds.
    Ignore user and kernel times (third and fourth return parameters) */
    FILETIME createTime, exitTime, unusedTime;
    ret = GetProcessTimes(hProcess, &createTime, &exitTime, &unusedTime, &unusedTime);
    assert(ret);

    LONGLONG createTime100Ns = (LONGLONG)createTime.dwHighDateTime << 32 | createTime.dwLowDateTime;
    LONGLONG exitTime100Ns = (LONGLONG)exitTime.dwHighDateTime << 32 | exitTime.dwLowDateTime;
    LONGLONG wallclockTime100Ns = exitTime100Ns - createTime100Ns;

    /* Get total user and kernel times for all processes of the job object */
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION jobInfo;
    ret = QueryInformationJobObject(hJob, JobObjectBasicAccountingInformation,
        &jobInfo, sizeof(jobInfo), NULL);
    assert(ret);
    /* Close unused handlers */
    CloseHandle(hProcess);
    CloseHandle(hJob);

    if (jobInfo.ActiveProcesses != 0) {
        std::cerr << "Warning: there are still "
            << jobInfo.ActiveProcesses
            << " alive children processes" << std::endl;
        /* We may kill survived processes, if desired */
        //std::cerr << "Killing them" << std::endl;
        //TerminateJobObject(hJob, 127);
    }

    /* Get kernel and user times in hundreds of nanoseconds */
    LONGLONG kernelTime100Ns = jobInfo.TotalKernelTime.QuadPart;
    LONGLONG userTime100Ns = jobInfo.TotalUserTime.QuadPart;
    DWORD pageFaults = jobInfo.TotalPageFaultCount; /* Also available, why not report it as well */

    /* Choose where to print results - to a file or stdout */
    std::wstreambuf *buf;
    std::wofstream of;
    if (!params.outputFileName.empty()) {
        of.open(params.outputFileName);
        buf = of.rdbuf();
    } else {
        buf = std::wcout.rdbuf();
    }
    std::wostream out(buf);

    /* Print floats with two digits after the dot */
    out << std::fixed << std::setprecision(2);

    out << std::endl;
    if (params.verbose) {
        out << L"Command being timed: " << L"\"" << params.cmdLine << L"\"" << std::endl;

        out << "Elapsed (wall clock) time (seconds): " << timeUnit * wallclockTime100Ns << std::endl;
        out << "User time (seconds): " << timeUnit * userTime100Ns << std::endl;
        out << "System time (seconds): " << timeUnit * kernelTime100Ns << std::endl;
        out << "Page faults: " << pageFaults << std::endl;
        out << "Exit status: " << exitCode << std::endl;
    } else {
        /* Match POSIX time output */
        out << "real" << "\t" << timeUnit * wallclockTime100Ns << "s"<< std::endl;
        out << "user" << "\t" << timeUnit * userTime100Ns << "s" << std::endl;
        out << "sys" << "\t" << timeUnit * kernelTime100Ns << "s" << std::endl;
    }

    if (of.is_open()) {
        of.close();
    }
    return exitCode;
}
