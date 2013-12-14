---
layout: post
title: "Promise Pipelining and Dependent Calls: Cap'n Proto vs. Thrift vs. Ice"
author: kentonv
---

_UPDATED:  Added Thrift to the comparison._

So, I totally botched the 0.4 release announcement yesterday.  I was excited about promise
pipelining, but I wasn't sure how to describe it in headline form.  I decided to be a bit
silly and call it "time travel", tongue-in-cheek.  My hope was that people would then be
curious, read the docs, find out that this is actually a really cool feature, and start doing
stuff with it.

Unfortunately, [my post](2013-12-12-capnproto-0.4-time-travel.html) only contained a link to
the full explanation and then confusingly followed the "time travel" section with a separate section
describing the fact that I had implemented a promise API in C++.  Half the readers clicked through
to the documentation and understood.  The other half thought I was claiming that promises alone
constituted "time travel", and thought I was ridiculously over-hyping an already-well-known
technique.  My HN post was subsequently flagged into oblivion.

Let me be clear:

**Promises alone are _not_ what I meant by "time travel"!**

<img src='{{ site.baseurl }}images/capnp-vs-thrift-vs-ice.png' style='width:350px; height:275px; float: right;'>

So what did I mean?  Perhaps [this benchmark](https://github.com/kentonv/capnp-vs-ice) will
make things clearer.  Here, I've defined a server that exports a simple four-function calculator
interface, with `add()`, `sub()`, `mult()`, and `div()` calls, each taking two integers and\
returning a result.

You are probably already thinking:  That's a ridiculously bad way to define an RPC interface!
You want to have _one_ method `eval()` that takes an expression tree (or graph, even), otherwise
you will have ridiculous latency.  But this is exactly the point.  **With promise pipelining, simple,
composable methods work fine.**

To prove the point, I've implemented servers in Cap'n Proto, [Apache Thrift](http://thrift.apache.org/),
and [ZeroC Ice](http://www.zeroc.com/).  I then implemented clients against each one, where the
client attempts to evaluate the expression:

    ((5 * 2) + ((7 - 3) * 10)) / (6 - 4)

All three frameworks support asynchronous calls with a promise/future-like interface, and all of my
clients use these interfaces to parallelize calls.  However, notice that even with parallelization,
it takes four steps to compute the result:

    # Even with parallelization, this takes four steps!
    ((5 * 2) + ((7 - 3) * 10)) / (6 - 4)
      (10    + (   4    * 10)) /    2      # 1
      (10    +         40)     /    2      # 2
            50                 /    2      # 3
                              25           # 4

As such, the Thrift and Ice clients take four network round trips.  Cap'n Proto, however, takes
only one.

Cap'n Proto, you see, sends all six calls from the client to the server at one time.  For the
latter calls, it simply tells the server to substitute the former calls' results into the new
requests, once those dependency calls finish.  Typical RPC systems can only send three calls to
start, then must wait for some to finish before it can continue with the remaining calls.  Over
a high-latency connection, this means they take 4x longer than Cap'n Proto to do their work in
this test.

So, does this matter outside of a contrived example case?  Yes, it does, because it allows you to
write cleaner interfaces with simple, composable methods, rather than monster do-everything-at-once
methods.  The four-method calculator interface is much simpler than one involving sending an
expression graph to the server in one batch.  Moreover, pipelining allows you to define
object-oriented interfaces where you might otherwise be tempted to settle for singletons.  See
[my extended argument]({{ site.baseurl }}rpc.html#introduction) (this is what I was trying to get
people to click on yesterday :) ).

Hopefully now it is clearer what I was trying to illustrate with this diagram, and what I meant
by "time travel"!

<img src='{{ site.baseurl }}images/time-travel.png' style='max-width:639px'>
