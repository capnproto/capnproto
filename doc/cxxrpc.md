---
layout: page
title: C++ RPC
---

# C++ RPC

The Cap'n Proto C++ RPC layer sits on top of the [serialization layer](cxx.html) and implements
the [RPC protocol](rpc.html).

## Current Status

As of version 0.4, Cap'n Proto's C++ RPC implementation is a [Level 1](rpc.html#protocol-features)
implementation.  Persistent capabilities, three-way introductions, and distributed equality are
not yet implemented.

## Sample Code

The [Calculator example](https://github.com/sandstorm-io/capnproto/tree/master/c++/samples) implements
a fully-functional Cap'n Proto client and server.

## KJ Concurrency Framework

RPC naturally requires a notion of concurrency.  Unfortunately,
[all concurrency models suck](https://plus.google.com/u/0/+KentonVarda/posts/D95XKtB5DhK).

Cap'n Proto's RPC is based on the [KJ library](cxx.html#kj-library)'s event-driven concurrency
framework.  The core of the KJ asynchronous framework (events, promises, callbacks) is defined in
`kj/async.h`, with I/O interfaces (streams, sockets, networks) defined in `kj/async-io.h`.

### Event Loop Concurrency

KJ's concurrency model is based on event loops.  While multiple threads are allowed, each thread
must have its own event loop.  KJ discourages fine-grained interaction between threads as
synchronization is expensive and error-prone.  Instead, threads are encouraged to communicate
through Cap'n Proto RPC.

KJ's event loop model bears a lot of similarity to the Javascript concurrency model.  Experienced
Javascript hackers -- especially node.js hackers -- will feel right at home.

_As of version 0.4, the only supported way to communicate between threads is over pipes or
socketpairs.  This will be improved in future versions.  For now, just set up an RPC connection
over that socketpair.  :)_

### Promises

Function calls that do I/O must do so asynchronously, and must return a "promise" for the
result.  Promises -- also known as "futures" in some systems -- are placeholders for the results
of operations that have not yet completed.  When the operation completes, we say that the promise
"resolves" to a value, or is "fulfilled".  A promise can also be "rejected", which means an
exception occurred.

{% highlight c++ %}
// Example promise-based interfaces.

kj::Promise<kj::String> fetchHttp(kj::StringPtr url);
// Asynchronously fetches an HTTP document and returns
// the content as a string.

kj::Promise<void> sendEmail(kj::StringPtr address,
    kj::StringPtr title, kj::StringPtr body);
// Sends an e-mail to the given address with the given title
// and body.  The returned promise resolves (to nothing) when
// the message has been successfully sent.
{% endhighlight %}

As you will see, KJ promises are very similar to the evolving Javascript promise standard, and
much of the [wisdom around it](https://www.google.com/search?q=javascript+promises) can be directly
applied to KJ promises.

### Callbacks

If you want to do something with the result of a promise, you must first wait for it to complete.
This is normally done by registering a callback to execute on completion.  Luckily, C++11 just
introduced lambdas, which makes this far more pleasant than it would have been a few years ago!

{% highlight c++ %}
kj::Promise<kj::String> contentPromise =
    fetchHttp("http://example.com");

kj::Promise<int> lineCountPromise =
    contentPromise.then([](kj::String&& content) {
  return countChars(content, '\n');
});
{% endhighlight %}

The callback passed to `then()` takes the promised result as its parameter and returns a new value.
`then()` itself returns a new promise for that value which the callback will eventually return.
If the callback itself returns a promise, then `then()` actually returns a promise for the
resolution of the latter promise -- that is, `Promise<Promise<T>>` is automatically reduced to
`Promise<T>`.

Note that `then()` consumes the original promise:  you can only call `then()` once.  This is true
of all of the methods of `Promise`.  The only way to consume a promise in multiple places is to
first "fork" it with the `fork()` method, which we don't get into here.  Relatedly, promises
are linear types, which means they have move constructors but not copy constructors.

### Error Propagation

`then()` takes an optional second parameter for handling errors.  Think of this like a `catch`
block.

{% highlight c++ %}
kj::Promise<int> lineCountPromise =
    promise.then([](kj::String&& content) {
  return countChars(content, '\n');
}, [](kj::Exception&& exception) {
  // Error!  Pretend the document was empty.
  return 0;
});
{% endhighlight %}

Note that the KJ framework coerces all exceptions to `kj::Exception` -- the exception's description
(as returned by `what()`) will be retained, but any type-specific information is lost.  Under KJ
exception philosophy, exceptions always represent an error that should not occur under normal
operation, and the only purpose of exceptions is to make software fault-tolerant.  In particular,
the only reasonable ways to handle an exception are to try again, tell a human, and/or propagate
to the caller.  To that end, `kj::Exception` contains information useful for reporting purposes
and to help decide if trying again is reasonable, but typed exception hierarchies are not useful
and not supported.

It is recommended that Cap'n Proto code use the assertion macros in `kj/debug.h` to throw
exceptions rather than use the C++ `throw` keyword.  These macros make it easy to add useful
debug information to an exception and generally play nicely with the KJ framework.  In fact, you
can even use these macros -- and propagate exceptions through promises -- if you compile your code
with exceptions disabled.  See the headers for more information.

### Waiting

It is illegal for code running in an event callback to wait, since this would stall the event loop.
However, if you are the one responsible for starting the event loop in the first place, then KJ
makes it easy to say "run the event loop until this promise resolves, then return the result".

{% highlight c++ %}
kj::EventLoop loop;
kj::WaitScope waitScope(loop);

kj::Promise<kj::String> contentPromise =
    fetchHttp("http://example.com");

kj::String content = contentPromise.wait(waitScope);

int lineCount = countChars(content, '\n');
{% endhighlight %}

Using `wait()` is common in high-level client-side code.  On the other hand, it is almost never
used in servers.

### Cancellation

If you discard a `Promise` without calling any of its methods, the operation it was waiting for
is canceled, because the `Promise` itself owns that operation.  This means than any pending
callbacks simply won't be executed.  If you need explicit notification when a promise is canceled,
you can use its `attach()` method to attach an object with a destructor -- the destructor will be
called when the promise either completes or is canceled.

### Lazy Execution

Callbacks registered with `.then()` which aren't themselves asynchronous (i.e. they return a value,
not a promise) by default won't execute unless the result is actually used -- they are executed
"lazily". This allows the runtime to optimize by combining a series of .then() callbacks into one.

To force a `.then()` callback to execute as soon as its input is available, do one of the
following:

* Add it to a `kj::TaskSet` -- this is usually the best choice. You can cancel all tasks in the set
  by destroying the `TaskSet`.
* `.wait()` on it -- but this only works in a top-level wait scope, typically your program's main
  function.
* Call `.eagerlyEvaluate()` on it. This returns a new `Promise`. You can cancel the task by
  destroying this `Promise` (without otherwise consuming it).
* `.detach()` it. **WARNING:** `.detach()` is dangerous because there is no way to cancel a promise
  once it has been detached. This can make it impossible to safely tear down the execution
  environment, e.g. if the callback has captured references to other objects. It is therefore
  recommended to avoid `.detach()` except in carefully-controlled circumstances.

### Other Features

KJ supports a number of primitive operations that can be performed on promises.  The complete API
is documented directly in the `kj/async.h` header.  Additionally, see the `kj/async-io.h` header
for APIs for performing basic network I/O -- although Cap'n Proto RPC users typically won't need
to use these APIs directly.

## Generated Code

Imagine the following interface:

{% highlight capnp %}
interface Directory {
  create @0 (name :Text) -> (file :File);
  open @1 (name :Text) -> (file :File);
  remove @2 (name :Text);
}
{% endhighlight %}

`capnp compile` will generate code that looks like this (edited for readability):

{% highlight c++ %}
struct Directory {
  Directory() = delete;

  class Client;
  class Server;

  struct CreateParams;
  struct CreateResults;
  struct OpenParams;
  struct OpenResults;
  struct RemoveParams;
  struct RemoveResults;
  // Each of these is equivalent to what would be generated for
  // a Cap'n Proto struct with one field for each parameter /
  // result.
};

class Directory::Client
    : public virtual capnp::Capability::Client {
public:
  Client(std::nullptr_t);
  Client(kj::Own<Directory::Server> server);
  Client(kj::Promise<Client> promise);
  Client(kj::Exception exception);

  capnp::Request<CreateParams, CreateResults> createRequest();
  capnp::Request<OpenParams, OpenResults> openRequest();
  capnp::Request<RemoveParams, RemoveResults> removeRequest();
};

class Directory::Server
    : public virtual capnp::Capability::Server {
protected:
  typedef capnp::CallContext<CreateParams, CreateResults> CreateContext;
  typedef capnp::CallContext<OpenParams, OpenResults> OpenContext;
  typedef capnp::CallContext<RemoveParams, RemoveResults> RemoveContext;
  // Convenience typedefs.

  virtual kj::Promise<void> create(CreateContext context);
  virtual kj::Promise<void> open(OpenContext context);
  virtual kj::Promise<void> remove(RemoveContext context);
  // Methods for you to implement.
};
{% endhighlight %}

### Clients

The generated `Client` type represents a reference to a remote `Server`.  `Client`s are
pass-by-value types that use reference counting under the hood.  (Warning:  For performance
reasons, the reference counting used by `Client`s is not thread-safe, so you must not copy a
`Client` to another thread, unless you do it by means of an inter-thread RPC.)

A `Client` can be implicitly constructed from any of:

* A `kj::Own<Server>`, which takes ownership of the server object and creates a client that
  calls it.  (You can get a `kj::Own<T>` to a newly-allocated heap object using
  `kj::heap<T>(constructorParams)`; see `kj/memory.h`.)
* A `kj::Promise<Client>`, which creates a client whose methods first wait for the promise to
  resolve, then forward the call to the resulting client.
* A `kj::Exception`, which creates a client whose methods always throw that exception.
* `nullptr`, which creates a client whose methods always throw.  This is meant to be used to
  initialize variables that will be initialized to a real value later on.

For each interface method `foo()`, the `Client` has a method `fooRequest()` which creates a new
request to call `foo()`.  The returned `capnp::Request` object has methods equivalent to a
`Builder` for the parameter struct (`FooParams`), with the addition of a method `send()`.
`send()` sends the RPC and returns a `capnp::RemotePromise<FooResults>`.

This `RemotePromise` is equivalent to `kj::Promise<capnp::Response<FooResults>>`, but also has
methods that allow pipelining.  Namely:

* For each interface-typed result, it has a getter method which returns a `Client` of that type.
  Calling this client will send a pipelined call to the server.
* For each struct-typed result, it has a getter method which returns an object containing pipeline
  getters for that struct's fields.

In other words, the `RemotePromise` effectively implements a subset of the eventual results'
`Reader` interface -- one that only allows access to interfaces and sub-structs.

The `RemotePromise` eventually resolves to `capnp::Response<FooResults>`, which behaves like a
`Reader` for the result struct except that it also owns the result message.

{% highlight c++ %}
Directory::Client dir = ...;

// Create a new request for the `open()` method.
auto request = dir.openRequest();
request.setName("foo");

// Send the request.
auto promise = request.send();

// Make a pipelined request.
auto promise2 = promise.getFile().getSizeRequest().send();

// Wait for the full results.
auto promise3 = promise2.then(
    [](capnp::Response<File::GetSizeResults>&& response) {
  cout << "File size is: " << response.getSize() << endl;
});
{% endhighlight %}

For [generic methods](language.html#generic-methods), the `fooRequest()` method will be a template;
you must explicitly specify type parameters.

### Servers

The generated `Server` type is an abstract interface which may be subclassed to implement a
capability.  Each method takes a `context` argument and returns a `kj::Promise<void>` which
resolves when the call is finished.  The parameter and result structures are accessed through the
context -- `context.getParams()` returns a `Reader` for the parameters, and `context.getResults()`
returns a `Builder` for the results.  The context also has methods for controlling RPC logistics,
such as cancellation -- see `capnp::CallContext` in `capnp/capability.h` for details.

Accessing the results through the context (rather than by returning them) is unintuitive, but
necessary because the underlying RPC transport needs to have control over where the results are
allocated.  For example, a zero-copy shared memory transport would need to allocate the results in
the shared memory segment.  Hence, the method implementation cannot just create its own
`MessageBuilder`.

{% highlight c++ %}
class DirectoryImpl final: public Directory::Server {
public:
  kj::Promise<void> open(OpenContext context) override {
    auto iter = files.find(context.getParams().getName());

    // Throw an exception if not found.
    KJ_REQUIRE(iter != files.end(), "File not found.");

    context.getResults().setFile(iter->second);

    return kj::READY_NOW;
  }

  // Any method which we don't implement will simply throw
  // an exception by default.

private:
  std::map<kj::StringPtr, File::Client> files;
};
{% endhighlight %}

On the server side, [generic methods](language.html#generic-methods) are NOT templates. Instead,
the generated code is exactly as if all of the generic parameters were bound to `AnyPointer`. The
server generally does not get to know exactly what type the client requested; it must be designed
to be correct for any parameterization.

## Initializing RPC

Cap'n Proto makes it easy to start up an RPC client or server using the  "EZ RPC" classes,
defined in `capnp/ez-rpc.h`.  These classes get you up and running quickly, but they hide a lot
of details that power users will likely want to manipulate.  Check out the comments in `ez-rpc.h`
to understand exactly what you get and what you miss.  For the purpose of this overview, we'll
show you how to use EZ RPC to get started.

### Starting a client

A client should typically look like this:

{% highlight c++ %}
#include <capnp/ez-rpc.h>
#include "my-interface.capnp.h"
#include <iostream>

int main(int argc, const char* argv[]) {
  // We expect one argument specifying the server address.
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " HOST[:PORT]" << std::endl;
    return 1;
  }

  // Set up the EzRpcClient, connecting to the server on port
  // 5923 unless a different port was specified by the user.
  capnp::EzRpcClient client(argv[1], 5923);
  auto& waitScope = client.getWaitScope();

  // Request the bootstrap capability from the server.
  MyInterface::Client cap = client.getMain<MyInterface>();

  // Make a call to the capability.
  auto request = cap.fooRequest();
  request.setParam(123);
  auto promise = request.send();

  // Wait for the result.  This is the only line that blocks.
  auto response = promise.wait(waitScope);

  // All done.
  std::cout << response.getResult() << std::endl;
  return 0;
}
{% endhighlight %}

Note that for the connect address, Cap'n Proto supports DNS host names as well as IPv4 and IPv6
addresses.  Additionally, a Unix domain socket can be specified as `unix:` followed by a path name.

For a more complete example, see the
[calculator client sample](https://github.com/sandstorm-io/capnproto/tree/master/c++/samples/calculator-client.c++).

### Starting a server

A server might look something like this:

{% highlight c++ %}
#include <capnp/ez-rpc.h>
#include "my-interface-impl.h"
#include <iostream>

int main(int argc, const char* argv[]) {
  // We expect one argument specifying the address to which
  // to bind and accept connections.
  if (argc != 2) {
    std::cerr << "usage: " << argv[0] << " ADDRESS[:PORT]"
              << std::endl;
    return 1;
  }

  // Set up the EzRpcServer, binding to port 5923 unless a
  // different port was specified by the user.  Note that the
  // first parameter here can be any "Client" object or anything
  // that can implicitly cast to a "Client" object.  You can even
  // re-export a capability imported from another server.
  capnp::EzRpcServer server(kj::heap<MyInterfaceImpl>(), argv[1], 5923);
  auto& waitScope = server.getWaitScope();

  // Run forever, accepting connections and handling requests.
  kj::NEVER_DONE.wait(waitScope);
}
{% endhighlight %}

Note that for the bind address, Cap'n Proto supports DNS host names as well as IPv4 and IPv6
addresses.  The special address `*` can be used to bind to the same port on all local IPv4 and
IPv6 interfaces.  Additionally, a Unix domain socket can be specified as `unix:` followed by a
path name.

For a more complete example, see the
[calculator server sample](https://github.com/sandstorm-io/capnproto/tree/master/c++/samples/calculator-server.c++).

## Debugging

If you've written a server and you want to connect to it to issue some calls for debugging, perhaps
interactively, the easiest way to do it is to use [pycapnp](http://jparyani.github.io/pycapnp/).
We have decided not to add RPC functionality to the `capnp` command-line tool because pycapnp is
better than anything we might provide.
