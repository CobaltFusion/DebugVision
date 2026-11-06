# DebugVision

DebugVision "See what your software does". It turns the logs your software already writes into a picture of its runtime
behavior: something like a trace viewer or the timeline of a profiler, but for debugging and profiling software while it runs.

The main idea is to use existing logs, without adding instrumentation to the code. DebugVision collects log lines from
wherever they are written (OutputDebugString, files, program output, the network, a web server), filters and highlights
them, and rules that match lines with regular expressions turn them into spans and events on a timeline, configured for
your application. You can stop at a specific event, and zoom in on a specific piece of your code on the timeline.

Logging that already marks where things begin and end, such as enter/leave lines of functions or the start and end of
requests, makes this a lot more insightful: every pair becomes a span, so you see how long things take, how they nest
and overlap, and where the time goes.

## Next release (not released yet)

The first release will contain the log viewer, `debugvision`, and its command line version, `dvcli`, for Windows, Linux and macOS:

- **Sources**: Win32 OutputDebugString (local and global), log files (tailed through truncation and rotation, DebugView++
  `.dblog` and Sysinternals `.log` formats), program output, standard input, UDP (with multicast and a sender filter),
  TCP, the Sysinternals DebugView agent, and log files on a web server over http(s)
- **Drag and drop**: drop a log file to open it, an executable or script to run it, or an `http(s)://` link to tail it
- **http(s) sources**: Range requests fetch only the new bytes; works through http, https and SOCKS proxies (e.g. an
  `ssh -D` tunnel), with proxy authentication (basic, digest, NTLM, Negotiate), the proxy settings of Windows, and
  suggestions for the SOCKS proxies found on this computer
- **Views and filters**: views as tabs, each with its own filters: include, exclude, highlight, token, stop, track, once,
  clear, beep and bookmark, as regular expressions (PCRE2) or plain text; find, bookmarks, process colors, clock or relative time
- **Timeline**: the lines of a view on a time axis, as spans from a begin line to an end line and as events, with rules
  in the filter dialog; `dvtest` generates test data for it
- **Columns**: show, hide and reorder them; double-click the separator in the header to fit a column to the visible lines
- **Traffic indicator**: an optional light and graph of the incoming lines in the toolbar
- **Saving**: the log, the view or the selection in DebugView++ `.dblog` format; views, filters and sources as a configuration file
- **dvcli**: capture and filter on the command line, write to the console and/or a `.dblog` file, quit on a message,
  exit when all sources have ended, send UDP test messages
- **User manual**: compiled into both programs (`debugvision --help`, Help > User Manual, `dvcli --webhelp`)
- **Windows**: `debugvision.exe` is one self-contained file, no installation or Visual C++ redistributable needed

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
| `http` | all | tail a log file on a web server over http(s) with Range requests (or drop a link on the window) |

`dvcli --list-sources` shows the parameters of every type.

### Platform restrictions

- OutputDebugString exists only on Windows.
- macOS: no tray icon ("Hide When Minimized"), no `dvcli -x`, `.app` bundles cannot be run as a process source.
- Windows PowerShell 5.1 `Add-Content` refuses to write to a file that any other program has open (even a plain reader), so it fails while the file is tailed; `>>` redirection and most loggers work.

## Layout

```
libs/dvcore         engine: storage, encoding, sources, filters, settings (no GUI dependency)
  src/platform      the only OS specific code: win32, linux, macos, posix
src/application     CopperSpice GUI
src/launcher        Windows: the self-contained debugvision.exe
tools/dvcli         command line version
tools/dvtest        generates nested calls and events to try out the timeline
docs/manual.md      user manual, converted to HTML at build time and compiled into both programs (debugvision --help, dvcli --webhelp)
```

## Building

Requirements: CMake 3.28+, a C++20 compiler (MSVC 2022, GCC 11+, Clang 14+, Apple Clang 15+), and for the GUI a GUI-enabled [CopperSpice](https://www.copperspice.com/) 2.1 install. nlohmann/json, PCRE2 and doctest are downloaded by CMake. libcurl (for the `http` source) is the system libcurl on Linux and macOS when installed (e.g. `libcurl4-openssl-dev`), otherwise, and always on Windows, CMake builds a small static libcurl from source (Schannel TLS on Windows; OpenSSL, e.g. `libssl-dev`, elsewhere).

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

On Windows the installed `debugvision.exe` is self-contained: the application, the CopperSpice DLLs, the platform plugin and the MSVC runtime are embedded in it, compressed with the Windows compression API (XPRESS Huffman, fast to decompress). At startup they are unpacked into a new directory under `%TEMP%\DebugVision`, which is removed when the application exits (directories left behind by a killed run are removed at the next start). `-DDV_SELF_CONTAINED=OFF` installs the plain executable with the DLLs next to it instead.

See `build.bat` and `build.sh`.
