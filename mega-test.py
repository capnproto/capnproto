#! /usr/bin/env python

# MEGA TEST
#
# usage:  mega-test.py <config>
#
# This runs several tests in parallel and shows progress bars for each, based on a config file.
#
# <config> is a file containing a list of commands to run along with the expected number of lines
# they will output (to stdout and stderr combined), which is how the progress bar is calculated.
# The format of the file is simply one test per line, with the line containing the test name,
# the number of output lines expected, and the test command.  Example:
#
#     mytest 1523 ./my-test --foo bar
#     another 862 ./another-test --baz
#
# Each command is interpreted by `sh -euc`, therefore it is acceptable to use environment
# variables and other shell syntax.
#
# After all tests complete, the config file will be rewritten to update the line counts to the
# actual number of lines seen for all passing tests (failing tests are not updated).

import sys
import re
import os
from errno import EAGAIN
from fcntl import fcntl, F_GETFL, F_SETFL
from select import poll, POLLIN, POLLHUP
from subprocess import Popen, PIPE, STDOUT

CONFIG_LINE = re.compile("^([^ ]+) +([0-9]+) +(.*)$")

if len(sys.argv) != 2:
  sys.stderr.write("Wrong number of arguments.\n");
  sys.exit(1)

if not os.access("/tmp/test-output", os.F_OK):
  os.mkdir("/tmp/test-output")

config = open(sys.argv[1], 'r')

tests = []

class Test:
  def __init__(self, name, command, lines):
    self.name = name
    self.command = command
    self.lines = lines
    self.count = 0
    self.done = False

  def start(self, poller):
    self.proc = Popen(["sh", "-euc", test.command], stdin=dev_null, stdout=PIPE, stderr=STDOUT)
    fd = self.proc.stdout.fileno()
    flags = fcntl(fd, F_GETFL)
    fcntl(fd, F_SETFL, flags | os.O_NONBLOCK)
    poller.register(self.proc.stdout, POLLIN)
    self.log = open("/tmp/test-output/" + self.name + ".log", "w")

  def update(self):
    try:
      while True:
        text = self.proc.stdout.read()
        if text == "":
          self.proc.wait()
          self.done = True
          self.log.close()
          return True
        self.count += text.count("\n")
        self.log.write(text)
    except IOError as e:
      if e.errno == EAGAIN:
        return False
      raise

  def print_bar(self):
    percent = self.count * 100 / self.lines
    status = "(%3d%%)" % percent

    color_on = ""
    color_off = ""

    if self.done:
      if self.proc.returncode == 0:
        color_on = "\033[0;32m"
        status = "PASS"
      else:
        color_on = "\033[0;31m"
        status = "FAIL: /tmp/test-output/%s.log" % self.name
      color_off = "\033[0m"

    print "%s%-16s |%-25s| %6d/%6d %s%s    " % (
        color_on, self.name, '=' * min(percent / 4, 25), self.count, self.lines, status, color_off)

  def passed(self):
    return self.proc.returncode == 0

for line in config:
  if len(line) > 0 and not line.startswith("#"):
    match = CONFIG_LINE.match(line)
    if not match:
      sys.stderr.write("Invalid config syntax: %s\n" % line);
      sys.exit(1)
    test = Test(match.group(1), match.group(3), int(match.group(2)))
    tests.append(test)

config.close()

dev_null = open("/dev/null", "rw")
poller = poll()
fd_map = {}

for test in tests:
  test.start(poller)
  fd_map[test.proc.stdout.fileno()] = test

active_count = len(tests)

def print_bars():
  for test in tests:
    test.print_bar()

print_bars()

while active_count > 0:
  for (fd, event) in poller.poll():
    if fd_map[fd].update():
      active_count -= 1
      poller.unregister(fd)
  sys.stdout.write("\033[%dA\r" % len(tests))
  print_bars()

new_config = open(sys.argv[1], "w")
for test in tests:
  if test.passed():
    new_config.write("%-16s %6d %s\n" % (test.name, test.count, test.command))
  else:
    new_config.write("%-16s %6d %s\n" % (test.name, test.lines, test.command))

for test in tests:
  if not test.passed():
    sys.exit(1)

sys.exit(0)
