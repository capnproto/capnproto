---
layout: post
title: Cap'n Proto Beta Release
author: kentonv
---

It's been nearly three months since Cap'n Proto was originally announced, and by now you're
probably wondering what I've been up to.  The answer is basically
[non-stop coding](https://github.com/kentonv/capnproto/commits/master).  Features were implemented,
code was refactored, tests were written, and now Cap'n Proto is beginning to resemble something
like a real product.  But as is so often the case with me, I've been so engrossed in coding that I
forgot to post updates!

Well, that changes today, with the first official release of Cap'n Proto, v0.1.  While not yet
"done", this release should be usable for Real Work.  Feature-wise, for C++, the library is roughly
on par with [Google's Protocol Buffers](http://protobuf.googlecode.com) (which, as you know, I used
to maintain).  Features include:

* Types: numbers, bytes, text, enums, lists, structs, and unions.
* Code generation from schema definition files.
* Reading from and writing to file descriptors (or other streams).
* Text-format output (e.g. for debugging).
* Reflection, for writing generic code that dynamically walks over message contents.
* Dynamic schema loading (to manipulate types not known at compile time).
* Code generator plugins for extending the compiler to support new languages.
* Tested on Linux and Mac OSX with GCC and Clang.

Notably missing from this list is RPC (something Protocol Buffers never provided either).  The RPC
system is still in the design phase, but will be implemented over the coming weeks.

Also missing is support for languages other than C++.  However, I'm happy to report that a number
of awesome contributors have stepped up and are working on
[implementations in C, Go, Python]({{ site.baseurl }}otherlang.html), and a few others not yet
announced.  None of these are "ready" just yet, but watch this space.  (Would you like to work on
an implementation in your favorite language?
[Let us know!](https://groups.google.com/group/capnproto))

Going forward, Cap'n Proto releases will occur more frequently, perhaps every 2-4 weeks.
Consider [signing up for release announcements](https://groups.google.com/group/capnproto-announce).

In any case, go [download the release]({{ site.baseurl }}install.html) and
[tell us your thoughts](https://groups.google.com/group/capnproto).
