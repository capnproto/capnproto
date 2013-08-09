// Generated code, DO NOT EDIT

#ifndef CAPNP_INCLUDED_c9e00273422ad7b91727f5b3ca1e8eb2
#define CAPNP_INCLUDED_c9e00273422ad7b91727f5b3ca1e8eb2

#include <capnp/generated-header-support.h>

namespace capnp {
namespace schema {


struct Node {
  Node() = delete;

  class Reader;
  class Builder;
  struct NestedNode;
  struct Body;

private:
};

struct Node::NestedNode {
  NestedNode() = delete;

  class Reader;
  class Builder;

private:
};

struct Node::Body {
  Body() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    FILE_NODE = 0,
    STRUCT_NODE = 1,
    ENUM_NODE = 2,
    INTERFACE_NODE = 3,
    CONST_NODE = 4,
    ANNOTATION_NODE = 5,
  };

private:
};

struct Type {
  Type() = delete;

  class Reader;
  class Builder;
  struct Body;

private:
};

struct Type::Body {
  Body() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    VOID_TYPE = 0,
    BOOL_TYPE = 1,
    INT8_TYPE = 2,
    INT16_TYPE = 3,
    INT32_TYPE = 4,
    INT64_TYPE = 5,
    UINT8_TYPE = 6,
    UINT16_TYPE = 7,
    UINT32_TYPE = 8,
    UINT64_TYPE = 9,
    FLOAT32_TYPE = 10,
    FLOAT64_TYPE = 11,
    TEXT_TYPE = 12,
    DATA_TYPE = 13,
    LIST_TYPE = 14,
    ENUM_TYPE = 15,
    STRUCT_TYPE = 16,
    INTERFACE_TYPE = 17,
    OBJECT_TYPE = 18,
  };

private:
};

struct Value {
  Value() = delete;

  class Reader;
  class Builder;
  struct Body;

private:
};

struct Value::Body {
  Body() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    VOID_VALUE = 9,
    BOOL_VALUE = 1,
    INT8_VALUE = 2,
    INT16_VALUE = 3,
    INT32_VALUE = 4,
    INT64_VALUE = 5,
    UINT8_VALUE = 6,
    UINT16_VALUE = 7,
    UINT32_VALUE = 8,
    UINT64_VALUE = 0,
    FLOAT32_VALUE = 10,
    FLOAT64_VALUE = 11,
    TEXT_VALUE = 12,
    DATA_VALUE = 13,
    LIST_VALUE = 14,
    ENUM_VALUE = 15,
    STRUCT_VALUE = 16,
    INTERFACE_VALUE = 17,
    OBJECT_VALUE = 18,
  };

private:
};

struct Annotation {
  Annotation() = delete;

  class Reader;
  class Builder;

private:
};

struct FileNode {
  FileNode() = delete;

  class Reader;
  class Builder;
  struct Import;

private:
};

struct FileNode::Import {
  Import() = delete;

  class Reader;
  class Builder;

private:
};

struct StructNode {
  StructNode() = delete;

  class Reader;
  class Builder;
  struct Member;
  struct Field;
  struct Union;
  struct Group;

private:
};

struct StructNode::Member {
  Member() = delete;

  class Reader;
  class Builder;
  struct Body;

private:
};

struct StructNode::Member::Body {
  Body() = delete;

  class Reader;
  class Builder;

  enum Which: uint16_t {
    FIELD_MEMBER = 0,
    UNION_MEMBER = 1,
    GROUP_MEMBER = 2,
  };

private:
};

struct StructNode::Field {
  Field() = delete;

  class Reader;
  class Builder;

private:
};

struct StructNode::Union {
  Union() = delete;

  class Reader;
  class Builder;

private:
};

struct StructNode::Group {
  Group() = delete;

  class Reader;
  class Builder;

private:
};

struct EnumNode {
  EnumNode() = delete;

  class Reader;
  class Builder;
  struct Enumerant;

private:
};

struct EnumNode::Enumerant {
  Enumerant() = delete;

  class Reader;
  class Builder;

private:
};

struct InterfaceNode {
  InterfaceNode() = delete;

  class Reader;
  class Builder;
  struct Method;

private:
};

struct InterfaceNode::Method {
  Method() = delete;

  class Reader;
  class Builder;
  struct Param;

private:
};

struct InterfaceNode::Method::Param {
  Param() = delete;

  class Reader;
  class Builder;

private:
};

struct ConstNode {
  ConstNode() = delete;

  class Reader;
  class Builder;

private:
};

struct AnnotationNode {
  AnnotationNode() = delete;

  class Reader;
  class Builder;

private:
};

struct CodeGeneratorRequest {
  CodeGeneratorRequest() = delete;

  class Reader;
  class Builder;

private:
};


enum class ElementSize: uint16_t {
  EMPTY = 0,
  BIT = 1,
  BYTE = 2,
  TWO_BYTES = 3,
  FOUR_BYTES = 4,
  EIGHT_BYTES = 5,
  POINTER = 6,
  INLINE_COMPOSITE = 7,
};


}  // namespace
}  // namespace

namespace capnp {
namespace schemas {
extern const ::capnp::_::RawSchema s_96ae9bbee664c195;
extern const ::capnp::_::RawSchema s_adfe8e889429ee28;
extern const ::capnp::_::RawSchema s_dddca9a9ee299e42;
extern const ::capnp::_::RawSchema s_c2c768aee22269ee;
extern const ::capnp::_::RawSchema s_db785131c0cfee73;
extern const ::capnp::_::RawSchema s_d59c380b31b76b1f;
extern const ::capnp::_::RawSchema s_d5d6a9044d63c158;
extern const ::capnp::_::RawSchema s_d7326bd22e1c298c;
extern const ::capnp::_::RawSchema s_bf81d92a0b7e0c1f;
extern const ::capnp::_::RawSchema s_9a2db4bd6b74f8c1;
extern const ::capnp::_::RawSchema s_c75846e17057a41f;
extern const ::capnp::_::RawSchema s_efff479ae161da06;
extern const ::capnp::_::RawSchema s_ac91947f51b055ed;
extern const ::capnp::_::RawSchema s_d612f44d78962abf;
extern const ::capnp::_::RawSchema s_c9ac441973b9f177;
extern const ::capnp::_::RawSchema s_b8a6ecfa2d5121e6;
extern const ::capnp::_::RawSchema s_bdd7f6f0832387ac;
extern const ::capnp::_::RawSchema s_d6d38cf4e366e91c;
extern const ::capnp::_::RawSchema s_8f0cf892b24a8062;
extern const ::capnp::_::RawSchema s_f386f41ae9f5cbe5;
extern const ::capnp::_::RawSchema s_d095654a26e15f1d;
}  // namespace schemas
namespace _ {  // private
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::Node, 96ae9bbee664c195,
    3, 4, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::schema::Node::Body,
    ::capnp::schema::Node, 5);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::Node::NestedNode, adfe8e889429ee28,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::Type, dddca9a9ee299e42,
    2, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::schema::Type::Body,
    ::capnp::schema::Type, 0);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::Value, c2c768aee22269ee,
    2, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::schema::Value::Body,
    ::capnp::schema::Value, 0);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::Annotation, db785131c0cfee73,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::FileNode, d59c380b31b76b1f,
    0, 1, POINTER);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::FileNode::Import, d5d6a9044d63c158,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::StructNode, bf81d92a0b7e0c1f,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::StructNode::Member, 9a2db4bd6b74f8c1,
    1, 3, INLINE_COMPOSITE);
CAPNP_DECLARE_UNION(
    ::capnp::schema::StructNode::Member::Body,
    ::capnp::schema::StructNode::Member, 4);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::StructNode::Field, c75846e17057a41f,
    1, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::StructNode::Union, efff479ae161da06,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::StructNode::Group, ac91947f51b055ed,
    0, 1, POINTER);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::EnumNode, d612f44d78962abf,
    0, 1, POINTER);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::EnumNode::Enumerant, c9ac441973b9f177,
    1, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::InterfaceNode, b8a6ecfa2d5121e6,
    0, 1, POINTER);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::InterfaceNode::Method, bdd7f6f0832387ac,
    1, 4, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::InterfaceNode::Method::Param, d6d38cf4e366e91c,
    0, 4, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::ConstNode, 8f0cf892b24a8062,
    0, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::AnnotationNode, f386f41ae9f5cbe5,
    1, 1, INLINE_COMPOSITE);
CAPNP_DECLARE_STRUCT(
    ::capnp::schema::CodeGeneratorRequest, d095654a26e15f1d,
    0, 2, INLINE_COMPOSITE);
CAPNP_DECLARE_ENUM(
    ::capnp::schema::ElementSize, d7326bd22e1c298c);
}  // namespace capnp
}  // namespace _ (private)

