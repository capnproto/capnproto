// This program is a code generator plugin for `capnp compile` which writes the schema back to
// stdout in json format. Useful for quickly bootstrapping a compiler plugin in another language
// with pre-existing support for json.

#include <capnp/schema.capnp.h>
#include "../serialize.h"
#include <kj/main.h>
#include <kj/miniposix.h>
#include <kj/encoding.h>
#include <kj/debug.h>
#include <capnp/compat/json.h>

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace capnp {
namespace {

class AnyPointerToBase64: public JsonCodec::Handler<DynamicValue> {
  void encode(const JsonCodec& codec, ReaderFor<DynamicValue> input, JsonValue::Builder output) const {
    auto anyPointer = input.as<AnyPointer>();
    MallocMessageBuilder message;
    message.setRoot(anyPointer);
    auto flatArray = messageToFlatArray(message);
    auto bytes = flatArray.asBytes();
    auto encoded = kj::encodeBase64(bytes);
    output.setString(encoded);
  }
  Orphan<DynamicValue> decode(const JsonCodec& codec, JsonValue::Reader input, Orphanage orphanage) const {
    KJ_UNIMPLEMENTED("AnyPointerToBase64::decode");
  }
};

class CapnpcJsonMain {
public:
  CapnpcJsonMain(kj::ProcessContext& context): context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "Cap'n Proto json plugin version " VERSION,
          "This is a Cap'n Proto compiler plugin which prints the code generator request "
          "in json format. This is meant to be run using the Cap'n Proto compiler, e.g.:\n"
          "    capnp compile -ojson foo.capnp")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }
private:
  kj::ProcessContext& context;

  kj::MainBuilder::Validity run() {
    ReaderOptions options;
    options.traversalLimitInWords = 1 << 30; // Don't limit.
    StreamFdMessageReader reader(STDIN_FILENO, options);
    auto request = reader.getRoot<schema::CodeGeneratorRequest>();

    AnyPointerToBase64 anyPointerHandler;
    JsonCodec codec;
    codec.handleByAnnotation<schema::CodeGeneratorRequest>();
    codec.addTypeHandler(Type(schema::Type::Which::ANY_POINTER), anyPointerHandler);
    auto serialized = codec.encode(request);

    kj::FdOutputStream rawOut(STDOUT_FILENO);
    rawOut.write(serialized.begin(), serialized.size());

    return true;
  }
};

} // namespace
} // namespace capnp

KJ_MAIN(capnp::CapnpcJsonMain);