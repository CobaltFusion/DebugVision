# DebugVision

DebugVision "The software oscilloscope". Our goal is to provide a set of features that can best be compared with an oscilloscope, but for debugging and profiling software.

The main idea is to instrument existing code with 'sample' points to collect information about the runtime behavior. Then the software can be configured to visualize and decode the information in an application specific way.
This could mean you can visualize the latency (min/max/average) of your main loop.
But you can also 'trigger' and 'zoom in' to specific pieces of your code.
