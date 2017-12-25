# DebugVisualizer
NextGen DebugView++ that is portable and with better visualization of data

# gathering requirements

- gantt chart timeline with markers to visualize processes and bottlenecks
- multi level / stacked gantt chart suggested by peter bindels to visualize chronological data that is spaced unevenly (decades vs. seconds)
- pluggable input sources
- pluggable visualizers
- portability for MacOs, Linux and Windows


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
- online filters/highlighing storage to sync settings over difference workstations
- what do we do with internationalization ? Chinese, Japanese characters, etc...
- 

Concepts:
- multi-in/multi-out 
   - in: for example, take multiple logfiles, or sockets
   - out: stream to disk, stream to memory-ring-buffer  
- client/server
   - for example, multiple server instances on linux and a client/viewer on windows (or vice versa)   

Search:
- search
- filter ! 
- (trigger action)

Inputs:
- compact binary format to collect high-resolution data (or games and financial trading applications)
- logs from text formatted file
- logs from OutputDebugString API (Win32 specfic)
- logs from http(s)
- logs from stdin
- logs from structured CSV (TICS results)
- logs from binary formatted file (use custimizable plugin?)
- remote log sources, such as ssh/sftp
- remote instance of the same tool?
- Event log !
- Event Tracing for Windows (ETW) is designed for C and C++ developers who write user-mode applications
  but can also be (used in kernel drivers)[https://docs.microsoft.com/en-gb/windows-hardware/drivers/devtest/adding-event-tracing-to-kernel-mode-drivers]

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

# references

- https://www.elastic.co/
- https://www.splunk.com/en_us/products/splunk-light.html  (real-time correlation and rules engine)
- https://www.telerik.com/fiddler 
- https://www.infoworld.com/article/3063614/analytics/6-splunk-alternatives-for-log-analysis.html

free similar applications:
- dbgview
- http://wintail.hofle.com/ (last updated 2015)
- http://logio.org/ (looks good)
- https://www.baremetalsoft.com/baretail/index.php
- https://code.google.com/archive/p/log-manager/
- notepad++ Document monitor (https://sourceforge.net/projects/npp-plugins/files/DocMonitor/)

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



 
