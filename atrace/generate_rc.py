#!/usr/bin/env python3

import argparse

parser = argparse.ArgumentParser("generate_rc.py", description="Generates an .rc files that fixes the permissions for all the ftrace events listed in the input atrace_categories.txt file")
parser.add_argument("filename", help="Path to the atrace_categories.txt file")

args = parser.parse_args()

on_boot_events = ["mali/", "power/gpu_work_period", "power/gpu_frequency"]

print("# Sets permission for vendor ftrace events on late-init")
print("on late-init")

with open(args.filename, 'r') as f:
  for line in f:
    line = line.rstrip('\n')
    if line.startswith(' ') or line.startswith('\t'):
      path = line.lstrip(" \t")

      if any(path.startswith(event) for event in on_boot_events):
        continue

      print("    chmod 0666 /sys/kernel/debug/tracing/events/{}/enable".format(path))
      print("    chmod 0666 /sys/kernel/tracing/events/{}/enable".format(path))
    else:
      print ("    # {} trace points".format(line))

print("# Sets permission for vendor ftrace events on boot")
print("on boot")

with open(args.filename, 'r') as f:
  for line in f:
    line = line.rstrip('\n')
    if not line.startswith(' ') or line.startswith('\t'):
      print ("    # {} trace points".format(line))
      continue

    path = line.lstrip(" \t")

    if not any(path.startswith(event) for event in on_boot_events):
      continue

    print("    chmod 0666 /sys/kernel/debug/tracing/events/{}/enable".format(path))
    print("    chmod 0666 /sys/kernel/tracing/events/{}/enable".format(path))
