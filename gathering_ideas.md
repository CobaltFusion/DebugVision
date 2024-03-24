# gathering requirements

- gantt chart timeline with markers to visualize processes and bottlenecks
- multi level / stacked gantt chart suggested by peter bindels to visualize chronological data that is spaced unevenly (decades vs. seconds)
- time/frequency analysis (group events per period at plot counts/timebox over time)
- pluggable input sources
- pluggable visualizers
- portability for MacOs, Linux and Windows
- a 1 microsecond accurate storage will cover most usecases. On very time critical systems where time is measured in nanoseconds (1e-9), picoseconds (1e-12) or femtoseconds (1e-15) optimizations are usually measured/aggragated in loops.
  This seems to be generally available:
  - <https://en.cppreference.com/w/cpp/chrono/steady_clock> 
  - <https://docs.microsoft.com/en-gb/windows/desktop/SysInfo/acquiring-high-resolution-time-stamps>
  - <https://www.postgresql.org/docs/9.0/datatype-datetime.html>
  - debugview++ stores times in both wall-clock format with millisecond accuracy and offset from the first entry in microsecond accuracy. We have not had requests for more accuracy in the last six years.
  - for embedded systems it is plausible to measure in clockticks that are typically in the picosecond domain. I would say we postpone this usecase until we have good reason to support it. However, if we do decide to support it, we could add a special type of series that supports an 'offset from start of the series' in clockticks, where the clock frequency can vary over series.

# ideas
- maybe use http://www.copperspice.com/
  - a derivative of Qt 4.8.2, written in modern C++
  - has no Meta Object Compiler (moc) 
  - a templated class can inherit from QObject
  - application can be built with any build system
  - complex data types can be used in the Property system and with Signals and Slots

- maybe use https://www.ultimatepp.org/index.html
  - has a graphical IDE
  - uses GTK backend on linux and native win32 on windows
  - Gui 'hello world' is 4MB (and can be created in 2 minutes, including installation, which requires only unzipping)
  - Gui tutorial https://www.ultimatepp.org/srcdoc$CtrlLib$Tutorial$en-us.html

# more...
- online filters/highlighing storage to sync settings over difference workstations (login with google account?)
- what do we do with internationalization? Chinese, Japanese characters, etc...

https://simplicable.com/new/information-analysis
 - gather 
 - aggregation / correlation 
 - map (different sources may provide the same information in slightly different ways)
 - categorization
 - summaries (frequency, totals occurances)
 - analysis 
 - visualization

Concepts:
- multi-in/multi-out 
   - in: for example, take multiple logfiles, sockets, or other input
   - out: stream to disk, stream to memory-ring-buffer  
- client/server
   - for example, multiple server instances on linux and a client/viewer on windows (or vice versa) 
   - server side buffer (late connecting client can get messages from the past x time)
   - Debugview++ could be one of the 'servers' for windows?
   - consider .dotnet core or java for a front-end visualization, to make the UI multi-platform

Visualization:
- query using some kind of elastic search?
- use online visualization (can that still be done as a 'live' view?)
- check out <https://github.com/jlfwong/speedscope>

communication:
 - gRPC https://grpc.io/
 

Filter:
- separation for input and view filter, meaning filtered messages from input filters is not kept at all, and filtered messages from view  filters and only not shown. (debugview++ only has the latter, which can make it difficult to setup very long lasting tests.) 

Search:
- search
- filter ! 
- (trigger action)

