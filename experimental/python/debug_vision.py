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

    def __init__(self):
        self.time = []
        self.labels = []
        self.deltas = []
        self.delta_heights = []

    def add(self, steady_time, label):
        self.time += [steady_time]
        self.labels += [label]        

start = None
records = {}
start_inspect_cycle_time = 0
labels = {}

def gatherLine(line):
    global start 
    global records 
    global start_inspect_cycle_time 

    data = line.split(";")
    pid = data[0]
    processName = data[1]
    tid = int(data[2])
    label = data[3]
    steady_time = int(data[4])
    #system_time = data[5]
    
    if not tid in records:
        records[tid] = DataContainer()
   
    if start == None:
        start = steady_time
    steady_time = steady_time - start
    steady_time = steady_time / 1000        # steady_time is in us, we want ms

    records[tid].add(steady_time, label)

    labels[label] = None    # collect labels in-order

    if label == "Start INSPCYCLE":
        start_inspect_cycle_time = steady_time
    
    if label == "End INSPCYCLE":
        records[tid].deltas += [start_inspect_cycle_time]
        inspect_cycle_time = (steady_time - start_inspect_cycle_time)
        records[tid].delta_heights += [inspect_cycle_time]


def gatherData(lines):
    for line in lines: #[500:10000]:
        line = line.strip()
        if line.startswith("sta;") and line.endswith(";end;"):
            gatherLine(line[4:-5])


def plot2(tid):
    fig, ax1 = plt.subplots(1, 1, figsize=(12,3))
    record = records[tid]

    ax2 = ax1.twinx()

    la = list(labels)
    ax2.yticks = la.reverse()
    
    ax1.scatter(record.deltas, record.delta_heights, marker='v', color='red', zorder=3.5)

    ax2.scatter(record.time, record.labels, marker='o', zorder=2.5)
    ax2.plot(record.time, record.labels, color='lime', zorder=1.5)
    plt.xlim(left=0)
    plt.grid(alpha=0.7)
    plt.show()


def writeFiles():
    sprint(";;".join(records.keys()))
    for key in records.keys():
        for entry in records[key]:
            sprint(";".join(entry))


def process(lines):
    gatherData(lines)
    #writeFiles()


def main():
    if len(sys.argv) != 1:
        eprint("error: invalid argument(s)\n")
        show_usage()
        return 1

    sprint("reading...")
    process(sys.stdin.readlines())
    plot2(4540)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception:
        traceback.print_exc(file=sys.stdout)
    show_usage()
    sys.exit(1)
    
    