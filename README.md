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
  
  
# notes

Search:
- search
- filter ! 
- (trigger action)

Inputs:
- logs from text formatted file
- logs from OutputDebugString API (Win32 specfic)
- logs from http(s)
- logs from stdin
- logs from structured CSV (TICS results)
- logs from binary formatted file (use custimizable plugin?)

Visualize: 
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

