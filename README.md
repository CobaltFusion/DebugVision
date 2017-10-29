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
  - A templated class can inherit from QObject
  - application can be built with any build system
  - complex data types can be used in the Property system and with Signals and Slots

