# DebugVision

User manual: a portable, CPU-efficient log viewer in the spirit of DebugView++

- [Overall usage](#overall-usage)
- [Command line](#command-line)
- [Options](#options)
- [Source specifications](#source-specifications)
- [Examples](#examples)
- [Settings](#settings)
- [Timeline](#timeline)
- [dvcli: the command line version](#dvcli-the-command-line-version)

## Overall usage

DebugVision collects log lines from one or more *sources* and shows them in one or more *views*.
Every line gets a line number, a time stamp, the process ID and the process name of whoever sent it.

- **Sources** (Log > Sources...): on Windows, `OutputDebugString` is captured by default. You can also tail files,
  capture the output of a program (File > Run...), listen on UDP or TCP, or connect to the Sysinternals DebugView agent.
  Dropping a log file on the window opens it; dropping an executable or script runs it as a process source.
- **Views** are tabs (File > New View, Ctrl+N). Each view has its own filters, so you can watch the same log in different ways.
- **Filters** (View > Filters..., F5) are regular expressions or plain text. Their types are: Include, Exclude, Highlight, Token (colors the matching text),
  Stop and Track (scrolling), Once, Clear, Beep and Bookmark. The quickest way to make one is to select text in the view and press
  Ctrl+H (highlight), Ctrl+I (include), Ctrl+E (exclude) or Ctrl+K (token).
- **Finding text**: Ctrl+F, then F3 for the next match and Shift+F3 for the previous one.
- **Bookmarks**: Ctrl+F2 toggles a bookmark, F2 and Shift+F2 jump between bookmarks.
- **Timeline**: View > Timeline (Ctrl+L) shows the lines of a view on a time axis below the list, as bars from a begin
  line to an end line and as markers for single events. See [Timeline](#timeline).
- **Pausing**: Pause (or Ctrl+Shift+P) stops capturing temporarily. Log > Clear Log (Ctrl+Shift+X) empties the log.
- **Saving**: File > Save Log saves everything in DebugView++ `.dblog` format, Save View only the filtered lines and Save Selection only the selected ones.
  File > Save Configuration saves the views, filters and sources to a `.json` file.

Help > User Manual (F1) opens this page. Your views, filters, sources and window layout are saved when DebugVision closes.

## Command line

```
debugvision [options] [logfile | settings.json]
```

All options are optional. Without any, DebugVision starts with the sources, views and filters of the previous session.
Sources given on the command line are added for *this session only* and are not saved in the settings.
An option that needs a value takes it from the next argument, so put quotes around values that contain spaces.
An unknown option or an invalid value is reported in a message box.

## Options

#### `-h`, `/h`, `--help`, `/?`

Opens this manual in your web browser, writes a short summary of the options to standard output, and exits without starting DebugVision.

#### `--min`, `-min`, `/min`

Starts with the window minimized. When Options > Hide When Minimized is on, DebugVision starts as an icon in the system tray (not on macOS).

#### `--source <spec>`

Adds a source for this session. `<spec>` is a source type, optionally followed by a colon and comma-separated
`key=value` parameters, for example `udp:port=2020` or `file:path=app.log,startAt=end`.
Parameters you leave out get their default. The option can be given more than once.
See [Source specifications](#source-specifications) for all types and parameters.

#### `--udp <port>`

Listens for log lines on UDP port `<port>` on all network interfaces. Each datagram is split into lines.
This is short for `--source udp:port=<port>`.

#### `--file <path>`

Tails a file: shows its current content, then follows lines appended to it.
Truncation and rotation (the file being replaced) are handled, and the format and encoding are detected automatically.
This is short for `--source file:path=<path>`.

#### `--settings <file>`

Uses `<file>` instead of the default settings file, both to load the settings at startup and to save them on exit.
If the file does not exist yet, DebugVision starts with default settings and creates it.
This makes it easy to keep separate setups, for example one per project.

#### `logfile`

Loads a log file once (it is not followed; use `--file` for that). Plain text, DebugView++ `.dblog`
and Sysinternals DebugView `.log` files are recognized; the original time stamps and process names are kept for the last two.
Only one log file can be given.

#### `settings.json`

An argument ending in `.json` is used as the settings file, the same as `--settings settings.json`.

#### Standard input

When data is piped or redirected into DebugVision, it is captured as a source as well, for example `myprogram | debugvision`.

## Source specifications

The format is `type:key=value,key=value,...`. List values (such as program arguments) are separated by semicolons.
Boolean values accept `true`/`false`, `1`/`0`, `yes`/`no` and `on`/`off`.
`dvcli --list-sources` lists the types and their defaults.

### Parameters of every source

| Parameter | Meaning |
|---|---|
| `name` | The name shown in Log > Sources. |
| `encoding` | How bytes are decoded into text: `auto` (default, detects a BOM and UTF-16), `utf-8`, `utf-8-or-legacy` (UTF-8, with a fallback to the legacy code page for each line that is not valid UTF-8), `legacy`, `utf-16le`, `utf-16be`. |
| `charset` | The legacy code page used by `legacy` and `utf-8-or-legacy`; empty means the system default. |
| `autonewline` | Ends a line that has no newline when no more data follows (default `true`), as Log > Auto Newline does. |

### Source types

| Type | Parameters (default) | Description |
|---|---|---|
| `outputdebugstring` | `scope` (`local`) | Windows only: captures `OutputDebugString` output. `scope` is `local` (programs in your session), `global` (services and other sessions; requires running as administrator) or `both`. |
| `file` | `path`, `tail` (`true`), `startAt` (`beginning`), `format` (`auto`) | Reads a file. With `tail=true` new lines are followed. `startAt=end` skips the existing content. `format` is `auto`, `text`, `dblog` or `sysinternals`. |
| `process` | `command`, `args`, `cwd`, `restartOnExit` (`false`), `restartDelaySeconds` (`5`) | Runs a program and captures its standard output and standard error. `args` is a semicolon-separated list, `cwd` the working directory. With `restartOnExit=true` the program is started again when it exits. |
| `stdin` | none | Data piped into DebugVision. Added automatically when standard input is a pipe or a file. |
| `udp` | `port` (`2020`), `bind` (`0.0.0.0`), `group`, `allowedSenders`, `splitOnNewline` (`true`), `processName` (`[UDP {ip}:{port}]`) | Receives datagrams. `bind` is the local address, `group` a multicast group to join, `allowedSenders` a semicolon-separated list of sender addresses to accept (empty accepts all). `processName` is shown in the Process column; `{ip}` and `{port}` are replaced by the sender's address and port. |
| `tcp-listen` | `port` (`2021`), `bind` (`0.0.0.0`), `maxClients` (`16`) | Accepts TCP connections and reads lines from every connected client. |
| `tcp-connect` | `host` (`127.0.0.1`), `port` (`2021`), `reconnectSeconds` (`5`) | Connects to a TCP server and reads lines from it, reconnecting after the given delay when the connection is lost. |
| `dbgview-agent` | `host` (`127.0.0.1`), `port` (`2020`) | Connects to a Sysinternals DebugView agent (`dbgview.exe /a`), which can capture output on another machine. |

## Examples

```
# the previous session, minimized
debugvision --min

# open a saved log
debugvision crash.dblog

# follow a log file, showing only lines written from now on
debugvision --source "file:path=C:\logs\server.log,startAt=end"

# listen on UDP port 2020 and on a multicast group
debugvision --udp 2020 --source udp:port=5000,group=239.1.1.1

# run a program and capture its output, restarting it when it exits
debugvision --source "process:command=python,args=worker.py;--verbose,cwd=C:\work,restartOnExit=true"

# capture the output of a pipeline
myprogram 2>&1 | debugvision

# a separate setup for one project
debugvision --settings project-x.json
```

## Settings

Settings are saved on exit to `settings.json` in the configuration directory, unless `--settings` names another file:

| Platform | Location |
|---|---|
| Windows | `%APPDATA%\DebugVision\settings.json` |
| Linux | `$XDG_CONFIG_HOME/debugvision/settings.json`, by default `~/.config/debugvision/settings.json` |
| macOS | `~/Library/Application Support/DebugVision/settings.json` |

Help > About DebugVision shows the settings file in use.

## Timeline

The timeline shows the lines of a view on a time axis, like an oscilloscope or [TimeLens](https://github.com/CobaltFusion/timelens):
how long things take, how they overlap and when events happen. It is shown below the log list with View > Timeline (Ctrl+L)
and uses only the lines the view shows, so the view's filters also filter the timeline. Each view has its own timeline and rules.

Each process gets a lane. What appears in it is set with View > Timeline Rules...:

| Kind | Patterns | Drawn as |
|---|---|---|
| Span | Begin and End | A bar from a line matching Begin to the next line of the same process matching End. Overlapping bars are stacked; a bar that has not ended yet is drawn faded up to the newest line. |
| Event | Begin | A marker on every matching line: a lollipop (a line with a circle), a square or a triangle, to tell kinds of events apart. |

The patterns use the match types of filters: Simple, Wildcard, Regex and RegexCase. When the Begin and End patterns contain
capture groups, a span only ends at an End line that captures the same text. With the rules

| Name | Kind | Begin | End | Match |
|---|---|---|---|---|
| frame | Span | `frame (\d+) start` | `frame (\d+) done` | Regex |
| IRQ | Event | `IRQ` | | Simple |

the lines `Frame 1 start`, `Frame 2 start`, `Frame 1 done`, `Frame 2 done` make two overlapping bars, and every line containing
`IRQ` gets a lollipop. Spans nest: an End closes the most recent open span of its rule and process.

#### Using the timeline

- **Mouse wheel**: zoom in or out around the mouse pointer.
- **Drag with the right or middle button**: pan.
- **Drag with the left button**: measure the time between two points.
- **Click a bar or marker**: select its line in the list. The line selected in the list is shown as a red vertical line.
- **Hover a bar**: its duration, its begin and end lines, and the minimum, average and maximum duration of the completed spans of that rule in that lane.
- **Keys** (click the timeline first): `a` and `d` pan, `w` and `s` zoom, Home or `q` show everything and follow new lines again, Esc removes the measurement.

While Auto Scroll is on, the newest line stays at the right edge. Zooming or panning stops that until Home or `q` is pressed.
Drag the divider between the list and the timeline to give the timeline more room.

## dvcli: the command line version

`dvcli` captures the same sources as DebugVision without a window, filters the messages and writes them to the console
and/or a log file. It is the equivalent of DebugView++'s DebugViewConsole and is meant for scripts, servers, build machines and
remote sessions. It keeps no history, so it can run for a long time with constant memory use. Stop it with Ctrl+C,
`dvcli -x` or a quit message (`-m`).

```
dvcli [options] [-c] [-d <file>] [--source <spec>]... [--udp <port>]... [--file <path>]... [--stdin]
dvcli -x
dvcli -u <message> [<port>]
dvcli --list-sources
dvcli -h | --help
dvcli --webhelp
```

At least one of `-c` and `-d` is required. Single-letter flags without a value can be combined:
`-clsn` is the same as `-c -l -s -n`.

### Output

#### `-c`

Writes the messages to standard output. On a console, Unicode text is shown correctly; when redirected, the output is UTF-8.

#### `-d <file>`

Writes the messages to `<file>` in DebugView++ `.dblog` format, with relative time, system time, process ID and process name,
regardless of the column options. An existing file is overwritten. The file can be opened in DebugVision and DebugView++.

#### `-f`

Flushes the output after every line. Without it, output is written in batches, which is faster; use `-f` when another
program reads the output as it is written.

### Columns

These select what is written before each message with `-c`, in this order. Without any of them only the message is written.

| Option | Column |
|---|---|
| `-l` | line number, counting only the lines that pass the filters |
| `-s` | system (clock) time, `HH:MM:SS.mmm` |
| `-q` | time in seconds since dvcli started |
| `-p` | process ID |
| `-n` | process name |
| `-t` | separates the columns with tabs instead of spaces, for importing into a spreadsheet |

### Capture

#### `--source <spec>`, `--udp <port>`, `--file <path>`

Add sources, exactly as for DebugVision (see [Source specifications](#source-specifications)). Each can be given more than once.
On Windows, when no source is given, `OutputDebugString` is captured with scope `both`
(global capture needs administrator rights and is skipped silently without them). On Linux and macOS a source is required.

#### `--stdin`

Reads standard input as a source, for example `make 2>&1 | dvcli -c --stdin -i error`.

#### `-a`, `-a-`

Auto newline on (the default) or off. With auto newline, a message that does not end with a newline is still a line of its own
once no more data follows. With `-a-`, such text is joined with the next message until a newline arrives.

#### `--list-sources`

Lists the source types available on this platform with their default parameters, and exits.

### Filters

Filter texts are plain text (not regular expressions) and case-insensitive. Each option can be given more than once.
Excludes win over includes; with several includes, a message is shown when it contains at least one of them.

#### `-i <text>`

Shows only messages that contain `<text>`.

#### `-e <text>`

Hides messages that contain `<text>`.

#### `--include-process <text>`, `--exclude-process <text>`

The same, applied to the process name instead of the message.

#### `-m <text>`

Quits after writing a message that passes the filters and contains `<text>`. Unlike the filters, this match is case-sensitive.
Useful in scripts that wait for a program to report something.

#### `-v`

Verbose: also shows dvcli's own messages, such as sources starting, stopping and failing. These have the process name `[internal]`.

### Other commands

#### `-x`

Stops all running dvcli instances (on Windows those in the same logon session, on Linux those of the same user) and exits;
the instances flush their output before they quit. Not available on macOS; use `pkill dvcli` there.

#### `-u <message> [<port>]`

Sends `<message>` as a UDP broadcast (to 255.255.255.255) on `<port>`, default 2020, and exits.
A DebugVision or dvcli with a UDP source on that port on the local network receives it, which is handy for testing a setup or marking a moment in a log.

#### `-h`, `--help`

Writes a summary of the options to standard output.

#### `--webhelp`

Opens this manual in the default web browser.

### dvcli examples

```
# show OutputDebugString output (Windows) with time and process name
dvcli -csn

# log everything to a file without console output
dvcli -d capture.dblog

# console and file, only messages from processes whose name contains "server", without heartbeat messages
dvcli -c -d server.dblog --include-process server -e heartbeat

# follow a log file and stop when the test run finishes
dvcli -c --file test.log -m "TESTS FINISHED"

# listen on UDP port 2020 and write tab-separated columns to a file for a spreadsheet
dvcli -clsqpnt --udp 2020 > capture.tsv

# send a test message to all listeners on port 2020
dvcli -u "hello from the build server"
```
