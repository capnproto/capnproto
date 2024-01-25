// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "glob-filter.h"

namespace kj {

GlobFilter::GlobFilter(const char* pattern): pattern(heapString(pattern)) {}
GlobFilter::GlobFilter(ArrayPtr<const char> pattern): pattern(heapString(pattern)) {}

bool GlobFilter::matches(StringPtr name) {
  // Get out your computer science books. We're implementing a non-deterministic finite automaton.
  //
  // Our NDFA has one "state" corresponding to each character in the pattern.
  //
  // As you may recall, an NDFA can be transformed into a DFA where every state in the DFA
  // represents some combination of states in the NDFA. Therefore, we actually have to store a
  // list of states here. (Actually, what we really want is a set of states, but because our
  // patterns are mostly non-cyclic a list of states should work fine and be a bit more efficient.)

  // Our state list starts out pointing only at the start of the pattern.
  states.resize(0);
  states.add(0);

  Vector<uint> scratch;

  // Iterate through each character in the name.
  for (char c: name) {
    // Pull the current set of states off to the side, so that we can populate `states` with the
    // new set of states.
    Vector<uint> oldStates = kj::mv(states);
    states = kj::mv(scratch);
    states.resize(0);

    // The pattern can omit a leading path. So if we're at a '/' then enter the state machine at
    // the beginning on the next char.
    if (c == '/' || c == '\\') {
      states.add(0);
    }

    // Process each state.
    for (uint state: oldStates) {
      applyState(c, state);
    }

    // Store the previous state vector for reuse.
    scratch = kj::mv(oldStates);
  }

  // If any one state is at the end of the pattern (or at a wildcard just before the end of the
  // pattern), we have a match.
  for (uint state: states) {
    while (state < pattern.size() && pattern[state] == '*') {
      ++state;
    }
    if (state == pattern.size()) {
      return true;
    }
  }
  return false;
}

void GlobFilter::applyState(char c, int state) {
  if (state < pattern.size()) {
    switch (pattern[state]) {
      case '*':
        // At a '*', we both re-add the current state and attempt to match the *next* state.
        if (c != '/' && c != '\\') {  // '*' doesn't match '/'.
          states.add(state);
        }
        applyState(c, state + 1);
        break;

      case '?':
        // A '?' matches one character (never a '/').
        if (c != '/' && c != '\\') {
          states.add(state + 1);
        }
        break;

      default:
        // Any other character matches only itself.
        if (c == pattern[state]) {
          states.add(state + 1);
        }
        break;
    }
  }
}

}  // namespace kj