namespace capnp {
namespace schema {



class Node::Reader {
public:
  typedef Node Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union body@5 {  # [128, 144)
  inline Body::Reader getBody() const;

  // id@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId() const;

  // displayName@1: Text;  # ptr[0]
  inline bool hasDisplayName() const;
  inline  ::capnp::Text::Reader getDisplayName() const;

  // displayNamePrefixLength@12: UInt32;  # bits[160, 192)
  inline  ::uint32_t getDisplayNamePrefixLength() const;

  // scopeId@2: UInt64 = 0;  # bits[64, 128)
  inline  ::uint64_t getScopeId() const;

  // nestedNodes@3: List(.Node.NestedNode);  # ptr[1]
  inline bool hasNestedNodes() const;
  inline  ::capnp::List< ::capnp::schema::Node::NestedNode>::Reader getNestedNodes() const;

  // annotations@4: List(.Annotation);  # ptr[2]
  inline bool hasAnnotations() const;
  inline  ::capnp::List< ::capnp::schema::Annotation>::Reader getAnnotations() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Node::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Node::Reader reader) {
  return ::capnp::_::structString<Node>(reader._reader);
}



class Node::Builder {
public:
  typedef Node Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union body@5 {  # [128, 144)
  inline Body::Builder getBody();

  // id@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId();
  inline void setId( ::uint64_t value);

  // displayName@1: Text;  # ptr[0]
  inline bool hasDisplayName();
  inline  ::capnp::Text::Builder getDisplayName();
  inline void setDisplayName( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initDisplayName(unsigned int size);
  inline void adoptDisplayName(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownDisplayName();

  // displayNamePrefixLength@12: UInt32;  # bits[160, 192)
  inline  ::uint32_t getDisplayNamePrefixLength();
  inline void setDisplayNamePrefixLength( ::uint32_t value);

  // scopeId@2: UInt64 = 0;  # bits[64, 128)
  inline  ::uint64_t getScopeId();
  inline void setScopeId( ::uint64_t value);

  // nestedNodes@3: List(.Node.NestedNode);  # ptr[1]
  inline bool hasNestedNodes();
  inline  ::capnp::List< ::capnp::schema::Node::NestedNode>::Builder getNestedNodes();
  inline void setNestedNodes( ::capnp::List< ::capnp::schema::Node::NestedNode>::Reader other);
  inline  ::capnp::List< ::capnp::schema::Node::NestedNode>::Builder initNestedNodes(unsigned int size);
  inline void adoptNestedNodes(::capnp::Orphan< ::capnp::List< ::capnp::schema::Node::NestedNode>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Node::NestedNode>> disownNestedNodes();

  // annotations@4: List(.Annotation);  # ptr[2]
  inline bool hasAnnotations();
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder getAnnotations();
  inline void setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader other);
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder initAnnotations(unsigned int size);
  inline void adoptAnnotations(::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> disownAnnotations();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Node::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Node::Builder builder) {
  return ::capnp::_::structString<Node>(builder._builder.asReader());
}

class Node::NestedNode::Reader {
public:
  typedef NestedNode Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // name@0: Text;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::Text::Reader getName() const;

  // id@1: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Node::NestedNode::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Node::NestedNode::Reader reader) {
  return ::capnp::_::structString<Node::NestedNode>(reader._reader);
}



class Node::NestedNode::Builder {
public:
  typedef NestedNode Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // name@0: Text;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::Text::Builder getName();
  inline void setName( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initName(unsigned int size);
  inline void adoptName(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownName();

  // id@1: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId();
  inline void setId( ::uint64_t value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Node::NestedNode::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Node::NestedNode::Builder builder) {
  return ::capnp::_::structString<Node::NestedNode>(builder._builder.asReader());
}

class Node::Body::Reader {
public:
  typedef Body Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // fileNode@6: .FileNode;  # ptr[3], union tag = 0
  inline bool hasFileNode() const;
  inline  ::capnp::schema::FileNode::Reader getFileNode() const;

  // structNode@7: .StructNode;  # ptr[3], union tag = 1
  inline bool hasStructNode() const;
  inline  ::capnp::schema::StructNode::Reader getStructNode() const;

  // enumNode@8: .EnumNode;  # ptr[3], union tag = 2
  inline bool hasEnumNode() const;
  inline  ::capnp::schema::EnumNode::Reader getEnumNode() const;

  // interfaceNode@9: .InterfaceNode;  # ptr[3], union tag = 3
  inline bool hasInterfaceNode() const;
  inline  ::capnp::schema::InterfaceNode::Reader getInterfaceNode() const;

  // constNode@10: .ConstNode;  # ptr[3], union tag = 4
  inline bool hasConstNode() const;
  inline  ::capnp::schema::ConstNode::Reader getConstNode() const;

  // annotationNode@11: .AnnotationNode;  # ptr[3], union tag = 5
  inline bool hasAnnotationNode() const;
  inline  ::capnp::schema::AnnotationNode::Reader getAnnotationNode() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Node::Body::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Node::Body::Reader reader) {
  return ::capnp::_::unionString<Node::Body>(reader._reader);
}



class Node::Body::Builder {
public:
  typedef Body Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // fileNode@6: .FileNode;  # ptr[3], union tag = 0
  inline bool hasFileNode();
  inline  ::capnp::schema::FileNode::Builder getFileNode();
  inline void setFileNode( ::capnp::schema::FileNode::Reader other);
  inline  ::capnp::schema::FileNode::Builder initFileNode();
  inline void adoptFileNode(::capnp::Orphan< ::capnp::schema::FileNode>&& value);
  inline ::capnp::Orphan< ::capnp::schema::FileNode> disownFileNode();

  // structNode@7: .StructNode;  # ptr[3], union tag = 1
  inline bool hasStructNode();
  inline  ::capnp::schema::StructNode::Builder getStructNode();
  inline void setStructNode( ::capnp::schema::StructNode::Reader other);
  inline  ::capnp::schema::StructNode::Builder initStructNode();
  inline void adoptStructNode(::capnp::Orphan< ::capnp::schema::StructNode>&& value);
  inline ::capnp::Orphan< ::capnp::schema::StructNode> disownStructNode();

  // enumNode@8: .EnumNode;  # ptr[3], union tag = 2
  inline bool hasEnumNode();
  inline  ::capnp::schema::EnumNode::Builder getEnumNode();
  inline void setEnumNode( ::capnp::schema::EnumNode::Reader other);
  inline  ::capnp::schema::EnumNode::Builder initEnumNode();
  inline void adoptEnumNode(::capnp::Orphan< ::capnp::schema::EnumNode>&& value);
  inline ::capnp::Orphan< ::capnp::schema::EnumNode> disownEnumNode();

  // interfaceNode@9: .InterfaceNode;  # ptr[3], union tag = 3
  inline bool hasInterfaceNode();
  inline  ::capnp::schema::InterfaceNode::Builder getInterfaceNode();
  inline void setInterfaceNode( ::capnp::schema::InterfaceNode::Reader other);
  inline  ::capnp::schema::InterfaceNode::Builder initInterfaceNode();
  inline void adoptInterfaceNode(::capnp::Orphan< ::capnp::schema::InterfaceNode>&& value);
  inline ::capnp::Orphan< ::capnp::schema::InterfaceNode> disownInterfaceNode();

  // constNode@10: .ConstNode;  # ptr[3], union tag = 4
  inline bool hasConstNode();
  inline  ::capnp::schema::ConstNode::Builder getConstNode();
  inline void setConstNode( ::capnp::schema::ConstNode::Reader other);
  inline  ::capnp::schema::ConstNode::Builder initConstNode();
  inline void adoptConstNode(::capnp::Orphan< ::capnp::schema::ConstNode>&& value);
  inline ::capnp::Orphan< ::capnp::schema::ConstNode> disownConstNode();

  // annotationNode@11: .AnnotationNode;  # ptr[3], union tag = 5
  inline bool hasAnnotationNode();
  inline  ::capnp::schema::AnnotationNode::Builder getAnnotationNode();
  inline void setAnnotationNode( ::capnp::schema::AnnotationNode::Reader other);
  inline  ::capnp::schema::AnnotationNode::Builder initAnnotationNode();
  inline void adoptAnnotationNode(::capnp::Orphan< ::capnp::schema::AnnotationNode>&& value);
  inline ::capnp::Orphan< ::capnp::schema::AnnotationNode> disownAnnotationNode();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Node::Body::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Node::Body::Builder builder) {
  return ::capnp::_::unionString<Node::Body>(builder._builder.asReader());
}

class Type::Reader {
public:
  typedef Type Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union body@0 {  # [0, 16)
  inline Body::Reader getBody() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Type::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Type::Reader reader) {
  return ::capnp::_::structString<Type>(reader._reader);
}



class Type::Builder {
public:
  typedef Type Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union body@0 {  # [0, 16)
  inline Body::Builder getBody();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Type::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Type::Builder builder) {
  return ::capnp::_::structString<Type>(builder._builder.asReader());
}

class Type::Body::Reader {
public:
  typedef Body Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // voidType@1: Void;  # (none), union tag = 0
  inline  ::capnp::Void getVoidType() const;

  // boolType@2: Void;  # (none), union tag = 1
  inline  ::capnp::Void getBoolType() const;

  // int8Type@3: Void;  # (none), union tag = 2
  inline  ::capnp::Void getInt8Type() const;

  // int16Type@4: Void;  # (none), union tag = 3
  inline  ::capnp::Void getInt16Type() const;

  // int32Type@5: Void;  # (none), union tag = 4
  inline  ::capnp::Void getInt32Type() const;

  // int64Type@6: Void;  # (none), union tag = 5
  inline  ::capnp::Void getInt64Type() const;

  // uint8Type@7: Void;  # (none), union tag = 6
  inline  ::capnp::Void getUint8Type() const;

  // uint16Type@8: Void;  # (none), union tag = 7
  inline  ::capnp::Void getUint16Type() const;

  // uint32Type@9: Void;  # (none), union tag = 8
  inline  ::capnp::Void getUint32Type() const;

  // uint64Type@10: Void;  # (none), union tag = 9
  inline  ::capnp::Void getUint64Type() const;

  // float32Type@11: Void;  # (none), union tag = 10
  inline  ::capnp::Void getFloat32Type() const;

  // float64Type@12: Void;  # (none), union tag = 11
  inline  ::capnp::Void getFloat64Type() const;

  // textType@13: Void;  # (none), union tag = 12
  inline  ::capnp::Void getTextType() const;

  // dataType@14: Void;  # (none), union tag = 13
  inline  ::capnp::Void getDataType() const;

  // listType@15: .Type;  # ptr[0], union tag = 14
  inline bool hasListType() const;
  inline  ::capnp::schema::Type::Reader getListType() const;

  // enumType@16: UInt64;  # bits[64, 128), union tag = 15
  inline  ::uint64_t getEnumType() const;

  // structType@17: UInt64;  # bits[64, 128), union tag = 16
  inline  ::uint64_t getStructType() const;

  // interfaceType@18: UInt64;  # bits[64, 128), union tag = 17
  inline  ::uint64_t getInterfaceType() const;

  // objectType@19: Void;  # (none), union tag = 18
  inline  ::capnp::Void getObjectType() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Type::Body::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Type::Body::Reader reader) {
  return ::capnp::_::unionString<Type::Body>(reader._reader);
}



class Type::Body::Builder {
public:
  typedef Body Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // voidType@1: Void;  # (none), union tag = 0
  inline  ::capnp::Void getVoidType();
  inline void setVoidType( ::capnp::Void value);

  // boolType@2: Void;  # (none), union tag = 1
  inline  ::capnp::Void getBoolType();
  inline void setBoolType( ::capnp::Void value);

  // int8Type@3: Void;  # (none), union tag = 2
  inline  ::capnp::Void getInt8Type();
  inline void setInt8Type( ::capnp::Void value);

  // int16Type@4: Void;  # (none), union tag = 3
  inline  ::capnp::Void getInt16Type();
  inline void setInt16Type( ::capnp::Void value);

  // int32Type@5: Void;  # (none), union tag = 4
  inline  ::capnp::Void getInt32Type();
  inline void setInt32Type( ::capnp::Void value);

  // int64Type@6: Void;  # (none), union tag = 5
  inline  ::capnp::Void getInt64Type();
  inline void setInt64Type( ::capnp::Void value);

  // uint8Type@7: Void;  # (none), union tag = 6
  inline  ::capnp::Void getUint8Type();
  inline void setUint8Type( ::capnp::Void value);

  // uint16Type@8: Void;  # (none), union tag = 7
  inline  ::capnp::Void getUint16Type();
  inline void setUint16Type( ::capnp::Void value);

  // uint32Type@9: Void;  # (none), union tag = 8
  inline  ::capnp::Void getUint32Type();
  inline void setUint32Type( ::capnp::Void value);

  // uint64Type@10: Void;  # (none), union tag = 9
  inline  ::capnp::Void getUint64Type();
  inline void setUint64Type( ::capnp::Void value);

  // float32Type@11: Void;  # (none), union tag = 10
  inline  ::capnp::Void getFloat32Type();
  inline void setFloat32Type( ::capnp::Void value);

  // float64Type@12: Void;  # (none), union tag = 11
  inline  ::capnp::Void getFloat64Type();
  inline void setFloat64Type( ::capnp::Void value);

  // textType@13: Void;  # (none), union tag = 12
  inline  ::capnp::Void getTextType();
  inline void setTextType( ::capnp::Void value);

  // dataType@14: Void;  # (none), union tag = 13
  inline  ::capnp::Void getDataType();
  inline void setDataType( ::capnp::Void value);

  // listType@15: .Type;  # ptr[0], union tag = 14
  inline bool hasListType();
  inline  ::capnp::schema::Type::Builder getListType();
  inline void setListType( ::capnp::schema::Type::Reader other);
  inline  ::capnp::schema::Type::Builder initListType();
  inline void adoptListType(::capnp::Orphan< ::capnp::schema::Type>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Type> disownListType();

  // enumType@16: UInt64;  # bits[64, 128), union tag = 15
  inline  ::uint64_t getEnumType();
  inline void setEnumType( ::uint64_t value);

  // structType@17: UInt64;  # bits[64, 128), union tag = 16
  inline  ::uint64_t getStructType();
  inline void setStructType( ::uint64_t value);

  // interfaceType@18: UInt64;  # bits[64, 128), union tag = 17
  inline  ::uint64_t getInterfaceType();
  inline void setInterfaceType( ::uint64_t value);

  // objectType@19: Void;  # (none), union tag = 18
  inline  ::capnp::Void getObjectType();
  inline void setObjectType( ::capnp::Void value);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Type::Body::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Type::Body::Builder builder) {
  return ::capnp::_::unionString<Type::Body>(builder._builder.asReader());
}

class Value::Reader {
public:
  typedef Value Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union body@0 {  # [0, 16)
  inline Body::Reader getBody() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Value::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Value::Reader reader) {
  return ::capnp::_::structString<Value>(reader._reader);
}



class Value::Builder {
public:
  typedef Value Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union body@0 {  # [0, 16)
  inline Body::Builder getBody();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Value::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Value::Builder builder) {
  return ::capnp::_::structString<Value>(builder._builder.asReader());
}

class Value::Body::Reader {
public:
  typedef Body Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // voidValue@10: Void;  # (none), union tag = 9
  inline  ::capnp::Void getVoidValue() const;

  // boolValue@2: Bool;  # bits[64, 65), union tag = 1
  inline bool getBoolValue() const;

  // int8Value@3: Int8;  # bits[64, 72), union tag = 2
  inline  ::int8_t getInt8Value() const;

  // int16Value@4: Int16;  # bits[64, 80), union tag = 3
  inline  ::int16_t getInt16Value() const;

  // int32Value@5: Int32;  # bits[64, 96), union tag = 4
  inline  ::int32_t getInt32Value() const;

  // int64Value@6: Int64;  # bits[64, 128), union tag = 5
  inline  ::int64_t getInt64Value() const;

  // uint8Value@7: UInt8;  # bits[64, 72), union tag = 6
  inline  ::uint8_t getUint8Value() const;

  // uint16Value@8: UInt16;  # bits[64, 80), union tag = 7
  inline  ::uint16_t getUint16Value() const;

  // uint32Value@9: UInt32;  # bits[64, 96), union tag = 8
  inline  ::uint32_t getUint32Value() const;

  // uint64Value@1: UInt64;  # bits[64, 128), union tag = 0
  inline  ::uint64_t getUint64Value() const;

  // float32Value@11: Float32;  # bits[64, 96), union tag = 10
  inline float getFloat32Value() const;

  // float64Value@12: Float64;  # bits[64, 128), union tag = 11
  inline double getFloat64Value() const;

  // textValue@13: Text;  # ptr[0], union tag = 12
  inline bool hasTextValue() const;
  inline  ::capnp::Text::Reader getTextValue() const;

  // dataValue@14: Data;  # ptr[0], union tag = 13
  inline bool hasDataValue() const;
  inline  ::capnp::Data::Reader getDataValue() const;

  // listValue@15: Object;  # ptr[0], union tag = 14
  inline bool hasListValue() const;
  template <typename T> inline typename T::Reader getListValue() const;
  template <typename T, typename Param> inline typename T::Reader
      getListValue(Param&& param) const;

  // enumValue@16: UInt16;  # bits[64, 80), union tag = 15
  inline  ::uint16_t getEnumValue() const;

  // structValue@17: Object;  # ptr[0], union tag = 16
  inline bool hasStructValue() const;
  template <typename T> inline typename T::Reader getStructValue() const;
  template <typename T, typename Param> inline typename T::Reader
      getStructValue(Param&& param) const;

  // interfaceValue@18: Void;  # (none), union tag = 17
  inline  ::capnp::Void getInterfaceValue() const;

  // objectValue@19: Object;  # ptr[0], union tag = 18
  inline bool hasObjectValue() const;
  template <typename T> inline typename T::Reader getObjectValue() const;
  template <typename T, typename Param> inline typename T::Reader
      getObjectValue(Param&& param) const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Value::Body::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Value::Body::Reader reader) {
  return ::capnp::_::unionString<Value::Body>(reader._reader);
}



class Value::Body::Builder {
public:
  typedef Body Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // voidValue@10: Void;  # (none), union tag = 9
  inline  ::capnp::Void getVoidValue();
  inline void setVoidValue( ::capnp::Void value);

  // boolValue@2: Bool;  # bits[64, 65), union tag = 1
  inline bool getBoolValue();
  inline void setBoolValue(bool value);

  // int8Value@3: Int8;  # bits[64, 72), union tag = 2
  inline  ::int8_t getInt8Value();
  inline void setInt8Value( ::int8_t value);

  // int16Value@4: Int16;  # bits[64, 80), union tag = 3
  inline  ::int16_t getInt16Value();
  inline void setInt16Value( ::int16_t value);

  // int32Value@5: Int32;  # bits[64, 96), union tag = 4
  inline  ::int32_t getInt32Value();
  inline void setInt32Value( ::int32_t value);

  // int64Value@6: Int64;  # bits[64, 128), union tag = 5
  inline  ::int64_t getInt64Value();
  inline void setInt64Value( ::int64_t value);

  // uint8Value@7: UInt8;  # bits[64, 72), union tag = 6
  inline  ::uint8_t getUint8Value();
  inline void setUint8Value( ::uint8_t value);

  // uint16Value@8: UInt16;  # bits[64, 80), union tag = 7
  inline  ::uint16_t getUint16Value();
  inline void setUint16Value( ::uint16_t value);

  // uint32Value@9: UInt32;  # bits[64, 96), union tag = 8
  inline  ::uint32_t getUint32Value();
  inline void setUint32Value( ::uint32_t value);

  // uint64Value@1: UInt64;  # bits[64, 128), union tag = 0
  inline  ::uint64_t getUint64Value();
  inline void setUint64Value( ::uint64_t value);

  // float32Value@11: Float32;  # bits[64, 96), union tag = 10
  inline float getFloat32Value();
  inline void setFloat32Value(float value);

  // float64Value@12: Float64;  # bits[64, 128), union tag = 11
  inline double getFloat64Value();
  inline void setFloat64Value(double value);

  // textValue@13: Text;  # ptr[0], union tag = 12
  inline bool hasTextValue();
  inline  ::capnp::Text::Builder getTextValue();
  inline void setTextValue( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initTextValue(unsigned int size);
  inline void adoptTextValue(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownTextValue();

  // dataValue@14: Data;  # ptr[0], union tag = 13
  inline bool hasDataValue();
  inline  ::capnp::Data::Builder getDataValue();
  inline void setDataValue( ::capnp::Data::Reader other);
  inline  ::capnp::Data::Builder initDataValue(unsigned int size);
  inline void adoptDataValue(::capnp::Orphan< ::capnp::Data>&& value);
  inline ::capnp::Orphan< ::capnp::Data> disownDataValue();

  // listValue@15: Object;  # ptr[0], union tag = 14
  inline bool hasListValue();
  template <typename T> inline typename T::Builder getListValue();
  template <typename T, typename Param> inline typename T::Builder
      getListValue(Param&& param);
  template <typename T> inline void setListValue(typename T::Reader value);
  template <typename T, typename U> inline void
      setListValue(std::initializer_list<U> value);
  template <typename T, typename... Params> inline typename T::Builder
      initListValue(Params&&... params);
  template <typename T> void adoptListValue(::capnp::Orphan<T>&& value);
  template <typename T, typename... Params> ::capnp::Orphan<T>
      disownListValue(Params&&... params);

  // enumValue@16: UInt16;  # bits[64, 80), union tag = 15
  inline  ::uint16_t getEnumValue();
  inline void setEnumValue( ::uint16_t value);

  // structValue@17: Object;  # ptr[0], union tag = 16
  inline bool hasStructValue();
  template <typename T> inline typename T::Builder getStructValue();
  template <typename T, typename Param> inline typename T::Builder
      getStructValue(Param&& param);
  template <typename T> inline void setStructValue(typename T::Reader value);
  template <typename T, typename U> inline void
      setStructValue(std::initializer_list<U> value);
  template <typename T, typename... Params> inline typename T::Builder
      initStructValue(Params&&... params);
  template <typename T> void adoptStructValue(::capnp::Orphan<T>&& value);
  template <typename T, typename... Params> ::capnp::Orphan<T>
      disownStructValue(Params&&... params);

  // interfaceValue@18: Void;  # (none), union tag = 17
  inline  ::capnp::Void getInterfaceValue();
  inline void setInterfaceValue( ::capnp::Void value);

  // objectValue@19: Object;  # ptr[0], union tag = 18
  inline bool hasObjectValue();
  template <typename T> inline typename T::Builder getObjectValue();
  template <typename T, typename Param> inline typename T::Builder
      getObjectValue(Param&& param);
  template <typename T> inline void setObjectValue(typename T::Reader value);
  template <typename T, typename U> inline void
      setObjectValue(std::initializer_list<U> value);
  template <typename T, typename... Params> inline typename T::Builder
      initObjectValue(Params&&... params);
  template <typename T> void adoptObjectValue(::capnp::Orphan<T>&& value);
  template <typename T, typename... Params> ::capnp::Orphan<T>
      disownObjectValue(Params&&... params);
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Value::Body::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Value::Body::Builder builder) {
  return ::capnp::_::unionString<Value::Body>(builder._builder.asReader());
}

class Annotation::Reader {
public:
  typedef Annotation Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // id@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId() const;

  // value@1: .Value;  # ptr[0]
  inline bool hasValue() const;
  inline  ::capnp::schema::Value::Reader getValue() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(Annotation::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(Annotation::Reader reader) {
  return ::capnp::_::structString<Annotation>(reader._reader);
}



class Annotation::Builder {
public:
  typedef Annotation Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // id@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId();
  inline void setId( ::uint64_t value);

  // value@1: .Value;  # ptr[0]
  inline bool hasValue();
  inline  ::capnp::schema::Value::Builder getValue();
  inline void setValue( ::capnp::schema::Value::Reader other);
  inline  ::capnp::schema::Value::Builder initValue();
  inline void adoptValue(::capnp::Orphan< ::capnp::schema::Value>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Value> disownValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(Annotation::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(Annotation::Builder builder) {
  return ::capnp::_::structString<Annotation>(builder._builder.asReader());
}

class FileNode::Reader {
public:
  typedef FileNode Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // imports@0: List(.FileNode.Import);  # ptr[0]
  inline bool hasImports() const;
  inline  ::capnp::List< ::capnp::schema::FileNode::Import>::Reader getImports() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(FileNode::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(FileNode::Reader reader) {
  return ::capnp::_::structString<FileNode>(reader._reader);
}



class FileNode::Builder {
public:
  typedef FileNode Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // imports@0: List(.FileNode.Import);  # ptr[0]
  inline bool hasImports();
  inline  ::capnp::List< ::capnp::schema::FileNode::Import>::Builder getImports();
  inline void setImports( ::capnp::List< ::capnp::schema::FileNode::Import>::Reader other);
  inline  ::capnp::List< ::capnp::schema::FileNode::Import>::Builder initImports(unsigned int size);
  inline void adoptImports(::capnp::Orphan< ::capnp::List< ::capnp::schema::FileNode::Import>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::FileNode::Import>> disownImports();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(FileNode::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(FileNode::Builder builder) {
  return ::capnp::_::structString<FileNode>(builder._builder.asReader());
}

class FileNode::Import::Reader {
public:
  typedef Import Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // id@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId() const;

  // name@1: Text;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::Text::Reader getName() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(FileNode::Import::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(FileNode::Import::Reader reader) {
  return ::capnp::_::structString<FileNode::Import>(reader._reader);
}



class FileNode::Import::Builder {
public:
  typedef Import Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // id@0: UInt64;  # bits[0, 64)
  inline  ::uint64_t getId();
  inline void setId( ::uint64_t value);

  // name@1: Text;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::Text::Builder getName();
  inline void setName( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initName(unsigned int size);
  inline void adoptName(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownName();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(FileNode::Import::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(FileNode::Import::Builder builder) {
  return ::capnp::_::structString<FileNode::Import>(builder._builder.asReader());
}

class StructNode::Reader {
public:
  typedef StructNode Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // dataSectionWordSize@0: UInt16;  # bits[0, 16)
  inline  ::uint16_t getDataSectionWordSize() const;

  // pointerSectionSize@1: UInt16;  # bits[16, 32)
  inline  ::uint16_t getPointerSectionSize() const;

  // preferredListEncoding@2: .ElementSize;  # bits[32, 48)
  inline  ::capnp::schema::ElementSize getPreferredListEncoding() const;

  // members@3: List(.StructNode.Member);  # ptr[0]
  inline bool hasMembers() const;
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Reader getMembers() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Reader reader) {
  return ::capnp::_::structString<StructNode>(reader._reader);
}



class StructNode::Builder {
public:
  typedef StructNode Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // dataSectionWordSize@0: UInt16;  # bits[0, 16)
  inline  ::uint16_t getDataSectionWordSize();
  inline void setDataSectionWordSize( ::uint16_t value);

  // pointerSectionSize@1: UInt16;  # bits[16, 32)
  inline  ::uint16_t getPointerSectionSize();
  inline void setPointerSectionSize( ::uint16_t value);

  // preferredListEncoding@2: .ElementSize;  # bits[32, 48)
  inline  ::capnp::schema::ElementSize getPreferredListEncoding();
  inline void setPreferredListEncoding( ::capnp::schema::ElementSize value);

  // members@3: List(.StructNode.Member);  # ptr[0]
  inline bool hasMembers();
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder getMembers();
  inline void setMembers( ::capnp::List< ::capnp::schema::StructNode::Member>::Reader other);
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder initMembers(unsigned int size);
  inline void adoptMembers(::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>> disownMembers();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Builder builder) {
  return ::capnp::_::structString<StructNode>(builder._builder.asReader());
}

class StructNode::Member::Reader {
public:
  typedef Member Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // union body@4 {  # [32, 48)
  inline Body::Reader getBody() const;

  // name@0: Text;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::Text::Reader getName() const;

  // ordinal@1: UInt16;  # bits[0, 16)
  inline  ::uint16_t getOrdinal() const;

  // codeOrder@2: UInt16;  # bits[16, 32)
  inline  ::uint16_t getCodeOrder() const;

  // annotations@3: List(.Annotation);  # ptr[1]
  inline bool hasAnnotations() const;
  inline  ::capnp::List< ::capnp::schema::Annotation>::Reader getAnnotations() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Reader reader) {
  return ::capnp::_::structString<StructNode::Member>(reader._reader);
}



class StructNode::Member::Builder {
public:
  typedef Member Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // union body@4 {  # [32, 48)
  inline Body::Builder getBody();

  // name@0: Text;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::Text::Builder getName();
  inline void setName( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initName(unsigned int size);
  inline void adoptName(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownName();

  // ordinal@1: UInt16;  # bits[0, 16)
  inline  ::uint16_t getOrdinal();
  inline void setOrdinal( ::uint16_t value);

  // codeOrder@2: UInt16;  # bits[16, 32)
  inline  ::uint16_t getCodeOrder();
  inline void setCodeOrder( ::uint16_t value);

  // annotations@3: List(.Annotation);  # ptr[1]
  inline bool hasAnnotations();
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder getAnnotations();
  inline void setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader other);
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder initAnnotations(unsigned int size);
  inline void adoptAnnotations(::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> disownAnnotations();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Builder builder) {
  return ::capnp::_::structString<StructNode::Member>(builder._builder.asReader());
}

class StructNode::Member::Body::Reader {
public:
  typedef Body Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline Which which() const;

  // fieldMember@5: .StructNode.Field;  # ptr[2], union tag = 0
  inline bool hasFieldMember() const;
  inline  ::capnp::schema::StructNode::Field::Reader getFieldMember() const;

  // unionMember@6: .StructNode.Union;  # ptr[2], union tag = 1
  inline bool hasUnionMember() const;
  inline  ::capnp::schema::StructNode::Union::Reader getUnionMember() const;

  // groupMember@7: .StructNode.Group;  # ptr[2], union tag = 2
  inline bool hasGroupMember() const;
  inline  ::capnp::schema::StructNode::Group::Reader getGroupMember() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Body::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Body::Reader reader) {
  return ::capnp::_::unionString<StructNode::Member::Body>(reader._reader);
}



class StructNode::Member::Body::Builder {
public:
  typedef Body Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline Which which();

  // fieldMember@5: .StructNode.Field;  # ptr[2], union tag = 0
  inline bool hasFieldMember();
  inline  ::capnp::schema::StructNode::Field::Builder getFieldMember();
  inline void setFieldMember( ::capnp::schema::StructNode::Field::Reader other);
  inline  ::capnp::schema::StructNode::Field::Builder initFieldMember();
  inline void adoptFieldMember(::capnp::Orphan< ::capnp::schema::StructNode::Field>&& value);
  inline ::capnp::Orphan< ::capnp::schema::StructNode::Field> disownFieldMember();

  // unionMember@6: .StructNode.Union;  # ptr[2], union tag = 1
  inline bool hasUnionMember();
  inline  ::capnp::schema::StructNode::Union::Builder getUnionMember();
  inline void setUnionMember( ::capnp::schema::StructNode::Union::Reader other);
  inline  ::capnp::schema::StructNode::Union::Builder initUnionMember();
  inline void adoptUnionMember(::capnp::Orphan< ::capnp::schema::StructNode::Union>&& value);
  inline ::capnp::Orphan< ::capnp::schema::StructNode::Union> disownUnionMember();

  // groupMember@7: .StructNode.Group;  # ptr[2], union tag = 2
  inline bool hasGroupMember();
  inline  ::capnp::schema::StructNode::Group::Builder getGroupMember();
  inline void setGroupMember( ::capnp::schema::StructNode::Group::Reader other);
  inline  ::capnp::schema::StructNode::Group::Builder initGroupMember();
  inline void adoptGroupMember(::capnp::Orphan< ::capnp::schema::StructNode::Group>&& value);
  inline ::capnp::Orphan< ::capnp::schema::StructNode::Group> disownGroupMember();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Body::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Member::Body::Builder builder) {
  return ::capnp::_::unionString<StructNode::Member::Body>(builder._builder.asReader());
}

class StructNode::Field::Reader {
public:
  typedef Field Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // offset@0: UInt32;  # bits[0, 32)
  inline  ::uint32_t getOffset() const;

  // type@1: .Type;  # ptr[0]
  inline bool hasType() const;
  inline  ::capnp::schema::Type::Reader getType() const;

  // defaultValue@2: .Value;  # ptr[1]
  inline bool hasDefaultValue() const;
  inline  ::capnp::schema::Value::Reader getDefaultValue() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Field::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Field::Reader reader) {
  return ::capnp::_::structString<StructNode::Field>(reader._reader);
}



class StructNode::Field::Builder {
public:
  typedef Field Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // offset@0: UInt32;  # bits[0, 32)
  inline  ::uint32_t getOffset();
  inline void setOffset( ::uint32_t value);

  // type@1: .Type;  # ptr[0]
  inline bool hasType();
  inline  ::capnp::schema::Type::Builder getType();
  inline void setType( ::capnp::schema::Type::Reader other);
  inline  ::capnp::schema::Type::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::schema::Type>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Type> disownType();

  // defaultValue@2: .Value;  # ptr[1]
  inline bool hasDefaultValue();
  inline  ::capnp::schema::Value::Builder getDefaultValue();
  inline void setDefaultValue( ::capnp::schema::Value::Reader other);
  inline  ::capnp::schema::Value::Builder initDefaultValue();
  inline void adoptDefaultValue(::capnp::Orphan< ::capnp::schema::Value>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Value> disownDefaultValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Field::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Field::Builder builder) {
  return ::capnp::_::structString<StructNode::Field>(builder._builder.asReader());
}

class StructNode::Union::Reader {
public:
  typedef Union Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // discriminantOffset@0: UInt32;  # bits[0, 32)
  inline  ::uint32_t getDiscriminantOffset() const;

  // members@1: List(.StructNode.Member);  # ptr[0]
  inline bool hasMembers() const;
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Reader getMembers() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Union::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Union::Reader reader) {
  return ::capnp::_::structString<StructNode::Union>(reader._reader);
}



class StructNode::Union::Builder {
public:
  typedef Union Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // discriminantOffset@0: UInt32;  # bits[0, 32)
  inline  ::uint32_t getDiscriminantOffset();
  inline void setDiscriminantOffset( ::uint32_t value);

  // members@1: List(.StructNode.Member);  # ptr[0]
  inline bool hasMembers();
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder getMembers();
  inline void setMembers( ::capnp::List< ::capnp::schema::StructNode::Member>::Reader other);
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder initMembers(unsigned int size);
  inline void adoptMembers(::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>> disownMembers();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Union::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Union::Builder builder) {
  return ::capnp::_::structString<StructNode::Union>(builder._builder.asReader());
}

class StructNode::Group::Reader {
public:
  typedef Group Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // members@0: List(.StructNode.Member);  # ptr[0]
  inline bool hasMembers() const;
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Reader getMembers() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Group::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Group::Reader reader) {
  return ::capnp::_::structString<StructNode::Group>(reader._reader);
}



class StructNode::Group::Builder {
public:
  typedef Group Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // members@0: List(.StructNode.Member);  # ptr[0]
  inline bool hasMembers();
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder getMembers();
  inline void setMembers( ::capnp::List< ::capnp::schema::StructNode::Member>::Reader other);
  inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder initMembers(unsigned int size);
  inline void adoptMembers(::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>> disownMembers();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(StructNode::Group::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(StructNode::Group::Builder builder) {
  return ::capnp::_::structString<StructNode::Group>(builder._builder.asReader());
}

class EnumNode::Reader {
public:
  typedef EnumNode Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // enumerants@0: List(.EnumNode.Enumerant);  # ptr[0]
  inline bool hasEnumerants() const;
  inline  ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Reader getEnumerants() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(EnumNode::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(EnumNode::Reader reader) {
  return ::capnp::_::structString<EnumNode>(reader._reader);
}



class EnumNode::Builder {
public:
  typedef EnumNode Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // enumerants@0: List(.EnumNode.Enumerant);  # ptr[0]
  inline bool hasEnumerants();
  inline  ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Builder getEnumerants();
  inline void setEnumerants( ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Reader other);
  inline  ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Builder initEnumerants(unsigned int size);
  inline void adoptEnumerants(::capnp::Orphan< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>> disownEnumerants();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(EnumNode::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(EnumNode::Builder builder) {
  return ::capnp::_::structString<EnumNode>(builder._builder.asReader());
}

class EnumNode::Enumerant::Reader {
public:
  typedef Enumerant Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // name@0: Text;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::Text::Reader getName() const;

  // codeOrder@1: UInt16;  # bits[0, 16)
  inline  ::uint16_t getCodeOrder() const;

  // annotations@2: List(.Annotation);  # ptr[1]
  inline bool hasAnnotations() const;
  inline  ::capnp::List< ::capnp::schema::Annotation>::Reader getAnnotations() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(EnumNode::Enumerant::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(EnumNode::Enumerant::Reader reader) {
  return ::capnp::_::structString<EnumNode::Enumerant>(reader._reader);
}



class EnumNode::Enumerant::Builder {
public:
  typedef Enumerant Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // name@0: Text;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::Text::Builder getName();
  inline void setName( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initName(unsigned int size);
  inline void adoptName(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownName();

  // codeOrder@1: UInt16;  # bits[0, 16)
  inline  ::uint16_t getCodeOrder();
  inline void setCodeOrder( ::uint16_t value);

  // annotations@2: List(.Annotation);  # ptr[1]
  inline bool hasAnnotations();
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder getAnnotations();
  inline void setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader other);
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder initAnnotations(unsigned int size);
  inline void adoptAnnotations(::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> disownAnnotations();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(EnumNode::Enumerant::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(EnumNode::Enumerant::Builder builder) {
  return ::capnp::_::structString<EnumNode::Enumerant>(builder._builder.asReader());
}

class InterfaceNode::Reader {
public:
  typedef InterfaceNode Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // methods@0: List(.InterfaceNode.Method);  # ptr[0]
  inline bool hasMethods() const;
  inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Reader getMethods() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Reader reader) {
  return ::capnp::_::structString<InterfaceNode>(reader._reader);
}



class InterfaceNode::Builder {
public:
  typedef InterfaceNode Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // methods@0: List(.InterfaceNode.Method);  # ptr[0]
  inline bool hasMethods();
  inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Builder getMethods();
  inline void setMethods( ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Reader other);
  inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Builder initMethods(unsigned int size);
  inline void adoptMethods(::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method>> disownMethods();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Builder builder) {
  return ::capnp::_::structString<InterfaceNode>(builder._builder.asReader());
}

class InterfaceNode::Method::Reader {
public:
  typedef Method Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // name@0: Text;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::Text::Reader getName() const;

  // codeOrder@1: UInt16;  # bits[0, 16)
  inline  ::uint16_t getCodeOrder() const;

  // params@2: List(.InterfaceNode.Method.Param);  # ptr[1]
  inline bool hasParams() const;
  inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Reader getParams() const;

  // requiredParamCount@3: UInt16;  # bits[16, 32)
  inline  ::uint16_t getRequiredParamCount() const;

  // returnType@4: .Type;  # ptr[2]
  inline bool hasReturnType() const;
  inline  ::capnp::schema::Type::Reader getReturnType() const;

  // annotations@5: List(.Annotation);  # ptr[3]
  inline bool hasAnnotations() const;
  inline  ::capnp::List< ::capnp::schema::Annotation>::Reader getAnnotations() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Reader reader) {
  return ::capnp::_::structString<InterfaceNode::Method>(reader._reader);
}



class InterfaceNode::Method::Builder {
public:
  typedef Method Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // name@0: Text;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::Text::Builder getName();
  inline void setName( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initName(unsigned int size);
  inline void adoptName(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownName();

  // codeOrder@1: UInt16;  # bits[0, 16)
  inline  ::uint16_t getCodeOrder();
  inline void setCodeOrder( ::uint16_t value);

  // params@2: List(.InterfaceNode.Method.Param);  # ptr[1]
  inline bool hasParams();
  inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Builder getParams();
  inline void setParams( ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Reader other);
  inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Builder initParams(unsigned int size);
  inline void adoptParams(::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>> disownParams();

  // requiredParamCount@3: UInt16;  # bits[16, 32)
  inline  ::uint16_t getRequiredParamCount();
  inline void setRequiredParamCount( ::uint16_t value);

  // returnType@4: .Type;  # ptr[2]
  inline bool hasReturnType();
  inline  ::capnp::schema::Type::Builder getReturnType();
  inline void setReturnType( ::capnp::schema::Type::Reader other);
  inline  ::capnp::schema::Type::Builder initReturnType();
  inline void adoptReturnType(::capnp::Orphan< ::capnp::schema::Type>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Type> disownReturnType();

  // annotations@5: List(.Annotation);  # ptr[3]
  inline bool hasAnnotations();
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder getAnnotations();
  inline void setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader other);
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder initAnnotations(unsigned int size);
  inline void adoptAnnotations(::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> disownAnnotations();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Builder builder) {
  return ::capnp::_::structString<InterfaceNode::Method>(builder._builder.asReader());
}

class InterfaceNode::Method::Param::Reader {
public:
  typedef Param Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // name@0: Text;  # ptr[0]
  inline bool hasName() const;
  inline  ::capnp::Text::Reader getName() const;

  // type@1: .Type;  # ptr[1]
  inline bool hasType() const;
  inline  ::capnp::schema::Type::Reader getType() const;

  // defaultValue@2: .Value;  # ptr[2]
  inline bool hasDefaultValue() const;
  inline  ::capnp::schema::Value::Reader getDefaultValue() const;

  // annotations@3: List(.Annotation);  # ptr[3]
  inline bool hasAnnotations() const;
  inline  ::capnp::List< ::capnp::schema::Annotation>::Reader getAnnotations() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Param::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Param::Reader reader) {
  return ::capnp::_::structString<InterfaceNode::Method::Param>(reader._reader);
}



class InterfaceNode::Method::Param::Builder {
public:
  typedef Param Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // name@0: Text;  # ptr[0]
  inline bool hasName();
  inline  ::capnp::Text::Builder getName();
  inline void setName( ::capnp::Text::Reader other);
  inline  ::capnp::Text::Builder initName(unsigned int size);
  inline void adoptName(::capnp::Orphan< ::capnp::Text>&& value);
  inline ::capnp::Orphan< ::capnp::Text> disownName();

  // type@1: .Type;  # ptr[1]
  inline bool hasType();
  inline  ::capnp::schema::Type::Builder getType();
  inline void setType( ::capnp::schema::Type::Reader other);
  inline  ::capnp::schema::Type::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::schema::Type>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Type> disownType();

  // defaultValue@2: .Value;  # ptr[2]
  inline bool hasDefaultValue();
  inline  ::capnp::schema::Value::Builder getDefaultValue();
  inline void setDefaultValue( ::capnp::schema::Value::Reader other);
  inline  ::capnp::schema::Value::Builder initDefaultValue();
  inline void adoptDefaultValue(::capnp::Orphan< ::capnp::schema::Value>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Value> disownDefaultValue();

  // annotations@3: List(.Annotation);  # ptr[3]
  inline bool hasAnnotations();
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder getAnnotations();
  inline void setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader other);
  inline  ::capnp::List< ::capnp::schema::Annotation>::Builder initAnnotations(unsigned int size);
  inline void adoptAnnotations(::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> disownAnnotations();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Param::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(InterfaceNode::Method::Param::Builder builder) {
  return ::capnp::_::structString<InterfaceNode::Method::Param>(builder._builder.asReader());
}

class ConstNode::Reader {
public:
  typedef ConstNode Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // type@0: .Type;  # ptr[0]
  inline bool hasType() const;
  inline  ::capnp::schema::Type::Reader getType() const;

  // value@1: .Value;  # ptr[1]
  inline bool hasValue() const;
  inline  ::capnp::schema::Value::Reader getValue() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(ConstNode::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(ConstNode::Reader reader) {
  return ::capnp::_::structString<ConstNode>(reader._reader);
}



class ConstNode::Builder {
public:
  typedef ConstNode Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // type@0: .Type;  # ptr[0]
  inline bool hasType();
  inline  ::capnp::schema::Type::Builder getType();
  inline void setType( ::capnp::schema::Type::Reader other);
  inline  ::capnp::schema::Type::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::schema::Type>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Type> disownType();

  // value@1: .Value;  # ptr[1]
  inline bool hasValue();
  inline  ::capnp::schema::Value::Builder getValue();
  inline void setValue( ::capnp::schema::Value::Reader other);
  inline  ::capnp::schema::Value::Builder initValue();
  inline void adoptValue(::capnp::Orphan< ::capnp::schema::Value>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Value> disownValue();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(ConstNode::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(ConstNode::Builder builder) {
  return ::capnp::_::structString<ConstNode>(builder._builder.asReader());
}

class AnnotationNode::Reader {
public:
  typedef AnnotationNode Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // type@0: .Type;  # ptr[0]
  inline bool hasType() const;
  inline  ::capnp::schema::Type::Reader getType() const;

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
  friend ::kj::StringTree KJ_STRINGIFY(AnnotationNode::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(AnnotationNode::Reader reader) {
  return ::capnp::_::structString<AnnotationNode>(reader._reader);
}



class AnnotationNode::Builder {
public:
  typedef AnnotationNode Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // type@0: .Type;  # ptr[0]
  inline bool hasType();
  inline  ::capnp::schema::Type::Builder getType();
  inline void setType( ::capnp::schema::Type::Reader other);
  inline  ::capnp::schema::Type::Builder initType();
  inline void adoptType(::capnp::Orphan< ::capnp::schema::Type>&& value);
  inline ::capnp::Orphan< ::capnp::schema::Type> disownType();

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
  friend ::kj::StringTree KJ_STRINGIFY(AnnotationNode::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(AnnotationNode::Builder builder) {
  return ::capnp::_::structString<AnnotationNode>(builder._builder.asReader());
}

class CodeGeneratorRequest::Reader {
public:
  typedef CodeGeneratorRequest Reads;

  Reader() = default;
  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}

  inline size_t totalSizeInWords() const {
    return _reader.totalSize() / ::capnp::WORDS;
  }

  // nodes@0: List(.Node);  # ptr[0]
  inline bool hasNodes() const;
  inline  ::capnp::List< ::capnp::schema::Node>::Reader getNodes() const;

  // requestedFiles@1: List(UInt64);  # ptr[1]
  inline bool hasRequestedFiles() const;
  inline  ::capnp::List< ::uint64_t>::Reader getRequestedFiles() const;
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
  friend ::kj::StringTree KJ_STRINGIFY(CodeGeneratorRequest::Reader reader);
};

inline ::kj::StringTree KJ_STRINGIFY(CodeGeneratorRequest::Reader reader) {
  return ::capnp::_::structString<CodeGeneratorRequest>(reader._reader);
}



class CodeGeneratorRequest::Builder {
public:
  typedef CodeGeneratorRequest Builds;

  Builder() = default;
  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}
  inline operator Reader() const { return Reader(_builder.asReader()); }
  inline Reader asReader() const { return *this; }

  inline size_t totalSizeInWords() { return asReader().totalSizeInWords(); }

  // nodes@0: List(.Node);  # ptr[0]
  inline bool hasNodes();
  inline  ::capnp::List< ::capnp::schema::Node>::Builder getNodes();
  inline void setNodes( ::capnp::List< ::capnp::schema::Node>::Reader other);
  inline  ::capnp::List< ::capnp::schema::Node>::Builder initNodes(unsigned int size);
  inline void adoptNodes(::capnp::Orphan< ::capnp::List< ::capnp::schema::Node>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Node>> disownNodes();

  // requestedFiles@1: List(UInt64);  # ptr[1]
  inline bool hasRequestedFiles();
  inline  ::capnp::List< ::uint64_t>::Builder getRequestedFiles();
  inline void setRequestedFiles( ::capnp::List< ::uint64_t>::Reader other);
  inline void setRequestedFiles(
      std::initializer_list< ::uint64_t> other);
  inline  ::capnp::List< ::uint64_t>::Builder initRequestedFiles(unsigned int size);
  inline void adoptRequestedFiles(::capnp::Orphan< ::capnp::List< ::uint64_t>>&& value);
  inline ::capnp::Orphan< ::capnp::List< ::uint64_t>> disownRequestedFiles();
private:
  ::capnp::_::StructBuilder _builder;
  template <typename T, ::capnp::Kind k>
  friend struct ::capnp::ToDynamic_;
  friend class ::capnp::Orphanage;
  friend ::kj::StringTree KJ_STRINGIFY(CodeGeneratorRequest::Builder builder);
};

inline ::kj::StringTree KJ_STRINGIFY(CodeGeneratorRequest::Builder builder) {
  return ::capnp::_::structString<CodeGeneratorRequest>(builder._builder.asReader());
}


inline Node::Body::Reader Node::Reader::getBody() const {
  return Node::Body::Reader(_reader);
}

inline Node::Body::Builder Node::Builder::getBody() {
  return Node::Body::Builder(_builder);
}


// Node::id@0: UInt64;  # bits[0, 64)

inline  ::uint64_t Node::Reader::getId() const {
  return _reader.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Node::Builder::getId() {
  return _builder.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}
inline void Node::Builder::setId( ::uint64_t value) {
  _builder.setDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS, value);
}


// Node::displayName@1: Text;  # ptr[0]


inline bool Node::Reader::hasDisplayName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Node::Builder::hasDisplayName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader Node::Reader::getDisplayName() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder Node::Builder::getDisplayName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Node::Builder::setDisplayName( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder Node::Builder::initDisplayName(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void Node::Builder::adoptDisplayName(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> Node::Builder::disownDisplayName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Node::displayNamePrefixLength@12: UInt32;  # bits[160, 192)

inline  ::uint32_t Node::Reader::getDisplayNamePrefixLength() const {
  return _reader.getDataField< ::uint32_t>(
      5 * ::capnp::ELEMENTS);
}

inline  ::uint32_t Node::Builder::getDisplayNamePrefixLength() {
  return _builder.getDataField< ::uint32_t>(
      5 * ::capnp::ELEMENTS);
}
inline void Node::Builder::setDisplayNamePrefixLength( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      5 * ::capnp::ELEMENTS, value);
}


// Node::scopeId@2: UInt64 = 0;  # bits[64, 128)

inline  ::uint64_t Node::Reader::getScopeId() const {
  return _reader.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Node::Builder::getScopeId() {
  return _builder.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void Node::Builder::setScopeId( ::uint64_t value) {
  _builder.setDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// Node::nestedNodes@3: List(.Node.NestedNode);  # ptr[1]


inline bool Node::Reader::hasNestedNodes() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool Node::Builder::hasNestedNodes() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Node::NestedNode>::Reader Node::Reader::getNestedNodes() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node::NestedNode>>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Node::NestedNode>::Builder Node::Builder::getNestedNodes() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node::NestedNode>>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void Node::Builder::setNestedNodes( ::capnp::List< ::capnp::schema::Node::NestedNode>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node::NestedNode>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::Node::NestedNode>::Builder Node::Builder::initNestedNodes(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node::NestedNode>>::init(
      _builder, 1 * ::capnp::POINTERS, size);
}


inline void Node::Builder::adoptNestedNodes(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::Node::NestedNode>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node::NestedNode>>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Node::NestedNode>> Node::Builder::disownNestedNodes() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node::NestedNode>>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// Node::annotations@4: List(.Annotation);  # ptr[2]


inline bool Node::Reader::hasAnnotations() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool Node::Builder::hasAnnotations() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Reader Node::Reader::getAnnotations() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder Node::Builder::getAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void Node::Builder::setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder Node::Builder::initAnnotations(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::init(
      _builder, 2 * ::capnp::POINTERS, size);
}


inline void Node::Builder::adoptAnnotations(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> Node::Builder::disownAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::disown(
      _builder, 2 * ::capnp::POINTERS);
}




// Node::NestedNode::name@0: Text;  # ptr[0]


inline bool Node::NestedNode::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Node::NestedNode::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader Node::NestedNode::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder Node::NestedNode::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Node::NestedNode::Builder::setName( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder Node::NestedNode::Builder::initName(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void Node::NestedNode::Builder::adoptName(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> Node::NestedNode::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Node::NestedNode::id@1: UInt64;  # bits[0, 64)

inline  ::uint64_t Node::NestedNode::Reader::getId() const {
  return _reader.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Node::NestedNode::Builder::getId() {
  return _builder.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}
inline void Node::NestedNode::Builder::setId( ::uint64_t value) {
  _builder.setDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS, value);
}


// Node::Body
inline Node::Body::Which Node::Body::Reader::which() const {
  return _reader.getDataField<Which>(8 * ::capnp::ELEMENTS);
}

inline Node::Body::Which Node::Body::Builder::which() {
  return _builder.getDataField<Which>(8 * ::capnp::ELEMENTS);
}


// Node::Body::fileNode@6: .FileNode;  # ptr[3], union tag = 0


inline bool Node::Body::Reader::hasFileNode() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Node::Body::Builder::hasFileNode() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::FileNode::Reader Node::Body::Reader::getFileNode() const {
  KJ_IREQUIRE(which() == Body::FILE_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::FileNode>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::FileNode::Builder Node::Body::Builder::getFileNode() {
  KJ_IREQUIRE(which() == Body::FILE_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::FileNode>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Node::Body::Builder::setFileNode( ::capnp::schema::FileNode::Reader value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::FILE_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::FileNode>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::FileNode::Builder Node::Body::Builder::initFileNode() {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::FILE_NODE);
  return ::capnp::_::PointerHelpers< ::capnp::schema::FileNode>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Node::Body::Builder::adoptFileNode(
    ::capnp::Orphan< ::capnp::schema::FileNode>&& value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::FILE_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::FileNode>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::FileNode> Node::Body::Builder::disownFileNode() {
  KJ_IREQUIRE(which() == Body::FILE_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::FileNode>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Node::Body::structNode@7: .StructNode;  # ptr[3], union tag = 1


inline bool Node::Body::Reader::hasStructNode() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Node::Body::Builder::hasStructNode() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Reader Node::Body::Reader::getStructNode() const {
  KJ_IREQUIRE(which() == Body::STRUCT_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Builder Node::Body::Builder::getStructNode() {
  KJ_IREQUIRE(which() == Body::STRUCT_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Node::Body::Builder::setStructNode( ::capnp::schema::StructNode::Reader value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::STRUCT_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::StructNode::Builder Node::Body::Builder::initStructNode() {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::STRUCT_NODE);
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Node::Body::Builder::adoptStructNode(
    ::capnp::Orphan< ::capnp::schema::StructNode>&& value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::STRUCT_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::StructNode> Node::Body::Builder::disownStructNode() {
  KJ_IREQUIRE(which() == Body::STRUCT_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Node::Body::enumNode@8: .EnumNode;  # ptr[3], union tag = 2


inline bool Node::Body::Reader::hasEnumNode() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Node::Body::Builder::hasEnumNode() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::EnumNode::Reader Node::Body::Reader::getEnumNode() const {
  KJ_IREQUIRE(which() == Body::ENUM_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::EnumNode>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::EnumNode::Builder Node::Body::Builder::getEnumNode() {
  KJ_IREQUIRE(which() == Body::ENUM_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::EnumNode>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Node::Body::Builder::setEnumNode( ::capnp::schema::EnumNode::Reader value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::ENUM_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::EnumNode>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::EnumNode::Builder Node::Body::Builder::initEnumNode() {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::ENUM_NODE);
  return ::capnp::_::PointerHelpers< ::capnp::schema::EnumNode>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Node::Body::Builder::adoptEnumNode(
    ::capnp::Orphan< ::capnp::schema::EnumNode>&& value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::ENUM_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::EnumNode>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::EnumNode> Node::Body::Builder::disownEnumNode() {
  KJ_IREQUIRE(which() == Body::ENUM_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::EnumNode>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Node::Body::interfaceNode@9: .InterfaceNode;  # ptr[3], union tag = 3


inline bool Node::Body::Reader::hasInterfaceNode() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Node::Body::Builder::hasInterfaceNode() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::InterfaceNode::Reader Node::Body::Reader::getInterfaceNode() const {
  KJ_IREQUIRE(which() == Body::INTERFACE_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::InterfaceNode>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::InterfaceNode::Builder Node::Body::Builder::getInterfaceNode() {
  KJ_IREQUIRE(which() == Body::INTERFACE_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::InterfaceNode>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Node::Body::Builder::setInterfaceNode( ::capnp::schema::InterfaceNode::Reader value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::INTERFACE_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::InterfaceNode>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::InterfaceNode::Builder Node::Body::Builder::initInterfaceNode() {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::INTERFACE_NODE);
  return ::capnp::_::PointerHelpers< ::capnp::schema::InterfaceNode>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Node::Body::Builder::adoptInterfaceNode(
    ::capnp::Orphan< ::capnp::schema::InterfaceNode>&& value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::INTERFACE_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::InterfaceNode>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::InterfaceNode> Node::Body::Builder::disownInterfaceNode() {
  KJ_IREQUIRE(which() == Body::INTERFACE_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::InterfaceNode>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Node::Body::constNode@10: .ConstNode;  # ptr[3], union tag = 4


inline bool Node::Body::Reader::hasConstNode() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Node::Body::Builder::hasConstNode() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::ConstNode::Reader Node::Body::Reader::getConstNode() const {
  KJ_IREQUIRE(which() == Body::CONST_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::ConstNode>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::ConstNode::Builder Node::Body::Builder::getConstNode() {
  KJ_IREQUIRE(which() == Body::CONST_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::ConstNode>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Node::Body::Builder::setConstNode( ::capnp::schema::ConstNode::Reader value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::CONST_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::ConstNode>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::ConstNode::Builder Node::Body::Builder::initConstNode() {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::CONST_NODE);
  return ::capnp::_::PointerHelpers< ::capnp::schema::ConstNode>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Node::Body::Builder::adoptConstNode(
    ::capnp::Orphan< ::capnp::schema::ConstNode>&& value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::CONST_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::ConstNode>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::ConstNode> Node::Body::Builder::disownConstNode() {
  KJ_IREQUIRE(which() == Body::CONST_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::ConstNode>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



// Node::Body::annotationNode@11: .AnnotationNode;  # ptr[3], union tag = 5


inline bool Node::Body::Reader::hasAnnotationNode() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool Node::Body::Builder::hasAnnotationNode() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::AnnotationNode::Reader Node::Body::Reader::getAnnotationNode() const {
  KJ_IREQUIRE(which() == Body::ANNOTATION_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::AnnotationNode>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::schema::AnnotationNode::Builder Node::Body::Builder::getAnnotationNode() {
  KJ_IREQUIRE(which() == Body::ANNOTATION_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::AnnotationNode>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void Node::Body::Builder::setAnnotationNode( ::capnp::schema::AnnotationNode::Reader value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::ANNOTATION_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::AnnotationNode>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::AnnotationNode::Builder Node::Body::Builder::initAnnotationNode() {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::ANNOTATION_NODE);
  return ::capnp::_::PointerHelpers< ::capnp::schema::AnnotationNode>::init(
      _builder, 3 * ::capnp::POINTERS);
}


inline void Node::Body::Builder::adoptAnnotationNode(
    ::capnp::Orphan< ::capnp::schema::AnnotationNode>&& value) {
  _builder.setDataField<Body::Which>(
      8 * ::capnp::ELEMENTS, Body::ANNOTATION_NODE);
  ::capnp::_::PointerHelpers< ::capnp::schema::AnnotationNode>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::AnnotationNode> Node::Body::Builder::disownAnnotationNode() {
  KJ_IREQUIRE(which() == Body::ANNOTATION_NODE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::AnnotationNode>::disown(
      _builder, 3 * ::capnp::POINTERS);
}



inline Type::Body::Reader Type::Reader::getBody() const {
  return Type::Body::Reader(_reader);
}

inline Type::Body::Builder Type::Builder::getBody() {
  return Type::Body::Builder(_builder);
}


// Type::Body
inline Type::Body::Which Type::Body::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline Type::Body::Which Type::Body::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// Type::Body::voidType@1: Void;  # (none), union tag = 0

inline  ::capnp::Void Type::Body::Reader::getVoidType() const {
  KJ_IREQUIRE(which() == Body::VOID_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getVoidType() {
  KJ_IREQUIRE(which() == Body::VOID_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setVoidType( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::VOID_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::boolType@2: Void;  # (none), union tag = 1

inline  ::capnp::Void Type::Body::Reader::getBoolType() const {
  KJ_IREQUIRE(which() == Body::BOOL_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getBoolType() {
  KJ_IREQUIRE(which() == Body::BOOL_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setBoolType( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::BOOL_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::int8Type@3: Void;  # (none), union tag = 2

inline  ::capnp::Void Type::Body::Reader::getInt8Type() const {
  KJ_IREQUIRE(which() == Body::INT8_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getInt8Type() {
  KJ_IREQUIRE(which() == Body::INT8_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setInt8Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT8_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::int16Type@4: Void;  # (none), union tag = 3

inline  ::capnp::Void Type::Body::Reader::getInt16Type() const {
  KJ_IREQUIRE(which() == Body::INT16_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getInt16Type() {
  KJ_IREQUIRE(which() == Body::INT16_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setInt16Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT16_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::int32Type@5: Void;  # (none), union tag = 4

inline  ::capnp::Void Type::Body::Reader::getInt32Type() const {
  KJ_IREQUIRE(which() == Body::INT32_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getInt32Type() {
  KJ_IREQUIRE(which() == Body::INT32_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setInt32Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT32_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::int64Type@6: Void;  # (none), union tag = 5

inline  ::capnp::Void Type::Body::Reader::getInt64Type() const {
  KJ_IREQUIRE(which() == Body::INT64_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getInt64Type() {
  KJ_IREQUIRE(which() == Body::INT64_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setInt64Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT64_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::uint8Type@7: Void;  # (none), union tag = 6

inline  ::capnp::Void Type::Body::Reader::getUint8Type() const {
  KJ_IREQUIRE(which() == Body::UINT8_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getUint8Type() {
  KJ_IREQUIRE(which() == Body::UINT8_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setUint8Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT8_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::uint16Type@8: Void;  # (none), union tag = 7

inline  ::capnp::Void Type::Body::Reader::getUint16Type() const {
  KJ_IREQUIRE(which() == Body::UINT16_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getUint16Type() {
  KJ_IREQUIRE(which() == Body::UINT16_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setUint16Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT16_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::uint32Type@9: Void;  # (none), union tag = 8

inline  ::capnp::Void Type::Body::Reader::getUint32Type() const {
  KJ_IREQUIRE(which() == Body::UINT32_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getUint32Type() {
  KJ_IREQUIRE(which() == Body::UINT32_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setUint32Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT32_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::uint64Type@10: Void;  # (none), union tag = 9

inline  ::capnp::Void Type::Body::Reader::getUint64Type() const {
  KJ_IREQUIRE(which() == Body::UINT64_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getUint64Type() {
  KJ_IREQUIRE(which() == Body::UINT64_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setUint64Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT64_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::float32Type@11: Void;  # (none), union tag = 10

inline  ::capnp::Void Type::Body::Reader::getFloat32Type() const {
  KJ_IREQUIRE(which() == Body::FLOAT32_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getFloat32Type() {
  KJ_IREQUIRE(which() == Body::FLOAT32_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setFloat32Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::FLOAT32_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::float64Type@12: Void;  # (none), union tag = 11

inline  ::capnp::Void Type::Body::Reader::getFloat64Type() const {
  KJ_IREQUIRE(which() == Body::FLOAT64_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getFloat64Type() {
  KJ_IREQUIRE(which() == Body::FLOAT64_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setFloat64Type( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::FLOAT64_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::textType@13: Void;  # (none), union tag = 12

inline  ::capnp::Void Type::Body::Reader::getTextType() const {
  KJ_IREQUIRE(which() == Body::TEXT_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getTextType() {
  KJ_IREQUIRE(which() == Body::TEXT_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setTextType( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::TEXT_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::dataType@14: Void;  # (none), union tag = 13

inline  ::capnp::Void Type::Body::Reader::getDataType() const {
  KJ_IREQUIRE(which() == Body::DATA_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getDataType() {
  KJ_IREQUIRE(which() == Body::DATA_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setDataType( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::DATA_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Type::Body::listType@15: .Type;  # ptr[0], union tag = 14


inline bool Type::Body::Reader::hasListType() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Type::Body::Builder::hasListType() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Reader Type::Body::Reader::getListType() const {
  KJ_IREQUIRE(which() == Body::LIST_TYPE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Builder Type::Body::Builder::getListType() {
  KJ_IREQUIRE(which() == Body::LIST_TYPE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Type::Body::Builder::setListType( ::capnp::schema::Type::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST_TYPE);
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Type::Builder Type::Body::Builder::initListType() {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST_TYPE);
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Type::Body::Builder::adoptListType(
    ::capnp::Orphan< ::capnp::schema::Type>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST_TYPE);
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Type> Type::Body::Builder::disownListType() {
  KJ_IREQUIRE(which() == Body::LIST_TYPE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Type::Body::enumType@16: UInt64;  # bits[64, 128), union tag = 15

inline  ::uint64_t Type::Body::Reader::getEnumType() const {
  KJ_IREQUIRE(which() == Body::ENUM_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Type::Body::Builder::getEnumType() {
  KJ_IREQUIRE(which() == Body::ENUM_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setEnumType( ::uint64_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::ENUM_TYPE);
  _builder.setDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// Type::Body::structType@17: UInt64;  # bits[64, 128), union tag = 16

inline  ::uint64_t Type::Body::Reader::getStructType() const {
  KJ_IREQUIRE(which() == Body::STRUCT_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Type::Body::Builder::getStructType() {
  KJ_IREQUIRE(which() == Body::STRUCT_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setStructType( ::uint64_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_TYPE);
  _builder.setDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// Type::Body::interfaceType@18: UInt64;  # bits[64, 128), union tag = 17

inline  ::uint64_t Type::Body::Reader::getInterfaceType() const {
  KJ_IREQUIRE(which() == Body::INTERFACE_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Type::Body::Builder::getInterfaceType() {
  KJ_IREQUIRE(which() == Body::INTERFACE_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setInterfaceType( ::uint64_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INTERFACE_TYPE);
  _builder.setDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// Type::Body::objectType@19: Void;  # (none), union tag = 18

inline  ::capnp::Void Type::Body::Reader::getObjectType() const {
  KJ_IREQUIRE(which() == Body::OBJECT_TYPE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Type::Body::Builder::getObjectType() {
  KJ_IREQUIRE(which() == Body::OBJECT_TYPE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Type::Body::Builder::setObjectType( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::OBJECT_TYPE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


inline Value::Body::Reader Value::Reader::getBody() const {
  return Value::Body::Reader(_reader);
}

inline Value::Body::Builder Value::Builder::getBody() {
  return Value::Body::Builder(_builder);
}


// Value::Body
inline Value::Body::Which Value::Body::Reader::which() const {
  return _reader.getDataField<Which>(0 * ::capnp::ELEMENTS);
}

inline Value::Body::Which Value::Body::Builder::which() {
  return _builder.getDataField<Which>(0 * ::capnp::ELEMENTS);
}


// Value::Body::voidValue@10: Void;  # (none), union tag = 9

inline  ::capnp::Void Value::Body::Reader::getVoidValue() const {
  KJ_IREQUIRE(which() == Body::VOID_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Value::Body::Builder::getVoidValue() {
  KJ_IREQUIRE(which() == Body::VOID_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setVoidValue( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::VOID_VALUE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Value::Body::boolValue@2: Bool;  # bits[64, 65), union tag = 1

inline bool Value::Body::Reader::getBoolValue() const {
  KJ_IREQUIRE(which() == Body::BOOL_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField<bool>(
      64 * ::capnp::ELEMENTS);
}

inline bool Value::Body::Builder::getBoolValue() {
  KJ_IREQUIRE(which() == Body::BOOL_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField<bool>(
      64 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setBoolValue(bool value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::BOOL_VALUE);
  _builder.setDataField<bool>(
      64 * ::capnp::ELEMENTS, value);
}


// Value::Body::int8Value@3: Int8;  # bits[64, 72), union tag = 2

inline  ::int8_t Value::Body::Reader::getInt8Value() const {
  KJ_IREQUIRE(which() == Body::INT8_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::int8_t>(
      8 * ::capnp::ELEMENTS);
}

inline  ::int8_t Value::Body::Builder::getInt8Value() {
  KJ_IREQUIRE(which() == Body::INT8_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::int8_t>(
      8 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setInt8Value( ::int8_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT8_VALUE);
  _builder.setDataField< ::int8_t>(
      8 * ::capnp::ELEMENTS, value);
}


// Value::Body::int16Value@4: Int16;  # bits[64, 80), union tag = 3

inline  ::int16_t Value::Body::Reader::getInt16Value() const {
  KJ_IREQUIRE(which() == Body::INT16_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::int16_t>(
      4 * ::capnp::ELEMENTS);
}

inline  ::int16_t Value::Body::Builder::getInt16Value() {
  KJ_IREQUIRE(which() == Body::INT16_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::int16_t>(
      4 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setInt16Value( ::int16_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT16_VALUE);
  _builder.setDataField< ::int16_t>(
      4 * ::capnp::ELEMENTS, value);
}


// Value::Body::int32Value@5: Int32;  # bits[64, 96), union tag = 4

inline  ::int32_t Value::Body::Reader::getInt32Value() const {
  KJ_IREQUIRE(which() == Body::INT32_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::int32_t>(
      2 * ::capnp::ELEMENTS);
}

inline  ::int32_t Value::Body::Builder::getInt32Value() {
  KJ_IREQUIRE(which() == Body::INT32_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::int32_t>(
      2 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setInt32Value( ::int32_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT32_VALUE);
  _builder.setDataField< ::int32_t>(
      2 * ::capnp::ELEMENTS, value);
}


// Value::Body::int64Value@6: Int64;  # bits[64, 128), union tag = 5

inline  ::int64_t Value::Body::Reader::getInt64Value() const {
  KJ_IREQUIRE(which() == Body::INT64_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::int64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::int64_t Value::Body::Builder::getInt64Value() {
  KJ_IREQUIRE(which() == Body::INT64_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::int64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setInt64Value( ::int64_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INT64_VALUE);
  _builder.setDataField< ::int64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// Value::Body::uint8Value@7: UInt8;  # bits[64, 72), union tag = 6

inline  ::uint8_t Value::Body::Reader::getUint8Value() const {
  KJ_IREQUIRE(which() == Body::UINT8_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint8_t>(
      8 * ::capnp::ELEMENTS);
}

inline  ::uint8_t Value::Body::Builder::getUint8Value() {
  KJ_IREQUIRE(which() == Body::UINT8_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint8_t>(
      8 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setUint8Value( ::uint8_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT8_VALUE);
  _builder.setDataField< ::uint8_t>(
      8 * ::capnp::ELEMENTS, value);
}


// Value::Body::uint16Value@8: UInt16;  # bits[64, 80), union tag = 7

inline  ::uint16_t Value::Body::Reader::getUint16Value() const {
  KJ_IREQUIRE(which() == Body::UINT16_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint16_t>(
      4 * ::capnp::ELEMENTS);
}

inline  ::uint16_t Value::Body::Builder::getUint16Value() {
  KJ_IREQUIRE(which() == Body::UINT16_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint16_t>(
      4 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setUint16Value( ::uint16_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT16_VALUE);
  _builder.setDataField< ::uint16_t>(
      4 * ::capnp::ELEMENTS, value);
}


// Value::Body::uint32Value@9: UInt32;  # bits[64, 96), union tag = 8

inline  ::uint32_t Value::Body::Reader::getUint32Value() const {
  KJ_IREQUIRE(which() == Body::UINT32_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}

inline  ::uint32_t Value::Body::Builder::getUint32Value() {
  KJ_IREQUIRE(which() == Body::UINT32_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setUint32Value( ::uint32_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT32_VALUE);
  _builder.setDataField< ::uint32_t>(
      2 * ::capnp::ELEMENTS, value);
}


// Value::Body::uint64Value@1: UInt64;  # bits[64, 128), union tag = 0

inline  ::uint64_t Value::Body::Reader::getUint64Value() const {
  KJ_IREQUIRE(which() == Body::UINT64_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Value::Body::Builder::getUint64Value() {
  KJ_IREQUIRE(which() == Body::UINT64_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setUint64Value( ::uint64_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::UINT64_VALUE);
  _builder.setDataField< ::uint64_t>(
      1 * ::capnp::ELEMENTS, value);
}


// Value::Body::float32Value@11: Float32;  # bits[64, 96), union tag = 10

inline float Value::Body::Reader::getFloat32Value() const {
  KJ_IREQUIRE(which() == Body::FLOAT32_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField<float>(
      2 * ::capnp::ELEMENTS);
}

inline float Value::Body::Builder::getFloat32Value() {
  KJ_IREQUIRE(which() == Body::FLOAT32_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField<float>(
      2 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setFloat32Value(float value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::FLOAT32_VALUE);
  _builder.setDataField<float>(
      2 * ::capnp::ELEMENTS, value);
}


// Value::Body::float64Value@12: Float64;  # bits[64, 128), union tag = 11

inline double Value::Body::Reader::getFloat64Value() const {
  KJ_IREQUIRE(which() == Body::FLOAT64_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField<double>(
      1 * ::capnp::ELEMENTS);
}

inline double Value::Body::Builder::getFloat64Value() {
  KJ_IREQUIRE(which() == Body::FLOAT64_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField<double>(
      1 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setFloat64Value(double value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::FLOAT64_VALUE);
  _builder.setDataField<double>(
      1 * ::capnp::ELEMENTS, value);
}


// Value::Body::textValue@13: Text;  # ptr[0], union tag = 12


inline bool Value::Body::Reader::hasTextValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Value::Body::Builder::hasTextValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader Value::Body::Reader::getTextValue() const {
  KJ_IREQUIRE(which() == Body::TEXT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder Value::Body::Builder::getTextValue() {
  KJ_IREQUIRE(which() == Body::TEXT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Value::Body::Builder::setTextValue( ::capnp::Text::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::TEXT_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder Value::Body::Builder::initTextValue(unsigned int size) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::TEXT_VALUE);
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void Value::Body::Builder::adoptTextValue(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::TEXT_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> Value::Body::Builder::disownTextValue() {
  KJ_IREQUIRE(which() == Body::TEXT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Value::Body::dataValue@14: Data;  # ptr[0], union tag = 13


inline bool Value::Body::Reader::hasDataValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Value::Body::Builder::hasDataValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Data::Reader Value::Body::Reader::getDataValue() const {
  KJ_IREQUIRE(which() == Body::DATA_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Data>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Data::Builder Value::Body::Builder::getDataValue() {
  KJ_IREQUIRE(which() == Body::DATA_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Data>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Value::Body::Builder::setDataValue( ::capnp::Data::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::DATA_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::Data>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Data::Builder Value::Body::Builder::initDataValue(unsigned int size) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::DATA_VALUE);
  return ::capnp::_::PointerHelpers< ::capnp::Data>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void Value::Body::Builder::adoptDataValue(
    ::capnp::Orphan< ::capnp::Data>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::DATA_VALUE);
  ::capnp::_::PointerHelpers< ::capnp::Data>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Data> Value::Body::Builder::disownDataValue() {
  KJ_IREQUIRE(which() == Body::DATA_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::Data>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// Value::Body::listValue@15: Object;  # ptr[0], union tag = 14


inline bool Value::Body::Reader::hasListValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Value::Body::Builder::hasListValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}


template <typename T>
inline typename T::Reader Value::Body::Reader::getListValue() const {
  KJ_IREQUIRE(which() == Body::LIST_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::get(
      _reader, 0 * ::capnp::POINTERS);
}

template <typename T>
inline typename T::Builder Value::Body::Builder::getListValue() {
  KJ_IREQUIRE(which() == Body::LIST_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::get(
      _builder, 0 * ::capnp::POINTERS);
}

template <typename T, typename Param>
inline typename T::Reader Value::Body::Reader::getListValue(Param&& param) const {
  KJ_IREQUIRE(which() == Body::LIST_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::getDynamic(
      _reader, 0 * ::capnp::POINTERS, ::kj::fwd<Param>(param));
}

template <typename T, typename Param>
inline typename T::Builder Value::Body::Builder::getListValue(Param&& param) {
  KJ_IREQUIRE(which() == Body::LIST_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::getDynamic(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Param>(param));
}

template <typename T>
inline void Value::Body::Builder::setListValue(typename T::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST_VALUE);
  ::capnp::_::PointerHelpers<T>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

template <typename T, typename U>
inline void Value::Body::Builder::setListValue(std::initializer_list<U> value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST_VALUE);
  ::capnp::_::PointerHelpers<T>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

template <typename T, typename... Params>
inline typename T::Builder Value::Body::Builder::initListValue(Params&&... params) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST_VALUE);
  return ::capnp::_::PointerHelpers<T>::init(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);
}

template <typename T>
void Value::Body::Builder::adoptListValue(::capnp::Orphan<T>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::LIST_VALUE);
  ::capnp::_::PointerHelpers<T>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

template <typename T, typename... Params>
::capnp::Orphan<T> Value::Body::Builder::disownListValue(Params&&... params) {
  KJ_IREQUIRE(which() == Body::LIST_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::disown(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);
}


// Value::Body::enumValue@16: UInt16;  # bits[64, 80), union tag = 15

inline  ::uint16_t Value::Body::Reader::getEnumValue() const {
  KJ_IREQUIRE(which() == Body::ENUM_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::uint16_t>(
      4 * ::capnp::ELEMENTS);
}

inline  ::uint16_t Value::Body::Builder::getEnumValue() {
  KJ_IREQUIRE(which() == Body::ENUM_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::uint16_t>(
      4 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setEnumValue( ::uint16_t value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::ENUM_VALUE);
  _builder.setDataField< ::uint16_t>(
      4 * ::capnp::ELEMENTS, value);
}


// Value::Body::structValue@17: Object;  # ptr[0], union tag = 16


inline bool Value::Body::Reader::hasStructValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Value::Body::Builder::hasStructValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}


template <typename T>
inline typename T::Reader Value::Body::Reader::getStructValue() const {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::get(
      _reader, 0 * ::capnp::POINTERS);
}

template <typename T>
inline typename T::Builder Value::Body::Builder::getStructValue() {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::get(
      _builder, 0 * ::capnp::POINTERS);
}

template <typename T, typename Param>
inline typename T::Reader Value::Body::Reader::getStructValue(Param&& param) const {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::getDynamic(
      _reader, 0 * ::capnp::POINTERS, ::kj::fwd<Param>(param));
}

template <typename T, typename Param>
inline typename T::Builder Value::Body::Builder::getStructValue(Param&& param) {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::getDynamic(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Param>(param));
}

template <typename T>
inline void Value::Body::Builder::setStructValue(typename T::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_VALUE);
  ::capnp::_::PointerHelpers<T>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

template <typename T, typename U>
inline void Value::Body::Builder::setStructValue(std::initializer_list<U> value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_VALUE);
  ::capnp::_::PointerHelpers<T>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

template <typename T, typename... Params>
inline typename T::Builder Value::Body::Builder::initStructValue(Params&&... params) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_VALUE);
  return ::capnp::_::PointerHelpers<T>::init(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);
}

template <typename T>
void Value::Body::Builder::adoptStructValue(::capnp::Orphan<T>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::STRUCT_VALUE);
  ::capnp::_::PointerHelpers<T>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

template <typename T, typename... Params>
::capnp::Orphan<T> Value::Body::Builder::disownStructValue(Params&&... params) {
  KJ_IREQUIRE(which() == Body::STRUCT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::disown(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);
}


// Value::Body::interfaceValue@18: Void;  # (none), union tag = 17

inline  ::capnp::Void Value::Body::Reader::getInterfaceValue() const {
  KJ_IREQUIRE(which() == Body::INTERFACE_VALUE,
              "Must check which() before get()ing a union member.");
  return _reader.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}

inline  ::capnp::Void Value::Body::Builder::getInterfaceValue() {
  KJ_IREQUIRE(which() == Body::INTERFACE_VALUE,
              "Must check which() before get()ing a union member.");
  return _builder.getDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS);
}
inline void Value::Body::Builder::setInterfaceValue( ::capnp::Void value = ::capnp::Void::VOID) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::INTERFACE_VALUE);
  _builder.setDataField< ::capnp::Void>(
      0 * ::capnp::ELEMENTS, value);
}


// Value::Body::objectValue@19: Object;  # ptr[0], union tag = 18


inline bool Value::Body::Reader::hasObjectValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Value::Body::Builder::hasObjectValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}


template <typename T>
inline typename T::Reader Value::Body::Reader::getObjectValue() const {
  KJ_IREQUIRE(which() == Body::OBJECT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::get(
      _reader, 0 * ::capnp::POINTERS);
}

template <typename T>
inline typename T::Builder Value::Body::Builder::getObjectValue() {
  KJ_IREQUIRE(which() == Body::OBJECT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::get(
      _builder, 0 * ::capnp::POINTERS);
}

template <typename T, typename Param>
inline typename T::Reader Value::Body::Reader::getObjectValue(Param&& param) const {
  KJ_IREQUIRE(which() == Body::OBJECT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::getDynamic(
      _reader, 0 * ::capnp::POINTERS, ::kj::fwd<Param>(param));
}

template <typename T, typename Param>
inline typename T::Builder Value::Body::Builder::getObjectValue(Param&& param) {
  KJ_IREQUIRE(which() == Body::OBJECT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::getDynamic(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Param>(param));
}

template <typename T>
inline void Value::Body::Builder::setObjectValue(typename T::Reader value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::OBJECT_VALUE);
  ::capnp::_::PointerHelpers<T>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

template <typename T, typename U>
inline void Value::Body::Builder::setObjectValue(std::initializer_list<U> value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::OBJECT_VALUE);
  ::capnp::_::PointerHelpers<T>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

template <typename T, typename... Params>
inline typename T::Builder Value::Body::Builder::initObjectValue(Params&&... params) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::OBJECT_VALUE);
  return ::capnp::_::PointerHelpers<T>::init(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);
}

template <typename T>
void Value::Body::Builder::adoptObjectValue(::capnp::Orphan<T>&& value) {
  _builder.setDataField<Body::Which>(
      0 * ::capnp::ELEMENTS, Body::OBJECT_VALUE);
  ::capnp::_::PointerHelpers<T>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

template <typename T, typename... Params>
::capnp::Orphan<T> Value::Body::Builder::disownObjectValue(Params&&... params) {
  KJ_IREQUIRE(which() == Body::OBJECT_VALUE,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers<T>::disown(
      _builder, 0 * ::capnp::POINTERS, ::kj::fwd<Params>(params)...);
}



// Annotation::id@0: UInt64;  # bits[0, 64)

inline  ::uint64_t Annotation::Reader::getId() const {
  return _reader.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint64_t Annotation::Builder::getId() {
  return _builder.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}
inline void Annotation::Builder::setId( ::uint64_t value) {
  _builder.setDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS, value);
}


// Annotation::value@1: .Value;  # ptr[0]


inline bool Annotation::Reader::hasValue() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool Annotation::Builder::hasValue() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Reader Annotation::Reader::getValue() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Builder Annotation::Builder::getValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void Annotation::Builder::setValue( ::capnp::schema::Value::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Value::Builder Annotation::Builder::initValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void Annotation::Builder::adoptValue(
    ::capnp::Orphan< ::capnp::schema::Value>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Value> Annotation::Builder::disownValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// FileNode::imports@0: List(.FileNode.Import);  # ptr[0]


inline bool FileNode::Reader::hasImports() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool FileNode::Builder::hasImports() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::FileNode::Import>::Reader FileNode::Reader::getImports() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::FileNode::Import>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::FileNode::Import>::Builder FileNode::Builder::getImports() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::FileNode::Import>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void FileNode::Builder::setImports( ::capnp::List< ::capnp::schema::FileNode::Import>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::FileNode::Import>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::FileNode::Import>::Builder FileNode::Builder::initImports(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::FileNode::Import>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void FileNode::Builder::adoptImports(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::FileNode::Import>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::FileNode::Import>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::FileNode::Import>> FileNode::Builder::disownImports() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::FileNode::Import>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// FileNode::Import::id@0: UInt64;  # bits[0, 64)

inline  ::uint64_t FileNode::Import::Reader::getId() const {
  return _reader.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint64_t FileNode::Import::Builder::getId() {
  return _builder.getDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS);
}
inline void FileNode::Import::Builder::setId( ::uint64_t value) {
  _builder.setDataField< ::uint64_t>(
      0 * ::capnp::ELEMENTS, value);
}


// FileNode::Import::name@1: Text;  # ptr[0]


inline bool FileNode::Import::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool FileNode::Import::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader FileNode::Import::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder FileNode::Import::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void FileNode::Import::Builder::setName( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder FileNode::Import::Builder::initName(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void FileNode::Import::Builder::adoptName(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> FileNode::Import::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// StructNode::dataSectionWordSize@0: UInt16;  # bits[0, 16)

inline  ::uint16_t StructNode::Reader::getDataSectionWordSize() const {
  return _reader.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint16_t StructNode::Builder::getDataSectionWordSize() {
  return _builder.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}
inline void StructNode::Builder::setDataSectionWordSize( ::uint16_t value) {
  _builder.setDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS, value);
}


// StructNode::pointerSectionSize@1: UInt16;  # bits[16, 32)

inline  ::uint16_t StructNode::Reader::getPointerSectionSize() const {
  return _reader.getDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint16_t StructNode::Builder::getPointerSectionSize() {
  return _builder.getDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS);
}
inline void StructNode::Builder::setPointerSectionSize( ::uint16_t value) {
  _builder.setDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS, value);
}


// StructNode::preferredListEncoding@2: .ElementSize;  # bits[32, 48)

inline  ::capnp::schema::ElementSize StructNode::Reader::getPreferredListEncoding() const {
  return _reader.getDataField< ::capnp::schema::ElementSize>(
      2 * ::capnp::ELEMENTS);
}

inline  ::capnp::schema::ElementSize StructNode::Builder::getPreferredListEncoding() {
  return _builder.getDataField< ::capnp::schema::ElementSize>(
      2 * ::capnp::ELEMENTS);
}
inline void StructNode::Builder::setPreferredListEncoding( ::capnp::schema::ElementSize value) {
  _builder.setDataField< ::capnp::schema::ElementSize>(
      2 * ::capnp::ELEMENTS, value);
}


// StructNode::members@3: List(.StructNode.Member);  # ptr[0]


inline bool StructNode::Reader::hasMembers() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool StructNode::Builder::hasMembers() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Reader StructNode::Reader::getMembers() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder StructNode::Builder::getMembers() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void StructNode::Builder::setMembers( ::capnp::List< ::capnp::schema::StructNode::Member>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder StructNode::Builder::initMembers(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void StructNode::Builder::adoptMembers(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>> StructNode::Builder::disownMembers() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



inline StructNode::Member::Body::Reader StructNode::Member::Reader::getBody() const {
  return StructNode::Member::Body::Reader(_reader);
}

inline StructNode::Member::Body::Builder StructNode::Member::Builder::getBody() {
  return StructNode::Member::Body::Builder(_builder);
}


// StructNode::Member::name@0: Text;  # ptr[0]


inline bool StructNode::Member::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool StructNode::Member::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader StructNode::Member::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder StructNode::Member::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void StructNode::Member::Builder::setName( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder StructNode::Member::Builder::initName(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void StructNode::Member::Builder::adoptName(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> StructNode::Member::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// StructNode::Member::ordinal@1: UInt16;  # bits[0, 16)

inline  ::uint16_t StructNode::Member::Reader::getOrdinal() const {
  return _reader.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint16_t StructNode::Member::Builder::getOrdinal() {
  return _builder.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}
inline void StructNode::Member::Builder::setOrdinal( ::uint16_t value) {
  _builder.setDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS, value);
}


// StructNode::Member::codeOrder@2: UInt16;  # bits[16, 32)

inline  ::uint16_t StructNode::Member::Reader::getCodeOrder() const {
  return _reader.getDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint16_t StructNode::Member::Builder::getCodeOrder() {
  return _builder.getDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS);
}
inline void StructNode::Member::Builder::setCodeOrder( ::uint16_t value) {
  _builder.setDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS, value);
}


// StructNode::Member::annotations@3: List(.Annotation);  # ptr[1]


inline bool StructNode::Member::Reader::hasAnnotations() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool StructNode::Member::Builder::hasAnnotations() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Reader StructNode::Member::Reader::getAnnotations() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder StructNode::Member::Builder::getAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void StructNode::Member::Builder::setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder StructNode::Member::Builder::initAnnotations(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::init(
      _builder, 1 * ::capnp::POINTERS, size);
}


inline void StructNode::Member::Builder::adoptAnnotations(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> StructNode::Member::Builder::disownAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// StructNode::Member::Body
inline StructNode::Member::Body::Which StructNode::Member::Body::Reader::which() const {
  return _reader.getDataField<Which>(2 * ::capnp::ELEMENTS);
}

inline StructNode::Member::Body::Which StructNode::Member::Body::Builder::which() {
  return _builder.getDataField<Which>(2 * ::capnp::ELEMENTS);
}


// StructNode::Member::Body::fieldMember@5: .StructNode.Field;  # ptr[2], union tag = 0


inline bool StructNode::Member::Body::Reader::hasFieldMember() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool StructNode::Member::Body::Builder::hasFieldMember() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Field::Reader StructNode::Member::Body::Reader::getFieldMember() const {
  KJ_IREQUIRE(which() == Body::FIELD_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Field>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Field::Builder StructNode::Member::Body::Builder::getFieldMember() {
  KJ_IREQUIRE(which() == Body::FIELD_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Field>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void StructNode::Member::Body::Builder::setFieldMember( ::capnp::schema::StructNode::Field::Reader value) {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::FIELD_MEMBER);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Field>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::StructNode::Field::Builder StructNode::Member::Body::Builder::initFieldMember() {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::FIELD_MEMBER);
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Field>::init(
      _builder, 2 * ::capnp::POINTERS);
}


inline void StructNode::Member::Body::Builder::adoptFieldMember(
    ::capnp::Orphan< ::capnp::schema::StructNode::Field>&& value) {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::FIELD_MEMBER);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Field>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::StructNode::Field> StructNode::Member::Body::Builder::disownFieldMember() {
  KJ_IREQUIRE(which() == Body::FIELD_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Field>::disown(
      _builder, 2 * ::capnp::POINTERS);
}



// StructNode::Member::Body::unionMember@6: .StructNode.Union;  # ptr[2], union tag = 1


inline bool StructNode::Member::Body::Reader::hasUnionMember() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool StructNode::Member::Body::Builder::hasUnionMember() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Union::Reader StructNode::Member::Body::Reader::getUnionMember() const {
  KJ_IREQUIRE(which() == Body::UNION_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Union>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Union::Builder StructNode::Member::Body::Builder::getUnionMember() {
  KJ_IREQUIRE(which() == Body::UNION_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Union>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void StructNode::Member::Body::Builder::setUnionMember( ::capnp::schema::StructNode::Union::Reader value) {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::UNION_MEMBER);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Union>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::StructNode::Union::Builder StructNode::Member::Body::Builder::initUnionMember() {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::UNION_MEMBER);
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Union>::init(
      _builder, 2 * ::capnp::POINTERS);
}


inline void StructNode::Member::Body::Builder::adoptUnionMember(
    ::capnp::Orphan< ::capnp::schema::StructNode::Union>&& value) {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::UNION_MEMBER);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Union>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::StructNode::Union> StructNode::Member::Body::Builder::disownUnionMember() {
  KJ_IREQUIRE(which() == Body::UNION_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Union>::disown(
      _builder, 2 * ::capnp::POINTERS);
}



// StructNode::Member::Body::groupMember@7: .StructNode.Group;  # ptr[2], union tag = 2


inline bool StructNode::Member::Body::Reader::hasGroupMember() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool StructNode::Member::Body::Builder::hasGroupMember() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Group::Reader StructNode::Member::Body::Reader::getGroupMember() const {
  KJ_IREQUIRE(which() == Body::GROUP_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Group>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::StructNode::Group::Builder StructNode::Member::Body::Builder::getGroupMember() {
  KJ_IREQUIRE(which() == Body::GROUP_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Group>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void StructNode::Member::Body::Builder::setGroupMember( ::capnp::schema::StructNode::Group::Reader value) {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::GROUP_MEMBER);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Group>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::StructNode::Group::Builder StructNode::Member::Body::Builder::initGroupMember() {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::GROUP_MEMBER);
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Group>::init(
      _builder, 2 * ::capnp::POINTERS);
}


inline void StructNode::Member::Body::Builder::adoptGroupMember(
    ::capnp::Orphan< ::capnp::schema::StructNode::Group>&& value) {
  _builder.setDataField<Body::Which>(
      2 * ::capnp::ELEMENTS, Body::GROUP_MEMBER);
  ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Group>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::StructNode::Group> StructNode::Member::Body::Builder::disownGroupMember() {
  KJ_IREQUIRE(which() == Body::GROUP_MEMBER,
              "Must check which() before get()ing a union member.");
  return ::capnp::_::PointerHelpers< ::capnp::schema::StructNode::Group>::disown(
      _builder, 2 * ::capnp::POINTERS);
}




// StructNode::Field::offset@0: UInt32;  # bits[0, 32)

inline  ::uint32_t StructNode::Field::Reader::getOffset() const {
  return _reader.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint32_t StructNode::Field::Builder::getOffset() {
  return _builder.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}
inline void StructNode::Field::Builder::setOffset( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS, value);
}


// StructNode::Field::type@1: .Type;  # ptr[0]


inline bool StructNode::Field::Reader::hasType() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool StructNode::Field::Builder::hasType() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Reader StructNode::Field::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Builder StructNode::Field::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void StructNode::Field::Builder::setType( ::capnp::schema::Type::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Type::Builder StructNode::Field::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void StructNode::Field::Builder::adoptType(
    ::capnp::Orphan< ::capnp::schema::Type>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Type> StructNode::Field::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// StructNode::Field::defaultValue@2: .Value;  # ptr[1]


inline bool StructNode::Field::Reader::hasDefaultValue() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool StructNode::Field::Builder::hasDefaultValue() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Reader StructNode::Field::Reader::getDefaultValue() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Builder StructNode::Field::Builder::getDefaultValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void StructNode::Field::Builder::setDefaultValue( ::capnp::schema::Value::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Value::Builder StructNode::Field::Builder::initDefaultValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void StructNode::Field::Builder::adoptDefaultValue(
    ::capnp::Orphan< ::capnp::schema::Value>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Value> StructNode::Field::Builder::disownDefaultValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::disown(
      _builder, 1 * ::capnp::POINTERS);
}




// StructNode::Union::discriminantOffset@0: UInt32;  # bits[0, 32)

inline  ::uint32_t StructNode::Union::Reader::getDiscriminantOffset() const {
  return _reader.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint32_t StructNode::Union::Builder::getDiscriminantOffset() {
  return _builder.getDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS);
}
inline void StructNode::Union::Builder::setDiscriminantOffset( ::uint32_t value) {
  _builder.setDataField< ::uint32_t>(
      0 * ::capnp::ELEMENTS, value);
}


// StructNode::Union::members@1: List(.StructNode.Member);  # ptr[0]


inline bool StructNode::Union::Reader::hasMembers() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool StructNode::Union::Builder::hasMembers() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Reader StructNode::Union::Reader::getMembers() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder StructNode::Union::Builder::getMembers() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void StructNode::Union::Builder::setMembers( ::capnp::List< ::capnp::schema::StructNode::Member>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder StructNode::Union::Builder::initMembers(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void StructNode::Union::Builder::adoptMembers(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>> StructNode::Union::Builder::disownMembers() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// StructNode::Group::members@0: List(.StructNode.Member);  # ptr[0]


inline bool StructNode::Group::Reader::hasMembers() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool StructNode::Group::Builder::hasMembers() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Reader StructNode::Group::Reader::getMembers() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder StructNode::Group::Builder::getMembers() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void StructNode::Group::Builder::setMembers( ::capnp::List< ::capnp::schema::StructNode::Member>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::StructNode::Member>::Builder StructNode::Group::Builder::initMembers(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void StructNode::Group::Builder::adoptMembers(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::StructNode::Member>> StructNode::Group::Builder::disownMembers() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::StructNode::Member>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// EnumNode::enumerants@0: List(.EnumNode.Enumerant);  # ptr[0]


inline bool EnumNode::Reader::hasEnumerants() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool EnumNode::Builder::hasEnumerants() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Reader EnumNode::Reader::getEnumerants() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Builder EnumNode::Builder::getEnumerants() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void EnumNode::Builder::setEnumerants( ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::EnumNode::Enumerant>::Builder EnumNode::Builder::initEnumerants(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void EnumNode::Builder::adoptEnumerants(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>> EnumNode::Builder::disownEnumerants() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::EnumNode::Enumerant>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// EnumNode::Enumerant::name@0: Text;  # ptr[0]


inline bool EnumNode::Enumerant::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool EnumNode::Enumerant::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader EnumNode::Enumerant::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder EnumNode::Enumerant::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void EnumNode::Enumerant::Builder::setName( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder EnumNode::Enumerant::Builder::initName(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void EnumNode::Enumerant::Builder::adoptName(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> EnumNode::Enumerant::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// EnumNode::Enumerant::codeOrder@1: UInt16;  # bits[0, 16)

inline  ::uint16_t EnumNode::Enumerant::Reader::getCodeOrder() const {
  return _reader.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint16_t EnumNode::Enumerant::Builder::getCodeOrder() {
  return _builder.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}
inline void EnumNode::Enumerant::Builder::setCodeOrder( ::uint16_t value) {
  _builder.setDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS, value);
}


// EnumNode::Enumerant::annotations@2: List(.Annotation);  # ptr[1]


inline bool EnumNode::Enumerant::Reader::hasAnnotations() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool EnumNode::Enumerant::Builder::hasAnnotations() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Reader EnumNode::Enumerant::Reader::getAnnotations() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder EnumNode::Enumerant::Builder::getAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void EnumNode::Enumerant::Builder::setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder EnumNode::Enumerant::Builder::initAnnotations(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::init(
      _builder, 1 * ::capnp::POINTERS, size);
}


inline void EnumNode::Enumerant::Builder::adoptAnnotations(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> EnumNode::Enumerant::Builder::disownAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::disown(
      _builder, 1 * ::capnp::POINTERS);
}




// InterfaceNode::methods@0: List(.InterfaceNode.Method);  # ptr[0]


inline bool InterfaceNode::Reader::hasMethods() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Builder::hasMethods() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Reader InterfaceNode::Reader::getMethods() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Builder InterfaceNode::Builder::getMethods() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void InterfaceNode::Builder::setMethods( ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method>::Builder InterfaceNode::Builder::initMethods(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void InterfaceNode::Builder::adoptMethods(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method>> InterfaceNode::Builder::disownMethods() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}




// InterfaceNode::Method::name@0: Text;  # ptr[0]


inline bool InterfaceNode::Method::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader InterfaceNode::Method::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder InterfaceNode::Method::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Builder::setName( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder InterfaceNode::Method::Builder::initName(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void InterfaceNode::Method::Builder::adoptName(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> InterfaceNode::Method::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// InterfaceNode::Method::codeOrder@1: UInt16;  # bits[0, 16)

inline  ::uint16_t InterfaceNode::Method::Reader::getCodeOrder() const {
  return _reader.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}

inline  ::uint16_t InterfaceNode::Method::Builder::getCodeOrder() {
  return _builder.getDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS);
}
inline void InterfaceNode::Method::Builder::setCodeOrder( ::uint16_t value) {
  _builder.setDataField< ::uint16_t>(
      0 * ::capnp::ELEMENTS, value);
}


// InterfaceNode::Method::params@2: List(.InterfaceNode.Method.Param);  # ptr[1]


inline bool InterfaceNode::Method::Reader::hasParams() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Builder::hasParams() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Reader InterfaceNode::Method::Reader::getParams() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Builder InterfaceNode::Method::Builder::getParams() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Builder::setParams( ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>::Builder InterfaceNode::Method::Builder::initParams(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>::init(
      _builder, 1 * ::capnp::POINTERS, size);
}


inline void InterfaceNode::Method::Builder::adoptParams(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>> InterfaceNode::Method::Builder::disownParams() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::InterfaceNode::Method::Param>>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// InterfaceNode::Method::requiredParamCount@3: UInt16;  # bits[16, 32)

inline  ::uint16_t InterfaceNode::Method::Reader::getRequiredParamCount() const {
  return _reader.getDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS);
}

inline  ::uint16_t InterfaceNode::Method::Builder::getRequiredParamCount() {
  return _builder.getDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS);
}
inline void InterfaceNode::Method::Builder::setRequiredParamCount( ::uint16_t value) {
  _builder.setDataField< ::uint16_t>(
      1 * ::capnp::ELEMENTS, value);
}


// InterfaceNode::Method::returnType@4: .Type;  # ptr[2]


inline bool InterfaceNode::Method::Reader::hasReturnType() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Builder::hasReturnType() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Reader InterfaceNode::Method::Reader::getReturnType() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Builder InterfaceNode::Method::Builder::getReturnType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Builder::setReturnType( ::capnp::schema::Type::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Type::Builder InterfaceNode::Method::Builder::initReturnType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::init(
      _builder, 2 * ::capnp::POINTERS);
}


inline void InterfaceNode::Method::Builder::adoptReturnType(
    ::capnp::Orphan< ::capnp::schema::Type>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Type> InterfaceNode::Method::Builder::disownReturnType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::disown(
      _builder, 2 * ::capnp::POINTERS);
}



// InterfaceNode::Method::annotations@5: List(.Annotation);  # ptr[3]


inline bool InterfaceNode::Method::Reader::hasAnnotations() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Builder::hasAnnotations() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Reader InterfaceNode::Method::Reader::getAnnotations() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder InterfaceNode::Method::Builder::getAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Builder::setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder InterfaceNode::Method::Builder::initAnnotations(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::init(
      _builder, 3 * ::capnp::POINTERS, size);
}


inline void InterfaceNode::Method::Builder::adoptAnnotations(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> InterfaceNode::Method::Builder::disownAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::disown(
      _builder, 3 * ::capnp::POINTERS);
}




// InterfaceNode::Method::Param::name@0: Text;  # ptr[0]


inline bool InterfaceNode::Method::Param::Reader::hasName() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Param::Builder::hasName() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Reader InterfaceNode::Method::Param::Reader::getName() const {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::Text::Builder InterfaceNode::Method::Param::Builder::getName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Param::Builder::setName( ::capnp::Text::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::Text::Builder InterfaceNode::Method::Param::Builder::initName(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void InterfaceNode::Method::Param::Builder::adoptName(
    ::capnp::Orphan< ::capnp::Text>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::Text>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::Text> InterfaceNode::Method::Param::Builder::disownName() {
  return ::capnp::_::PointerHelpers< ::capnp::Text>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// InterfaceNode::Method::Param::type@1: .Type;  # ptr[1]


inline bool InterfaceNode::Method::Param::Reader::hasType() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Param::Builder::hasType() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Reader InterfaceNode::Method::Param::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Builder InterfaceNode::Method::Param::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Param::Builder::setType( ::capnp::schema::Type::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Type::Builder InterfaceNode::Method::Param::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void InterfaceNode::Method::Param::Builder::adoptType(
    ::capnp::Orphan< ::capnp::schema::Type>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Type> InterfaceNode::Method::Param::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::disown(
      _builder, 1 * ::capnp::POINTERS);
}



// InterfaceNode::Method::Param::defaultValue@2: .Value;  # ptr[2]


inline bool InterfaceNode::Method::Param::Reader::hasDefaultValue() const {
  return !_reader.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Param::Builder::hasDefaultValue() {
  return !_builder.isPointerFieldNull(2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Reader InterfaceNode::Method::Param::Reader::getDefaultValue() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _reader, 2 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Builder InterfaceNode::Method::Param::Builder::getDefaultValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _builder, 2 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Param::Builder::setDefaultValue( ::capnp::schema::Value::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::set(
      _builder, 2 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Value::Builder InterfaceNode::Method::Param::Builder::initDefaultValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::init(
      _builder, 2 * ::capnp::POINTERS);
}


inline void InterfaceNode::Method::Param::Builder::adoptDefaultValue(
    ::capnp::Orphan< ::capnp::schema::Value>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::adopt(
      _builder, 2 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Value> InterfaceNode::Method::Param::Builder::disownDefaultValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::disown(
      _builder, 2 * ::capnp::POINTERS);
}



// InterfaceNode::Method::Param::annotations@3: List(.Annotation);  # ptr[3]


inline bool InterfaceNode::Method::Param::Reader::hasAnnotations() const {
  return !_reader.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline bool InterfaceNode::Method::Param::Builder::hasAnnotations() {
  return !_builder.isPointerFieldNull(3 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Reader InterfaceNode::Method::Param::Reader::getAnnotations() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _reader, 3 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder InterfaceNode::Method::Param::Builder::getAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::get(
      _builder, 3 * ::capnp::POINTERS);
}

inline void InterfaceNode::Method::Param::Builder::setAnnotations( ::capnp::List< ::capnp::schema::Annotation>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::set(
      _builder, 3 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::Annotation>::Builder InterfaceNode::Method::Param::Builder::initAnnotations(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::init(
      _builder, 3 * ::capnp::POINTERS, size);
}


inline void InterfaceNode::Method::Param::Builder::adoptAnnotations(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::adopt(
      _builder, 3 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Annotation>> InterfaceNode::Method::Param::Builder::disownAnnotations() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Annotation>>::disown(
      _builder, 3 * ::capnp::POINTERS);
}




// ConstNode::type@0: .Type;  # ptr[0]


inline bool ConstNode::Reader::hasType() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool ConstNode::Builder::hasType() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Reader ConstNode::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Builder ConstNode::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void ConstNode::Builder::setType( ::capnp::schema::Type::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Type::Builder ConstNode::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void ConstNode::Builder::adoptType(
    ::capnp::Orphan< ::capnp::schema::Type>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Type> ConstNode::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// ConstNode::value@1: .Value;  # ptr[1]


inline bool ConstNode::Reader::hasValue() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool ConstNode::Builder::hasValue() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Reader ConstNode::Reader::getValue() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Value::Builder ConstNode::Builder::getValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void ConstNode::Builder::setValue( ::capnp::schema::Value::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Value::Builder ConstNode::Builder::initValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::init(
      _builder, 1 * ::capnp::POINTERS);
}


inline void ConstNode::Builder::adoptValue(
    ::capnp::Orphan< ::capnp::schema::Value>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Value>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Value> ConstNode::Builder::disownValue() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Value>::disown(
      _builder, 1 * ::capnp::POINTERS);
}




// AnnotationNode::type@0: .Type;  # ptr[0]


inline bool AnnotationNode::Reader::hasType() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool AnnotationNode::Builder::hasType() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Reader AnnotationNode::Reader::getType() const {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::schema::Type::Builder AnnotationNode::Builder::getType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void AnnotationNode::Builder::setType( ::capnp::schema::Type::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::schema::Type::Builder AnnotationNode::Builder::initType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::init(
      _builder, 0 * ::capnp::POINTERS);
}


inline void AnnotationNode::Builder::adoptType(
    ::capnp::Orphan< ::capnp::schema::Type>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::schema::Type>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::schema::Type> AnnotationNode::Builder::disownType() {
  return ::capnp::_::PointerHelpers< ::capnp::schema::Type>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// AnnotationNode::targetsFile@1: Bool;  # bits[0, 1)

inline bool AnnotationNode::Reader::getTargetsFile() const {
  return _reader.getDataField<bool>(
      0 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsFile() {
  return _builder.getDataField<bool>(
      0 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsFile(bool value) {
  _builder.setDataField<bool>(
      0 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsConst@2: Bool;  # bits[1, 2)

inline bool AnnotationNode::Reader::getTargetsConst() const {
  return _reader.getDataField<bool>(
      1 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsConst() {
  return _builder.getDataField<bool>(
      1 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsConst(bool value) {
  _builder.setDataField<bool>(
      1 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsEnum@3: Bool;  # bits[2, 3)

inline bool AnnotationNode::Reader::getTargetsEnum() const {
  return _reader.getDataField<bool>(
      2 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsEnum() {
  return _builder.getDataField<bool>(
      2 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsEnum(bool value) {
  _builder.setDataField<bool>(
      2 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsEnumerant@4: Bool;  # bits[3, 4)

inline bool AnnotationNode::Reader::getTargetsEnumerant() const {
  return _reader.getDataField<bool>(
      3 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsEnumerant() {
  return _builder.getDataField<bool>(
      3 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsEnumerant(bool value) {
  _builder.setDataField<bool>(
      3 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsStruct@5: Bool;  # bits[4, 5)

inline bool AnnotationNode::Reader::getTargetsStruct() const {
  return _reader.getDataField<bool>(
      4 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsStruct() {
  return _builder.getDataField<bool>(
      4 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsStruct(bool value) {
  _builder.setDataField<bool>(
      4 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsField@6: Bool;  # bits[5, 6)

inline bool AnnotationNode::Reader::getTargetsField() const {
  return _reader.getDataField<bool>(
      5 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsField() {
  return _builder.getDataField<bool>(
      5 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsField(bool value) {
  _builder.setDataField<bool>(
      5 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsUnion@7: Bool;  # bits[6, 7)

inline bool AnnotationNode::Reader::getTargetsUnion() const {
  return _reader.getDataField<bool>(
      6 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsUnion() {
  return _builder.getDataField<bool>(
      6 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsUnion(bool value) {
  _builder.setDataField<bool>(
      6 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsInterface@8: Bool;  # bits[7, 8)

inline bool AnnotationNode::Reader::getTargetsInterface() const {
  return _reader.getDataField<bool>(
      7 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsInterface() {
  return _builder.getDataField<bool>(
      7 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsInterface(bool value) {
  _builder.setDataField<bool>(
      7 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsMethod@9: Bool;  # bits[8, 9)

inline bool AnnotationNode::Reader::getTargetsMethod() const {
  return _reader.getDataField<bool>(
      8 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsMethod() {
  return _builder.getDataField<bool>(
      8 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsMethod(bool value) {
  _builder.setDataField<bool>(
      8 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsParam@10: Bool;  # bits[9, 10)

inline bool AnnotationNode::Reader::getTargetsParam() const {
  return _reader.getDataField<bool>(
      9 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsParam() {
  return _builder.getDataField<bool>(
      9 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsParam(bool value) {
  _builder.setDataField<bool>(
      9 * ::capnp::ELEMENTS, value);
}


// AnnotationNode::targetsAnnotation@11: Bool;  # bits[10, 11)

inline bool AnnotationNode::Reader::getTargetsAnnotation() const {
  return _reader.getDataField<bool>(
      10 * ::capnp::ELEMENTS);
}

inline bool AnnotationNode::Builder::getTargetsAnnotation() {
  return _builder.getDataField<bool>(
      10 * ::capnp::ELEMENTS);
}
inline void AnnotationNode::Builder::setTargetsAnnotation(bool value) {
  _builder.setDataField<bool>(
      10 * ::capnp::ELEMENTS, value);
}



// CodeGeneratorRequest::nodes@0: List(.Node);  # ptr[0]


inline bool CodeGeneratorRequest::Reader::hasNodes() const {
  return !_reader.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline bool CodeGeneratorRequest::Builder::hasNodes() {
  return !_builder.isPointerFieldNull(0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Node>::Reader CodeGeneratorRequest::Reader::getNodes() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node>>::get(
      _reader, 0 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::capnp::schema::Node>::Builder CodeGeneratorRequest::Builder::getNodes() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node>>::get(
      _builder, 0 * ::capnp::POINTERS);
}

inline void CodeGeneratorRequest::Builder::setNodes( ::capnp::List< ::capnp::schema::Node>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node>>::set(
      _builder, 0 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::capnp::schema::Node>::Builder CodeGeneratorRequest::Builder::initNodes(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node>>::init(
      _builder, 0 * ::capnp::POINTERS, size);
}


inline void CodeGeneratorRequest::Builder::adoptNodes(
    ::capnp::Orphan< ::capnp::List< ::capnp::schema::Node>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node>>::adopt(
      _builder, 0 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::capnp::schema::Node>> CodeGeneratorRequest::Builder::disownNodes() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::capnp::schema::Node>>::disown(
      _builder, 0 * ::capnp::POINTERS);
}



// CodeGeneratorRequest::requestedFiles@1: List(UInt64);  # ptr[1]


inline bool CodeGeneratorRequest::Reader::hasRequestedFiles() const {
  return !_reader.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline bool CodeGeneratorRequest::Builder::hasRequestedFiles() {
  return !_builder.isPointerFieldNull(1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::uint64_t>::Reader CodeGeneratorRequest::Reader::getRequestedFiles() const {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::uint64_t>>::get(
      _reader, 1 * ::capnp::POINTERS);
}

inline  ::capnp::List< ::uint64_t>::Builder CodeGeneratorRequest::Builder::getRequestedFiles() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::uint64_t>>::get(
      _builder, 1 * ::capnp::POINTERS);
}

inline void CodeGeneratorRequest::Builder::setRequestedFiles( ::capnp::List< ::uint64_t>::Reader value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::uint64_t>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline void CodeGeneratorRequest::Builder::setRequestedFiles(
    std::initializer_list< ::uint64_t> value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::uint64_t>>::set(
      _builder, 1 * ::capnp::POINTERS, value);
}

inline  ::capnp::List< ::uint64_t>::Builder CodeGeneratorRequest::Builder::initRequestedFiles(unsigned int size) {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::uint64_t>>::init(
      _builder, 1 * ::capnp::POINTERS, size);
}


inline void CodeGeneratorRequest::Builder::adoptRequestedFiles(
    ::capnp::Orphan< ::capnp::List< ::uint64_t>>&& value) {
  ::capnp::_::PointerHelpers< ::capnp::List< ::uint64_t>>::adopt(
      _builder, 1 * ::capnp::POINTERS, kj::mv(value));
}

inline ::capnp::Orphan< ::capnp::List< ::uint64_t>> CodeGeneratorRequest::Builder::disownRequestedFiles() {
  return ::capnp::_::PointerHelpers< ::capnp::List< ::uint64_t>>::disown(
      _builder, 1 * ::capnp::POINTERS);
}




}  // namespace
}  // namespace
#endif  // CAPNP_INCLUDED_c9e00273422ad7b91727f5b3ca1e8eb2
