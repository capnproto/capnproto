#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <capnp/dynamic.h>
#include <capnp/test.capnp.h>
#include <kj/exception.h>
#include <kj/array.h>
#include <kj/string.h>

/* libFuzzer entry point fuzzing Cap'n Proto's JSON decoder
 * (text JSON -> Cap'n Proto message). Cap'n Proto throws kj exceptions on
 * malformed input; those are expected, not bugs, so we swallow them via
 * kj::runCatchingExceptions.
 */
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  kj::runCatchingExceptions([&]() {
    capnp::JsonCodec json;
    capnp::MallocMessageBuilder mb;
    auto root = mb.initRoot<capnproto_test::capnp::test::TestAllTypes>();

    json.decode(kj::arrayPtr(reinterpret_cast<const char*>(Data), Size), root);

    // Re-encode to exercise the encoder over whatever was successfully decoded.
    kj::String reencoded = json.encode(root.asReader());
    (void)reencoded;
  });
  return 0;
}
