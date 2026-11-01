# DebugVision

DebugVision "The software oscilloscope". Our goal is to provide a set of features that can best be compared with an oscilloscope, but for debugging and profiling software.

The main idea is to instrument existing code with 'sample' points to collect information about the runtime behavior. Then the software can be configured to visualize and decode the information in an application specific way.
This could mean you can visualize the latency (min/max/average) of your main loop.
But you can also 'trigger' and 'zoom in' to specific pieces of your code.

## The log viewer

The first part is a log viewer with the features of [DebugView++](https://github.com/CobaltFusion/DebugViewPP), rebuilt to be portable (Windows, Linux, macOS) and to use as little CPU as possible:

- nothing runs while idle: sources wake up on events (DBWIN events, epoll/kqueue, overlapped I/O); there is no fixed rate polling
- every line is filtered once, on a worker thread; the GUI paints only the visible rows, straight from storage
- filters use PCRE2 with JIT, plain text filters use a literal search
- text is decoded once when it arrives and stored as UTF-8 (UTF-16/UCS-2, BOM detection, per line UTF-8-or-legacy fallback)

### Sources

All inputs are configurable sources (Log > Sources..., `--source type:key=value,...` on the command line):

| Type | Platforms | |
|---|---|---|
| `outputdebugstring` | Windows | Win32 OutputDebugString, local and/or `Global\` |
| `file` | all | tail a file: truncation, rotation, UTF-8/UTF-16, DebugView++ `.dblog` and Sysinternals `.log` formats |
| `process` | all | capture stdout/stderr of a program (Run..., or drop an executable / script) |
| `stdin` | all | data piped into the program |
| `udp` | all | UDP listener with optional multicast group and sender filter |
| `tcp-listen`, `tcp-connect` | all | line based TCP |
| `dbgview-agent` | all | client for the Sysinternals DebugView agent (`dbgview.exe /a`) |

`dvconsole --list-sources` shows the parameters of every type.

### Platform restrictions

- OutputDebugString exists only on Windows.
- macOS: no tray icon ("Hide When Minimized"), no `dvconsole -x`, `.app` bundles cannot be run as a process source.
- Windows PowerShell 5.1 `Add-Content` refuses to write to a file that any other program has open (even a plain reader), so it fails while the file is tailed; `>>` redirection and most loggers work.

## Layout

```
libs/dvcore         engine: storage, encoding, sources, filters, settings (no GUI dependency)
  src/platform      the only OS specific code: win32, linux, macos, posix
src/application     CopperSpice GUI
tools/dvconsole     command line version
```

## Building

Requirements: CMake 3.28+, a C++20 compiler (MSVC 2022, GCC 11+, Clang 14+, Apple Clang 15+), and for the GUI a GUI-enabled [CopperSpice](https://www.copperspice.com/) 2.1 install. nlohmann/json, PCRE2 and doctest are downloaded by CMake.

```
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOPPERSPICE_DIR=<copperspice install prefix>
cmake --build build -t install
ctest --test-dir build --output-on-failure
```

### Where CopperSpice is installed

The GUI is built by default when a CopperSpice install is specified or found; otherwise only the engine, console tool and tests are built. CMake looks for `CopperSpiceConfig.cmake` under the CopperSpice install prefix (in `cmake/CopperSpice`, `lib/cmake/CopperSpice` or `lib64/cmake/CopperSpice`), taken from the first of:

1. `-DCOPPERSPICE_DIR=/path/to/copperspice` on the cmake command line (stored in the cache, so only needed on the first configure)
2. the `COPPERSPICE_DIR` environment variable, e.g. `COPPERSPICE_DIR=/path/to/copperspice ./build.sh`
3. `/opt/copperspice`, if it exists. On Debian 13, extract the prebuilt release there:
   `sudo mkdir -p /opt/copperspice && curl -sSL https://github.com/copperspice/copperspice/releases/download/cs-2.1.0/copperspice-2.1.0-debian13-x64.tar.bz2 | sudo tar xj -C /opt/copperspice`

Alternatively `-DCopperSpice_DIR=<dir containing CopperSpiceConfig.cmake>` or `-DCMAKE_PREFIX_PATH=...` also enables the GUI and points CMake at the install. With MSVC the GUI is always on by default and falls back to a CopperSpice nuget package. `-DDV_BUILD_GUI=ON|OFF` overrides the default; `build.sh` passes its arguments on to cmake, e.g. `./build.sh -DDV_BUILD_GUI=OFF`.

See `build.bat` and `build.sh`.
