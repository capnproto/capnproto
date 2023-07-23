@0x8f5d14e1c273738d;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("capnp");
$Cxx.allowCancellation;

interface ByteStream {
  write @0 (bytes :Data) -> stream;
  # Write a chunk.

  end @1 ();
  # Signals clean EOF. (If the ByteStream is dropped without calling this, then the stream was
  # prematurely canceled and so the body should not be considered complete.)

  getSubstream @2 (callback :SubstreamCallback,
                   limit :UInt64 = 0xffffffffffffffff) -> (substream :ByteStream);
  # This method is used to implement path shortening optimization. It is designed in particular
  # with KJ streams' pumpTo() in mind.
  #
  # getSubstream() returns a new stream object that can be used to write to the same destination
  # as this stream. The substream will operate until it has received `limit` bytes, or its `end()`
  # method has been called, whichever occurs first. At that time, it invokes one of the methods of
  # `callback` based on the termination condition.
  #
  # While a substream is active, it is an error to call write() on the original stream. Doing so
  # may throw an exception or may arbitrarily interleave bytes with the substream's writes.

  startTls @3 (expectedServerHostname :Text) -> stream;
  # Client calls this method when it wants to initiate TLS. This ByteStream is not terminated,
  # the caller should reuse it.

  interface SubstreamCallback {
    ended @0 (byteCount :UInt64);
    # `end()` was called on the substream after writing `byteCount` bytes. The `end()` call was
    # NOT forwarded to the underlying stream, which remains open.

    reachedLimit @1 () -> (next :ByteStream);
    # The number of bytes specified by the `limit` parameter of `getSubstream()` was reached.
    # The substream will "resolve itself" to `next`, so that all future calls to the substream
    # are forwarded to `next`.
    #
    # If the `write()` call which reached the limit included bytes past the limit, then the first
    # `write()` call to `next` will be for those leftover bytes.
  }
}