Possible inputs:
- compact binary format to collect high-resolution data (telemetry) for games and financial trading applications.
- [VCD](https://en.wikipedia.org/wiki/Value_change_dump) (Value Change Dump) an ASCII-based format described by IEEE standard 1364-2001
- logs from a text formatted file
- logs from OutputDebugString API (Win32 specfic)
- logs from http(s)
- logs from stdin
- logs from structured CSV (TICS results)
- logs from binary formatted file (use customizable plugin?)
- remote log sources, such as ssh/sftp
- remote instance of the same tool, attach/monitor it remotely
- [windows specific] Event log !
- [windows specific] Event Tracing for Windows (ETW) is designed for C and C++ developers who write user-mode applications
  but can also be [used in kernel drivers](https://docs.microsoft.com/en-gb/windows-hardware/drivers/devtest/adding-event-tracing-to-kernel-mode-drivers)
  
 Formatting:
 - allow to edit columns, on tabs that read form files, maybe apply restrictions on OutputDebugString/Event log tabs.
 - unformating/parsing by allowing specification of colomn separator or fixed width (per colomn) 
 - allow/be agnostic to (log)files without timestamps

Visualize: 
- open event source in new tab, allow to merge tabs
- listview, coloring / highlighting
- timelines (gantt charts) with annotations
- summaries / reports ?
- scaling from scattered datapoints milliseconds <=> years ?

Outputs:
- choose filtered / unfiltered
- to file
- to socket? (for transport / remote monitoring) 

# commercial logo?

- <https://www.fiverr.com/mubieman/make-logo-for-the-companies>

# google keywords

**Telemetry** is an automated communications process by which measurements and other data are collected at remote or inaccessible points and transmitted to receiving equipment for monitoring.

# process mining

We almost certainly have overlap between our goals and the goals from process mining

- https://fluxicon.com/book/read/aboutbook/


# references
- https://github.com/Microsoft/krabsetw
- https://www.elastic.co/
- https://www.splunk.com/en_us/products/splunk-light.html  (real-time correlation and rules engine)
- https://www.telerik.com/fiddler 
- https://www.infoworld.com/article/3063614/analytics/6-splunk-alternatives-for-log-analysis.html
- https://ai.google/research/pubs/pub36356
- [racing performance of your service calls (Sleuth, Zipkin & ELK) - Rafaëla Breed](https://www.youtube.com/watch?v=hl4-tpbZGOY)
- [Dapper, a Large-Scale Distributed Systems Tracing Infrastructure](https://ai.google/research/pubs/pub36356)
- https://zipkin.io/
- https://docs.timescale.com/v1.0/getting-started/installation/windows/installation-docker <= 
- [Diana Hsieh - Time-Series Data NY Meetup #3 Livestream - Sep 20 2018](https://www.youtube.com/watch?v=TQk_tc03MyQ)
- https://papertrailapp.com
- https://www.spectx.com/ (good demo Liisa and Jüri, thanks!)
- https://github.com/facontidavide/PlotJuggler
- https://gtkwave.sourceforge.net/ (VCD) 
- https://sigrok.org/wiki/PulseView (VCD) 
- https://marketplace.visualstudio.com/items?itemName=wavetrace.wavetrace (VCD) - 2021
- https://docs.wokwi.com/guides/logic-analyzer
- https://github.com/Ben1152000/sootty
- https://github.com/brendangregg/FlameGraph (generate interactive SVG)
- https://github.com/spiermar/d3-flame-graph?tab=readme-ov-file (d3-version)
- https://fuchsia.dev/fuchsia-src/concepts/kernel/tracing-system#trace-client
- https://perfetto.dev/ (google) / https://perfetto.dev/docs/analysis/trace-processor
- https://github.com/janestreet/magic-trace
- https://doc.qt.io/qtcreator/creator-ctf-visualizer.html
- https://www.kdab.com/full-stack-tracing-part-3/
- https://babeltrace.org/
- https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU/preview#heading=h.yr4qxyxotyw
- https://www.eclipse.org/tracecompass/


# print debugging 
- https://buttondown.email/geoffreylitt/archive/starting-this-newsletter-print-debugging-byoc/
- https://www.geoffreylitt.com/projects/todomvc-vis.html?utm_source=geoffreylitt&utm_medium=email&utm_campaign=starting-this-newsletter-print-debugging-byoc
- https://www.geoffreylitt.com/resources/todomvc-vis.pdf
- 
# site references

- http://newtondynamics.com/forum/viewtopic.php?f=9&t=8727
- http://www.chromium.org/developers/how-tos/trace-event-profiling-tool 

TimeScale+Grafana -> grouping by seconds an lowest timeunit may be a problem?

- Flamegraph?

free similar applications:
- dbgview
- http://wintail.hofle.com/ (last updated 2015)
- http://logio.org/ (looks good)
- https://www.baremetalsoft.com/baretail/index.php
- https://code.google.com/archive/p/log-manager/
- notepad++ Document monitor (https://sourceforge.net/projects/npp-plugins/files/DocMonitor/)
- https://github.com/zarunbal/LogExpert
- https://www.hawktracer.org/

non-free similar applications:
- http://uvviewsoft.com/logviewer/
- http://www.hootech.com/WinTail/ (30 days trail, last updated 2013)
- https://www.logviewplus.com/ (looks pretty nice)
- https://www.kiwisyslog.com/kiwi-log-viewer (not very feature rich)
- https://www.scalyr.com/product (tag: Log aggregation, system metrics, and server monitoring that's 
fast, powerful, and easy to set up, Drop the Scalyr Agent on any server
Each agent installation automatically registers itself and begins collecting logs and system metrics), **sounds scary**
- https://www.logfusion.ca/Screenshots/ (actually looks a lot like a debugview++, not optimized for speed, but nicer UI)
- https://www.sumologic.com (nice sales/ marketing pitch)
- http://www.radgametools.com/telemetry.htm RAD Telemetry (Insanely good, but also not free)
- http://www.farrellf.com/TelemetryViewer/
- https://devblogs.microsoft.com/pix/download/
