// Generated code, DO NOT EDIT

#ifndef CAPNP_INCLUDED_1ad763faa64eb47f39f6241af24b4471
#define CAPNP_INCLUDED_1ad763faa64eb47f39f6241af24b4471

#include <capnp/generated-header-support.h>

namespace capnp {
namespace compiler {


struct LocatedText {
  LocatedText() = delete;

  class Reader;
  class Builder;

private:
};

struct LocatedInteger {
  LocatedInteger() = delete;

  class Reader;
  class Builder;

private:
};

struct LocatedFloat {
  LocatedFloat() = delete;

  class Reader;
  class Builder;

private:
};

struct DeclName {
  DeclName() = delete;

  class Reader;
  class Builder;
  struct Base;

private:
};

struct DeclName::Base {
  Base() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    ABSOLUTE_NAME = 0,
    RELATIVE_NAME = 1,
    IMPORT_NAME = 2,
  };

private:
};

struct TypeExpression {
  TypeExpression() = delete;

  class Reader;
  class Builder;

private:
};

struct ValueExpression {
  ValueExpression() = delete;

  class Reader;
  class Builder;
  struct FieldAssignment;
  struct Body;

private:
};

struct ValueExpression::Body {
  Body() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    UNKNOWN = 0,
    POSITIVE_INT = 1,
    NEGATIVE_INT = 2,
    FLOAT = 3,
    STRING = 4,
    NAME = 5,
    LIST = 6,
    STRUCT_VALUE = 7,
    UNION_VALUE = 8,
  };

private:
};

struct ValueExpression::FieldAssignment {
  FieldAssignment() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration {
  Declaration() = delete;

  class Reader;
  class Builder;
  struct AnnotationApplication;
  struct File;
  struct Using;
  struct Const;
  struct Enum;
  struct Enumerant;
  struct Struct;
  struct Field;
  struct Union;
  struct Group;
  struct Interface;
  struct Method;
  struct Annotation;
  struct Id;
  struct Body;

private:
};

struct Declaration::Id {
  Id() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    UNSPECIFIED = 0,
    UID = 1,
    ORDINAL = 2,
  };

private:
};

struct Declaration::AnnotationApplication {
  AnnotationApplication() = delete;

  class Reader;
  class Builder;
  struct Value;

private:
};

struct Declaration::AnnotationApplication::Value {
  Value() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    NONE = 0,
    EXPRESSION = 1,
  };

private:
};

struct Declaration::Body {
  Body() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    FILE_DECL = 13,
    USING_DECL = 0,
    CONST_DECL = 1,
    ENUM_DECL = 2,
    ENUMERANT_DECL = 3,
    STRUCT_DECL = 4,
    FIELD_DECL = 5,
    UNION_DECL = 6,
    GROUP_DECL = 12,
    INTERFACE_DECL = 7,
    METHOD_DECL = 8,
    ANNOTATION_DECL = 9,
    NAKED_ID = 10,
    NAKED_ANNOTATION = 11,
    BUILTIN_VOID = 14,
    BUILTIN_BOOL = 15,
    BUILTIN_INT8 = 16,
    BUILTIN_INT16 = 17,
    BUILTIN_INT32 = 18,
    BUILTIN_INT64 = 19,
    BUILTIN_U_INT8 = 20,
    BUILTIN_U_INT16 = 21,
    BUILTIN_U_INT32 = 22,
    BUILTIN_U_INT64 = 23,
    BUILTIN_FLOAT32 = 24,
    BUILTIN_FLOAT64 = 25,
    BUILTIN_TEXT = 26,
    BUILTIN_DATA = 27,
    BUILTIN_LIST = 28,
    BUILTIN_OBJECT = 29,
  };

private:
};

struct Declaration::File {
  File() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Using {
  Using() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Const {
  Const() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Enum {
  Enum() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Enumerant {
  Enumerant() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Struct {
  Struct() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Field {
  Field() = delete;

  class Reader;
  class Builder;
  struct DefaultValue;

private:
};

struct Declaration::Field::DefaultValue {
  DefaultValue() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    NONE = 0,
    VALUE = 1,
  };

private:
};

struct Declaration::Union {
  Union() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Group {
  Group() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Interface {
  Interface() = delete;

  class Reader;
  class Builder;

private:
};

struct Declaration::Method {
  Method() = delete;

  class Reader;
  class Builder;
  struct Param;
  struct ReturnType;

private:
};

struct Declaration::Method::Param {
  Param() = delete;

  class Reader;
  class Builder;
  struct DefaultValue;

private:
};

struct Declaration::Method::Param::DefaultValue {
  DefaultValue() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    NONE = 0,
    VALUE = 1,
  };

private:
};

struct Declaration::Method::ReturnType {
  ReturnType() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    NONE = 0,
    EXPRESSION = 1,
  };

private:
};

struct Declaration::Annotation {
  Annotation() = delete;

  class Reader;
  class Builder;

private:
};

struct ParsedFile {
  ParsedFile() = delete;

  class Reader;
  class Builder;

private:
};



}  // namespace
}  // namespace

namespace capnp {
namespace schemas {
extern const ::capnp::_::RawSchema s_e75816b56529d464;
extern const ::capnp::_::RawSchema s_991c7a3693d62cf2;
extern const ::capnp::_::RawSchema s_90f2a60678fd2367;
extern const ::capnp::_::RawSchema s_ce5c2afd239fe34e;
extern const ::capnp::_::RawSchema s_8751968764a2e298;
extern const ::capnp::_::RawSchema s_9ca8b2acb16fc545;
extern const ::capnp::_::RawSchema s_b6b57cf8b27fba0e;
extern const ::capnp::_::RawSchema s_96efe787c17e83bb;
extern const ::capnp::_::RawSchema s_d00489d473826290;
extern const ::capnp::_::RawSchema s_8c0f454d10e0ee3d;
extern const ::capnp::_::RawSchema s_bca73158d62fc3fc;
extern const ::capnp::_::RawSchema s_f5e78dbd46b5e566;
extern const ::capnp::_::RawSchema s_e198451eb078456c;
extern const ::capnp::_::RawSchema s_d7e03a99254174e7;
extern const ::capnp::_::RawSchema s_fa92f59a8e6964e3;
extern const ::capnp::_::RawSchema s_bd636be80952066d;
extern const ::capnp::_::RawSchema s_a237aa9b0c8da6fe;
extern const ::capnp::_::RawSchema s_b0aaba24204ed5f5;
extern const ::capnp::_::RawSchema s_834c86b267d88b47;
extern const ::capnp::_::RawSchema s_85421cec78943d5b;
extern const ::capnp::_::RawSchema s_ad319ea1c001f0b1;
extern const ::capnp::_::RawSchema s_be9476246e7b4d7f;
extern const ::capnp::_::RawSchema s_84e4f3f5a807605c;
}  // namespace schemas
namespace _ {  // private
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::LocatedText, e75816b56529d464,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::LocatedInteger, 991c7a3693d62cf2,
    2, 0, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::LocatedFloat, 90f2a60678fd2367,
    2, 0, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::DeclName, ce5c2afd239fe34e,
    2, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::DeclName::Base,
    ::capnp::compiler::DeclName, 0);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::TypeExpression, 8751968764a2e298,
    1, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::ValueExpression, 9ca8b2acb16fc545,
    3, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::ValueExpression::Body,
    ::capnp::compiler::ValueExpression, 0);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::ValueExpression::FieldAssignment, b6b57cf8b27fba0e,
    0, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration, 96efe787c17e83bb,
    2, 6, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::Declaration::Id,
    ::capnp::compiler::Declaration, 1);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::Declaration::Body,
    ::capnp::compiler::Declaration, 3);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::AnnotationApplication, d00489d473826290,
    1, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::Declaration::AnnotationApplication::Value,
    ::capnp::compiler::Declaration::AnnotationApplication, 1);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::File, 8c0f454d10e0ee3d,
    0, 0, VOID);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Using, bca73158d62fc3fc,
    0, 1, POINTER);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Const, f5e78dbd46b5e566,
    0, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Enum, e198451eb078456c,
    0, 0, VOID);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Enumerant, d7e03a99254174e7,
    0, 0, VOID);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Struct, fa92f59a8e6964e3,
    0, 0, VOID);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Field, bd636be80952066d,
    1, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::Declaration::Field::DefaultValue,
    ::capnp::compiler::Declaration::Field, 1);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Union, a237aa9b0c8da6fe,
    0, 0, VOID);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Group, b0aaba24204ed5f5,
    0, 0, VOID);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Interface, 834c86b267d88b47,
    0, 0, VOID);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Method, 85421cec78943d5b,
    1, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::Declaration::Method::ReturnType,
    ::capnp::compiler::Declaration::Method, 1);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Method::Param, ad319ea1c001f0b1,
    1, 4, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::compiler::Declaration::Method::Param::DefaultValue,
    ::capnp::compiler::Declaration::Method::Param, 3);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::Declaration::Annotation, be9476246e7b4d7f,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::compiler::ParsedFile, 84e4f3f5a807605c,
    0, 1, POINTER);
}  // namespace capnp
}  // namespace _ (private)

namespace capnp {
namespace compiler {



class LocatedText::Reader {
public:
  typedef LocatedText Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // value@0: Text;  # ptr[0]
  inline bool hasValue() const;
  inline  ::capnp::Text::Reader getValue() const;

  // startByte@1: UInt32;  # bits[0, 32)
  inline  ::uint32_t getStartByte() const;

