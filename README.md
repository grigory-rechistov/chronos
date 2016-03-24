# chronos

A better `time` utiltity for Windows that takes into account children times.

## Rationale

I needed a program that can run another program and report its wallclock, user and
system times on Windows, similar to what `time` utility does on POSIX systems (Linux/BSD).
There was nothing like this preinstalled on Windows, and alternatives found on
the Web were either defunct or flawed. I needed something to be able to sum
times of the process and *its children*. A simple `GetProcessTimes()` WinAPI call
was not enough for that.

Ultimately, I had to write it myself. On Windows/DOS, `time.exe` name is already
occupied by an unrelated system utility; therefore, I called my program `chronos`.

I tried to make the program mimic `time` output found on Linux.

## Building

For those who might need it, I included a Visual Studio 2015 project file.
Essentially, the compilation can be done with a single command:

    cl.exe /D _CONSOLE /D UNICODE /D _UNICODE /EHsc chronos.cpp

Do not forget to run `vcvarsall.bat` beforehand. I was able to build `chronos`
with Visual Studio 2015 and Visual Studio 2010, both x86 and x64 architectures.


## Running

    Usage: chronos.exe [-v] [-o file] [--] program [options]

    Run program and report its resources usage
    --verbose, -v          produce results in verbose forma
    --output, -o filename  write result to filename instead
    program                program name to start
    options                the program's own arguments


## Bugs and Limitations

* Not tested much. It works for my programs, but can fail for yours.
* The utility does not wait for all children to terminate; it only monitors the
parent process.
* Possibly because of the previos note, some processes, like `explorer.exe`,
escape the control of `chronos` immediately after start.
For such programs, reported times will be incorrect
* No full compatibility with POSIX `time`. In fact, no output formatting is
supported.
* No I18N whatsoever.

## Links

1. The discusion at the [StackOverflow thread](http://stackoverflow.com/questions/36011572/how-to-obtain-handles-for-all-children-process-of-current-process-in-windows).


