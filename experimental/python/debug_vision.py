#!/usr/bin/env python3
"""include all categories that should currently have zero issues
exclude all projects that do not comply yet
any issues that pass this filter fail the build
"""

import os, sys, traceback
import matplotlib.pyplot as plt
import numpy as np

def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def sprint(*args, **kwargs):
    print(*args, file=sys.stdout, **kwargs)


def show_usage():
    eprint("Usage: ")
    eprint("   To extract timepoint information for separate threads:")
    eprint("   cat log.txt | " + os.path.basename(__file__))


class DataContainer:

    def __init__(self, tid):
        self.tid = tid

        self.event_time = []        # time-axis events
        self.event_linenumber = []  
        self.event_labels = []      # y-axis events 

        self.deltas = []            # time-axis delta-T
        self.delta_heights = []     # y-axis delta-T

        self.start_inspect_cycle_time = 0

    def add(self, steady_time, label, linenumber):
        self.event_time += [steady_time]
        self.event_labels += [label]    
        self.event_linenumber += [linenumber]    

        self.update_deltas(steady_time, label)
        

    def update_deltas(self, steady_time, label):
        if label == "Start INSPCYCLE":
            self.start_inspect_cycle_time = steady_time
        
        if label == "End INSPCYCLE":
            inspect_cycle_time = (steady_time - self.start_inspect_cycle_time)

            if inspect_cycle_time > 500 and inspect_cycle_time < 900:
                sprint("  Warning, HIGH inspection time on thread", self.tid, " start_inspect_cycle_time:", self.start_inspect_cycle_time, ", inspect_cycle_time: ", inspect_cycle_time)
                self.deltas += [self.start_inspect_cycle_time]
                self.delta_heights += [inspect_cycle_time]        


start = None
records = {}

def gatherLine(line, linecounter):
    global start 

    data = line.split(";")

    if len(data) < 5:
        #eprint("Ignored incomplete line:", line)
        return

    pid = data[0]
    processName = data[1]
    tid = int(data[2])
    label = data[3]
    steady_time = int(data[4])
    #system_time = data[5]

    if label.startswith("CosmItemCount"):
        return
    
    if not tid in records:
        records[tid] = DataContainer(tid)
   
    if start == None:
        start = steady_time

    steady_time = steady_time - start
    steady_time = steady_time / 1000        # steady_time is in us, we want ms

    records[tid].add(steady_time, label, linecounter)

stored_line = None

def gatherData(lines):
    global stored_line

    linecounter = 0
    for line in lines: #[500:10000]:
        linecounter = linecounter + 1
        line = line.strip()
        
        if line.startswith("sta;") and line.endswith(";end;"):
            pass            
        else:
            if stored_line == None:
                stored_line = line
                continue
            else:
                line = stored_line + line
                stored_line = None

        try:
            gatherLine(line[4:-5], linecounter)
        except ValueError as e:
            #eprint("Ignore line", linecounter, " because of", e)
            pass

    if len(records.keys()) == 0:
        eprint("No log information found!")
        sys.exit(1)


def plot2(tid):
    #fig, ax1 = plt.subplots()
    #fig.subplots_adjust(left=0.2)

    fig = plt.figure()
    ax1 = plt.gca()
    record = records[tid]
    ax2 = ax1.twinx()
   
    # time points
    ax1.plot(record.event_linenumber, record.event_labels, color='lime', zorder=1)
    ax1.scatter(record.event_linenumber, record.event_labels, marker='o', zorder=2)
    ax1.grid(alpha=0.7)

    # delta Ts
    #ax2.yticks = record.ylabels

    ax2.set_ylim(300, 2600)

    sprint("len: ", len(record.deltas))
    sprint("x ", record.deltas[:10])
    sprint("y ", record.delta_heights[:10])
    ax2.scatter(record.deltas, record.delta_heights, marker='v', color='red', zorder=3)
    #ax2.grid(alpha=0.7)

    #for i, v in enumerate(record.deltas):
    #    ax2.annotate(str(v), xy=(i,v), xytext=(-7,7), textcoords='offset points')

    #for i, v in enumerate(record.delta_heights):
    #    ax2.text(i, v+25, "%d" %v, ha="center")

    plt.xlim(left=0)
    #plt.tight_layout(pad=0.05)
    plt.show()


def writeFiles():
    sprint(";;".join(records.keys()))
    for key in records.keys():
        for entry in records[key]:
            sprint(";".join(entry))


def process(lines):
    gatherData(lines)
    

def preprocess(lines):
    result = []
    for line in lines:
        if "sta;" in line[4:]:
            index = line[4:].index("sta;")
            result += [line[:4+index]]
            result += [line[4+index:]]
        else:
            result += [line]
    return result

def main():
    if len(sys.argv) != 1:
        eprint("error: invalid argument(s)\n")
        show_usage()
        return 1

    eprint("reading...")
    process(preprocess(sys.stdin.readlines()))
    
    eprint("found logging for threads: ", list(records.keys()))
    plot2(27127)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc(file=sys.stdout)
    show_usage()
    sys.exit(1)
    
    