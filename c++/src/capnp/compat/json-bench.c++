// Copyright (c) 2026 Cap'n Proto contributors
// Licensed under the MIT License:

// Microbenchmarks for the JSON codec.  The inputs are generated in memory so that these
// benchmarks measure the codec rather than filesystem or Cap'n Proto wire-format I/O.

#include "json.h"
#include <capnp/compat/json-bench.capnp.h>

#include <benchmark/benchmark.h>
#include <capnp/message.h>
#include <kj/debug.h>

#include <cmath>
#include <string>

namespace capnp {
namespace {

using json_bench::Annotated;
using json_bench::Color;
using json_bench::Document;
using json_bench::Everything;
using json_bench::Small;

void accountBytes(benchmark::State& state, size_t bytes) {
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * bytes);
}

void initSmall(Small::Builder value, uint32_t id = 123) {
  value.setId(id);
  value.setEnabled((id & 1) != 0);
  value.setRatio(123.25 + id);
  value.setName("a small object");
  value.setColor(Color::ULTRAVIOLET);
}

void initEverything(Everything::Builder value) {
  value.setVoidField(VOID);
  value.setBoolField(true);
  value.setInt8Field(-12);
  value.setInt16Field(-12345);
  value.setInt32Field(-123456789);
  value.setInt64Field(-1234567890123456ll);
  value.setUint8Field(234);
  value.setUint16Field(45678);
  value.setUint32Field(3456789012u);
  value.setUint64Field(12345678901234567890ull);
  value.setFloat32Field(1.25e20f);
  value.setFloat64Field(-1.25e200);
  value.setTextField("quotes: \"; slash: \\; unicode: \342\230\203");
  const byte data[] = {0, 1, 2, 31, 127, 128, 254, 255};
  value.setDataField(kj::arrayPtr(data));
  value.setEnumField(Color::BLUE);
  initSmall(value.initSmall());

  auto bools = value.initBoolList(5);
  bools.set(0, true); bools.set(1, false); bools.set(2, true); bools.set(3, true); bools.set(4, false);
  auto ints = value.initIntList(5);
  for (uint i = 0; i < ints.size(); ++i) ints.set(i, static_cast<int32_t>(i) * 100000 - 200000);
  auto uints = value.initUint64List(4);
  uints.set(0, 0); uints.set(1, 1); uints.set(2, 9007199254740992ull); uints.set(3, UINT64_MAX);
  auto floats = value.initFloatList(7);
  floats.set(0, 0); floats.set(1, -0.0); floats.set(2, 1.5); floats.set(3, 1e200);
  floats.set(4, kj::inf()); floats.set(5, -kj::inf()); floats.set(6, kj::nan());
  auto texts = value.initTextList(4);
  texts.set(0, "short"); texts.set(1, ""); texts.set(2, "line1\nline2"); texts.set(3, "\342\230\203");
  auto blobs = value.initDataList(3);
  blobs.set(0, kj::arrayPtr(data, 2)); blobs.set(1, kj::arrayPtr(data, 5)); blobs.set(2, kj::arrayPtr(data));
  auto structs = value.initStructList(3);
  for (uint i = 0; i < structs.size(); ++i) initSmall(structs[i], i + 1);
  auto enums = value.initEnumList(4);
  enums.set(0, Color::RED); enums.set(1, Color::GREEN);
  enums.set(2, Color::BLUE); enums.set(3, Color::ULTRAVIOLET);
  value.initChoice().setString("the selected union member");
}

void initDocument(Document::Builder document, uint count, bool repetitive) {
  document.setTitle(repetitive ? "repetitive synthetic document" : "varied synthetic document");
  document.setVersion(7);
  auto records = document.initRecords(count);
  const byte repeatedPayload[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
  for (uint i = 0; i < count; ++i) {
    auto record = records[i];
    record.setId(repetitive ? 123456789012345ull : 123456789012345ull + i);
    record.setActive(repetitive || (i % 3 == 0));
    record.setScore(repetitive ? 17.25 : i * 0.125 - 100.0);
    if (repetitive) {
      record.setName("the same moderately long record name repeated many times");
    } else {
      record.setName(kj::str("record-", i, "-with-synthetic-content"));
    }
    auto tags = record.initTags(4);
    tags.set(0, repetitive ? "repeated" : "generated");
    tags.set(1, "json");
    tags.set(2, "capnproto");
    if (repetitive) {
      tags.set(3, "repeated");
    } else {
      tags.set(3, kj::str("bucket-", i % 97));
    }
    record.setPayload(kj::arrayPtr(repeatedPayload));
    initSmall(record.initLocation(), repetitive ? 42 : i);
  }
}

void initAnnotated(Annotated::Builder value) {
  value.setDisplayName("annotated object");
  const byte data[] = {0, 1, 2, 3, 0xde, 0xad, 0xbe, 0xef};
  value.setBase64Data(kj::arrayPtr(data));
  value.setHexData(kj::arrayPtr(data));
  auto details = value.initDetails();
  details.setCount(9001);
  details.setNote("flattened group");
  value.initSelection().setText("discriminated union");
  auto fields = value.initRaw().initObject(3);
  fields[0].setName("null"); fields[0].initValue().setNull();
  fields[1].setName("number"); fields[1].initValue().setNumber(123.5);
  fields[2].setName("array");
  auto array = fields[2].initValue().initArray(3);
  array[0].setBoolean(true); array[1].setString("raw json"); array[2].setNumber(-7);
}

template <typename T>
void encodeScalar(benchmark::State& state, T value) {
  JsonCodec codec;
  size_t bytes = 0;
  for (auto _ : state) {
    auto output = codec.encode(value);
    bytes = output.size();
    benchmark::DoNotOptimize(output.begin());
  }
  accountBytes(state, bytes);
}

static void BM_EncodeVoid(benchmark::State& s) { encodeScalar(s, VOID); }
static void BM_EncodeBool(benchmark::State& s) { encodeScalar(s, true); }
static void BM_EncodeInt32(benchmark::State& s) { encodeScalar(s, -123456789); }
static void BM_EncodeInt64(benchmark::State& s) { encodeScalar(s, int64_t{-1234567890123456ll}); }
static void BM_EncodeUInt64(benchmark::State& s) { encodeScalar(s, uint64_t{12345678901234567890ull}); }
static void BM_EncodeDouble(benchmark::State& s) { encodeScalar(s, -1.23456789012345e200); }
static void BM_EncodeSpecialDouble(benchmark::State& s) { encodeScalar(s, kj::nan()); }
static void BM_EncodeEnum(benchmark::State& s) { encodeScalar(s, Color::ULTRAVIOLET); }
static void BM_EncodeTextPlain(benchmark::State& s) { encodeScalar(s, Text::Reader("a short plain string")); }
static void BM_EncodeTextEscaped(benchmark::State& s) {
  encodeScalar(s, Text::Reader("quote=\" slash=\\ controls=\n\r\t unicode=\342\230\203"));
}
static void BM_EncodeData(benchmark::State& s) {
  const byte data[] = {0, 1, 2, 31, 127, 128, 254, 255};
  encodeScalar(s, Data::Reader(data, sizeof(data)));
}
BENCHMARK(BM_EncodeVoid);
BENCHMARK(BM_EncodeBool);
BENCHMARK(BM_EncodeInt32);
BENCHMARK(BM_EncodeInt64);
BENCHMARK(BM_EncodeUInt64);
BENCHMARK(BM_EncodeDouble);
BENCHMARK(BM_EncodeSpecialDouble);
BENCHMARK(BM_EncodeEnum);
BENCHMARK(BM_EncodeTextPlain);
BENCHMARK(BM_EncodeTextEscaped);
BENCHMARK(BM_EncodeData);

template <typename T>
void decodePrimitive(benchmark::State& state, kj::StringPtr input) {
  JsonCodec codec;
  for (auto _ : state) {
    auto value = codec.decode<T>(input);
    benchmark::DoNotOptimize(value);
  }
  accountBytes(state, input.size());
}

static void BM_DecodeBool(benchmark::State& s) { decodePrimitive<bool>(s, "true"); }
static void BM_DecodeInt32(benchmark::State& s) { decodePrimitive<int32_t>(s, "-123456789"); }
static void BM_DecodeInt64(benchmark::State& s) { decodePrimitive<int64_t>(s, "\"-1234567890123456\""); }
static void BM_DecodeUInt64(benchmark::State& s) { decodePrimitive<uint64_t>(s, "\"12345678901234567890\""); }
static void BM_DecodeDouble(benchmark::State& s) { decodePrimitive<double>(s, "-1.23456789012345e200"); }
static void BM_DecodeSpecialDouble(benchmark::State& s) { decodePrimitive<double>(s, "\"NaN\""); }
static void BM_DecodeEnum(benchmark::State& s) { decodePrimitive<Color>(s, "\"ultraviolet\""); }
BENCHMARK(BM_DecodeBool);
BENCHMARK(BM_DecodeInt32);
BENCHMARK(BM_DecodeInt64);
BENCHMARK(BM_DecodeUInt64);
BENCHMARK(BM_DecodeDouble);
BENCHMARK(BM_DecodeSpecialDouble);
BENCHMARK(BM_DecodeEnum);

static void BM_DecodeText(benchmark::State& state) {
  const kj::StringPtr input = "\"quote=\\\" slash=\\\\ unicode=\\u2603\"";
  JsonCodec codec;
  for (auto _ : state) {
    MallocMessageBuilder message;
    auto value = codec.decode<Text>(input, message.getOrphanage());
    benchmark::DoNotOptimize(value.getReader().size());
  }
  accountBytes(state, input.size());
}

static void BM_EncodeTopLevelList(benchmark::State& state) {
  MallocMessageBuilder message;
  auto list = message.initRoot<List<int32_t>>(16);
  for (uint i = 0; i < list.size(); ++i) list.set(i, static_cast<int32_t>(i) * 1000 - 7000);
  JsonCodec codec;
  size_t bytes = 0;
  for (auto _ : state) {
    auto output = codec.encode(list.asReader());
    bytes = output.size();
    benchmark::DoNotOptimize(output.begin());
  }
  accountBytes(state, bytes);
}

static void BM_DecodeTopLevelList(benchmark::State& state) {
  const kj::StringPtr input = "[-7000,-6000,-5000,-4000,-3000,-2000,-1000,0,1000,2000,3000,4000,5000,6000,7000,8000]";
  JsonCodec codec;
  for (auto _ : state) {
    MallocMessageBuilder message;
    auto value = codec.decode<List<int32_t>>(input, message.getOrphanage());
    benchmark::DoNotOptimize(value.getReader().size());
  }
  accountBytes(state, input.size());
}
BENCHMARK(BM_DecodeText);
BENCHMARK(BM_EncodeTopLevelList);
BENCHMARK(BM_DecodeTopLevelList);

void rawDecode(benchmark::State& state, kj::ArrayPtr<const char> input) {
  JsonCodec codec;
  for (auto _ : state) {
    MallocMessageBuilder message;
    codec.decodeRaw(input, message.initRoot<JsonValue>());
    benchmark::DoNotOptimize(message.getRoot<JsonValue>().which());
  }
  accountBytes(state, input.size());
}

template <size_t size>
void rawDecode(benchmark::State& state, const char (&input)[size]) {
  rawDecode(state, kj::arrayPtr(input, size - 1));
}

static void BM_RawDecodeNull(benchmark::State& s) { rawDecode(s, "null"); }
static void BM_RawDecodeString(benchmark::State& s) { rawDecode(s, "\"a short plain string\""); }
static void BM_RawDecodeEscapedString(benchmark::State& s) {
  rawDecode(s, "\"quote=\\\" slash=\\\\ controls=\\n\\r\\t unicode=\\u2603\"");
}
static void BM_RawDecodeArray(benchmark::State& s) {
  rawDecode(s, "[null,true,false,123,-4.5e20,\"text\",[1,2,3]]");
}
static void BM_RawDecodeObject(benchmark::State& s) {
  rawDecode(s, "{\"id\":123,\"ok\":true,\"name\":\"small\",\"items\":[1,2,3],\"child\":{\"x\":null}}");
}
BENCHMARK(BM_RawDecodeNull);
BENCHMARK(BM_RawDecodeString);
BENCHMARK(BM_RawDecodeEscapedString);
BENCHMARK(BM_RawDecodeArray);
BENCHMARK(BM_RawDecodeObject);

void rawEncode(benchmark::State& state, kj::StringPtr input, bool pretty = false) {
  JsonCodec codec;
  codec.setPrettyPrint(pretty);
  MallocMessageBuilder message;
  codec.decodeRaw(input, message.initRoot<JsonValue>());
  auto value = message.getRoot<JsonValue>();
  size_t bytes = 0;
  for (auto _ : state) {
    auto output = codec.encodeRaw(value);
    bytes = output.size();
    benchmark::DoNotOptimize(output.begin());
  }
  accountBytes(state, bytes);
}

static void BM_RawEncodeArray(benchmark::State& s) {
  rawEncode(s, "[null,true,false,123,-4.5e20,\"text\",[1,2,3]]");
}
static void BM_RawEncodeObject(benchmark::State& s) {
  rawEncode(s, "{\"id\":123,\"ok\":true,\"name\":\"small\",\"items\":[1,2,3],\"child\":{\"x\":null}}");
}
static void BM_RawEncodeObjectPretty(benchmark::State& s) {
  rawEncode(s, "{\"id\":123,\"ok\":true,\"name\":\"small\",\"items\":[1,2,3],\"child\":{\"x\":null}}", true);
}
BENCHMARK(BM_RawEncodeArray);
BENCHMARK(BM_RawEncodeObject);
BENCHMARK(BM_RawEncodeObjectPretty);

template <typename T, typename Initializer>
void typedEncode(benchmark::State& state, Initializer&& initialize, bool pretty = false,
                 HasMode hasMode = HasMode::NON_NULL) {
  MallocMessageBuilder message;
  auto root = message.initRoot<T>();
  initialize(root);
  JsonCodec codec;
  codec.setPrettyPrint(pretty);
  codec.setHasMode(hasMode);
  size_t bytes = 0;
  for (auto _ : state) {
    auto output = codec.encode(root.asReader());
    bytes = output.size();
    benchmark::DoNotOptimize(output.begin());
  }
  accountBytes(state, bytes);
}

template <typename T>
void typedDecode(benchmark::State& state, kj::StringPtr input, bool annotated = false) {
  JsonCodec codec;
  if (annotated) codec.handleByAnnotation<T>();
  for (auto _ : state) {
    MallocMessageBuilder message;
    codec.decode(input, message.initRoot<T>());
    benchmark::DoNotOptimize(message.getRoot<T>().totalSize().wordCount);
  }
  accountBytes(state, input.size());
}

static void BM_EncodeSmall(benchmark::State& s) { typedEncode<Small>(s, [](auto v) { initSmall(v); }); }
static void BM_DecodeSmall(benchmark::State& s) {
  typedDecode<Small>(s, "{\"id\":123,\"enabled\":true,\"ratio\":246.25,\"name\":\"a small object\",\"color\":\"ultraviolet\"}");
}
static void BM_DynamicDecodeSmall(benchmark::State& state) {
  const kj::StringPtr INPUT = "{\"id\":123,\"enabled\":true,\"ratio\":246.25,\"name\":\"a small object\",\"color\":\"ultraviolet\"}";
  JsonCodec codec;
  auto schema = StructSchema::from<Small>();
  for (auto _ : state) {
    MallocMessageBuilder message;
    auto value = codec.decode(INPUT, schema, message.getOrphanage());
    benchmark::DoNotOptimize(value.getReader().totalSize().wordCount);
  }
  accountBytes(state, INPUT.size());
}
static void BM_EncodeEverything(benchmark::State& s) {
  typedEncode<Everything>(s, [](auto v) { initEverything(v); });
}
static void BM_EncodeEverythingPretty(benchmark::State& s) {
  typedEncode<Everything>(s, [](auto v) { initEverything(v); }, true);
}
static void BM_EncodeEverythingNonDefault(benchmark::State& s) {
  typedEncode<Everything>(s, [](auto v) { initEverything(v); }, false, HasMode::NON_DEFAULT);
}
static void BM_DecodeEverything(benchmark::State& state) {
  MallocMessageBuilder source;
  initEverything(source.initRoot<Everything>());
  JsonCodec codec;
  auto input = codec.encode(source.getRoot<Everything>());
  typedDecode<Everything>(state, input);
}
BENCHMARK(BM_EncodeSmall);
BENCHMARK(BM_DecodeSmall);
BENCHMARK(BM_DynamicDecodeSmall);
BENCHMARK(BM_EncodeEverything);
BENCHMARK(BM_EncodeEverythingPretty);
BENCHMARK(BM_EncodeEverythingNonDefault);
BENCHMARK(BM_DecodeEverything);

static void BM_EncodeAnnotated(benchmark::State& state) {
  MallocMessageBuilder message;
  initAnnotated(message.initRoot<Annotated>());
  auto root = message.getRoot<Annotated>();
  JsonCodec codec;
  codec.handleByAnnotation<Annotated>();
  size_t bytes = 0;
  for (auto _ : state) {
    auto output = codec.encode(root);
    bytes = output.size();
    benchmark::DoNotOptimize(output.begin());
  }
  accountBytes(state, bytes);
}
static void BM_DecodeAnnotated(benchmark::State& state) {
  MallocMessageBuilder source;
  initAnnotated(source.initRoot<Annotated>());
  JsonCodec codec;
  codec.handleByAnnotation<Annotated>();
  auto input = codec.encode(source.getRoot<Annotated>());
  for (auto _ : state) {
    MallocMessageBuilder message;
    codec.decode(input, message.initRoot<Annotated>());
    benchmark::DoNotOptimize(message.getRoot<Annotated>().totalSize().wordCount);
  }
  accountBytes(state, input.size());
}
BENCHMARK(BM_EncodeAnnotated);
BENCHMARK(BM_DecodeAnnotated);

std::string makeLargeRawJson(uint count, bool repetitive) {
  std::string result;
  result.reserve(static_cast<size_t>(count) * 150);
  result += "{\"records\":[";
  for (uint i = 0; i < count; ++i) {
    if (i != 0) result += ',';
    if (repetitive) {
      result += "{\"id\":\"123456789012345\",\"active\":true,\"score\":17.25,"
                "\"name\":\"the same moderately long record name repeated many times\","
                "\"tags\":[\"repeated\",\"json\",\"repeated\"]}";
    } else {
      result += "{\"id\":\"" + std::to_string(123456789012345ull + i) +
          "\",\"active\":" + (i % 3 == 0 ? std::string("true") : std::string("false")) +
          ",\"score\":" + std::to_string(i * 0.125 - 100.0) +
          ",\"name\":\"record-" + std::to_string(i) +
          "\",\"tags\":[\"generated\",\"json\",\"bucket-" + std::to_string(i % 97) + "\"]}";
    }
  }
  result += "]}";
  return result;
}

void largeRawDecode(benchmark::State& state, bool repetitive) {
  auto storage = makeLargeRawJson(static_cast<uint>(state.range(0)), repetitive);
  kj::ArrayPtr<const char> input(storage.data(), storage.size());
  JsonCodec codec;
  for (auto _ : state) {
    MallocMessageBuilder message;
    codec.decodeRaw(input, message.initRoot<JsonValue>());
    benchmark::DoNotOptimize(message.getRoot<JsonValue>().getObject().size());
  }
  accountBytes(state, input.size());
}
static void BM_RawDecodeLargeVaried(benchmark::State& s) { largeRawDecode(s, false); }
static void BM_RawDecodeLargeRepetitive(benchmark::State& s) { largeRawDecode(s, true); }
BENCHMARK(BM_RawDecodeLargeVaried)->Arg(256)->Arg(4096);
BENCHMARK(BM_RawDecodeLargeRepetitive)->Arg(256)->Arg(4096);

void largeRawEncode(benchmark::State& state, bool repetitive) {
  auto storage = makeLargeRawJson(static_cast<uint>(state.range(0)), repetitive);
  kj::ArrayPtr<const char> input(storage.data(), storage.size());
  JsonCodec codec;
  MallocMessageBuilder message;
  codec.decodeRaw(input, message.initRoot<JsonValue>());
  auto value = message.getRoot<JsonValue>();
  size_t bytes = 0;
  for (auto _ : state) {
    auto output = codec.encodeRaw(value);
    bytes = output.size();
    benchmark::DoNotOptimize(output.begin());
  }
  accountBytes(state, bytes);
}
static void BM_RawEncodeLargeVaried(benchmark::State& s) { largeRawEncode(s, false); }
static void BM_RawEncodeLargeRepetitive(benchmark::State& s) { largeRawEncode(s, true); }
BENCHMARK(BM_RawEncodeLargeVaried)->Arg(256)->Arg(4096);
BENCHMARK(BM_RawEncodeLargeRepetitive)->Arg(256)->Arg(4096);

static void BM_RawDecodeLargePlainString(benchmark::State& state) {
  std::string storage = "{\"payload\":\"";
  storage.append(static_cast<size_t>(state.range(0)), 'a');
  storage += "\"}";
  rawDecode(state, kj::ArrayPtr<const char>(storage.data(), storage.size()));
}
static void BM_RawDecodeLargeEscapedString(benchmark::State& state) {
  std::string storage = "{\"payload\":\"";
  storage.reserve(static_cast<size_t>(state.range(0)) * 2 + 16);
  for (int64_t i = 0; i < state.range(0); ++i) storage += "\\n";
  storage += "\"}";
  rawDecode(state, kj::ArrayPtr<const char>(storage.data(), storage.size()));
}
BENCHMARK(BM_RawDecodeLargePlainString)->Arg(64 * 1024)->Arg(1024 * 1024);
BENCHMARK(BM_RawDecodeLargeEscapedString)->Arg(32 * 1024)->Arg(512 * 1024);

void typedDocumentEncode(benchmark::State& state, bool repetitive) {
  MallocMessageBuilder message;
  initDocument(message.initRoot<Document>(), static_cast<uint>(state.range(0)), repetitive);
  auto root = message.getRoot<Document>();
  JsonCodec codec;
  size_t bytes = 0;
  for (auto _ : state) {
    auto output = codec.encode(root);
    bytes = output.size();
    benchmark::DoNotOptimize(output.begin());
  }
  accountBytes(state, bytes);
}
void typedDocumentDecode(benchmark::State& state, bool repetitive) {
  MallocMessageBuilder source;
  initDocument(source.initRoot<Document>(), static_cast<uint>(state.range(0)), repetitive);
  JsonCodec codec;
  auto input = codec.encode(source.getRoot<Document>());
  for (auto _ : state) {
    MallocMessageBuilder message;
    codec.decode(input, message.initRoot<Document>());
    benchmark::DoNotOptimize(message.getRoot<Document>().getRecords().size());
  }
  accountBytes(state, input.size());
}
static void BM_EncodeLargeTypedVaried(benchmark::State& s) { typedDocumentEncode(s, false); }
static void BM_EncodeLargeTypedRepetitive(benchmark::State& s) { typedDocumentEncode(s, true); }
static void BM_DecodeLargeTypedVaried(benchmark::State& s) { typedDocumentDecode(s, false); }
static void BM_DecodeLargeTypedRepetitive(benchmark::State& s) { typedDocumentDecode(s, true); }
BENCHMARK(BM_EncodeLargeTypedVaried)->Arg(256)->Arg(4096);
BENCHMARK(BM_EncodeLargeTypedRepetitive)->Arg(256)->Arg(4096);
BENCHMARK(BM_DecodeLargeTypedVaried)->Arg(256)->Arg(4096);
BENCHMARK(BM_DecodeLargeTypedRepetitive)->Arg(256)->Arg(4096);

}  // namespace
}  // namespace capnp

BENCHMARK_MAIN();