  // endByte@2: UInt32;  # bits[32, 64)
  inline  ::uint32_t getEndByte() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(LocatedText::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(LocatedText::Reader reader) {
  return ::capnp::_::structString<LocatedText>(reader._reader);
}



class LocatedText::Builder {
public:
  typedef LocatedText Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // value@0: Text;  # ptr[0]
  inline bool hasValue();
  inline  ::capnp::Text::Builder getValue();
  inline void setValue( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initValue(unsigned int size);
  inline void adoptValue(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownValue();

  // startByte@1: UInt32;  # bits[0, 32)
  inline  ::uint32_t getStartByte();
  inline void setStartByte( ::uint32_t value);

  // endByte@2: UInt32;  # bits[32, 64)
  inline  ::uint32_t getEndByte();
  inline void setEndByte( ::uint32_t value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(LocatedText::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(LocatedText::Builder builder) {
  return ::capnp::_::structString<LocatedText>(builder._builder.asReader());
}

class LocatedInteger::Reader {
public:
  typedef LocatedInteger Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // value@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getValue() const;

  // startByte@1: UInt32;  # bits[64, 96)
  inline  ::uint32_t getStartByte() const;

  // endByte@2: UInt32;  # bits[96, 128)
  inline  ::uint32_t getEndByte() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(LocatedInteger::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(LocatedInteger::Reader reader) {
  return ::capnp::_::structString<LocatedInteger>(reader._reader);
}



class LocatedInteger::Builder {
public:
  typedef LocatedInteger Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // value@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getValue();
  inline void setValue( ::uint64_t value);

  // startByte@1: UInt32;  # bits[64, 96)
  inline  ::uint32_t getStartByte();
  inline void setStartByte( ::uint32_t value);

  // endByte@2: UInt32;  # bits[96, 128)
  inline  ::uint32_t getEndByte();
  inline void setEndByte( ::uint32_t value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(LocatedInteger::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(LocatedInteger::Builder builder) {
  return ::capnp::_::structString<LocatedInteger>(builder._builder.asReader());
}

class LocatedFloat::Reader {
public:
  typedef LocatedFloat Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // value@0: Float64;  # bits[0, 64)
  inline double getValue() const;

  // startByte@1: UInt32;  # bits[64, 96)
  inline  ::uint32_t getStartByte() const;

  // endByte@2: UInt32;  # bits[96, 128)
  inline  ::uint32_t getEndByte() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(LocatedFloat::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(LocatedFloat::Reader reader) {
  return ::capnp::_::structString<LocatedFloat>(reader._reader);
}



class LocatedFloat::Builder {
public:
  typedef LocatedFloat Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // value@0: Float64;  # bits[0, 64)
  inline double getValue();
  inline void setValue(double value);

  // startByte@1: UInt32;  # bits[64, 96)
  inline  ::uint32_t getStartByte();
  inline void setStartByte( ::uint32_t value);

  // endByte@2: UInt32;  # bits[96, 128)
  inline  ::uint32_t getEndByte();
  inline void setEndByte( ::uint32_t value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(LocatedFloat::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(LocatedFloat::Builder builder) {
  return ::capnp::_::structString<LocatedFloat>(builder._builder.asReader());
}

class DeclName::Reader {
public:
  typedef DeclName Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union base@0 {  # [0, 16)
  inline Base::Reader getBase() const;

  // memberPath@4: List(.LocatedText);  # ptr[1]
  inline bool hasMemberPath() const;
  inline  ::capnp::List< ::capnp::compiler::LocatedText>::Reader getMemberPath() const;

  // startByte@5: UInt32;  # bits[32, 64)
  inline  ::uint32_t getStartByte() const;

  // endByte@6: UInt32;  # bits[64, 96)
  inline  ::uint32_t getEndByte() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(DeclName::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(DeclName::Reader reader) {
  return ::capnp::_::structString<DeclName>(reader._reader);
}



class DeclName::Builder {
public:
  typedef DeclName Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union base@0 {  # [0, 16)
  inline Base::Builder getBase();

  // memberPath@4: List(.LocatedText);  # ptr[1]
  inline bool hasMemberPath();
  inline  ::capnp::List< ::capnp::compiler::LocatedText>::Builder getMemberPath();
  inline void setMemberPath( ::capnp::List< ::capnp::compiler::LocatedText>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::LocatedText>::Builder initMemberPath(unsigned int size);
  inline void adoptMemberPath(::capnp::Orphan< ::capnp::List< ::capnp::compiler::LocatedText>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::LocatedText>> disownMemberPath();

  // startByte@5: UInt32;  # bits[32, 64)
  inline  ::uint32_t getStartByte();
  inline void setStartByte( ::uint32_t value);

  // endByte@6: UInt32;  # bits[64, 96)
  inline  ::uint32_t getEndByte();
  inline void setEndByte( ::uint32_t value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(DeclName::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(DeclName::Builder builder) {
  return ::capnp::_::structString<DeclName>(builder._builder.asReader());
}

class DeclName::Base::Reader {
public:
  typedef Base Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // absoluteName@1: .LocatedText;  # ptr[0], union tag = 0
  inline bool hasAbsoluteName() const;
  inline  ::capnp::compiler::LocatedText::Reader getAbsoluteName() const;

  // relativeName@2: .LocatedText;  # ptr[0], union tag = 1
  inline bool hasRelativeName() const;
  inline  ::capnp::compiler::LocatedText::Reader getRelativeName() const;

  // importName@3: .LocatedText;  # ptr[0], union tag = 2
  inline bool hasImportName() const;
  inline  ::capnp::compiler::LocatedText::Reader getImportName() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(DeclName::Base::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(DeclName::Base::Reader reader) {
  return ::capnp::_::unionString<DeclName::Base>(reader._reader);
}



class DeclName::Base::Builder {
public:
  typedef Base Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // absoluteName@1: .LocatedText;  # ptr[0], union tag = 0
  inline bool hasAbsoluteName();
  inline  ::capnp::compiler::LocatedText::Builder getAbsoluteName();
  inline void setAbsoluteName( ::capnp::compiler::LocatedText::Reader other);
  inline  ::capnp::compiler::LocatedText::Builder initAbsoluteName();
  inline void adoptAbsoluteName(::capnp::Orphan< ::capnp::compiler::LocatedText>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedText> disownAbsoluteName();

  // relativeName@2: .LocatedText;  # ptr[0], union tag = 1
  inline bool hasRelativeName();
  inline  ::capnp::compiler::LocatedText::Builder getRelativeName();
  inline void setRelativeName( ::capnp::compiler::LocatedText::Reader other);
  inline  ::capnp::compiler::LocatedText::Builder initRelativeName();
  inline void adoptRelativeName(::capnp::Orphan< ::capnp::compiler::LocatedText>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedText> disownRelativeName();

  // importName@3: .LocatedText;  # ptr[0], union tag = 2
  inline bool hasImportName();
  inline  ::capnp::compiler::LocatedText::Builder getImportName();
  inline void setImportName( ::capnp::compiler::LocatedText::Reader other);
  inline  ::capnp::compiler::LocatedText::Builder initImportName();
  inline void adoptImportName(::capnp::Orphan< ::capnp::compiler::LocatedText>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedText> disownImportName();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(DeclName::Base::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(DeclName::Base::Builder builder) {
  return ::capnp::_::unionString<DeclName::Base>(builder._builder.asReader());
}

class TypeExpression::Reader {
public:
  typedef TypeExpression Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // name@0: .DeclName;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::compiler::DeclName::Reader getName() const;

  // params@1: List(.TypeExpression);  # ptr[1]
  inline bool hasParams() const;
  inline  ::capnp::List< ::capnp::compiler::TypeExpression>::Reader getParams() const;

  // startByte@2: UInt32;  # bits[0, 32)
  inline  ::uint32_t getStartByte() const;

  // endByte@3: UInt32;  # bits[32, 64)
  inline  ::uint32_t getEndByte() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(TypeExpression::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(TypeExpression::Reader reader) {
  return ::capnp::_::structString<TypeExpression>(reader._reader);
}



class TypeExpression::Builder {
public:
  typedef TypeExpression Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // name@0: .DeclName;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::compiler::DeclName::Builder getName();
  inline void setName( ::capnp::compiler::DeclName::Reader other);
  inline  ::capnp::compiler::DeclName::Builder initName();
  inline void adoptName(::capnp::Orphan< ::capnp::compiler::DeclName>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::DeclName> disownName();

  // params@1: List(.TypeExpression);  # ptr[1]
  inline bool hasParams();
  inline  ::capnp::List< ::capnp::compiler::TypeExpression>::Builder getParams();
  inline void setParams( ::capnp::List< ::capnp::compiler::TypeExpression>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::TypeExpression>::Builder initParams(unsigned int size);
  inline void adoptParams(::capnp::Orphan< ::capnp::List< ::capnp::compiler::TypeExpression>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::TypeExpression>> disownParams();

  // startByte@2: UInt32;  # bits[0, 32)
  inline  ::uint32_t getStartByte();
  inline void setStartByte( ::uint32_t value);

  // endByte@3: UInt32;  # bits[32, 64)
  inline  ::uint32_t getEndByte();
  inline void setEndByte( ::uint32_t value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(TypeExpression::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(TypeExpression::Builder builder) {
  return ::capnp::_::structString<TypeExpression>(builder._builder.asReader());
}

class ValueExpression::Reader {
public:
  typedef ValueExpression Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union body@0 {  # [0, 16)
  inline Body::Reader getBody() const;

  // startByte@10: UInt32;  # bits[32, 64)
  inline  ::uint32_t getStartByte() const;

  // endByte@11: UInt32;  # bits[128, 160)
  inline  ::uint32_t getEndByte() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ValueExpression::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(ValueExpression::Reader reader) {
  return ::capnp::_::structString<ValueExpression>(reader._reader);
}



class ValueExpression::Builder {
public:
  typedef ValueExpression Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union body@0 {  # [0, 16)
  inline Body::Builder getBody();

  // startByte@10: UInt32;  # bits[32, 64)
  inline  ::uint32_t getStartByte();
  inline void setStartByte( ::uint32_t value);

  // endByte@11: UInt32;  # bits[128, 160)
  inline  ::uint32_t getEndByte();
  inline void setEndByte( ::uint32_t value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ValueExpression::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(ValueExpression::Builder builder) {
  return ::capnp::_::structString<ValueExpression>(builder._builder.asReader());
}

class ValueExpression::Body::Reader {
public:
  typedef Body Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // unknown@1: Void;  # (none), union tag = 0
  inline  ::capnp::Void getUnknown() const;

  // positiveInt@2: UInt64;  # bits[64, 128), union tag = 1
  inline  ::uint64_t getPositiveInt() const;

  // negativeInt@3: UInt64;  # bits[64, 128), union tag = 2
  inline  ::uint64_t getNegativeInt() const;

  // float@4: Float64;  # bits[64, 128), union tag = 3
  inline double getFloat() const;

  // string@5: Text;  # ptr[0], union tag = 4
  inline bool hasString() const;
  inline  ::capnp::Text::Reader getString() const;

  // name@6: .DeclName;  # ptr[0], union tag = 5
  inline bool hasName() const;
  inline  ::capnp::compiler::DeclName::Reader getName() const;

  // list@7: List(.ValueExpression);  # ptr[0], union tag = 6
  inline bool hasList() const;
  inline  ::capnp::List< ::capnp::compiler::ValueExpression>::Reader getList() const;

  // structValue@8: List(.ValueExpression.FieldAssignment);  # ptr[0], union tag = 7
  inline bool hasStructValue() const;
  inline  ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Reader getStructValue() const;

  // unionValue@9: .ValueExpression.FieldAssignment;  # ptr[0], union tag = 8
  inline bool hasUnionValue() const;
  inline  ::capnp::compiler::ValueExpression::FieldAssignment::Reader getUnionValue() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ValueExpression::Body::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(ValueExpression::Body::Reader reader) {
  return ::capnp::_::unionString<ValueExpression::Body>(reader._reader);
}



class ValueExpression::Body::Builder {
public:
  typedef Body Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // unknown@1: Void;  # (none), union tag = 0
  inline  ::capnp::Void getUnknown();
  inline void setUnknown( ::capnp::Void value);

  // positiveInt@2: UInt64;  # bits[64, 128), union tag = 1
  inline  ::uint64_t getPositiveInt();
  inline void setPositiveInt( ::uint64_t value);

  // negativeInt@3: UInt64;  # bits[64, 128), union tag = 2
  inline  ::uint64_t getNegativeInt();
  inline void setNegativeInt( ::uint64_t value);

  // float@4: Float64;  # bits[64, 128), union tag = 3
  inline double getFloat();
  inline void setFloat(double value);

  // string@5: Text;  # ptr[0], union tag = 4
  inline bool hasString();
  inline  ::capnp::Text::Builder getString();
  inline void setString( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initString(unsigned int size);
  inline void adoptString(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownString();

  // name@6: .DeclName;  # ptr[0], union tag = 5
  inline bool hasName();
  inline  ::capnp::compiler::DeclName::Builder getName();
  inline void setName( ::capnp::compiler::DeclName::Reader other);
  inline  ::capnp::compiler::DeclName::Builder initName();
  inline void adoptName(::capnp::Orphan< ::capnp::compiler::DeclName>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::DeclName> disownName();

  // list@7: List(.ValueExpression);  # ptr[0], union tag = 6
  inline bool hasList();
  inline  ::capnp::List< ::capnp::compiler::ValueExpression>::Builder getList();
  inline void setList( ::capnp::List< ::capnp::compiler::ValueExpression>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::ValueExpression>::Builder initList(unsigned int size);
  inline void adoptList(::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression>> disownList();

  // structValue@8: List(.ValueExpression.FieldAssignment);  # ptr[0], union tag = 7
  inline bool hasStructValue();
  inline  ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Builder getStructValue();
  inline void setStructValue( ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Builder initStructValue(unsigned int size);
  inline void adoptStructValue(::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>> disownStructValue();

  // unionValue@9: .ValueExpression.FieldAssignment;  # ptr[0], union tag = 8
  inline bool hasUnionValue();
  inline  ::capnp::compiler::ValueExpression::FieldAssignment::Builder getUnionValue();
  inline void setUnionValue( ::capnp::compiler::ValueExpression::FieldAssignment::Reader other);
  inline  ::capnp::compiler::ValueExpression::FieldAssignment::Builder initUnionValue();
  inline void adoptUnionValue(::capnp::Orphan< ::capnp::compiler::ValueExpression::FieldAssignment>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::ValueExpression::FieldAssignment> disownUnionValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ValueExpression::Body::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(ValueExpression::Body::Builder builder) {
  return ::capnp::_::unionString<ValueExpression::Body>(builder._builder.asReader());
}

class ValueExpression::FieldAssignment::Reader {
public:
  typedef FieldAssignment Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // fieldName@0: .LocatedText;  # ptr[0]
  inline bool hasFieldName() const;
  inline  ::capnp::compiler::LocatedText::Reader getFieldName() const;

  // value@1: .ValueExpression;  # ptr[1]
  inline bool hasValue() const;
  inline  ::capnp::compiler::ValueExpression::Reader getValue() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ValueExpression::FieldAssignment::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(ValueExpression::FieldAssignment::Reader reader) {
  return ::capnp::_::structString<ValueExpression::FieldAssignment>(reader._reader);
}



class ValueExpression::FieldAssignment::Builder {
public:
  typedef FieldAssignment Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // fieldName@0: .LocatedText;  # ptr[0]
  inline bool hasFieldName();
  inline  ::capnp::compiler::LocatedText::Builder getFieldName();
  inline void setFieldName( ::capnp::compiler::LocatedText::Reader other);
  inline  ::capnp::compiler::LocatedText::Builder initFieldName();
  inline void adoptFieldName(::capnp::Orphan< ::capnp::compiler::LocatedText>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedText> disownFieldName();

  // value@1: .ValueExpression;  # ptr[1]
  inline bool hasValue();
  inline  ::capnp::compiler::ValueExpression::Builder getValue();
  inline void setValue( ::capnp::compiler::ValueExpression::Reader other);
  inline  ::capnp::compiler::ValueExpression::Builder initValue();
  inline void adoptValue(::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> disownValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ValueExpression::FieldAssignment::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(ValueExpression::FieldAssignment::Builder builder) {
  return ::capnp::_::structString<ValueExpression::FieldAssignment>(builder._builder.asReader());
}

class Declaration::Reader {
public:
  typedef Declaration Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union id@1 {  # [0, 16)
  inline Id::Reader getId() const;

  // union body@6 {  # [16, 32)
  inline Body::Reader getBody() const;

  // name@0: .LocatedText;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::compiler::LocatedText::Reader getName() const;

  // nestedDecls@17: List(.Declaration);  # ptr[4]
  inline bool hasNestedDecls() const;
  inline  ::capnp::List< ::capnp::compiler::Declaration>::Reader getNestedDecls() const;

  // annotations@5: List(.Declaration.AnnotationApplication);  # ptr[2]
  inline bool hasAnnotations() const;
  inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader getAnnotations() const;

  // startByte@18: UInt32;  # bits[32, 64)
  inline  ::uint32_t getStartByte() const;

  // endByte@19: UInt32;  # bits[64, 96)
  inline  ::uint32_t getEndByte() const;

  // docComment@20: Text;  # ptr[5]
  inline bool hasDocComment() const;
  inline  ::capnp::Text::Reader getDocComment() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Reader reader) {
  return ::capnp::_::structString<Declaration>(reader._reader);
}



class Declaration::Builder {
public:
  typedef Declaration Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union id@1 {  # [0, 16)
  inline Id::Builder getId();

  // union body@6 {  # [16, 32)
  inline Body::Builder getBody();

  // name@0: .LocatedText;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::compiler::LocatedText::Builder getName();
  inline void setName( ::capnp::compiler::LocatedText::Reader other);
  inline  ::capnp::compiler::LocatedText::Builder initName();
  inline void adoptName(::capnp::Orphan< ::capnp::compiler::LocatedText>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedText> disownName();

  // nestedDecls@17: List(.Declaration);  # ptr[4]
  inline bool hasNestedDecls();
  inline  ::capnp::List< ::capnp::compiler::Declaration>::Builder getNestedDecls();
  inline void setNestedDecls( ::capnp::List< ::capnp::compiler::Declaration>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::Declaration>::Builder initNestedDecls(unsigned int size);
  inline void adoptNestedDecls(::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration>> disownNestedDecls();

  // annotations@5: List(.Declaration.AnnotationApplication);  # ptr[2]
  inline bool hasAnnotations();
  inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder getAnnotations();
  inline void setAnnotations( ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder initAnnotations(unsigned int size);
  inline void adoptAnnotations(::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>> disownAnnotations();

  // startByte@18: UInt32;  # bits[32, 64)
  inline  ::uint32_t getStartByte();
  inline void setStartByte( ::uint32_t value);

  // endByte@19: UInt32;  # bits[64, 96)
  inline  ::uint32_t getEndByte();
  inline void setEndByte( ::uint32_t value);

  // docComment@20: Text;  # ptr[5]
  inline bool hasDocComment();
  inline  ::capnp::Text::Builder getDocComment();
  inline void setDocComment( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initDocComment(unsigned int size);
  inline void adoptDocComment(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownDocComment();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Builder builder) {
  return ::capnp::_::structString<Declaration>(builder._builder.asReader());
}

class Declaration::Id::Reader {
public:
  typedef Id Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // unspecified@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getUnspecified() const;

  // uid@3: .LocatedInteger;  # ptr[1], union tag = 1
  inline bool hasUid() const;
  inline  ::capnp::compiler::LocatedInteger::Reader getUid() const;

  // ordinal@4: .LocatedInteger;  # ptr[1], union tag = 2
  inline bool hasOrdinal() const;
  inline  ::capnp::compiler::LocatedInteger::Reader getOrdinal() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Id::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Id::Reader reader) {
  return ::capnp::_::unionString<Declaration::Id>(reader._reader);
}



class Declaration::Id::Builder {
public:
  typedef Id Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // unspecified@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getUnspecified();
  inline void setUnspecified( ::capnp::Void value);

  // uid@3: .LocatedInteger;  # ptr[1], union tag = 1
  inline bool hasUid();
  inline  ::capnp::compiler::LocatedInteger::Builder getUid();
  inline void setUid( ::capnp::compiler::LocatedInteger::Reader other);
  inline  ::capnp::compiler::LocatedInteger::Builder initUid();
  inline void adoptUid(::capnp::Orphan< ::capnp::compiler::LocatedInteger>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedInteger> disownUid();

  // ordinal@4: .LocatedInteger;  # ptr[1], union tag = 2
  inline bool hasOrdinal();
  inline  ::capnp::compiler::LocatedInteger::Builder getOrdinal();
  inline void setOrdinal( ::capnp::compiler::LocatedInteger::Reader other);
  inline  ::capnp::compiler::LocatedInteger::Builder initOrdinal();
  inline void adoptOrdinal(::capnp::Orphan< ::capnp::compiler::LocatedInteger>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedInteger> disownOrdinal();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Id::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Id::Builder builder) {
  return ::capnp::_::unionString<Declaration::Id>(builder._builder.asReader());
}

class Declaration::AnnotationApplication::Reader {
public:
  typedef AnnotationApplication Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union value@1 {  # [0, 16)
  inline Value::Reader getValue() const;

  // name@0: .DeclName;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::compiler::DeclName::Reader getName() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Reader reader) {
  return ::capnp::_::structString<Declaration::AnnotationApplication>(reader._reader);
}



class Declaration::AnnotationApplication::Builder {
public:
  typedef AnnotationApplication Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union value@1 {  # [0, 16)
  inline Value::Builder getValue();

  // name@0: .DeclName;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::compiler::DeclName::Builder getName();
  inline void setName( ::capnp::compiler::DeclName::Reader other);
  inline  ::capnp::compiler::DeclName::Builder initName();
  inline void adoptName(::capnp::Orphan< ::capnp::compiler::DeclName>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::DeclName> disownName();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Builder builder) {
  return ::capnp::_::structString<Declaration::AnnotationApplication>(builder._builder.asReader());
}

class Declaration::AnnotationApplication::Value::Reader {
public:
  typedef Value Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // none@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone() const;

  // expression@3: .ValueExpression;  # ptr[1], union tag = 1
  inline bool hasExpression() const;
  inline  ::capnp::compiler::ValueExpression::Reader getExpression() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Value::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Value::Reader reader) {
  return ::capnp::_::unionString<Declaration::AnnotationApplication::Value>(reader._reader);
}



class Declaration::AnnotationApplication::Value::Builder {
public:
  typedef Value Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // none@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone();
  inline void setNone( ::capnp::Void value);

  // expression@3: .ValueExpression;  # ptr[1], union tag = 1
  inline bool hasExpression();
  inline  ::capnp::compiler::ValueExpression::Builder getExpression();
  inline void setExpression( ::capnp::compiler::ValueExpression::Reader other);
  inline  ::capnp::compiler::ValueExpression::Builder initExpression();
  inline void adoptExpression(::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> disownExpression();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Value::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::AnnotationApplication::Value::Builder builder) {
  return ::capnp::_::unionString<Declaration::AnnotationApplication::Value>(builder._builder.asReader());
}

class Declaration::Body::Reader {
public:
  typedef Body Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // fileDecl@24: .Declaration.File;  # ptr[3], union tag = 13
  inline bool hasFileDecl() const;
  inline  ::capnp::compiler::Declaration::File::Reader getFileDecl() const;

  // usingDecl@7: .Declaration.Using;  # ptr[3], union tag = 0
  inline bool hasUsingDecl() const;
  inline  ::capnp::compiler::Declaration::Using::Reader getUsingDecl() const;

  // constDecl@8: .Declaration.Const;  # ptr[3], union tag = 1
  inline bool hasConstDecl() const;
  inline  ::capnp::compiler::Declaration::Const::Reader getConstDecl() const;

  // enumDecl@9: .Declaration.Enum;  # ptr[3], union tag = 2
  inline bool hasEnumDecl() const;
  inline  ::capnp::compiler::Declaration::Enum::Reader getEnumDecl() const;

  // enumerantDecl@10: .Declaration.Enumerant;  # ptr[3], union tag = 3
  inline bool hasEnumerantDecl() const;
  inline  ::capnp::compiler::Declaration::Enumerant::Reader getEnumerantDecl() const;

  // structDecl@11: .Declaration.Struct;  # ptr[3], union tag = 4
  inline bool hasStructDecl() const;
  inline  ::capnp::compiler::Declaration::Struct::Reader getStructDecl() const;

  // fieldDecl@12: .Declaration.Field;  # ptr[3], union tag = 5
  inline bool hasFieldDecl() const;
  inline  ::capnp::compiler::Declaration::Field::Reader getFieldDecl() const;

  // unionDecl@13: .Declaration.Union;  # ptr[3], union tag = 6
  inline bool hasUnionDecl() const;
  inline  ::capnp::compiler::Declaration::Union::Reader getUnionDecl() const;

  // groupDecl@23: .Declaration.Group;  # ptr[3], union tag = 12
  inline bool hasGroupDecl() const;
  inline  ::capnp::compiler::Declaration::Group::Reader getGroupDecl() const;

  // interfaceDecl@14: .Declaration.Interface;  # ptr[3], union tag = 7
  inline bool hasInterfaceDecl() const;
  inline  ::capnp::compiler::Declaration::Interface::Reader getInterfaceDecl() const;

  // methodDecl@15: .Declaration.Method;  # ptr[3], union tag = 8
  inline bool hasMethodDecl() const;
  inline  ::capnp::compiler::Declaration::Method::Reader getMethodDecl() const;

  // annotationDecl@16: .Declaration.Annotation;  # ptr[3], union tag = 9
  inline bool hasAnnotationDecl() const;
  inline  ::capnp::compiler::Declaration::Annotation::Reader getAnnotationDecl() const;

  // nakedId@21: .LocatedInteger;  # ptr[3], union tag = 10
  inline bool hasNakedId() const;
  inline  ::capnp::compiler::LocatedInteger::Reader getNakedId() const;

  // nakedAnnotation@22: .Declaration.AnnotationApplication;  # ptr[3], union tag = 11
  inline bool hasNakedAnnotation() const;
  inline  ::capnp::compiler::Declaration::AnnotationApplication::Reader getNakedAnnotation() const;

  // builtinVoid@25: Void;  # (none), union tag = 14
  inline  ::capnp::Void getBuiltinVoid() const;

  // builtinBool@26: Void;  # (none), union tag = 15
  inline  ::capnp::Void getBuiltinBool() const;

  // builtinInt8@27: Void;  # (none), union tag = 16
  inline  ::capnp::Void getBuiltinInt8() const;

  // builtinInt16@28: Void;  # (none), union tag = 17
  inline  ::capnp::Void getBuiltinInt16() const;

  // builtinInt32@29: Void;  # (none), union tag = 18
  inline  ::capnp::Void getBuiltinInt32() const;

  // builtinInt64@30: Void;  # (none), union tag = 19
  inline  ::capnp::Void getBuiltinInt64() const;

  // builtinUInt8@31: Void;  # (none), union tag = 20
  inline  ::capnp::Void getBuiltinUInt8() const;

  // builtinUInt16@32: Void;  # (none), union tag = 21
  inline  ::capnp::Void getBuiltinUInt16() const;

  // builtinUInt32@33: Void;  # (none), union tag = 22
  inline  ::capnp::Void getBuiltinUInt32() const;

  // builtinUInt64@34: Void;  # (none), union tag = 23
  inline  ::capnp::Void getBuiltinUInt64() const;

  // builtinFloat32@35: Void;  # (none), union tag = 24
  inline  ::capnp::Void getBuiltinFloat32() const;

  // builtinFloat64@36: Void;  # (none), union tag = 25
  inline  ::capnp::Void getBuiltinFloat64() const;

  // builtinText@37: Void;  # (none), union tag = 26
  inline  ::capnp::Void getBuiltinText() const;

  // builtinData@38: Void;  # (none), union tag = 27
  inline  ::capnp::Void getBuiltinData() const;

  // builtinList@39: Void;  # (none), union tag = 28
  inline  ::capnp::Void getBuiltinList() const;

  // builtinObject@40: Void;  # (none), union tag = 29
  inline  ::capnp::Void getBuiltinObject() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Body::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Body::Reader reader) {
  return ::capnp::_::unionString<Declaration::Body>(reader._reader);
}



class Declaration::Body::Builder {
public:
  typedef Body Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // fileDecl@24: .Declaration.File;  # ptr[3], union tag = 13
  inline bool hasFileDecl();
  inline  ::capnp::compiler::Declaration::File::Builder getFileDecl();
  inline void setFileDecl( ::capnp::compiler::Declaration::File::Reader other);
  inline  ::capnp::compiler::Declaration::File::Builder initFileDecl();
  inline void adoptFileDecl(::capnp::Orphan< ::capnp::compiler::Declaration::File>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::File> disownFileDecl();

  // usingDecl@7: .Declaration.Using;  # ptr[3], union tag = 0
  inline bool hasUsingDecl();
  inline  ::capnp::compiler::Declaration::Using::Builder getUsingDecl();
  inline void setUsingDecl( ::capnp::compiler::Declaration::Using::Reader other);
  inline  ::capnp::compiler::Declaration::Using::Builder initUsingDecl();
  inline void adoptUsingDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Using>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Using> disownUsingDecl();

  // constDecl@8: .Declaration.Const;  # ptr[3], union tag = 1
  inline bool hasConstDecl();
  inline  ::capnp::compiler::Declaration::Const::Builder getConstDecl();
  inline void setConstDecl( ::capnp::compiler::Declaration::Const::Reader other);
  inline  ::capnp::compiler::Declaration::Const::Builder initConstDecl();
  inline void adoptConstDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Const>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Const> disownConstDecl();

  // enumDecl@9: .Declaration.Enum;  # ptr[3], union tag = 2
  inline bool hasEnumDecl();
  inline  ::capnp::compiler::Declaration::Enum::Builder getEnumDecl();
  inline void setEnumDecl( ::capnp::compiler::Declaration::Enum::Reader other);
  inline  ::capnp::compiler::Declaration::Enum::Builder initEnumDecl();
  inline void adoptEnumDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Enum>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Enum> disownEnumDecl();

  // enumerantDecl@10: .Declaration.Enumerant;  # ptr[3], union tag = 3
  inline bool hasEnumerantDecl();
  inline  ::capnp::compiler::Declaration::Enumerant::Builder getEnumerantDecl();
  inline void setEnumerantDecl( ::capnp::compiler::Declaration::Enumerant::Reader other);
  inline  ::capnp::compiler::Declaration::Enumerant::Builder initEnumerantDecl();
  inline void adoptEnumerantDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Enumerant>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Enumerant> disownEnumerantDecl();

  // structDecl@11: .Declaration.Struct;  # ptr[3], union tag = 4
  inline bool hasStructDecl();
  inline  ::capnp::compiler::Declaration::Struct::Builder getStructDecl();
  inline void setStructDecl( ::capnp::compiler::Declaration::Struct::Reader other);
  inline  ::capnp::compiler::Declaration::Struct::Builder initStructDecl();
  inline void adoptStructDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Struct>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Struct> disownStructDecl();

  // fieldDecl@12: .Declaration.Field;  # ptr[3], union tag = 5
  inline bool hasFieldDecl();
  inline  ::capnp::compiler::Declaration::Field::Builder getFieldDecl();
  inline void setFieldDecl( ::capnp::compiler::Declaration::Field::Reader other);
  inline  ::capnp::compiler::Declaration::Field::Builder initFieldDecl();
  inline void adoptFieldDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Field>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Field> disownFieldDecl();

  // unionDecl@13: .Declaration.Union;  # ptr[3], union tag = 6
  inline bool hasUnionDecl();
  inline  ::capnp::compiler::Declaration::Union::Builder getUnionDecl();
  inline void setUnionDecl( ::capnp::compiler::Declaration::Union::Reader other);
  inline  ::capnp::compiler::Declaration::Union::Builder initUnionDecl();
  inline void adoptUnionDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Union>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Union> disownUnionDecl();

  // groupDecl@23: .Declaration.Group;  # ptr[3], union tag = 12
  inline bool hasGroupDecl();
  inline  ::capnp::compiler::Declaration::Group::Builder getGroupDecl();
  inline void setGroupDecl( ::capnp::compiler::Declaration::Group::Reader other);
  inline  ::capnp::compiler::Declaration::Group::Builder initGroupDecl();
  inline void adoptGroupDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Group>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Group> disownGroupDecl();

  // interfaceDecl@14: .Declaration.Interface;  # ptr[3], union tag = 7
  inline bool hasInterfaceDecl();
  inline  ::capnp::compiler::Declaration::Interface::Builder getInterfaceDecl();
  inline void setInterfaceDecl( ::capnp::compiler::Declaration::Interface::Reader other);
  inline  ::capnp::compiler::Declaration::Interface::Builder initInterfaceDecl();
  inline void adoptInterfaceDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Interface>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Interface> disownInterfaceDecl();

  // methodDecl@15: .Declaration.Method;  # ptr[3], union tag = 8
  inline bool hasMethodDecl();
  inline  ::capnp::compiler::Declaration::Method::Builder getMethodDecl();
  inline void setMethodDecl( ::capnp::compiler::Declaration::Method::Reader other);
  inline  ::capnp::compiler::Declaration::Method::Builder initMethodDecl();
  inline void adoptMethodDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Method>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Method> disownMethodDecl();

  // annotationDecl@16: .Declaration.Annotation;  # ptr[3], union tag = 9
  inline bool hasAnnotationDecl();
  inline  ::capnp::compiler::Declaration::Annotation::Builder getAnnotationDecl();
  inline void setAnnotationDecl( ::capnp::compiler::Declaration::Annotation::Reader other);
  inline  ::capnp::compiler::Declaration::Annotation::Builder initAnnotationDecl();
  inline void adoptAnnotationDecl(::capnp::Orphan< ::capnp::compiler::Declaration::Annotation>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::Annotation> disownAnnotationDecl();

  // nakedId@21: .LocatedInteger;  # ptr[3], union tag = 10
  inline bool hasNakedId();
  inline  ::capnp::compiler::LocatedInteger::Builder getNakedId();
  inline void setNakedId( ::capnp::compiler::LocatedInteger::Reader other);
  inline  ::capnp::compiler::LocatedInteger::Builder initNakedId();
  inline void adoptNakedId(::capnp::Orphan< ::capnp::compiler::LocatedInteger>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedInteger> disownNakedId();

  // nakedAnnotation@22: .Declaration.AnnotationApplication;  # ptr[3], union tag = 11
  inline bool hasNakedAnnotation();
  inline  ::capnp::compiler::Declaration::AnnotationApplication::Builder getNakedAnnotation();
  inline void setNakedAnnotation( ::capnp::compiler::Declaration::AnnotationApplication::Reader other);
  inline  ::capnp::compiler::Declaration::AnnotationApplication::Builder initNakedAnnotation();
  inline void adoptNakedAnnotation(::capnp::Orphan< ::capnp::compiler::Declaration::AnnotationApplication>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration::AnnotationApplication> disownNakedAnnotation();

  // builtinVoid@25: Void;  # (none), union tag = 14
  inline  ::capnp::Void getBuiltinVoid();
  inline void setBuiltinVoid( ::capnp::Void value);

  // builtinBool@26: Void;  # (none), union tag = 15
  inline  ::capnp::Void getBuiltinBool();
  inline void setBuiltinBool( ::capnp::Void value);

  // builtinInt8@27: Void;  # (none), union tag = 16
  inline  ::capnp::Void getBuiltinInt8();
  inline void setBuiltinInt8( ::capnp::Void value);

  // builtinInt16@28: Void;  # (none), union tag = 17
  inline  ::capnp::Void getBuiltinInt16();
  inline void setBuiltinInt16( ::capnp::Void value);

  // builtinInt32@29: Void;  # (none), union tag = 18
  inline  ::capnp::Void getBuiltinInt32();
  inline void setBuiltinInt32( ::capnp::Void value);

  // builtinInt64@30: Void;  # (none), union tag = 19
  inline  ::capnp::Void getBuiltinInt64();
  inline void setBuiltinInt64( ::capnp::Void value);

  // builtinUInt8@31: Void;  # (none), union tag = 20
  inline  ::capnp::Void getBuiltinUInt8();
  inline void setBuiltinUInt8( ::capnp::Void value);

  // builtinUInt16@32: Void;  # (none), union tag = 21
  inline  ::capnp::Void getBuiltinUInt16();
  inline void setBuiltinUInt16( ::capnp::Void value);

  // builtinUInt32@33: Void;  # (none), union tag = 22
  inline  ::capnp::Void getBuiltinUInt32();
  inline void setBuiltinUInt32( ::capnp::Void value);

  // builtinUInt64@34: Void;  # (none), union tag = 23
  inline  ::capnp::Void getBuiltinUInt64();
  inline void setBuiltinUInt64( ::capnp::Void value);

  // builtinFloat32@35: Void;  # (none), union tag = 24
  inline  ::capnp::Void getBuiltinFloat32();
  inline void setBuiltinFloat32( ::capnp::Void value);

  // builtinFloat64@36: Void;  # (none), union tag = 25
  inline  ::capnp::Void getBuiltinFloat64();
  inline void setBuiltinFloat64( ::capnp::Void value);

  // builtinText@37: Void;  # (none), union tag = 26
  inline  ::capnp::Void getBuiltinText();
  inline void setBuiltinText( ::capnp::Void value);

  // builtinData@38: Void;  # (none), union tag = 27
  inline  ::capnp::Void getBuiltinData();
  inline void setBuiltinData( ::capnp::Void value);

  // builtinList@39: Void;  # (none), union tag = 28
  inline  ::capnp::Void getBuiltinList();
  inline void setBuiltinList( ::capnp::Void value);

  // builtinObject@40: Void;  # (none), union tag = 29
  inline  ::capnp::Void getBuiltinObject();
  inline void setBuiltinObject( ::capnp::Void value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Body::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Body::Builder builder) {
  return ::capnp::_::unionString<Declaration::Body>(builder._builder.asReader());
}

class Declaration::File::Reader {
public:
  typedef File Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::File::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::File::Reader reader) {
  return ::capnp::_::structString<Declaration::File>(reader._reader);
}



class Declaration::File::Builder {
public:
  typedef File Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::File::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::File::Builder builder) {
  return ::capnp::_::structString<Declaration::File>(builder._builder.asReader());
}

class Declaration::Using::Reader {
public:
  typedef Using Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // target@0: .DeclName;  # ptr[0]
  inline bool hasTarget() const;
  inline  ::capnp::compiler::DeclName::Reader getTarget() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Using::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Using::Reader reader) {
  return ::capnp::_::structString<Declaration::Using>(reader._reader);
}



class Declaration::Using::Builder {
public:
  typedef Using Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // target@0: .DeclName;  # ptr[0]
  inline bool hasTarget();
  inline  ::capnp::compiler::DeclName::Builder getTarget();
  inline void setTarget( ::capnp::compiler::DeclName::Reader other);
  inline  ::capnp::compiler::DeclName::Builder initTarget();
  inline void adoptTarget(::capnp::Orphan< ::capnp::compiler::DeclName>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::DeclName> disownTarget();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Using::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Using::Builder builder) {
  return ::capnp::_::structString<Declaration::Using>(builder._builder.asReader());
}

class Declaration::Const::Reader {
public:
  typedef Const Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // type@0: .TypeExpression;  # ptr[0]
  inline bool hasType() const;
  inline  ::capnp::compiler::TypeExpression::Reader getType() const;

  // value@1: .ValueExpression;  # ptr[1]
  inline bool hasValue() const;
  inline  ::capnp::compiler::ValueExpression::Reader getValue() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Const::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Const::Reader reader) {
  return ::capnp::_::structString<Declaration::Const>(reader._reader);
}



class Declaration::Const::Builder {
public:
  typedef Const Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // type@0: .TypeExpression;  # ptr[0]
  inline bool hasType();
  inline  ::capnp::compiler::TypeExpression::Builder getType();
  inline void setType( ::capnp::compiler::TypeExpression::Reader other);
  inline  ::capnp::compiler::TypeExpression::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> disownType();

  // value@1: .ValueExpression;  # ptr[1]
  inline bool hasValue();
  inline  ::capnp::compiler::ValueExpression::Builder getValue();
  inline void setValue( ::capnp::compiler::ValueExpression::Reader other);
  inline  ::capnp::compiler::ValueExpression::Builder initValue();
  inline void adoptValue(::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> disownValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Const::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Const::Builder builder) {
  return ::capnp::_::structString<Declaration::Const>(builder._builder.asReader());
}

class Declaration::Enum::Reader {
public:
  typedef Enum Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Enum::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Enum::Reader reader) {
  return ::capnp::_::structString<Declaration::Enum>(reader._reader);
}



class Declaration::Enum::Builder {
public:
  typedef Enum Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Enum::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Enum::Builder builder) {
  return ::capnp::_::structString<Declaration::Enum>(builder._builder.asReader());
}

class Declaration::Enumerant::Reader {
public:
  typedef Enumerant Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Enumerant::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Enumerant::Reader reader) {
  return ::capnp::_::structString<Declaration::Enumerant>(reader._reader);
}



class Declaration::Enumerant::Builder {
public:
  typedef Enumerant Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Enumerant::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Enumerant::Builder builder) {
  return ::capnp::_::structString<Declaration::Enumerant>(builder._builder.asReader());
}

class Declaration::Struct::Reader {
public:
  typedef Struct Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Struct::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Struct::Reader reader) {
  return ::capnp::_::structString<Declaration::Struct>(reader._reader);
}



class Declaration::Struct::Builder {
public:
  typedef Struct Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Struct::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Struct::Builder builder) {
  return ::capnp::_::structString<Declaration::Struct>(builder._builder.asReader());
}

class Declaration::Field::Reader {
public:
  typedef Field Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union defaultValue@1 {  # [0, 16)
  inline DefaultValue::Reader getDefaultValue() const;

  // type@0: .TypeExpression;  # ptr[0]
  inline bool hasType() const;
  inline  ::capnp::compiler::TypeExpression::Reader getType() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Field::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Field::Reader reader) {
  return ::capnp::_::structString<Declaration::Field>(reader._reader);
}



class Declaration::Field::Builder {
public:
  typedef Field Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union defaultValue@1 {  # [0, 16)
  inline DefaultValue::Builder getDefaultValue();

  // type@0: .TypeExpression;  # ptr[0]
  inline bool hasType();
  inline  ::capnp::compiler::TypeExpression::Builder getType();
  inline void setType( ::capnp::compiler::TypeExpression::Reader other);
  inline  ::capnp::compiler::TypeExpression::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> disownType();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Field::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Field::Builder builder) {
  return ::capnp::_::structString<Declaration::Field>(builder._builder.asReader());
}

class Declaration::Field::DefaultValue::Reader {
public:
  typedef DefaultValue Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // none@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone() const;

  // value@3: .ValueExpression;  # ptr[1], union tag = 1
  inline bool hasValue() const;
  inline  ::capnp::compiler::ValueExpression::Reader getValue() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Field::DefaultValue::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Field::DefaultValue::Reader reader) {
  return ::capnp::_::unionString<Declaration::Field::DefaultValue>(reader._reader);
}



class Declaration::Field::DefaultValue::Builder {
public:
  typedef DefaultValue Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // none@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone();
  inline void setNone( ::capnp::Void value);

  // value@3: .ValueExpression;  # ptr[1], union tag = 1
  inline bool hasValue();
  inline  ::capnp::compiler::ValueExpression::Builder getValue();
  inline void setValue( ::capnp::compiler::ValueExpression::Reader other);
  inline  ::capnp::compiler::ValueExpression::Builder initValue();
  inline void adoptValue(::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> disownValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Field::DefaultValue::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Field::DefaultValue::Builder builder) {
  return ::capnp::_::unionString<Declaration::Field::DefaultValue>(builder._builder.asReader());
}

class Declaration::Union::Reader {
public:
  typedef Union Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Union::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Union::Reader reader) {
  return ::capnp::_::structString<Declaration::Union>(reader._reader);
}



class Declaration::Union::Builder {
public:
  typedef Union Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Union::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Union::Builder builder) {
  return ::capnp::_::structString<Declaration::Union>(builder._builder.asReader());
}

class Declaration::Group::Reader {
public:
  typedef Group Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Group::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Group::Reader reader) {
  return ::capnp::_::structString<Declaration::Group>(reader._reader);
}



class Declaration::Group::Builder {
public:
  typedef Group Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Group::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Group::Builder builder) {
  return ::capnp::_::structString<Declaration::Group>(builder._builder.asReader());
}

class Declaration::Interface::Reader {
public:
  typedef Interface Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Interface::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Interface::Reader reader) {
  return ::capnp::_::structString<Declaration::Interface>(reader._reader);
}



class Declaration::Interface::Builder {
public:
  typedef Interface Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Interface::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Interface::Builder builder) {
  return ::capnp::_::structString<Declaration::Interface>(builder._builder.asReader());
}

class Declaration::Method::Reader {
public:
  typedef Method Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union returnType@1 {  # [0, 16)
  inline ReturnType::Reader getReturnType() const;

  // params@0: List(.Declaration.Method.Param);  # ptr[0]
  inline bool hasParams() const;
  inline  ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Reader getParams() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Reader reader) {
  return ::capnp::_::structString<Declaration::Method>(reader._reader);
}



class Declaration::Method::Builder {
public:
  typedef Method Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union returnType@1 {  # [0, 16)
  inline ReturnType::Builder getReturnType();

  // params@0: List(.Declaration.Method.Param);  # ptr[0]
  inline bool hasParams();
  inline  ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Builder getParams();
  inline void setParams( ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Builder initParams(unsigned int size);
  inline void adoptParams(::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>> disownParams();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Builder builder) {
  return ::capnp::_::structString<Declaration::Method>(builder._builder.asReader());
}

class Declaration::Method::Param::Reader {
public:
  typedef Param Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union defaultValue@3 {  # [0, 16)
  inline DefaultValue::Reader getDefaultValue() const;

  // name@0: .LocatedText;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::compiler::LocatedText::Reader getName() const;

  // type@1: .TypeExpression;  # ptr[1]
  inline bool hasType() const;
  inline  ::capnp::compiler::TypeExpression::Reader getType() const;

  // annotations@2: List(.Declaration.AnnotationApplication);  # ptr[2]
  inline bool hasAnnotations() const;
  inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader getAnnotations() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::Reader reader) {
  return ::capnp::_::structString<Declaration::Method::Param>(reader._reader);
}



class Declaration::Method::Param::Builder {
public:
  typedef Param Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union defaultValue@3 {  # [0, 16)
  inline DefaultValue::Builder getDefaultValue();

  // name@0: .LocatedText;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::compiler::LocatedText::Builder getName();
  inline void setName( ::capnp::compiler::LocatedText::Reader other);
  inline  ::capnp::compiler::LocatedText::Builder initName();
  inline void adoptName(::capnp::Orphan< ::capnp::compiler::LocatedText>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::LocatedText> disownName();

  // type@1: .TypeExpression;  # ptr[1]
  inline bool hasType();
  inline  ::capnp::compiler::TypeExpression::Builder getType();
  inline void setType( ::capnp::compiler::TypeExpression::Reader other);
  inline  ::capnp::compiler::TypeExpression::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> disownType();

  // annotations@2: List(.Declaration.AnnotationApplication);  # ptr[2]
  inline bool hasAnnotations();
  inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder getAnnotations();
  inline void setAnnotations( ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader other);
  inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder initAnnotations(unsigned int size);
  inline void adoptAnnotations(::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>> disownAnnotations();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::Builder builder) {
  return ::capnp::_::structString<Declaration::Method::Param>(builder._builder.asReader());
}

class Declaration::Method::Param::DefaultValue::Reader {
public:
  typedef DefaultValue Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // none@4: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone() const;

  // value@5: .ValueExpression;  # ptr[3], union tag = 1
  inline bool hasValue() const;
  inline  ::capnp::compiler::ValueExpression::Reader getValue() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::DefaultValue::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::DefaultValue::Reader reader) {
  return ::capnp::_::unionString<Declaration::Method::Param::DefaultValue>(reader._reader);
}



class Declaration::Method::Param::DefaultValue::Builder {
public:
  typedef DefaultValue Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // none@4: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone();
  inline void setNone( ::capnp::Void value);

  // value@5: .ValueExpression;  # ptr[3], union tag = 1
  inline bool hasValue();
  inline  ::capnp::compiler::ValueExpression::Builder getValue();
  inline void setValue( ::capnp::compiler::ValueExpression::Reader other);
  inline  ::capnp::compiler::ValueExpression::Builder initValue();
  inline void adoptValue(::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> disownValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::DefaultValue::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::Param::DefaultValue::Builder builder) {
  return ::capnp::_::unionString<Declaration::Method::Param::DefaultValue>(builder._builder.asReader());
}

class Declaration::Method::ReturnType::Reader {
public:
  typedef ReturnType Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // none@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone() const;

  // expression@3: .TypeExpression;  # ptr[1], union tag = 1
  inline bool hasExpression() const;
  inline  ::capnp::compiler::TypeExpression::Reader getExpression() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::ReturnType::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::ReturnType::Reader reader) {
  return ::capnp::_::unionString<Declaration::Method::ReturnType>(reader._reader);
}



class Declaration::Method::ReturnType::Builder {
public:
  typedef ReturnType Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // none@2: Void;  # (none), union tag = 0
  inline  ::capnp::Void getNone();
  inline void setNone( ::capnp::Void value);

  // expression@3: .TypeExpression;  # ptr[1], union tag = 1
  inline bool hasExpression();
  inline  ::capnp::compiler::TypeExpression::Builder getExpression();
  inline void setExpression( ::capnp::compiler::TypeExpression::Reader other);
  inline  ::capnp::compiler::TypeExpression::Builder initExpression();
  inline void adoptExpression(::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> disownExpression();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Method::ReturnType::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Method::ReturnType::Builder builder) {
  return ::capnp::_::unionString<Declaration::Method::ReturnType>(builder._builder.asReader());
}

class Declaration::Annotation::Reader {
public:
  typedef Annotation Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // type@0: .TypeExpression;  # ptr[0]
  inline bool hasType() const;
  inline  ::capnp::compiler::TypeExpression::Reader getType() const;

  // targetsFile@1: Bool;  # bits[0, 1)
  inline bool getTargetsFile() const;

  // targetsConst@2: Bool;  # bits[1, 2)
  inline bool getTargetsConst() const;

  // targetsEnum@3: Bool;  # bits[2, 3)
  inline bool getTargetsEnum() const;

  // targetsEnumerant@4: Bool;  # bits[3, 4)
  inline bool getTargetsEnumerant() const;

  // targetsStruct@5: Bool;  # bits[4, 5)
  inline bool getTargetsStruct() const;

  // targetsField@6: Bool;  # bits[5, 6)
  inline bool getTargetsField() const;

  // targetsUnion@7: Bool;  # bits[6, 7)
  inline bool getTargetsUnion() const;

  // targetsInterface@8: Bool;  # bits[7, 8)
  inline bool getTargetsInterface() const;

  // targetsMethod@9: Bool;  # bits[8, 9)
  inline bool getTargetsMethod() const;

  // targetsParam@10: Bool;  # bits[9, 10)
  inline bool getTargetsParam() const;

  // targetsAnnotation@11: Bool;  # bits[10, 11)
  inline bool getTargetsAnnotation() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Annotation::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Annotation::Reader reader) {
  return ::capnp::_::structString<Declaration::Annotation>(reader._reader);
}



class Declaration::Annotation::Builder {
public:
  typedef Annotation Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // type@0: .TypeExpression;  # ptr[0]
  inline bool hasType();
  inline  ::capnp::compiler::TypeExpression::Builder getType();
  inline void setType( ::capnp::compiler::TypeExpression::Reader other);
  inline  ::capnp::compiler::TypeExpression::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> disownType();

  // targetsFile@1: Bool;  # bits[0, 1)
  inline bool getTargetsFile();
  inline void setTargetsFile(bool value);

  // targetsConst@2: Bool;  # bits[1, 2)
  inline bool getTargetsConst();
  inline void setTargetsConst(bool value);

  // targetsEnum@3: Bool;  # bits[2, 3)
  inline bool getTargetsEnum();
  inline void setTargetsEnum(bool value);

  // targetsEnumerant@4: Bool;  # bits[3, 4)
  inline bool getTargetsEnumerant();
  inline void setTargetsEnumerant(bool value);

  // targetsStruct@5: Bool;  # bits[4, 5)
  inline bool getTargetsStruct();
  inline void setTargetsStruct(bool value);

  // targetsField@6: Bool;  # bits[5, 6)
  inline bool getTargetsField();
  inline void setTargetsField(bool value);

  // targetsUnion@7: Bool;  # bits[6, 7)
  inline bool getTargetsUnion();
  inline void setTargetsUnion(bool value);

  // targetsInterface@8: Bool;  # bits[7, 8)
  inline bool getTargetsInterface();
  inline void setTargetsInterface(bool value);

  // targetsMethod@9: Bool;  # bits[8, 9)
  inline bool getTargetsMethod();
  inline void setTargetsMethod(bool value);

  // targetsParam@10: Bool;  # bits[9, 10)
  inline bool getTargetsParam();
  inline void setTargetsParam(bool value);

  // targetsAnnotation@11: Bool;  # bits[10, 11)
  inline bool getTargetsAnnotation();
  inline void setTargetsAnnotation(bool value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Declaration::Annotation::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Declaration::Annotation::Builder builder) {
  return ::capnp::_::structString<Declaration::Annotation>(builder._builder.asReader());
}

class ParsedFile::Reader {
public:
  typedef ParsedFile Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // root@0: .Declaration;  # ptr[0]
  inline bool hasRoot() const;
  inline  ::capnp::compiler::Declaration::Reader getRoot() const;
private:
  ::capnp::_::StructReader _reader;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::_::PointerHelpers;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::List;
  friend class ::capnp::MessageBuilder;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ParsedFile::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(ParsedFile::Reader reader) {
  return ::capnp::_::structString<ParsedFile>(reader._reader);
}



class ParsedFile::Builder {
public:
  typedef ParsedFile Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // root@0: .Declaration;  # ptr[0]
  inline bool hasRoot();
  inline  ::capnp::compiler::Declaration::Builder getRoot();
  inline void setRoot( ::capnp::compiler::Declaration::Reader other);
  inline  ::capnp::compiler::Declaration::Builder initRoot();
  inline void adoptRoot(::capnp::Orphan< ::capnp::compiler::Declaration>&& value);
  inline ::capnp::Orphan< ::capnp::compiler::Declaration> disownRoot();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ParsedFile::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(ParsedFile::Builder builder) {
  return ::capnp::_::structString<ParsedFile>(builder._builder.asReader());
}



// LocatedText::value@0: Text;  # ptr[0]


inline bool LocatedText::Reader::hasValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool LocatedText::Builder::hasValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader LocatedText::Reader::getValue() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder LocatedText::Builder::getValue() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void LocatedText::Builder::setValue( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder LocatedText::Builder::initValue(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void LocatedText::Builder::adoptValue(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> LocatedText::Builder::disownValue() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// LocatedText::startByte@1: UInt32;  # bits[0, 32)

inline  ::uint32_t LocatedText::Reader::getStartByte() const {
  return _reader.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint32_t LocatedText::Builder::getStartByte() {
  return _builder.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}
inline void LocatedText::Builder::setStartByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS, value);
}


// LocatedText::endByte@2: UInt32;  # bits[32, 64)

inline  ::uint32_t LocatedText::Reader::getEndByte() const {
  return _reader.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint32_t LocatedText::Builder::getEndByte() {
  return _builder.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}
inline void LocatedText::Builder::setEndByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS, value);
}



// LocatedInteger::value@0: UInt64;  # bits[0, 64)

inline  ::uint64_t LocatedInteger::Reader::getValue() const {
  return _reader.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint64_t LocatedInteger::Builder::getValue() {
  return _builder.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}
inline void LocatedInteger::Builder::setValue( ::uint64_t value) {
  _builder.setDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS, value);
}


// LocatedInteger::startByte@1: UInt32;  # bits[64, 96)

inline  ::uint32_t LocatedInteger::Reader::getStartByte() const {
  return _reader.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}

inline  ::uint32_t LocatedInteger::Builder::getStartByte() {
  return _builder.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}
inline void LocatedInteger::Builder::setStartByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS, value);
}


// LocatedInteger::endByte@2: UInt32;  # bits[96, 128)

inline  ::uint32_t LocatedInteger::Reader::getEndByte() const {
  return _reader.getDataField< ::uint32_t>(
      3 * ::capnp::ELEMENTS);
}

inline  ::uint32_t LocatedInteger::Builder::getEndByte() {
  return _builder.getDataField< ::uint32_t>(
      3 * ::capnp::ELEMENTS);
}
inline void LocatedInteger::Builder::setEndByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      3 * ::capnp::ELEMENTS, value);
}



// LocatedFloat::value@0: Float64;  # bits[0, 64)

inline double LocatedFloat::Reader::getValue() const {
  return _reader.getDataField<double>(
      0 * ::capnp::ELEMENTS);
}

inline double LocatedFloat::Builder::getValue() {
  return _builder.getDataField<double>(
      0 * ::capnp::ELEMENTS);
}
inline void LocatedFloat::Builder::setValue(double value) {
  _builder.setDataField<double>(
      0 * ::capnp::ELEMENTS, value);
}


// LocatedFloat::startByte@1: UInt32;  # bits[64, 96)

inline  ::uint32_t LocatedFloat::Reader::getStartByte() const {
  return _reader.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}

inline  ::uint32_t LocatedFloat::Builder::getStartByte() {
  return _builder.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}
inline void LocatedFloat::Builder::setStartByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS, value);
}


// LocatedFloat::endByte@2: UInt32;  # bits[96, 128)

inline  ::uint32_t LocatedFloat::Reader::getEndByte() const {
  return _reader.getDataField< ::uint32_t>(
      3 * ::capnp::ELEMENTS);
}

inline  ::uint32_t LocatedFloat::Builder::getEndByte() {
  return _builder.getDataField< ::uint32_t>(
      3 * ::capnp::ELEMENTS);
}
inline void LocatedFloat::Builder::setEndByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      3 * ::capnp::ELEMENTS, value);
}


inline DeclName::Base::Reader DeclName::Reader::getBase() const {
  return DeclName::Base::Reader(_reader);
}

inline DeclName::Base::Builder DeclName::Builder::getBase() {
  return DeclName::Base::Builder(_builder);
}


// DeclName::memberPath@4: List(.LocatedText);  # ptr[1]


inline bool DeclName::Reader::hasMemberPath() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool DeclName::Builder::hasMemberPath() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::LocatedText>::Reader DeclName::Reader::getMemberPath() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::LocatedText>>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::LocatedText>::Builder DeclName::Builder::getMemberPath() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::LocatedText>>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void DeclName::Builder::setMemberPath( ::capnp::List< ::capnp::compiler::LocatedText>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::LocatedText>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::LocatedText>::Builder DeclName::Builder::initMemberPath(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::LocatedText>>::init(
      _builder, 1 * ::capnp::POINTERS, size);
}


inline void DeclName::Builder::adoptMemberPath(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::LocatedText>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::LocatedText>>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::LocatedText>> DeclName::Builder::disownMemberPath() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::LocatedText>>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// DeclName::startByte@5: UInt32;  # bits[32, 64)

inline  ::uint32_t DeclName::Reader::getStartByte() const {
  return _reader.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint32_t DeclName::Builder::getStartByte() {
  return _builder.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}
inline void DeclName::Builder::setStartByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS, value);
}


// DeclName::endByte@6: UInt32;  # bits[64, 96)

inline  ::uint32_t DeclName::Reader::getEndByte() const {
  return _reader.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}

inline  ::uint32_t DeclName::Builder::getEndByte() {
  return _builder.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}
inline void DeclName::Builder::setEndByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS, value);
}


// DeclName::Base
inline DeclName::Base::Which DeclName::Base::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline DeclName::Base::Which DeclName::Base::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// DeclName::Base::absoluteName@1: .LocatedText;  # ptr[0], union tag = 0


inline bool DeclName::Base::Reader::hasAbsoluteName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool DeclName::Base::Builder::hasAbsoluteName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Reader DeclName::Base::Reader::getAbsoluteName() const {
  KJ_IREQUIRE(which() == Base::ABSOLUTE_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Builder DeclName::Base::Builder::getAbsoluteName() {
  KJ_IREQUIRE(which() == Base::ABSOLUTE_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void DeclName::Base::Builder::setAbsoluteName( ::capnp::compiler::LocatedText::Reader value) {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::ABSOLUTE_NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedText::Builder DeclName::Base::Builder::initAbsoluteName() {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::ABSOLUTE_NAME);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void DeclName::Base::Builder::adoptAbsoluteName(
    ::capnp::Orphan< ::capnp::compiler::LocatedText>&& value) {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::ABSOLUTE_NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedText> DeclName::Base::Builder::disownAbsoluteName() {
  KJ_IREQUIRE(which() == Base::ABSOLUTE_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// DeclName::Base::relativeName@2: .LocatedText;  # ptr[0], union tag = 1


inline bool DeclName::Base::Reader::hasRelativeName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool DeclName::Base::Builder::hasRelativeName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Reader DeclName::Base::Reader::getRelativeName() const {
  KJ_IREQUIRE(which() == Base::RELATIVE_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Builder DeclName::Base::Builder::getRelativeName() {
  KJ_IREQUIRE(which() == Base::RELATIVE_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void DeclName::Base::Builder::setRelativeName( ::capnp::compiler::LocatedText::Reader value) {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::RELATIVE_NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedText::Builder DeclName::Base::Builder::initRelativeName() {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::RELATIVE_NAME);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void DeclName::Base::Builder::adoptRelativeName(
    ::capnp::Orphan< ::capnp::compiler::LocatedText>&& value) {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::RELATIVE_NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedText> DeclName::Base::Builder::disownRelativeName() {
  KJ_IREQUIRE(which() == Base::RELATIVE_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// DeclName::Base::importName@3: .LocatedText;  # ptr[0], union tag = 2


inline bool DeclName::Base::Reader::hasImportName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool DeclName::Base::Builder::hasImportName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Reader DeclName::Base::Reader::getImportName() const {
  KJ_IREQUIRE(which() == Base::IMPORT_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Builder DeclName::Base::Builder::getImportName() {
  KJ_IREQUIRE(which() == Base::IMPORT_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void DeclName::Base::Builder::setImportName( ::capnp::compiler::LocatedText::Reader value) {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::IMPORT_NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedText::Builder DeclName::Base::Builder::initImportName() {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::IMPORT_NAME);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void DeclName::Base::Builder::adoptImportName(
    ::capnp::Orphan< ::capnp::compiler::LocatedText>&& value) {
  _builder.setDataField<Base::Which>(
      0 * ::capnp::ELEMENTS, Base::IMPORT_NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedText> DeclName::Base::Builder::disownImportName() {
  KJ_IREQUIRE(which() == Base::IMPORT_NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// TypeExpression::name@0: .DeclName;  # ptr[0]


inline bool TypeExpression::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool TypeExpression::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Reader TypeExpression::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Builder TypeExpression::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void TypeExpression::Builder::setName( ::capnp::compiler::DeclName::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::DeclName::Builder TypeExpression::Builder::initName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void TypeExpression::Builder::adoptName(
    ::capnp::Orphan< ::capnp::compiler::DeclName>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::DeclName> TypeExpression::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// TypeExpression::params@1: List(.TypeExpression);  # ptr[1]


inline bool TypeExpression::Reader::hasParams() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool TypeExpression::Builder::hasParams() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::TypeExpression>::Reader TypeExpression::Reader::getParams() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::TypeExpression>>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::TypeExpression>::Builder TypeExpression::Builder::getParams() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::TypeExpression>>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void TypeExpression::Builder::setParams( ::capnp::List< ::capnp::compiler::TypeExpression>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::TypeExpression>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::TypeExpression>::Builder TypeExpression::Builder::initParams(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::TypeExpression>>::init(
      _builder, 1 * ::capnp::POINTERS, size);
}


inline void TypeExpression::Builder::adoptParams(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::TypeExpression>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::TypeExpression>>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::TypeExpression>> TypeExpression::Builder::disownParams() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::TypeExpression>>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// TypeExpression::startByte@2: UInt32;  # bits[0, 32)

inline  ::uint32_t TypeExpression::Reader::getStartByte() const {
  return _reader.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint32_t TypeExpression::Builder::getStartByte() {
  return _builder.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}
inline void TypeExpression::Builder::setStartByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS, value);
}


// TypeExpression::endByte@3: UInt32;  # bits[32, 64)

inline  ::uint32_t TypeExpression::Reader::getEndByte() const {
  return _reader.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint32_t TypeExpression::Builder::getEndByte() {
  return _builder.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}
inline void TypeExpression::Builder::setEndByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS, value);
}


inline ValueExpression::Body::Reader ValueExpression::Reader::getBody() const {
  return ValueExpression::Body::Reader(_reader);
}

inline ValueExpression::Body::Builder ValueExpression::Builder::getBody() {
  return ValueExpression::Body::Builder(_builder);
}


// ValueExpression::startByte@10: UInt32;  # bits[32, 64)

inline  ::uint32_t ValueExpression::Reader::getStartByte() const {
  return _reader.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint32_t ValueExpression::Builder::getStartByte() {
  return _builder.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}
inline void ValueExpression::Builder::setStartByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS, value);
}


// ValueExpression::endByte@11: UInt32;  # bits[128, 160)

inline  ::uint32_t ValueExpression::Reader::getEndByte() const {
  return _reader.getDataField< ::uint32_t>(
      4 * ::capnp::ELEMENTS);
}

inline  ::uint32_t ValueExpression::Builder::getEndByte() {
  return _builder.getDataField< ::uint32_t>(
      4 * ::capnp::ELEMENTS);
}
inline void ValueExpression::Builder::setEndByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      4 * ::capnp::ELEMENTS, value);
}


// ValueExpression::Body
inline ValueExpression::Body::Which ValueExpression::Body::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline ValueExpression::Body::Which ValueExpression::Body::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// ValueExpression::Body::unknown@1: Void;  # (none), union tag = 0

inline  ::capnp::Void ValueExpression::Body::Reader::getUnknown() const {
  KJ_IREQUIRE(which() == Body::UNKNOWN,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void ValueExpression::Body::Builder::getUnknown() {
  KJ_IREQUIRE(which() == Body::UNKNOWN,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void ValueExpression::Body::Builder::setUnknown( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UNKNOWN);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// ValueExpression::Body::positiveInt@2: UInt64;  # bits[64, 128), union tag = 1

inline  ::uint64_t ValueExpression::Body::Reader::getPositiveInt() const {
  KJ_IREQUIRE(which() == Body::POSITIVE_INT,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint64_t ValueExpression::Body::Builder::getPositiveInt() {
  KJ_IREQUIRE(which() == Body::POSITIVE_INT,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void ValueExpression::Body::Builder::setPositiveInt( ::uint64_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::POSITIVE_INT);
  _builder.setDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// ValueExpression::Body::negativeInt@3: UInt64;  # bits[64, 128), union tag = 2

inline  ::uint64_t ValueExpression::Body::Reader::getNegativeInt() const {
  KJ_IREQUIRE(which() == Body::NEGATIVE_INT,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint64_t ValueExpression::Body::Builder::getNegativeInt() {
  KJ_IREQUIRE(which() == Body::NEGATIVE_INT,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void ValueExpression::Body::Builder::setNegativeInt( ::uint64_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::NEGATIVE_INT);
  _builder.setDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// ValueExpression::Body::float@4: Float64;  # bits[64, 128), union tag = 3

inline double ValueExpression::Body::Reader::getFloat() const {
  KJ_IREQUIRE(which() == Body::FLOAT,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField<double>(
      1 * ::capnp::ELEMENTS);
}

inline double ValueExpression::Body::Builder::getFloat() {
  KJ_IREQUIRE(which() == Body::FLOAT,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField<double>(
      1 * ::capnp::ELEMENTS);
}
inline void ValueExpression::Body::Builder::setFloat(double value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::FLOAT);
  _builder.setDataField<double>(
      1 * ::capnp::ELEMENTS, value);
}


// ValueExpression::Body::string@5: Text;  # ptr[0], union tag = 4


inline bool ValueExpression::Body::Reader::hasString() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ValueExpression::Body::Builder::hasString() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader ValueExpression::Body::Reader::getString() const {
  KJ_IREQUIRE(which() == Body::STRING,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder ValueExpression::Body::Builder::getString() {
  KJ_IREQUIRE(which() == Body::STRING,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ValueExpression::Body::Builder::setString( ::capnp::Text::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRING);
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder ValueExpression::Body::Builder::initString(unsigned int size) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRING);
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void ValueExpression::Body::Builder::adoptString(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRING);
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> ValueExpression::Body::Builder::disownString() {
  KJ_IREQUIRE(which() == Body::STRING,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// ValueExpression::Body::name@6: .DeclName;  # ptr[0], union tag = 5


inline bool ValueExpression::Body::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ValueExpression::Body::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Reader ValueExpression::Body::Reader::getName() const {
  KJ_IREQUIRE(which() == Body::NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Builder ValueExpression::Body::Builder::getName() {
  KJ_IREQUIRE(which() == Body::NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ValueExpression::Body::Builder::setName( ::capnp::compiler::DeclName::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::DeclName::Builder ValueExpression::Body::Builder::initName() {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::NAME);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void ValueExpression::Body::Builder::adoptName(
    ::capnp::Orphan< ::capnp::compiler::DeclName>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::NAME);
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::DeclName> ValueExpression::Body::Builder::disownName() {
  KJ_IREQUIRE(which() == Body::NAME,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// ValueExpression::Body::list@7: List(.ValueExpression);  # ptr[0], union tag = 6


inline bool ValueExpression::Body::Reader::hasList() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ValueExpression::Body::Builder::hasList() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::ValueExpression>::Reader ValueExpression::Body::Reader::getList() const {
  KJ_IREQUIRE(which() == Body::LIST,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::ValueExpression>::Builder ValueExpression::Body::Builder::getList() {
  KJ_IREQUIRE(which() == Body::LIST,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ValueExpression::Body::Builder::setList( ::capnp::List< ::capnp::compiler::ValueExpression>::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST);
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::ValueExpression>::Builder ValueExpression::Body::Builder::initList(unsigned int size) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST);
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void ValueExpression::Body::Builder::adoptList(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression>>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST);
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression>> ValueExpression::Body::Builder::disownList() {
  KJ_IREQUIRE(which() == Body::LIST,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// ValueExpression::Body::structValue@8: List(.ValueExpression.FieldAssignment);  # ptr[0], union tag = 7


inline bool ValueExpression::Body::Reader::hasStructValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ValueExpression::Body::Builder::hasStructValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Reader ValueExpression::Body::Reader::getStructValue() const {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Builder ValueExpression::Body::Builder::getStructValue() {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ValueExpression::Body::Builder::setStructValue( ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>::Builder ValueExpression::Body::Builder::initStructValue(unsigned int size) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_VALUE);
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void ValueExpression::Body::Builder::adoptStructValue(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>> ValueExpression::Body::Builder::disownStructValue() {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::ValueExpression::FieldAssignment>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// ValueExpression::Body::unionValue@9: .ValueExpression.FieldAssignment;  # ptr[0], union tag = 8


inline bool ValueExpression::Body::Reader::hasUnionValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ValueExpression::Body::Builder::hasUnionValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::FieldAssignment::Reader ValueExpression::Body::Reader::getUnionValue() const {
  KJ_IREQUIRE(which() == Body::UNION_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression::FieldAssignment>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::FieldAssignment::Builder ValueExpression::Body::Builder::getUnionValue() {
  KJ_IREQUIRE(which() == Body::UNION_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression::FieldAssignment>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ValueExpression::Body::Builder::setUnionValue( ::capnp::compiler::ValueExpression::FieldAssignment::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UNION_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression::FieldAssignment>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::ValueExpression::FieldAssignment::Builder ValueExpression::Body::Builder::initUnionValue() {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UNION_VALUE);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression::FieldAssignment>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void ValueExpression::Body::Builder::adoptUnionValue(
    ::capnp::Orphan< ::capnp::compiler::ValueExpression::FieldAssignment>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UNION_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression::FieldAssignment>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::ValueExpression::FieldAssignment> ValueExpression::Body::Builder::disownUnionValue() {
  KJ_IREQUIRE(which() == Body::UNION_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression::FieldAssignment>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// ValueExpression::FieldAssignment::fieldName@0: .LocatedText;  # ptr[0]


inline bool ValueExpression::FieldAssignment::Reader::hasFieldName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ValueExpression::FieldAssignment::Builder::hasFieldName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Reader ValueExpression::FieldAssignment::Reader::getFieldName() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Builder ValueExpression::FieldAssignment::Builder::getFieldName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ValueExpression::FieldAssignment::Builder::setFieldName( ::capnp::compiler::LocatedText::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedText::Builder ValueExpression::FieldAssignment::Builder::initFieldName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void ValueExpression::FieldAssignment::Builder::adoptFieldName(
    ::capnp::Orphan< ::capnp::compiler::LocatedText>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedText> ValueExpression::FieldAssignment::Builder::disownFieldName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// ValueExpression::FieldAssignment::value@1: .ValueExpression;  # ptr[1]


inline bool ValueExpression::FieldAssignment::Reader::hasValue() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool ValueExpression::FieldAssignment::Builder::hasValue() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Reader ValueExpression::FieldAssignment::Reader::getValue() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Builder ValueExpression::FieldAssignment::Builder::getValue() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void ValueExpression::FieldAssignment::Builder::setValue( ::capnp::compiler::ValueExpression::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::ValueExpression::Builder ValueExpression::FieldAssignment::Builder::initValue() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void ValueExpression::FieldAssignment::Builder::adoptValue(
    ::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> ValueExpression::FieldAssignment::Builder::disownValue() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



inline Declaration::Id::Reader Declaration::Reader::getId() const {
  return Declaration::Id::Reader(_reader);
}

inline Declaration::Id::Builder Declaration::Builder::getId() {
  return Declaration::Id::Builder(_builder);
}

inline Declaration::Body::Reader Declaration::Reader::getBody() const {
  return Declaration::Body::Reader(_reader);
}

inline Declaration::Body::Builder Declaration::Builder::getBody() {
  return Declaration::Body::Builder(_builder);
}


// Declaration::name@0: .LocatedText;  # ptr[0]


inline bool Declaration::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Reader Declaration::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Builder Declaration::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::Builder::setName( ::capnp::compiler::LocatedText::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedText::Builder Declaration::Builder::initName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Declaration::Builder::adoptName(
    ::capnp::Orphan< ::capnp::compiler::LocatedText>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedText> Declaration::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Declaration::nestedDecls@17: List(.Declaration);  # ptr[4]


inline bool Declaration::Reader::hasNestedDecls() const {
  return !_reader.isPointerFieldNull(4 * ::capnp::POINTERS);
}

inline bool Declaration::Builder::hasNestedDecls() {
  return !_builder.isPointerFieldNull(4 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration>::Reader Declaration::Reader::getNestedDecls() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration>>::get(
      _reader, 4 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration>::Builder Declaration::Builder::getNestedDecls() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration>>::get(
      _builder, 4 * ::capnp::POINTERS);
}

inline void Declaration::Builder::setNestedDecls( ::capnp::List< ::capnp::compiler::Declaration>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration>>::set(
      _builder, 4 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::Declaration>::Builder Declaration::Builder::initNestedDecls(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration>>::init(
      _builder, 4 * ::capnp::POINTERS, size);
}


inline void Declaration::Builder::adoptNestedDecls(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration>>::adopt(
      _builder, 4 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration>> Declaration::Builder::disownNestedDecls() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration>>::disown(
      _builder, 4 * ::capnp::POINTERS);
}



// Declaration::annotations@5: List(.Declaration.AnnotationApplication);  # ptr[2]


inline bool Declaration::Reader::hasAnnotations() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool Declaration::Builder::hasAnnotations() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader Declaration::Reader::getAnnotations() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder Declaration::Builder::getAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void Declaration::Builder::setAnnotations( ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder Declaration::Builder::initAnnotations(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::init(
      _builder, 2 * ::capnp::POINTERS, size);
}


inline void Declaration::Builder::adoptAnnotations(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>> Declaration::Builder::disownAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::disown(
      _builder, 2 * ::capnp::POINTERS);
}



// Declaration::startByte@18: UInt32;  # bits[32, 64)

inline  ::uint32_t Declaration::Reader::getStartByte() const {
  return _reader.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint32_t Declaration::Builder::getStartByte() {
  return _builder.getDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS);
}
inline void Declaration::Builder::setStartByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      1 * ::capnp::ELEMENTS, value);
}


// Declaration::endByte@19: UInt32;  # bits[64, 96)

inline  ::uint32_t Declaration::Reader::getEndByte() const {
  return _reader.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}

inline  ::uint32_t Declaration::Builder::getEndByte() {
  return _builder.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}
inline void Declaration::Builder::setEndByte( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS, value);
}


// Declaration::docComment@20: Text;  # ptr[5]


inline bool Declaration::Reader::hasDocComment() const {
  return !_reader.isPointerFieldNull(5 * ::capnp::POINTERS);
}

inline bool Declaration::Builder::hasDocComment() {
  return !_builder.isPointerFieldNull(5 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader Declaration::Reader::getDocComment() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 5 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder Declaration::Builder::getDocComment() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 5 * ::capnp::POINTERS);
}

inline void Declaration::Builder::setDocComment( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 5 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder Declaration::Builder::initDocComment(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 5 * ::capnp::POINTERS, size);
}


inline void Declaration::Builder::adoptDocComment(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 5 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> Declaration::Builder::disownDocComment() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 5 * ::capnp::POINTERS);
}



// Declaration::Id
inline Declaration::Id::Which Declaration::Id::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline Declaration::Id::Which Declaration::Id::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// Declaration::Id::unspecified@2: Void;  # (none), union tag = 0

inline  ::capnp::Void Declaration::Id::Reader::getUnspecified() const {
  KJ_IREQUIRE(which() == Id::UNSPECIFIED,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Id::Builder::getUnspecified() {
  KJ_IREQUIRE(which() == Id::UNSPECIFIED,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Id::Builder::setUnspecified( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Id::Which>(
      0 * ::capnp::ELEMENTS, Id::UNSPECIFIED);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Id::uid@3: .LocatedInteger;  # ptr[1], union tag = 1


inline bool Declaration::Id::Reader::hasUid() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Declaration::Id::Builder::hasUid() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedInteger::Reader Declaration::Id::Reader::getUid() const {
  KJ_IREQUIRE(which() == Id::UID,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedInteger::Builder Declaration::Id::Builder::getUid() {
  KJ_IREQUIRE(which() == Id::UID,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Declaration::Id::Builder::setUid( ::capnp::compiler::LocatedInteger::Reader value) {
  _builder.setDataField<Id::Which>(
      0 * ::capnp::ELEMENTS, Id::UID);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedInteger::Builder Declaration::Id::Builder::initUid() {
  _builder.setDataField<Id::Which>(
      0 * ::capnp::ELEMENTS, Id::UID);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void Declaration::Id::Builder::adoptUid(
    ::capnp::Orphan< ::capnp::compiler::LocatedInteger>&& value) {
  _builder.setDataField<Id::Which>(
      0 * ::capnp::ELEMENTS, Id::UID);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedInteger> Declaration::Id::Builder::disownUid() {
  KJ_IREQUIRE(which() == Id::UID,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// Declaration::Id::ordinal@4: .LocatedInteger;  # ptr[1], union tag = 2


inline bool Declaration::Id::Reader::hasOrdinal() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Declaration::Id::Builder::hasOrdinal() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedInteger::Reader Declaration::Id::Reader::getOrdinal() const {
  KJ_IREQUIRE(which() == Id::ORDINAL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedInteger::Builder Declaration::Id::Builder::getOrdinal() {
  KJ_IREQUIRE(which() == Id::ORDINAL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Declaration::Id::Builder::setOrdinal( ::capnp::compiler::LocatedInteger::Reader value) {
  _builder.setDataField<Id::Which>(
      0 * ::capnp::ELEMENTS, Id::ORDINAL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedInteger::Builder Declaration::Id::Builder::initOrdinal() {
  _builder.setDataField<Id::Which>(
      0 * ::capnp::ELEMENTS, Id::ORDINAL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void Declaration::Id::Builder::adoptOrdinal(
    ::capnp::Orphan< ::capnp::compiler::LocatedInteger>&& value) {
  _builder.setDataField<Id::Which>(
      0 * ::capnp::ELEMENTS, Id::ORDINAL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedInteger> Declaration::Id::Builder::disownOrdinal() {
  KJ_IREQUIRE(which() == Id::ORDINAL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



inline Declaration::AnnotationApplication::Value::Reader Declaration::AnnotationApplication::Reader::getValue() const {
  return Declaration::AnnotationApplication::Value::Reader(_reader);
}

inline Declaration::AnnotationApplication::Value::Builder Declaration::AnnotationApplication::Builder::getValue() {
  return Declaration::AnnotationApplication::Value::Builder(_builder);
}


// Declaration::AnnotationApplication::name@0: .DeclName;  # ptr[0]


inline bool Declaration::AnnotationApplication::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::AnnotationApplication::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Reader Declaration::AnnotationApplication::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Builder Declaration::AnnotationApplication::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::AnnotationApplication::Builder::setName( ::capnp::compiler::DeclName::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::DeclName::Builder Declaration::AnnotationApplication::Builder::initName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Declaration::AnnotationApplication::Builder::adoptName(
    ::capnp::Orphan< ::capnp::compiler::DeclName>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::DeclName> Declaration::AnnotationApplication::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Declaration::AnnotationApplication::Value
inline Declaration::AnnotationApplication::Value::Which Declaration::AnnotationApplication::Value::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline Declaration::AnnotationApplication::Value::Which Declaration::AnnotationApplication::Value::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// Declaration::AnnotationApplication::Value::none@2: Void;  # (none), union tag = 0

inline  ::capnp::Void Declaration::AnnotationApplication::Value::Reader::getNone() const {
  KJ_IREQUIRE(which() == Value::NONE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::AnnotationApplication::Value::Builder::getNone() {
  KJ_IREQUIRE(which() == Value::NONE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::AnnotationApplication::Value::Builder::setNone( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Value::Which>(
      0 * ::capnp::ELEMENTS, Value::NONE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::AnnotationApplication::Value::expression@3: .ValueExpression;  # ptr[1], union tag = 1


inline bool Declaration::AnnotationApplication::Value::Reader::hasExpression() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Declaration::AnnotationApplication::Value::Builder::hasExpression() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Reader Declaration::AnnotationApplication::Value::Reader::getExpression() const {
  KJ_IREQUIRE(which() == Value::EXPRESSION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::AnnotationApplication::Value::Builder::getExpression() {
  KJ_IREQUIRE(which() == Value::EXPRESSION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Declaration::AnnotationApplication::Value::Builder::setExpression( ::capnp::compiler::ValueExpression::Reader value) {
  _builder.setDataField<Value::Which>(
      0 * ::capnp::ELEMENTS, Value::EXPRESSION);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::AnnotationApplication::Value::Builder::initExpression() {
  _builder.setDataField<Value::Which>(
      0 * ::capnp::ELEMENTS, Value::EXPRESSION);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void Declaration::AnnotationApplication::Value::Builder::adoptExpression(
    ::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value) {
  _builder.setDataField<Value::Which>(
      0 * ::capnp::ELEMENTS, Value::EXPRESSION);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> Declaration::AnnotationApplication::Value::Builder::disownExpression() {
  KJ_IREQUIRE(which() == Value::EXPRESSION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// Declaration::Body
inline Declaration::Body::Which Declaration::Body::Reader::which() const {
  return _reader.getDataField<Which>(1 * ::capnp::ELEMENTS);
}

inline Declaration::Body::Which Declaration::Body::Builder::which() {
  return _builder.getDataField<Which>(1 * ::capnp::ELEMENTS);
}


// Declaration::Body::fileDecl@24: .Declaration.File;  # ptr[3], union tag = 13


inline bool Declaration::Body::Reader::hasFileDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasFileDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::File::Reader Declaration::Body::Reader::getFileDecl() const {
  KJ_IREQUIRE(which() == Body::FILE_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::File>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::File::Builder Declaration::Body::Builder::getFileDecl() {
  KJ_IREQUIRE(which() == Body::FILE_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::File>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setFileDecl( ::capnp::compiler::Declaration::File::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::FILE_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::File>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::File::Builder Declaration::Body::Builder::initFileDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::FILE_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::File>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptFileDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::File>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::FILE_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::File>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::File> Declaration::Body::Builder::disownFileDecl() {
  KJ_IREQUIRE(which() == Body::FILE_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::File>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::usingDecl@7: .Declaration.Using;  # ptr[3], union tag = 0


inline bool Declaration::Body::Reader::hasUsingDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasUsingDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Using::Reader Declaration::Body::Reader::getUsingDecl() const {
  KJ_IREQUIRE(which() == Body::USING_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Using>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Using::Builder Declaration::Body::Builder::getUsingDecl() {
  KJ_IREQUIRE(which() == Body::USING_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Using>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setUsingDecl( ::capnp::compiler::Declaration::Using::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::USING_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Using>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Using::Builder Declaration::Body::Builder::initUsingDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::USING_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Using>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptUsingDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Using>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::USING_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Using>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Using> Declaration::Body::Builder::disownUsingDecl() {
  KJ_IREQUIRE(which() == Body::USING_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Using>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::constDecl@8: .Declaration.Const;  # ptr[3], union tag = 1


inline bool Declaration::Body::Reader::hasConstDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasConstDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Const::Reader Declaration::Body::Reader::getConstDecl() const {
  KJ_IREQUIRE(which() == Body::CONST_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Const>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Const::Builder Declaration::Body::Builder::getConstDecl() {
  KJ_IREQUIRE(which() == Body::CONST_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Const>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setConstDecl( ::capnp::compiler::Declaration::Const::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::CONST_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Const>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Const::Builder Declaration::Body::Builder::initConstDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::CONST_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Const>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptConstDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Const>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::CONST_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Const>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Const> Declaration::Body::Builder::disownConstDecl() {
  KJ_IREQUIRE(which() == Body::CONST_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Const>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::enumDecl@9: .Declaration.Enum;  # ptr[3], union tag = 2


inline bool Declaration::Body::Reader::hasEnumDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasEnumDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Enum::Reader Declaration::Body::Reader::getEnumDecl() const {
  KJ_IREQUIRE(which() == Body::ENUM_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enum>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Enum::Builder Declaration::Body::Builder::getEnumDecl() {
  KJ_IREQUIRE(which() == Body::ENUM_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enum>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setEnumDecl( ::capnp::compiler::Declaration::Enum::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ENUM_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enum>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Enum::Builder Declaration::Body::Builder::initEnumDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ENUM_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enum>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptEnumDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Enum>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ENUM_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enum>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Enum> Declaration::Body::Builder::disownEnumDecl() {
  KJ_IREQUIRE(which() == Body::ENUM_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enum>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::enumerantDecl@10: .Declaration.Enumerant;  # ptr[3], union tag = 3


inline bool Declaration::Body::Reader::hasEnumerantDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasEnumerantDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Enumerant::Reader Declaration::Body::Reader::getEnumerantDecl() const {
  KJ_IREQUIRE(which() == Body::ENUMERANT_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enumerant>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Enumerant::Builder Declaration::Body::Builder::getEnumerantDecl() {
  KJ_IREQUIRE(which() == Body::ENUMERANT_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enumerant>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setEnumerantDecl( ::capnp::compiler::Declaration::Enumerant::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ENUMERANT_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enumerant>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Enumerant::Builder Declaration::Body::Builder::initEnumerantDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ENUMERANT_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enumerant>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptEnumerantDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Enumerant>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ENUMERANT_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enumerant>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Enumerant> Declaration::Body::Builder::disownEnumerantDecl() {
  KJ_IREQUIRE(which() == Body::ENUMERANT_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Enumerant>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::structDecl@11: .Declaration.Struct;  # ptr[3], union tag = 4


inline bool Declaration::Body::Reader::hasStructDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasStructDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Struct::Reader Declaration::Body::Reader::getStructDecl() const {
  KJ_IREQUIRE(which() == Body::STRUCT_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Struct>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Struct::Builder Declaration::Body::Builder::getStructDecl() {
  KJ_IREQUIRE(which() == Body::STRUCT_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Struct>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setStructDecl( ::capnp::compiler::Declaration::Struct::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::STRUCT_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Struct>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Struct::Builder Declaration::Body::Builder::initStructDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::STRUCT_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Struct>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptStructDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Struct>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::STRUCT_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Struct>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Struct> Declaration::Body::Builder::disownStructDecl() {
  KJ_IREQUIRE(which() == Body::STRUCT_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Struct>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::fieldDecl@12: .Declaration.Field;  # ptr[3], union tag = 5


inline bool Declaration::Body::Reader::hasFieldDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasFieldDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Field::Reader Declaration::Body::Reader::getFieldDecl() const {
  KJ_IREQUIRE(which() == Body::FIELD_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Field>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Field::Builder Declaration::Body::Builder::getFieldDecl() {
  KJ_IREQUIRE(which() == Body::FIELD_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Field>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setFieldDecl( ::capnp::compiler::Declaration::Field::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::FIELD_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Field>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Field::Builder Declaration::Body::Builder::initFieldDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::FIELD_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Field>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptFieldDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Field>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::FIELD_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Field>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Field> Declaration::Body::Builder::disownFieldDecl() {
  KJ_IREQUIRE(which() == Body::FIELD_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Field>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::unionDecl@13: .Declaration.Union;  # ptr[3], union tag = 6


inline bool Declaration::Body::Reader::hasUnionDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasUnionDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Union::Reader Declaration::Body::Reader::getUnionDecl() const {
  KJ_IREQUIRE(which() == Body::UNION_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Union>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Union::Builder Declaration::Body::Builder::getUnionDecl() {
  KJ_IREQUIRE(which() == Body::UNION_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Union>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setUnionDecl( ::capnp::compiler::Declaration::Union::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::UNION_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Union>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Union::Builder Declaration::Body::Builder::initUnionDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::UNION_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Union>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptUnionDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Union>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::UNION_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Union>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Union> Declaration::Body::Builder::disownUnionDecl() {
  KJ_IREQUIRE(which() == Body::UNION_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Union>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::groupDecl@23: .Declaration.Group;  # ptr[3], union tag = 12


inline bool Declaration::Body::Reader::hasGroupDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasGroupDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Group::Reader Declaration::Body::Reader::getGroupDecl() const {
  KJ_IREQUIRE(which() == Body::GROUP_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Group>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Group::Builder Declaration::Body::Builder::getGroupDecl() {
  KJ_IREQUIRE(which() == Body::GROUP_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Group>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setGroupDecl( ::capnp::compiler::Declaration::Group::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::GROUP_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Group>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Group::Builder Declaration::Body::Builder::initGroupDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::GROUP_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Group>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptGroupDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Group>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::GROUP_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Group>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Group> Declaration::Body::Builder::disownGroupDecl() {
  KJ_IREQUIRE(which() == Body::GROUP_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Group>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::interfaceDecl@14: .Declaration.Interface;  # ptr[3], union tag = 7


inline bool Declaration::Body::Reader::hasInterfaceDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasInterfaceDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Interface::Reader Declaration::Body::Reader::getInterfaceDecl() const {
  KJ_IREQUIRE(which() == Body::INTERFACE_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Interface>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Interface::Builder Declaration::Body::Builder::getInterfaceDecl() {
  KJ_IREQUIRE(which() == Body::INTERFACE_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Interface>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setInterfaceDecl( ::capnp::compiler::Declaration::Interface::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::INTERFACE_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Interface>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Interface::Builder Declaration::Body::Builder::initInterfaceDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::INTERFACE_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Interface>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptInterfaceDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Interface>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::INTERFACE_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Interface>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Interface> Declaration::Body::Builder::disownInterfaceDecl() {
  KJ_IREQUIRE(which() == Body::INTERFACE_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Interface>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::methodDecl@15: .Declaration.Method;  # ptr[3], union tag = 8


inline bool Declaration::Body::Reader::hasMethodDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasMethodDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Method::Reader Declaration::Body::Reader::getMethodDecl() const {
  KJ_IREQUIRE(which() == Body::METHOD_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Method>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Method::Builder Declaration::Body::Builder::getMethodDecl() {
  KJ_IREQUIRE(which() == Body::METHOD_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Method>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setMethodDecl( ::capnp::compiler::Declaration::Method::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::METHOD_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Method>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Method::Builder Declaration::Body::Builder::initMethodDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::METHOD_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Method>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptMethodDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Method>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::METHOD_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Method>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Method> Declaration::Body::Builder::disownMethodDecl() {
  KJ_IREQUIRE(which() == Body::METHOD_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Method>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::annotationDecl@16: .Declaration.Annotation;  # ptr[3], union tag = 9


inline bool Declaration::Body::Reader::hasAnnotationDecl() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasAnnotationDecl() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Annotation::Reader Declaration::Body::Reader::getAnnotationDecl() const {
  KJ_IREQUIRE(which() == Body::ANNOTATION_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Annotation>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Annotation::Builder Declaration::Body::Builder::getAnnotationDecl() {
  KJ_IREQUIRE(which() == Body::ANNOTATION_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Annotation>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setAnnotationDecl( ::capnp::compiler::Declaration::Annotation::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ANNOTATION_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Annotation>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Annotation::Builder Declaration::Body::Builder::initAnnotationDecl() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ANNOTATION_DECL);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Annotation>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptAnnotationDecl(
    ::capnp::Orphan< ::capnp::compiler::Declaration::Annotation>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::ANNOTATION_DECL);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Annotation>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::Annotation> Declaration::Body::Builder::disownAnnotationDecl() {
  KJ_IREQUIRE(which() == Body::ANNOTATION_DECL,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::Annotation>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::nakedId@21: .LocatedInteger;  # ptr[3], union tag = 10


inline bool Declaration::Body::Reader::hasNakedId() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasNakedId() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedInteger::Reader Declaration::Body::Reader::getNakedId() const {
  KJ_IREQUIRE(which() == Body::NAKED_ID,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedInteger::Builder Declaration::Body::Builder::getNakedId() {
  KJ_IREQUIRE(which() == Body::NAKED_ID,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setNakedId( ::capnp::compiler::LocatedInteger::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::NAKED_ID);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedInteger::Builder Declaration::Body::Builder::initNakedId() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::NAKED_ID);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptNakedId(
    ::capnp::Orphan< ::capnp::compiler::LocatedInteger>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::NAKED_ID);
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedInteger> Declaration::Body::Builder::disownNakedId() {
  KJ_IREQUIRE(which() == Body::NAKED_ID,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedInteger>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::nakedAnnotation@22: .Declaration.AnnotationApplication;  # ptr[3], union tag = 11


inline bool Declaration::Body::Reader::hasNakedAnnotation() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Body::Builder::hasNakedAnnotation() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::AnnotationApplication::Reader Declaration::Body::Reader::getNakedAnnotation() const {
  KJ_IREQUIRE(which() == Body::NAKED_ANNOTATION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::AnnotationApplication>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::AnnotationApplication::Builder Declaration::Body::Builder::getNakedAnnotation() {
  KJ_IREQUIRE(which() == Body::NAKED_ANNOTATION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::AnnotationApplication>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Body::Builder::setNakedAnnotation( ::capnp::compiler::Declaration::AnnotationApplication::Reader value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::NAKED_ANNOTATION);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::AnnotationApplication>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::AnnotationApplication::Builder Declaration::Body::Builder::initNakedAnnotation() {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::NAKED_ANNOTATION);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::AnnotationApplication>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Body::Builder::adoptNakedAnnotation(
    ::capnp::Orphan< ::capnp::compiler::Declaration::AnnotationApplication>&& value) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::NAKED_ANNOTATION);
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::AnnotationApplication>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration::AnnotationApplication> Declaration::Body::Builder::disownNakedAnnotation() {
  KJ_IREQUIRE(which() == Body::NAKED_ANNOTATION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration::AnnotationApplication>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Body::builtinVoid@25: Void;  # (none), union tag = 14

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinVoid() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_VOID,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinVoid() {
  KJ_IREQUIRE(which() == Body::BUILTIN_VOID,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinVoid( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_VOID);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinBool@26: Void;  # (none), union tag = 15

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinBool() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_BOOL,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinBool() {
  KJ_IREQUIRE(which() == Body::BUILTIN_BOOL,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinBool( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_BOOL);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinInt8@27: Void;  # (none), union tag = 16

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinInt8() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT8,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinInt8() {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT8,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinInt8( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_INT8);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinInt16@28: Void;  # (none), union tag = 17

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinInt16() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT16,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinInt16() {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT16,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinInt16( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_INT16);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinInt32@29: Void;  # (none), union tag = 18

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinInt32() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT32,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinInt32() {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT32,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinInt32( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_INT32);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinInt64@30: Void;  # (none), union tag = 19

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinInt64() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT64,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinInt64() {
  KJ_IREQUIRE(which() == Body::BUILTIN_INT64,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinInt64( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_INT64);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinUInt8@31: Void;  # (none), union tag = 20

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinUInt8() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT8,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinUInt8() {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT8,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinUInt8( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_U_INT8);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinUInt16@32: Void;  # (none), union tag = 21

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinUInt16() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT16,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinUInt16() {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT16,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinUInt16( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_U_INT16);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinUInt32@33: Void;  # (none), union tag = 22

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinUInt32() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT32,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinUInt32() {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT32,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinUInt32( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_U_INT32);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinUInt64@34: Void;  # (none), union tag = 23

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinUInt64() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT64,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinUInt64() {
  KJ_IREQUIRE(which() == Body::BUILTIN_U_INT64,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinUInt64( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_U_INT64);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinFloat32@35: Void;  # (none), union tag = 24

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinFloat32() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_FLOAT32,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinFloat32() {
  KJ_IREQUIRE(which() == Body::BUILTIN_FLOAT32,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinFloat32( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_FLOAT32);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinFloat64@36: Void;  # (none), union tag = 25

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinFloat64() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_FLOAT64,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinFloat64() {
  KJ_IREQUIRE(which() == Body::BUILTIN_FLOAT64,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinFloat64( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_FLOAT64);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinText@37: Void;  # (none), union tag = 26

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinText() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_TEXT,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinText() {
  KJ_IREQUIRE(which() == Body::BUILTIN_TEXT,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinText( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_TEXT);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinData@38: Void;  # (none), union tag = 27

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinData() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_DATA,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinData() {
  KJ_IREQUIRE(which() == Body::BUILTIN_DATA,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinData( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_DATA);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinList@39: Void;  # (none), union tag = 28

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinList() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_LIST,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinList() {
  KJ_IREQUIRE(which() == Body::BUILTIN_LIST,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinList( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_LIST);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Body::builtinObject@40: Void;  # (none), union tag = 29

inline  ::capnp::Void Declaration::Body::Reader::getBuiltinObject() const {
  KJ_IREQUIRE(which() == Body::BUILTIN_OBJECT,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Body::Builder::getBuiltinObject() {
  KJ_IREQUIRE(which() == Body::BUILTIN_OBJECT,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Body::Builder::setBuiltinObject( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      1 * ::capnp::ELEMENTS, Body::BUILTIN_OBJECT);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}




// Declaration::Using::target@0: .DeclName;  # ptr[0]


inline bool Declaration::Using::Reader::hasTarget() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::Using::Builder::hasTarget() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Reader Declaration::Using::Reader::getTarget() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::DeclName::Builder Declaration::Using::Builder::getTarget() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::Using::Builder::setTarget( ::capnp::compiler::DeclName::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::DeclName::Builder Declaration::Using::Builder::initTarget() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Declaration::Using::Builder::adoptTarget(
    ::capnp::Orphan< ::capnp::compiler::DeclName>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::DeclName> Declaration::Using::Builder::disownTarget() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::DeclName>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// Declaration::Const::type@0: .TypeExpression;  # ptr[0]


inline bool Declaration::Const::Reader::hasType() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::Const::Builder::hasType() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Reader Declaration::Const::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Const::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::Const::Builder::setType( ::capnp::compiler::TypeExpression::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Const::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Declaration::Const::Builder::adoptType(
    ::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> Declaration::Const::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Declaration::Const::value@1: .ValueExpression;  # ptr[1]


inline bool Declaration::Const::Reader::hasValue() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Declaration::Const::Builder::hasValue() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Reader Declaration::Const::Reader::getValue() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::Const::Builder::getValue() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Declaration::Const::Builder::setValue( ::capnp::compiler::ValueExpression::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::Const::Builder::initValue() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void Declaration::Const::Builder::adoptValue(
    ::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> Declaration::Const::Builder::disownValue() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::disown(
      _builder, 1 * ::capnp::POINTERS);
}






inline Declaration::Field::DefaultValue::Reader Declaration::Field::Reader::getDefaultValue() const {
  return Declaration::Field::DefaultValue::Reader(_reader);
}

inline Declaration::Field::DefaultValue::Builder Declaration::Field::Builder::getDefaultValue() {
  return Declaration::Field::DefaultValue::Builder(_builder);
}


// Declaration::Field::type@0: .TypeExpression;  # ptr[0]


inline bool Declaration::Field::Reader::hasType() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::Field::Builder::hasType() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Reader Declaration::Field::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Field::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::Field::Builder::setType( ::capnp::compiler::TypeExpression::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Field::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Declaration::Field::Builder::adoptType(
    ::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> Declaration::Field::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Declaration::Field::DefaultValue
inline Declaration::Field::DefaultValue::Which Declaration::Field::DefaultValue::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline Declaration::Field::DefaultValue::Which Declaration::Field::DefaultValue::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// Declaration::Field::DefaultValue::none@2: Void;  # (none), union tag = 0

inline  ::capnp::Void Declaration::Field::DefaultValue::Reader::getNone() const {
  KJ_IREQUIRE(which() == DefaultValue::NONE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Field::DefaultValue::Builder::getNone() {
  KJ_IREQUIRE(which() == DefaultValue::NONE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Field::DefaultValue::Builder::setNone( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::NONE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Field::DefaultValue::value@3: .ValueExpression;  # ptr[1], union tag = 1


inline bool Declaration::Field::DefaultValue::Reader::hasValue() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Declaration::Field::DefaultValue::Builder::hasValue() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Reader Declaration::Field::DefaultValue::Reader::getValue() const {
  KJ_IREQUIRE(which() == DefaultValue::VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::Field::DefaultValue::Builder::getValue() {
  KJ_IREQUIRE(which() == DefaultValue::VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Declaration::Field::DefaultValue::Builder::setValue( ::capnp::compiler::ValueExpression::Reader value) {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::VALUE);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::Field::DefaultValue::Builder::initValue() {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::VALUE);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void Declaration::Field::DefaultValue::Builder::adoptValue(
    ::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value) {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::VALUE);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> Declaration::Field::DefaultValue::Builder::disownValue() {
  KJ_IREQUIRE(which() == DefaultValue::VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::disown(
      _builder, 1 * ::capnp::POINTERS);
}






inline Declaration::Method::ReturnType::Reader Declaration::Method::Reader::getReturnType() const {
  return Declaration::Method::ReturnType::Reader(_reader);
}

inline Declaration::Method::ReturnType::Builder Declaration::Method::Builder::getReturnType() {
  return Declaration::Method::ReturnType::Builder(_builder);
}


// Declaration::Method::params@0: List(.Declaration.Method.Param);  # ptr[0]


inline bool Declaration::Method::Reader::hasParams() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::Method::Builder::hasParams() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Reader Declaration::Method::Reader::getParams() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Builder Declaration::Method::Builder::getParams() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::Method::Builder::setParams( ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::Method::Param>::Builder Declaration::Method::Builder::initParams(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void Declaration::Method::Builder::adoptParams(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>> Declaration::Method::Builder::disownParams() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::Method::Param>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



inline Declaration::Method::Param::DefaultValue::Reader Declaration::Method::Param::Reader::getDefaultValue() const {
  return Declaration::Method::Param::DefaultValue::Reader(_reader);
}

inline Declaration::Method::Param::DefaultValue::Builder Declaration::Method::Param::Builder::getDefaultValue() {
  return Declaration::Method::Param::DefaultValue::Builder(_builder);
}


// Declaration::Method::Param::name@0: .LocatedText;  # ptr[0]


inline bool Declaration::Method::Param::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::Method::Param::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Reader Declaration::Method::Param::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::LocatedText::Builder Declaration::Method::Param::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::Method::Param::Builder::setName( ::capnp::compiler::LocatedText::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::LocatedText::Builder Declaration::Method::Param::Builder::initName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Declaration::Method::Param::Builder::adoptName(
    ::capnp::Orphan< ::capnp::compiler::LocatedText>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::LocatedText> Declaration::Method::Param::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::LocatedText>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Declaration::Method::Param::type@1: .TypeExpression;  # ptr[1]


inline bool Declaration::Method::Param::Reader::hasType() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Declaration::Method::Param::Builder::hasType() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Reader Declaration::Method::Param::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Method::Param::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Declaration::Method::Param::Builder::setType( ::capnp::compiler::TypeExpression::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Method::Param::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void Declaration::Method::Param::Builder::adoptType(
    ::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> Declaration::Method::Param::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// Declaration::Method::Param::annotations@2: List(.Declaration.AnnotationApplication);  # ptr[2]


inline bool Declaration::Method::Param::Reader::hasAnnotations() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool Declaration::Method::Param::Builder::hasAnnotations() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader Declaration::Method::Param::Reader::getAnnotations() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder Declaration::Method::Param::Builder::getAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void Declaration::Method::Param::Builder::setAnnotations( ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>::Builder Declaration::Method::Param::Builder::initAnnotations(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::init(
      _builder, 2 * ::capnp::POINTERS, size);
}


inline void Declaration::Method::Param::Builder::adoptAnnotations(
    ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>> Declaration::Method::Param::Builder::disownAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::compiler::Declaration::AnnotationApplication>>::disown(
      _builder, 2 * ::capnp::POINTERS);
}



// Declaration::Method::Param::DefaultValue
inline Declaration::Method::Param::DefaultValue::Which Declaration::Method::Param::DefaultValue::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline Declaration::Method::Param::DefaultValue::Which Declaration::Method::Param::DefaultValue::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// Declaration::Method::Param::DefaultValue::none@4: Void;  # (none), union tag = 0

inline  ::capnp::Void Declaration::Method::Param::DefaultValue::Reader::getNone() const {
  KJ_IREQUIRE(which() == DefaultValue::NONE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Method::Param::DefaultValue::Builder::getNone() {
  KJ_IREQUIRE(which() == DefaultValue::NONE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Method::Param::DefaultValue::Builder::setNone( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::NONE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Method::Param::DefaultValue::value@5: .ValueExpression;  # ptr[3], union tag = 1


inline bool Declaration::Method::Param::DefaultValue::Reader::hasValue() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Declaration::Method::Param::DefaultValue::Builder::hasValue() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Reader Declaration::Method::Param::DefaultValue::Reader::getValue() const {
  KJ_IREQUIRE(which() == DefaultValue::VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::Method::Param::DefaultValue::Builder::getValue() {
  KJ_IREQUIRE(which() == DefaultValue::VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Declaration::Method::Param::DefaultValue::Builder::setValue( ::capnp::compiler::ValueExpression::Reader value) {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::VALUE);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::ValueExpression::Builder Declaration::Method::Param::DefaultValue::Builder::initValue() {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::VALUE);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Declaration::Method::Param::DefaultValue::Builder::adoptValue(
    ::capnp::Orphan< ::capnp::compiler::ValueExpression>&& value) {
  _builder.setDataField<DefaultValue::Which>(
      0 * ::capnp::ELEMENTS, DefaultValue::VALUE);
  ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::ValueExpression> Declaration::Method::Param::DefaultValue::Builder::disownValue() {
  KJ_IREQUIRE(which() == DefaultValue::VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::ValueExpression>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Declaration::Method::ReturnType
inline Declaration::Method::ReturnType::Which Declaration::Method::ReturnType::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline Declaration::Method::ReturnType::Which Declaration::Method::ReturnType::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// Declaration::Method::ReturnType::none@2: Void;  # (none), union tag = 0

inline  ::capnp::Void Declaration::Method::ReturnType::Reader::getNone() const {
  KJ_IREQUIRE(which() == ReturnType::NONE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Declaration::Method::ReturnType::Builder::getNone() {
  KJ_IREQUIRE(which() == ReturnType::NONE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Method::ReturnType::Builder::setNone( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<ReturnType::Which>(
      0 * ::capnp::ELEMENTS, ReturnType::NONE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Method::ReturnType::expression@3: .TypeExpression;  # ptr[1], union tag = 1


inline bool Declaration::Method::ReturnType::Reader::hasExpression() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Declaration::Method::ReturnType::Builder::hasExpression() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Reader Declaration::Method::ReturnType::Reader::getExpression() const {
  KJ_IREQUIRE(which() == ReturnType::EXPRESSION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Method::ReturnType::Builder::getExpression() {
  KJ_IREQUIRE(which() == ReturnType::EXPRESSION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Declaration::Method::ReturnType::Builder::setExpression( ::capnp::compiler::TypeExpression::Reader value) {
  _builder.setDataField<ReturnType::Which>(
      0 * ::capnp::ELEMENTS, ReturnType::EXPRESSION);
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Method::ReturnType::Builder::initExpression() {
  _builder.setDataField<ReturnType::Which>(
      0 * ::capnp::ELEMENTS, ReturnType::EXPRESSION);
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void Declaration::Method::ReturnType::Builder::adoptExpression(
    ::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value) {
  _builder.setDataField<ReturnType::Which>(
      0 * ::capnp::ELEMENTS, ReturnType::EXPRESSION);
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> Declaration::Method::ReturnType::Builder::disownExpression() {
  KJ_IREQUIRE(which() == ReturnType::EXPRESSION,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::disown(
      _builder, 1 * ::capnp::POINTERS);
}




// Declaration::Annotation::type@0: .TypeExpression;  # ptr[0]


inline bool Declaration::Annotation::Reader::hasType() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Declaration::Annotation::Builder::hasType() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Reader Declaration::Annotation::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Annotation::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Declaration::Annotation::Builder::setType( ::capnp::compiler::TypeExpression::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::TypeExpression::Builder Declaration::Annotation::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Declaration::Annotation::Builder::adoptType(
    ::capnp::Orphan< ::capnp::compiler::TypeExpression>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::TypeExpression> Declaration::Annotation::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::TypeExpression>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Declaration::Annotation::targetsFile@1: Bool;  # bits[0, 1)

inline bool Declaration::Annotation::Reader::getTargetsFile() const {
  return _reader.getDataField<bool>(
      0 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsFile() {
  return _builder.getDataField<bool>(
      0 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsFile(bool value) {
  _builder.setDataField<bool>(
      0 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsConst@2: Bool;  # bits[1, 2)

inline bool Declaration::Annotation::Reader::getTargetsConst() const {
  return _reader.getDataField<bool>(
      1 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsConst() {
  return _builder.getDataField<bool>(
      1 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsConst(bool value) {
  _builder.setDataField<bool>(
      1 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsEnum@3: Bool;  # bits[2, 3)

inline bool Declaration::Annotation::Reader::getTargetsEnum() const {
  return _reader.getDataField<bool>(
      2 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsEnum() {
  return _builder.getDataField<bool>(
      2 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsEnum(bool value) {
  _builder.setDataField<bool>(
      2 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsEnumerant@4: Bool;  # bits[3, 4)

inline bool Declaration::Annotation::Reader::getTargetsEnumerant() const {
  return _reader.getDataField<bool>(
      3 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsEnumerant() {
  return _builder.getDataField<bool>(
      3 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsEnumerant(bool value) {
  _builder.setDataField<bool>(
      3 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsStruct@5: Bool;  # bits[4, 5)

inline bool Declaration::Annotation::Reader::getTargetsStruct() const {
  return _reader.getDataField<bool>(
      4 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsStruct() {
  return _builder.getDataField<bool>(
      4 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsStruct(bool value) {
  _builder.setDataField<bool>(
      4 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsField@6: Bool;  # bits[5, 6)

inline bool Declaration::Annotation::Reader::getTargetsField() const {
  return _reader.getDataField<bool>(
      5 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsField() {
  return _builder.getDataField<bool>(
      5 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsField(bool value) {
  _builder.setDataField<bool>(
      5 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsUnion@7: Bool;  # bits[6, 7)

inline bool Declaration::Annotation::Reader::getTargetsUnion() const {
  return _reader.getDataField<bool>(
      6 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsUnion() {
  return _builder.getDataField<bool>(
      6 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsUnion(bool value) {
  _builder.setDataField<bool>(
      6 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsInterface@8: Bool;  # bits[7, 8)

inline bool Declaration::Annotation::Reader::getTargetsInterface() const {
  return _reader.getDataField<bool>(
      7 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsInterface() {
  return _builder.getDataField<bool>(
      7 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsInterface(bool value) {
  _builder.setDataField<bool>(
      7 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsMethod@9: Bool;  # bits[8, 9)

inline bool Declaration::Annotation::Reader::getTargetsMethod() const {
  return _reader.getDataField<bool>(
      8 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsMethod() {
  return _builder.getDataField<bool>(
      8 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsMethod(bool value) {
  _builder.setDataField<bool>(
      8 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsParam@10: Bool;  # bits[9, 10)

inline bool Declaration::Annotation::Reader::getTargetsParam() const {
  return _reader.getDataField<bool>(
      9 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsParam() {
  return _builder.getDataField<bool>(
      9 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsParam(bool value) {
  _builder.setDataField<bool>(
      9 * ::capnp::ELEMENTS, value);
}


// Declaration::Annotation::targetsAnnotation@11: Bool;  # bits[10, 11)

inline bool Declaration::Annotation::Reader::getTargetsAnnotation() const {
  return _reader.getDataField<bool>(
      10 * ::capnp::ELEMENTS);
}

inline bool Declaration::Annotation::Builder::getTargetsAnnotation() {
  return _builder.getDataField<bool>(
      10 * ::capnp::ELEMENTS);
}
inline void Declaration::Annotation::Builder::setTargetsAnnotation(bool value) {
  _builder.setDataField<bool>(
      10 * ::capnp::ELEMENTS, value);
}



// ParsedFile::root@0: .Declaration;  # ptr[0]


inline bool ParsedFile::Reader::hasRoot() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ParsedFile::Builder::hasRoot() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Reader ParsedFile::Reader::getRoot() const {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::compiler::Declaration::Builder ParsedFile::Builder::getRoot() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ParsedFile::Builder::setRoot( ::capnp::compiler::Declaration::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::compiler::Declaration::Builder ParsedFile::Builder::initRoot() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void ParsedFile::Builder::adoptRoot(
    ::capnp::Orphan< ::capnp::compiler::Declaration>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::compiler::Declaration> ParsedFile::Builder::disownRoot() {
  return ::capnp::_::PointerHelpers< ::capnp::compiler::Declaration>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




}  // namespace
}  // namespace
#endif  // CAPNP_INCLUDED_1ad763faa64eb47f39f6241af24b4471
