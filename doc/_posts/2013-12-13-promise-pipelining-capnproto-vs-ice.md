---
layout: post
title: "Promise Pipelining and Dependent Calls: Cap'n Proto vs. Ice"
author: kentonv
---

So, I totally botched the 0.4 release announcement yesterday.  I was excited about promise
pipelining, but I wasn't sure how to describe it in headline form.  I decided to be a bit
silly and call it "time travel", tongue-in-cheek.  My hope was that people would then be
curious, read the docs, find out that this is actually a really cool feature, and start doing
stuff with it.

Unfortunately, [my post](2013-12-12-capnproto-0.4-time-travel.html) only contained a link to
the full explanation and then confusingly followed "time travel" section with a separate section
describing the fact that I had implemented a promise API in C++.  Half the readers clicked through
to the documentation and understood.  The other half thought I was claiming that promises alone
constituted "time travel", and thought I was over-hyping an already-widely-used technique.

Let me be clear:

**Promises alone are _not_ what I meant by "time travel"!**

<img src='{{ site.baseurl }}images/capnp-vs-ice.png' style='width:318px; height:276px; float: right;'>

So what did I mean?  Perhaps [this benchmark](https://github.com/kentonv/capnp-vs-ice) will
make things clearer.  Here, I've defined a server that exports a simple four-function calculator
interface, with `add()`, `sub()`, `mult()`, and `div()` calls, each taking two integers and\
returning a result.

You are probably already thinking:  That's a ridiculously bad way to define an RPC interface!
You want to have _one_ method `eval()` that takes an expression tree, otherwise you will have
ridiculous latency.  But this is exactly the point.  With promise pipelining, this is a perfectly
fine interface.

To prove the point, I implemented servers in both Cap'n Proto and
[ZeroC Ice](http://www.zeroc.com/), an alternative RPC framework.  I also implemented clients
against each one, where the client attempts to evaluate the expression:

    ((5 * 2) + ((7 - 3) * 10)) / (6 - 4)

Both frameworks support asynchronous calls with a promise/future-like interface, and both of my
clients use these interfaces to parallelize calls.  However, notice that even with parallelization,
it takes four steps to compute the result:

    # Even with parallelization, this takes four steps!
    ((5 * 2) + ((7 - 3) * 10)) / (6 - 4)
      (10    + (   4    * 10)) /    2      # 1
      (10    +         40)     /    2      # 2
            50                 /    2      # 3
                              25           # 4

As such, the ZeroC client takes four network round trips.  Cap'n Proto, however, takes only one.

Cap'n Proto, you see, sends all six calls from the client to the server at one time.  For the
latter calls, it simply tells the server to substitute the former calls' results into the new
requests, once those dependency calls finish.  Ice can only send three calls to start, then must
wait for some to finish before it can continue with the remaining calls.  Over a high-latency
connection, this means the Ice client takes 4x longer than Cap'n Proto to do its work.

So, does this matter outside of a contrived example case?  Yes, it does, because it allows you to
write cleaner, simpler interfaces.  The four-method calculator interface is much simpler than
one involving sending an expression graph to the server in one batch.  Moreover, pipelining
allows you to define object-oriented interfaces where you might otherwise be tempted to go
procedural.  See [my extended argument]({{ site.baseurl }}rpc.html#introduction)
(this is what I was trying to get people to click on yesterday :) ).

Hopefully now it is clearer what I was trying to illustrate with this diagram, and what I meant
by "time travel"!

<img src='{{ site.baseurl }}images/time-travel.png' style='max-width:639px'>
