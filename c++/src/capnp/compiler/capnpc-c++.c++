// Copyright (c) 2013-2014 Sandstorm Development Group, Inc. and contributors
// Licensed under the MIT License:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// This program is a code generator plugin for `capnp compile` which generates C++ code.

#include <capnp/schema.capnp.h>
#include "../serialize.h"
#include <kj/debug.h>
#include <kj/io.h>
#include <kj/string-tree.h>
#include <kj/tuple.h>
#include <kj/vector.h>
#include "../schema-loader.h"
#include "../dynamic.h"
#include <kj/miniposix.h>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <kj/main.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#if HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef VERSION
#define VERSION "(unknown)"
#endif

namespace capnp {
namespace {

static constexpr uint64_t NAMESPACE_ANNOTATION_ID = 0xb9c6f99ebf805f2cull;
static constexpr uint64_t NAME_ANNOTATION_ID = 0xf264a779fef191ceull;

bool hasDiscriminantValue(const schema::Field::Reader& reader) {
  return reader.getDiscriminantValue() != schema::Field::NO_DISCRIMINANT;
}

void enumerateDeps(schema::Type::Reader type, std::set<uint64_t>& deps) {
  switch (type.which()) {
    case schema::Type::STRUCT:
      deps.insert(type.getStruct().getTypeId());
      break;
    case schema::Type::ENUM:
      deps.insert(type.getEnum().getTypeId());
      break;
    case schema::Type::INTERFACE:
      deps.insert(type.getInterface().getTypeId());
      break;
    case schema::Type::LIST:
      enumerateDeps(type.getList().getElementType(), deps);
      break;
    default:
      break;
  }
}

void enumerateDeps(schema::Node::Reader node, std::set<uint64_t>& deps) {
  switch (node.which()) {
    case schema::Node::STRUCT: {
      auto structNode = node.getStruct();
      for (auto field: structNode.getFields()) {
        switch (field.which()) {
          case schema::Field::SLOT:
            enumerateDeps(field.getSlot().getType(), deps);
            break;
          case schema::Field::GROUP:
            deps.insert(field.getGroup().getTypeId());
            break;
        }
      }
      if (structNode.getIsGroup()) {
        deps.insert(node.getScopeId());
      }
      break;
    }
    case schema::Node::INTERFACE: {
      auto interfaceNode = node.getInterface();
      for (auto superclass: interfaceNode.getSuperclasses()) {
        deps.insert(superclass.getId());
      }
      for (auto method: interfaceNode.getMethods()) {
        deps.insert(method.getParamStructType());
        deps.insert(method.getResultStructType());
      }
      break;
    }
    default:
      break;
  }
}

struct OrderByName {
  template <typename T>
  inline bool operator()(const T& a, const T& b) const {
    return a.getProto().getName() < b.getProto().getName();
  }
};

template <typename MemberList>
kj::Array<uint> makeMembersByName(MemberList&& members) {
  auto sorted = KJ_MAP(member, members) { return member; };
  std::sort(sorted.begin(), sorted.end(), OrderByName());
  return KJ_MAP(member, sorted) { return member.getIndex(); };
}

kj::StringPtr baseName(kj::StringPtr path) {
  KJ_IF_MAYBE(slashPos, path.findLast('/')) {
    return path.slice(*slashPos + 1);
  } else {
    return path;
  }
}

kj::String safeIdentifier(kj::StringPtr identifier) {
  // Given a desired identifier name, munge it to make it safe for use in generated code.
  //
  // If the identifier is a keyword, this adds an underscore to the end.

  static const std::set<kj::StringPtr> keywords({
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
    "case", "catch", "char", "char16_t", "char32_t", "class", "compl", "const", "constexpr",
    "const_cast", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast",
    "else", "enum", "explicit", "export", "extern", "false", "float", "for", "friend", "goto",
    "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
    "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
    "reinterpret_cast", "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true",
    "try", "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
    "volatile", "wchar_t", "while", "xor", "xor_eq"
  });

  if (keywords.count(identifier) > 0) {
    return kj::str(identifier, '_');
  } else {
    return kj::heapString(identifier);
  }
}

// =======================================================================================

class CppTypeName {
  // Used to build a C++ type name string. This is complicated in the presence of templates,
  // because we must add the "typename" and "template" disambiguator keywords as needed.

public:
  inline CppTypeName(): isArgDependent(false), needsTypename(false),
                        hasInterfaces_(false), hasDisambiguatedTemplate_(false) {}
  CppTypeName(CppTypeName&& other) = default;
  CppTypeName(const CppTypeName& other)
      : name(kj::strTree(other.name.flatten())),
        isArgDependent(other.isArgDependent),
        needsTypename(other.needsTypename),
        hasInterfaces_(other.hasInterfaces_),
        hasDisambiguatedTemplate_(other.hasDisambiguatedTemplate_) {}

  CppTypeName& operator=(CppTypeName&& other) = default;
  CppTypeName& operator=(const CppTypeName& other) {
    name = kj::strTree(other.name.flatten());
    isArgDependent = other.isArgDependent;
    needsTypename = other.needsTypename;
    return *this;
  }

  static CppTypeName makeRoot() {
    return CppTypeName(kj::strTree(" "), false);
  }

  static CppTypeName makeNamespace(kj::StringPtr name) {
    return CppTypeName(kj::strTree(" ::", name), false);
  }

  static CppTypeName makeTemplateParam(kj::StringPtr name) {
    return CppTypeName(kj::strTree(name), true);
  }

  static CppTypeName makePrimitive(kj::StringPtr name) {
    return CppTypeName(kj::strTree(name), false);
  }

  void addMemberType(kj::StringPtr innerName) {
    // Append "::innerName" to refer to a member.

    name = kj::strTree(kj::mv(name), "::", innerName);
    needsTypename = isArgDependent;
  }

  void addMemberValue(kj::StringPtr innerName) {
    // Append "::innerName" to refer to a member.

    name = kj::strTree(kj::mv(name), "::", innerName);
    needsTypename = false;
  }

  bool hasDisambiguatedTemplate() {
    return hasDisambiguatedTemplate_;
  }

  void addMemberTemplate(kj::StringPtr innerName, kj::Array<CppTypeName>&& params) {
    // Append "::innerName<params, ...>".
    //
    // If necessary, add the "template" disambiguation keyword in front of `innerName`.

    bool parentIsArgDependent = isArgDependent;
    needsTypename = parentIsArgDependent;
    hasDisambiguatedTemplate_ = hasDisambiguatedTemplate_ || parentIsArgDependent;

    name = kj::strTree(kj::mv(name),
        parentIsArgDependent ? "::template " : "::",
        innerName, '<',
        kj::StringTree(KJ_MAP(p, params) {
          if (p.isArgDependent) isArgDependent = true;
          if (p.hasInterfaces_) hasInterfaces_ = true;
          if (p.hasDisambiguatedTemplate_) hasDisambiguatedTemplate_ = true;
          return kj::strTree(kj::mv(p));
        }, ", "),
        '>');
  }

  void setHasInterfaces() {
    hasInterfaces_ = true;
  }

  bool hasInterfaces() {
    return hasInterfaces_;
  }

  kj::StringTree strNoTypename() const & { return name.flatten(); }
  kj::StringTree strNoTypename() && { return kj::mv(name); }
  // Stringify but never prefix with `typename`. Use in contexts where `typename` is implicit.

private:
  kj::StringTree name;

  bool isArgDependent;
  // Does the name contain any template-argument-dependent types?

  bool needsTypename;
  // Does the name require a prefix of "typename"?

  bool hasInterfaces_;
  // Does this type name refer to any interface types? If so it may need to be #ifdefed out in
  // lite mode.

  bool hasDisambiguatedTemplate_;
  // Whether the type name contains a template type that had to be disambiguated using the
  // "template" keyword, e.g. "Foo<T>::template Bar<U>".
  //
  // TODO(msvc): We only track this because MSVC seems to get confused by it in some weird cases.

  inline CppTypeName(kj::StringTree&& name, bool isArgDependent)
      : name(kj::mv(name)), isArgDependent(isArgDependent), needsTypename(false),
        hasInterfaces_(false), hasDisambiguatedTemplate_(false) {}

  friend kj::StringTree KJ_STRINGIFY(CppTypeName&& typeName);
  friend kj::String KJ_STRINGIFY(const CppTypeName& typeName);
};

kj::StringTree KJ_STRINGIFY(CppTypeName&& typeName) {
  if (typeName.needsTypename) {
    return kj::strTree("typename ", kj::mv(typeName.name));
  } else {
    return kj::mv(typeName.name);
  }
}
kj::String KJ_STRINGIFY(const CppTypeName& typeName) {
  if (typeName.needsTypename) {
    return kj::str("typename ", typeName.name);
  } else {
    return typeName.name.flatten();
  }
}

// =======================================================================================

class CapnpcCppMain {
public:
  CapnpcCppMain(kj::ProcessContext& context): context(context) {}

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, "Cap'n Proto C++ plugin version " VERSION,
          "This is a Cap'n Proto compiler plugin which generates C++ code. "
          "It is meant to be run using the Cap'n Proto compiler, e.g.:\n"
          "    capnp compile -oc++ foo.capnp")
        .callAfterParsing(KJ_BIND_METHOD(*this, run))
        .build();
  }

private:
  kj::ProcessContext& context;
  SchemaLoader schemaLoader;
  std::unordered_set<uint64_t> usedImports;
  bool hasInterfaces = false;

  CppTypeName cppFullName(Schema schema, kj::Maybe<InterfaceSchema::Method> method) {
    return cppFullName(schema, schema, method);
  }

  CppTypeName cppFullName(Schema schema, Schema brand, kj::Maybe<InterfaceSchema::Method> method) {
    auto node = schema.getProto();

    if (node.getScopeId() == 0) {
      // This is the file-level scope. Search for the namespace annotation.
      KJ_REQUIRE(node.isFile(),
          "Non-file had scopeId zero; perhaps it's a method param / result struct?");
      usedImports.insert(node.getId());
      KJ_IF_MAYBE(ns, annotationValue(node, NAMESPACE_ANNOTATION_ID)) {
        return CppTypeName::makeNamespace(ns->getText());
      } else {
        return CppTypeName::makeRoot();
      }
    } else {
      // This is a named type.

      // Figure out what name to use.
      Schema parent = schemaLoader.get(node.getScopeId());
      kj::StringPtr unqualifiedName;
      kj::String ownUnqualifiedName;
      KJ_IF_MAYBE(annotatedName, annotationValue(node, NAME_ANNOTATION_ID)) {
        // The node's name has been overridden for C++ by an annotation.
        unqualifiedName = annotatedName->getText();
      } else {
        // Search among the parent's nested nodes to for this node, in order to determine its name.
        auto parentProto = parent.getProto();
        for (auto nested: parentProto.getNestedNodes()) {
          if (nested.getId() == node.getId()) {
            unqualifiedName = nested.getName();
            break;
          }
        }
        if (unqualifiedName == nullptr) {
          // Hmm, maybe it's a group node?
          if (parentProto.isStruct()) {
            for (auto field: parentProto.getStruct().getFields()) {
              if (field.isGroup() && field.getGroup().getTypeId() == node.getId()) {
                ownUnqualifiedName = toTitleCase(protoName(field));
                unqualifiedName = ownUnqualifiedName;
                break;
              }
            }
          }
        }
        KJ_REQUIRE(unqualifiedName != nullptr,
            "A schema Node's supposed scope did not contain the node as a NestedNode.");
      }

      auto result = cppFullName(parent, brand, method);

      // Construct the generic arguments.
      auto params = node.getParameters();
      if (params.size() > 0) {
        auto args = brand.getBrandArgumentsAtScope(node.getId());

#if 0
        // Figure out exactly how many params are not bound to AnyPointer.
        // TODO(msvc): In a few obscure cases, MSVC does not like empty template pramater lists,
        //   even if all parameters have defaults. So, we give in and explicitly list all
        //   parameters in our generated code for now. Try again later.
        uint paramCount = 0;
        for (uint i: kj::indices(params)) {
          auto arg = args[i];
          if (arg.which() != schema::Type::ANY_POINTER || arg.getBrandParameter() != nullptr ||
              (arg.getImplicitParameter() != nullptr && method != nullptr)) {
            paramCount = i + 1;
          }
        }
#else
        uint paramCount = params.size();
#endif

        result.addMemberTemplate(unqualifiedName,
            KJ_MAP(i, kj::range(0u, paramCount)) {
              return typeName(args[i], method);
            });
      } else {
        result.addMemberType(unqualifiedName);
      }

      return result;
    }
  }

  kj::String toUpperCase(kj::StringPtr name) {
    kj::Vector<char> result(name.size() + 4);

    for (char c: name) {
      if ('a' <= c && c <= 'z') {
        result.add(c - 'a' + 'A');
      } else if (result.size() > 0 && 'A' <= c && c <= 'Z') {
        result.add('_');
        result.add(c);
      } else {
        result.add(c);
      }
    }

    if (result.size() == 4 && memcmp(result.begin(), "NULL", 4) == 0) {
      // NULL probably collides with a macro.
      result.add('_');
    }

    result.add('\0');

    return kj::String(result.releaseAsArray());
  }

  kj::String toTitleCase(kj::StringPtr name) {
    kj::String result = kj::heapString(name);
    if ('a' <= result[0] && result[0] <= 'z') {
      result[0] = result[0] - 'a' + 'A';
    }
    return kj::mv(result);
  }

  CppTypeName typeName(Type type, kj::Maybe<InterfaceSchema::Method> method) {
    switch (type.which()) {
      case schema::Type::VOID: return CppTypeName::makePrimitive(" ::capnp::Void");

      case schema::Type::BOOL: return CppTypeName::makePrimitive("bool");
      case schema::Type::INT8: return CppTypeName::makePrimitive(" ::int8_t");
      case schema::Type::INT16: return CppTypeName::makePrimitive(" ::int16_t");
      case schema::Type::INT32: return CppTypeName::makePrimitive(" ::int32_t");
      case schema::Type::INT64: return CppTypeName::makePrimitive(" ::int64_t");
      case schema::Type::UINT8: return CppTypeName::makePrimitive(" ::uint8_t");
      case schema::Type::UINT16: return CppTypeName::makePrimitive(" ::uint16_t");
      case schema::Type::UINT32: return CppTypeName::makePrimitive(" ::uint32_t");
      case schema::Type::UINT64: return CppTypeName::makePrimitive(" ::uint64_t");
      case schema::Type::FLOAT32: return CppTypeName::makePrimitive("float");
      case schema::Type::FLOAT64: return CppTypeName::makePrimitive("double");

      case schema::Type::TEXT: return CppTypeName::makePrimitive(" ::capnp::Text");
      case schema::Type::DATA: return CppTypeName::makePrimitive(" ::capnp::Data");

      case schema::Type::ENUM:
        return cppFullName(type.asEnum(), method);
      case schema::Type::STRUCT:
        return cppFullName(type.asStruct(), method);
      case schema::Type::INTERFACE: {
        auto result = cppFullName(type.asInterface(), method);
        result.setHasInterfaces();
        return result;
      }

      case schema::Type::LIST: {
        CppTypeName result = CppTypeName::makeNamespace("capnp");
        auto params = kj::heapArrayBuilder<CppTypeName>(1);
        params.add(typeName(type.asList().getElementType(), method));
        result.addMemberTemplate("List", params.finish());
        return result;
      }

      case schema::Type::ANY_POINTER:
        KJ_IF_MAYBE(param, type.getBrandParameter()) {
          return CppTypeName::makeTemplateParam(schemaLoader.get(param->scopeId).getProto()
              .getParameters()[param->index].getName());
        } else KJ_IF_MAYBE(param, type.getImplicitParameter()) {
          KJ_IF_MAYBE(m, method) {
            auto params = m->getProto().getImplicitParameters();
            KJ_REQUIRE(param->index < params.size());
            return CppTypeName::makeTemplateParam(params[param->index].getName());
          } else {
            return CppTypeName::makePrimitive(" ::capnp::AnyPointer");
          }
        } else {
          switch (type.whichAnyPointerKind()) {
            case schema::Type::AnyPointer::Unconstrained::ANY_KIND:
              return CppTypeName::makePrimitive(" ::capnp::AnyPointer");
            case schema::Type::AnyPointer::Unconstrained::STRUCT:
              return CppTypeName::makePrimitive(" ::capnp::AnyStruct");
            case schema::Type::AnyPointer::Unconstrained::LIST:
              return CppTypeName::makePrimitive(" ::capnp::AnyList");
            case schema::Type::AnyPointer::Unconstrained::CAPABILITY:
              hasInterfaces = true;  // Probably need to #inculde <capnp/capability.h>.
              return CppTypeName::makePrimitive(" ::capnp::Capability");
          }
          KJ_UNREACHABLE;
        }
    }
    KJ_UNREACHABLE;
  }

  template <typename P>
  kj::Maybe<schema::Value::Reader> annotationValue(P proto, uint64_t annotationId) {
    for (auto annotation: proto.getAnnotations()) {
      if (annotation.getId() == annotationId) {
        return annotation.getValue();
      }
    }
    return nullptr;
  }

  template <typename P>
  kj::StringPtr protoName(P proto) {
    KJ_IF_MAYBE(name, annotationValue(proto, NAME_ANNOTATION_ID)) {
      return name->getText();
    } else {
      return proto.getName();
    }
  }

  kj::StringTree literalValue(Type type, schema::Value::Reader value) {
    switch (value.which()) {
      case schema::Value::VOID: return kj::strTree(" ::capnp::VOID");
      case schema::Value::BOOL: return kj::strTree(value.getBool() ? "true" : "false");
      case schema::Value::INT8: return kj::strTree(value.getInt8());
      case schema::Value::INT16: return kj::strTree(value.getInt16());
      case schema::Value::INT32: return kj::strTree(value.getInt32());
      case schema::Value::INT64: return kj::strTree(value.getInt64(), "ll");
      case schema::Value::UINT8: return kj::strTree(value.getUint8(), "u");
      case schema::Value::UINT16: return kj::strTree(value.getUint16(), "u");
      case schema::Value::UINT32: return kj::strTree(value.getUint32(), "u");
      case schema::Value::UINT64: return kj::strTree(value.getUint64(), "llu");
      case schema::Value::FLOAT32: {
        auto text = kj::str(value.getFloat32());
        if (text.findFirst('.') == nullptr &&
            text.findFirst('e') == nullptr &&
            text.findFirst('E') == nullptr) {
          text = kj::str(text, ".0");
        }
        return kj::strTree(kj::mv(text), "f");
      }
      case schema::Value::FLOAT64: return kj::strTree(value.getFloat64());
      case schema::Value::ENUM: {
        EnumSchema schema = type.asEnum();
        if (value.getEnum() < schema.getEnumerants().size()) {
          return kj::strTree(
              cppFullName(schema, nullptr), "::",
              toUpperCase(protoName(schema.getEnumerants()[value.getEnum()].getProto())));
        } else {
          return kj::strTree("static_cast<", cppFullName(schema, nullptr),
                             ">(", value.getEnum(), ")");
        }
      }

      case schema::Value::TEXT:
      case schema::Value::DATA:
      case schema::Value::STRUCT:
      case schema::Value::INTERFACE:
      case schema::Value::LIST:
      case schema::Value::ANY_POINTER:
        KJ_FAIL_REQUIRE("literalValue() can only be used on primitive types.");
    }
    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------

  class TemplateContext {
  public:
    TemplateContext(): parent(nullptr) {}
    explicit TemplateContext(schema::Node::Reader node)
        : parent(nullptr), node(node) {}
    TemplateContext(const TemplateContext& parent, kj::StringPtr name, schema::Node::Reader node)
        : parent(parent), name(name), node(node) {}

    bool hasParams() const {
      return node.getParameters().size() > 0;
    }
    bool isGeneric() const {
      return node.getIsGeneric();
    }
    kj::Maybe<const TemplateContext&> getParent() const {
      return parent;
    }
    kj::StringPtr getName() const { return name; }

    kj::StringTree decl(bool withDefaults, kj::StringPtr suffix = nullptr) const {
      // "template <typename T, typename U>" for this type. Includes default assignments
      // ("= ::capnp::AnyPointer") if `withDefaults` is true. Returns empty string if this type
      // is not parameterized.

      auto params = node.getParameters();

      if (params.size() == 0) {
        return kj::strTree();
      } else {
        return kj::strTree(
            "template <", kj::StringTree(KJ_MAP(p, params) {
              return kj::strTree("typename ", p.getName(), suffix,
                  withDefaults ? " = ::capnp::AnyPointer" : "");
            }, ", "), ">\n");
      }
    }

    kj::StringTree allDecls() const {
      // Decl for each generic parent plus this type, one per line.
      return kj::strTree(parentDecls(), decl(false));
    }

    kj::StringTree parentDecls() const {
      // Decls just for the parents.
      KJ_IF_MAYBE(p, parent) {
        return p->allDecls();
      } else {
        return kj::strTree();
      }
    }

    kj::StringTree args(kj::StringPtr suffix = nullptr) const {
      // "<T, U>" for this type.
      auto params = node.getParameters();

      if (params.size() == 0) {
        return kj::strTree();
      } else {
        return kj::strTree(
            "<", kj::StringTree(KJ_MAP(p, params) {
              return kj::strTree(p.getName(), suffix);
            }, ", "), ">");
      }
    }

    kj::StringTree allArgs() const {
      // "S, T, U, V" -- listing all args for all enclosing scopes starting from the outermost.

      auto params = node.getParameters();

      kj::StringTree self(KJ_MAP(p, params) { return kj::strTree(p.getName()); }, ", ");
      kj::StringTree up;
      KJ_IF_MAYBE(p, parent) {
        up = p->allArgs();
      }

      if (self.size() == 0) {
        return up;
      } else if (up.size() == 0) {
        return self;
      } else {
        return kj::strTree(kj::mv(up), ", ", kj::mv(self));
      }
    }

    std::map<uint64_t, List<schema::Node::Parameter>::Reader> getScopeMap() const {
      std::map<uint64_t, List<schema::Node::Parameter>::Reader> result;
      const TemplateContext* current = this;
      for (;;) {
        auto params = current->node.getParameters();
        if (params.size() > 0) {
          result[current->node.getId()] = params;
        }
        KJ_IF_MAYBE(p, current->parent) {
          current = p;
        } else {
          return result;
        }
      }
    }

  private:
    kj::Maybe<const TemplateContext&> parent;
    kj::StringPtr name;
    schema::Node::Reader node;
  };

  struct BrandInitializerText {
    kj::StringTree scopes;
    kj::StringTree bindings;
    kj::StringTree dependencies;
    size_t dependencyCount;
    // TODO(msvc):  `dependencyCount` is the number of individual dependency definitions in
    // `dependencies`. It's a hack to allow makeGenericDefinitions to hard-code the size of the
    // `_capnpPrivate::brandDependencies` array into the definition of
    // `_capnpPrivate::specificBrand::dependencyCount`. This is necessary because MSVC cannot deduce
    // the size of `brandDependencies` if it is nested under a class template. It's probably this
    // demoralizingly deferred bug:
    // https://connect.microsoft.com/VisualStudio/feedback/details/759407/can-not-get-size-of-static-array-defined-in-class-template
  };

  BrandInitializerText makeBrandInitializers(
      const TemplateContext& templateContext, Schema schema) {
    auto scopeMap = templateContext.getScopeMap();

    auto scopes = kj::heapArrayBuilder<kj::StringTree>(scopeMap.size());
    kj::Vector<kj::StringTree> bindings(scopeMap.size() * 2);  // (estimate two params per scope)

    for (auto& scope: scopeMap) {
      scopes.add(kj::strTree("  { ",
        "0x", kj::hex(scope.first), ", "
        "brandBindings + ", bindings.size(), ", ",
        scope.second.size(), ", "
        "false"
      "},\n"));

      for (auto param: scope.second) {
        bindings.add(kj::strTree("  ::capnp::_::brandBindingFor<", param.getName(), ">(),\n"));
      }
    }

    auto depMap = makeBrandDepMap(templateContext, schema);
    auto dependencyCount = depMap.size();
    return {
      kj::strTree("{\n", scopes.finish(), "}"),
      kj::strTree("{\n", bindings.releaseAsArray(), "}"),
      makeBrandDepInitializers(kj::mv(depMap)),
      dependencyCount
    };
  }

  std::map<uint, kj::StringTree>
  makeBrandDepMap(const TemplateContext& templateContext, Schema schema) {
    // Build deps. This is separate from makeBrandDepInitializers to give calling code the
    // opportunity to count the number of dependencies, to calculate array sizes.
    std::map<uint, kj::StringTree> depMap;

#define ADD_DEP(kind, index, ...) \
    { \
      uint location = _::RawBrandedSchema::makeDepLocation( \
          _::RawBrandedSchema::DepKind::kind, index); \
      KJ_IF_MAYBE(dep, makeBrandDepInitializer(__VA_ARGS__)) { \
        depMap[location] = kj::mv(*dep); \
      } \
    }

    switch (schema.getProto().which()) {
      case schema::Node::FILE:
      case schema::Node::ENUM:
      case schema::Node::ANNOTATION:
        break;

      case schema::Node::STRUCT:
        for (auto field: schema.asStruct().getFields()) {
          ADD_DEP(FIELD, field.getIndex(), field.getType());
        }
        break;
      case schema::Node::INTERFACE: {
        auto interface = schema.asInterface();
        auto superclasses = interface.getSuperclasses();
        for (auto i: kj::indices(superclasses)) {
          ADD_DEP(SUPERCLASS, i, superclasses[i]);
        }
        auto methods = interface.getMethods();
        for (auto i: kj::indices(methods)) {
          auto method = methods[i];
          ADD_DEP(METHOD_PARAMS, i, method, method.getParamType(), "Params");
          ADD_DEP(METHOD_RESULTS, i, method, method.getResultType(), "Results");
        }
        break;
      }
      case schema::Node::CONST:
        ADD_DEP(CONST_TYPE, 0, schema.asConst().getType());
        break;
    }
#undef ADD_DEP
    return depMap;
  }

  kj::StringTree makeBrandDepInitializers(std::map<uint, kj::StringTree>&& depMap) {
    // Process depMap. Returns a braced initialiser list, or an empty string if there are no
    // dependencies.
    if (!depMap.size()) {
      return kj::strTree();
    }

    auto deps = kj::heapArrayBuilder<kj::StringTree>(depMap.size());
    for (auto& entry: depMap) {
      deps.add(kj::strTree("  { ", entry.first, ", ", kj::mv(entry.second), " },\n"));
    }

    return kj::strTree("{\n", kj::StringTree(deps.finish(), ""), "}");
  }

  kj::Maybe<kj::StringTree> makeBrandDepInitializer(Schema type) {
    return makeBrandDepInitializer(type, cppFullName(type, nullptr));
  }

  kj::Maybe<kj::StringTree> makeBrandDepInitializer(
      InterfaceSchema::Method method, StructSchema type, kj::StringPtr suffix) {
    auto typeProto = type.getProto();
    if (typeProto.getScopeId() == 0) {
      // This is an auto-generated params or results type.
      auto name = cppFullName(method.getContainingInterface(), nullptr);
      auto memberTypeName = kj::str(toTitleCase(protoName(method.getProto())), suffix);

      if (typeProto.getParameters().size() == 0) {
        name.addMemberType(memberTypeName);
      } else {
        // The method has implicit parameters (i.e. it's generic). For the purpose of the brand
        // dep initializer, we only want to supply the default AnyPointer variant, so just don't
        // pass any parameters here.
        name.addMemberTemplate(memberTypeName, nullptr);
      }
      return makeBrandDepInitializer(type, kj::mv(name));
    } else {
      return makeBrandDepInitializer(type);
    }
  }

  kj::Maybe<kj::StringTree> makeBrandDepInitializer(Schema type, CppTypeName name) {
    if (type.isBranded()) {
      name.addMemberType("_capnpPrivate");
      name.addMemberValue("brand");
      return kj::strTree(name, "()");
    } else {
      return nullptr;
    }
  }

  kj::Maybe<kj::StringTree> makeBrandDepInitializer(Type type) {
    switch (type.which()) {
      case schema::Type::VOID:
      case schema::Type::BOOL:
      case schema::Type::INT8:
      case schema::Type::INT16:
      case schema::Type::INT32:
      case schema::Type::INT64:
      case schema::Type::UINT8:
      case schema::Type::UINT16:
      case schema::Type::UINT32:
      case schema::Type::UINT64:
      case schema::Type::FLOAT32:
      case schema::Type::FLOAT64:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::ENUM:
      case schema::Type::ANY_POINTER:
        return nullptr;

      case schema::Type::STRUCT:
        return makeBrandDepInitializer(type.asStruct());
      case schema::Type::INTERFACE:
        return makeBrandDepInitializer(type.asInterface());
      case schema::Type::LIST:
        return makeBrandDepInitializer(type.asList().getElementType());
    }

    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------
  // Code to deal with "slots" -- determines what to zero out when we clear a group.

  static uint typeSizeBits(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::BOOL: return 1;
      case schema::Type::INT8: return 8;
      case schema::Type::INT16: return 16;
      case schema::Type::INT32: return 32;
      case schema::Type::INT64: return 64;
      case schema::Type::UINT8: return 8;
      case schema::Type::UINT16: return 16;
      case schema::Type::UINT32: return 32;
      case schema::Type::UINT64: return 64;
      case schema::Type::FLOAT32: return 32;
      case schema::Type::FLOAT64: return 64;
      case schema::Type::ENUM: return 16;

      case schema::Type::VOID:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        KJ_FAIL_REQUIRE("Should only be called for data types.");
    }
    KJ_UNREACHABLE;
  }

  enum class Section {
    NONE,
    DATA,
    POINTERS
  };

  static Section sectionFor(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::VOID:
        return Section::NONE;
      case schema::Type::BOOL:
      case schema::Type::INT8:
      case schema::Type::INT16:
      case schema::Type::INT32:
      case schema::Type::INT64:
      case schema::Type::UINT8:
      case schema::Type::UINT16:
      case schema::Type::UINT32:
      case schema::Type::UINT64:
      case schema::Type::FLOAT32:
      case schema::Type::FLOAT64:
      case schema::Type::ENUM:
        return Section::DATA;
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        return Section::POINTERS;
    }
    KJ_UNREACHABLE;
  }

  static kj::StringPtr maskType(schema::Type::Which whichType) {
    switch (whichType) {
      case schema::Type::BOOL: return "bool";
      case schema::Type::INT8: return " ::uint8_t";
      case schema::Type::INT16: return " ::uint16_t";
      case schema::Type::INT32: return " ::uint32_t";
      case schema::Type::INT64: return " ::uint64_t";
      case schema::Type::UINT8: return " ::uint8_t";
      case schema::Type::UINT16: return " ::uint16_t";
      case schema::Type::UINT32: return " ::uint32_t";
      case schema::Type::UINT64: return " ::uint64_t";
      case schema::Type::FLOAT32: return " ::uint32_t";
      case schema::Type::FLOAT64: return " ::uint64_t";
      case schema::Type::ENUM: return " ::uint16_t";

      case schema::Type::VOID:
      case schema::Type::TEXT:
      case schema::Type::DATA:
      case schema::Type::LIST:
      case schema::Type::STRUCT:
      case schema::Type::INTERFACE:
      case schema::Type::ANY_POINTER:
        KJ_FAIL_REQUIRE("Should only be called for data types.");
    }
    KJ_UNREACHABLE;
  }

  struct Slot {
    schema::Type::Which whichType;
    uint offset;

    bool isSupersetOf(Slot other) const {
      auto section = sectionFor(whichType);
      if (section != sectionFor(other.whichType)) return false;
      switch (section) {
        case Section::NONE:
          return true;  // all voids overlap
        case Section::DATA: {
          auto bits = typeSizeBits(whichType);
          auto start = offset * bits;
          auto otherBits = typeSizeBits(other.whichType);
          auto otherStart = other.offset * otherBits;
          return start <= otherStart && otherStart + otherBits <= start + bits;
        }
        case Section::POINTERS:
          return offset == other.offset;
      }
      KJ_UNREACHABLE;
    }

    bool operator<(Slot other) const {
      // Sort by section, then start position, and finally size.

      auto section = sectionFor(whichType);
      auto otherSection = sectionFor(other.whichType);
      if (section < otherSection) {
        return true;
      } else if (section > otherSection) {
        return false;
      }

      switch (section) {
        case Section::NONE:
          return false;
        case Section::DATA: {
          auto bits = typeSizeBits(whichType);
          auto start = offset * bits;
          auto otherBits = typeSizeBits(other.whichType);
          auto otherStart = other.offset * otherBits;
          if (start < otherStart) {
            return true;
          } else if (start > otherStart) {
            return false;
          }

          // Sort larger sizes before smaller.
          return bits > otherBits;
        }
        case Section::POINTERS:
          return offset < other.offset;
      }
      KJ_UNREACHABLE;
    }
  };

  void getSlots(StructSchema schema, kj::Vector<Slot>& slots) {
    auto structProto = schema.getProto().getStruct();
    if (structProto.getDiscriminantCount() > 0) {
      slots.add(Slot { schema::Type::UINT16, structProto.getDiscriminantOffset() });
    }

    for (auto field: schema.getFields()) {
      auto proto = field.getProto();
      switch (proto.which()) {
        case schema::Field::SLOT: {
          auto slot = proto.getSlot();
          slots.add(Slot { slot.getType().which(), slot.getOffset() });
          break;
        }
        case schema::Field::GROUP:
          getSlots(field.getType().asStruct(), slots);
          break;
      }
    }
  }

  kj::Array<Slot> getSortedSlots(StructSchema schema) {
    // Get a representation of all of the field locations owned by this schema, e.g. so that they
    // can be zero'd out.

    kj::Vector<Slot> slots(schema.getFields().size());
    getSlots(schema, slots);
    std::sort(slots.begin(), slots.end());

    kj::Vector<Slot> result(slots.size());

    // All void slots are redundant, and they sort towards the front of the list.  By starting out
    // with `prevSlot` = void, we will end up skipping them all, which is what we want.
    Slot prevSlot = { schema::Type::VOID, 0 };
    for (auto slot: slots) {
      if (prevSlot.isSupersetOf(slot)) {
        // This slot is redundant as prevSlot is a superset of it.
        continue;
      }

      // Since all sizes are power-of-two, if two slots overlap at all, one must be a superset of
      // the other.  Since we sort slots by starting position, we know that the only way `slot`
      // could be a superset of `prevSlot` is if they have the same starting position.  However,
      // since we sort slots with the same starting position by descending size, this is not
      // possible.
      KJ_DASSERT(!slot.isSupersetOf(prevSlot));

      result.add(slot);

      prevSlot = slot;
    }

    return result.releaseAsArray();
  }

  // -----------------------------------------------------------------

  struct DiscriminantChecks {
    kj::String has;
    kj::String check;
    kj::String set;
    kj::StringTree readerIsDecl;
    kj::StringTree builderIsDecl;
    kj::StringTree isDefs;
  };

  DiscriminantChecks makeDiscriminantChecks(kj::StringPtr scope,
                                            kj::StringPtr memberName,
                                            StructSchema containingStruct,
                                            const TemplateContext& templateContext) {
    auto discrimOffset = containingStruct.getProto().getStruct().getDiscriminantOffset();

    kj::String titleCase = toTitleCase(memberName);
    kj::String upperCase = toUpperCase(memberName);

    return DiscriminantChecks {
        kj::str(
            "  if (which() != ", scope, upperCase, ") return false;\n"),
        kj::str(
            // Extra parens around the condition are needed for when we're compiling a multi-arg
            // generic type, which will have a comma, which would otherwise mess up the macro.
            // Ah, C++.
            "  KJ_IREQUIRE((which() == ", scope, upperCase, "),\n"
            "              \"Must check which() before get()ing a union member.\");\n"),
        kj::str(
            "  _builder.setDataField<", scope, "Which>(\n"
            "      ::capnp::bounded<", discrimOffset, ">() * ::capnp::ELEMENTS, ",
                      scope, upperCase, ");\n"),
        kj::strTree("  inline bool is", titleCase, "() const;\n"),
        kj::strTree("  inline bool is", titleCase, "();\n"),
        kj::strTree(
            templateContext.allDecls(),
            "inline bool ", scope, "Reader::is", titleCase, "() const {\n"
            "  return which() == ", scope, upperCase, ";\n"
            "}\n",
            templateContext.allDecls(),
            "inline bool ", scope, "Builder::is", titleCase, "() {\n"
            "  return which() == ", scope, upperCase, ";\n"
            "}\n")
    };
  }

  // -----------------------------------------------------------------

  struct FieldText {
    kj::StringTree readerMethodDecls;
    kj::StringTree builderMethodDecls;
    kj::StringTree pipelineMethodDecls;
    kj::StringTree inlineMethodDefs;
  };

  enum class FieldKind {
    PRIMITIVE,
    BLOB,
    STRUCT,
    LIST,
    INTERFACE,
    ANY_POINTER,
    BRAND_PARAMETER
  };

  FieldText makeFieldText(kj::StringPtr scope, StructSchema::Field field,
                          const TemplateContext& templateContext) {
    auto proto = field.getProto();
    auto typeSchema = field.getType();
    auto baseName = protoName(proto);
    kj::String titleCase = toTitleCase(baseName);

    DiscriminantChecks unionDiscrim;
    if (hasDiscriminantValue(proto)) {
      unionDiscrim = makeDiscriminantChecks(scope, baseName, field.getContainingStruct(),
                                            templateContext);
    }

    switch (proto.which()) {
      case schema::Field::SLOT:
        // Continue below.
        break;

      case schema::Field::GROUP: {
        auto slots = getSortedSlots(field.getType().asStruct());
        return FieldText {
            kj::strTree(
                kj::mv(unionDiscrim.readerIsDecl),
                "  inline typename ", titleCase, "::Reader get", titleCase, "() const;\n"
                "\n"),

            kj::strTree(
                kj::mv(unionDiscrim.builderIsDecl),
                "  inline typename ", titleCase, "::Builder get", titleCase, "();\n"
                "  inline typename ", titleCase, "::Builder init", titleCase, "();\n"
                "\n"),

            hasDiscriminantValue(proto) ? kj::strTree() :
                kj::strTree("  inline typename ", titleCase, "::Pipeline get", titleCase, "();\n"),

            kj::strTree(
                kj::mv(unionDiscrim.isDefs),
                templateContext.allDecls(),
                "inline typename ", scope, titleCase, "::Reader ", scope, "Reader::get", titleCase, "() const {\n",
                unionDiscrim.check,
                "  return typename ", scope, titleCase, "::Reader(_reader);\n"
                "}\n",
                templateContext.allDecls(),
                "inline typename ", scope, titleCase, "::Builder ", scope, "Builder::get", titleCase, "() {\n",
                unionDiscrim.check,
                "  return typename ", scope, titleCase, "::Builder(_builder);\n"
                "}\n",
                hasDiscriminantValue(proto) ? kj::strTree() : kj::strTree(
                  "#if !CAPNP_LITE\n",
                  templateContext.allDecls(),
                  "inline typename ", scope, titleCase, "::Pipeline ", scope, "Pipeline::get", titleCase, "() {\n",
                  "  return typename ", scope, titleCase, "::Pipeline(_typeless.noop());\n"
                  "}\n"
                  "#endif  // !CAPNP_LITE\n"),
                templateContext.allDecls(),
                "inline typename ", scope, titleCase, "::Builder ", scope, "Builder::init", titleCase, "() {\n",
                unionDiscrim.set,
                KJ_MAP(slot, slots) {
                  switch (sectionFor(slot.whichType)) {
                    case Section::NONE:
                      return kj::strTree();
                    case Section::DATA:
                      return kj::strTree(
                          "  _builder.setDataField<", maskType(slot.whichType), ">(::capnp::bounded<",
                              slot.offset, ">() * ::capnp::ELEMENTS, 0);\n");
                    case Section::POINTERS:
                      return kj::strTree(
                          "  _builder.getPointerField(::capnp::bounded<", slot.offset,
                              ">() * ::capnp::POINTERS).clear();\n");
                  }
                  KJ_UNREACHABLE;
                },
                "  return typename ", scope, titleCase, "::Builder(_builder);\n"
                "}\n")
          };
      }
    }

    auto slot = proto.getSlot();

    FieldKind kind = FieldKind::PRIMITIVE;
    kj::String ownedType;
    CppTypeName type = typeName(typeSchema, nullptr);
    kj::StringPtr setterDefault;  // only for void
    kj::String defaultMask;    // primitives only
    size_t defaultOffset = 0;    // pointers only: offset of the default value within the schema.
    size_t defaultSize = 0;      // blobs only: byte size of the default value.

    auto defaultBody = slot.getDefaultValue();
    switch (typeSchema.which()) {
      case schema::Type::VOID:
        kind = FieldKind::PRIMITIVE;
        setterDefault = " = ::capnp::VOID";
        break;

#define HANDLE_PRIMITIVE(discrim, typeName, defaultName, suffix) \
      case schema::Type::discrim: \
        kind = FieldKind::PRIMITIVE; \
        if (defaultBody.get##defaultName() != 0) { \
          defaultMask = kj::str(defaultBody.get##defaultName(), #suffix); \
        } \
        break;

      HANDLE_PRIMITIVE(BOOL, bool, Bool, );
      HANDLE_PRIMITIVE(INT8 , ::int8_t , Int8 , );
      HANDLE_PRIMITIVE(INT16, ::int16_t, Int16, );
      HANDLE_PRIMITIVE(INT32, ::int32_t, Int32, );
      HANDLE_PRIMITIVE(INT64, ::int64_t, Int64, ll);
      HANDLE_PRIMITIVE(UINT8 , ::uint8_t , Uint8 , u);
      HANDLE_PRIMITIVE(UINT16, ::uint16_t, Uint16, u);
      HANDLE_PRIMITIVE(UINT32, ::uint32_t, Uint32, u);
      HANDLE_PRIMITIVE(UINT64, ::uint64_t, Uint64, ull);
#undef HANDLE_PRIMITIVE

      case schema::Type::FLOAT32:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat32() != 0) {
          uint32_t mask;
          float value = defaultBody.getFloat32();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask, "u");
        }
        break;

      case schema::Type::FLOAT64:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getFloat64() != 0) {
          uint64_t mask;
          double value = defaultBody.getFloat64();
          static_assert(sizeof(mask) == sizeof(value), "bug");
          memcpy(&mask, &value, sizeof(mask));
          defaultMask = kj::str(mask, "ull");
        }
        break;

      case schema::Type::TEXT:
        kind = FieldKind::BLOB;
        if (defaultBody.hasText()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getText().size();
        }
        break;
      case schema::Type::DATA:
        kind = FieldKind::BLOB;
        if (defaultBody.hasData()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
          defaultSize = defaultBody.getData().size();
        }
        break;

      case schema::Type::ENUM:
        kind = FieldKind::PRIMITIVE;
        if (defaultBody.getEnum() != 0) {
          defaultMask = kj::str(defaultBody.getEnum(), "u");
        }
        break;

      case schema::Type::STRUCT:
        kind = FieldKind::STRUCT;
        if (defaultBody.hasStruct()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::LIST:
        kind = FieldKind::LIST;
        if (defaultBody.hasList()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        break;
      case schema::Type::INTERFACE:
        kind = FieldKind::INTERFACE;
        break;
      case schema::Type::ANY_POINTER:
        if (defaultBody.hasAnyPointer()) {
          defaultOffset = field.getDefaultValueSchemaOffset();
        }
        if (typeSchema.getBrandParameter() != nullptr) {
          kind = FieldKind::BRAND_PARAMETER;
        } else {
          kind = FieldKind::ANY_POINTER;
          switch (typeSchema.whichAnyPointerKind()) {
            case schema::Type::AnyPointer::Unconstrained::ANY_KIND:
              kind = FieldKind::ANY_POINTER;
              break;
            case schema::Type::AnyPointer::Unconstrained::STRUCT:
              kind = FieldKind::STRUCT;
              break;
            case schema::Type::AnyPointer::Unconstrained::LIST:
              kind = FieldKind::LIST;
              break;
            case schema::Type::AnyPointer::Unconstrained::CAPABILITY:
              kind = FieldKind::INTERFACE;
              break;
          }
        }
        break;
    }

    kj::String defaultMaskParam;
    if (defaultMask.size() > 0) {
      defaultMaskParam = kj::str(", ", defaultMask);
    }

    uint offset = slot.getOffset();

    if (kind == FieldKind::PRIMITIVE) {
      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDecl),
            "  inline ", type, " get", titleCase, "() const;\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDecl),
            "  inline ", type, " get", titleCase, "();\n"
            "  inline void set", titleCase, "(", type, " value", setterDefault, ");\n"
            "\n"),

        kj::strTree(),

        kj::strTree(
            kj::mv(unionDiscrim.isDefs),
            templateContext.allDecls(),
            "inline ", type, " ", scope, "Reader::get", titleCase, "() const {\n",
            unionDiscrim.check,
            "  return _reader.getDataField<", type, ">(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::ELEMENTS", defaultMaskParam, ");\n",
            "}\n"
            "\n",
            templateContext.allDecls(),
            "inline ", type, " ", scope, "Builder::get", titleCase, "() {\n",
            unionDiscrim.check,
            "  return _builder.getDataField<", type, ">(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::ELEMENTS", defaultMaskParam, ");\n",
            "}\n",
            templateContext.allDecls(),
            "inline void ", scope, "Builder::set", titleCase, "(", type, " value) {\n",
            unionDiscrim.set,
            "  _builder.setDataField<", type, ">(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::ELEMENTS, value", defaultMaskParam, ");\n",
            "}\n"
            "\n")
      };

    } else if (kind == FieldKind::INTERFACE) {
      CppTypeName clientType = type;
      clientType.addMemberType("Client");

      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDecl),
            "  inline bool has", titleCase, "() const;\n"
            "#if !CAPNP_LITE\n"
            "  inline ", clientType, " get", titleCase, "() const;\n"
            "#endif  // !CAPNP_LITE\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDecl),
            "  inline bool has", titleCase, "();\n"
            "#if !CAPNP_LITE\n"
            "  inline ", clientType, " get", titleCase, "();\n"
            "  inline void set", titleCase, "(", clientType, "&& value);\n",
            "  inline void set", titleCase, "(", clientType, "& value);\n",
            "  inline void adopt", titleCase, "(::capnp::Orphan<", type, ">&& value);\n"
            "  inline ::capnp::Orphan<", type, "> disown", titleCase, "();\n"
            "#endif  // !CAPNP_LITE\n"
            "\n"),

        kj::strTree(
            hasDiscriminantValue(proto) ? kj::strTree() : kj::strTree(
              "  inline ", clientType, " get", titleCase, "();\n")),

        kj::strTree(
            kj::mv(unionDiscrim.isDefs),
            templateContext.allDecls(),
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionDiscrim.has,
            "  return !_reader.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS).isNull();\n"
            "}\n",
            templateContext.allDecls(),
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionDiscrim.has,
            "  return !_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS).isNull();\n"
            "}\n"
            "#if !CAPNP_LITE\n",
            templateContext.allDecls(),
            "inline ", clientType, " ", scope, "Reader::get", titleCase, "() const {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(_reader.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
            "}\n",
            templateContext.allDecls(),
            "inline ", clientType, " ", scope, "Builder::get", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
            "}\n",
            hasDiscriminantValue(proto) ? kj::strTree() : kj::strTree(
              templateContext.allDecls(),
              "inline ", clientType, " ", scope, "Pipeline::get", titleCase, "() {\n",
              "  return ", clientType, "(_typeless.getPointerField(", offset, ").asCap());\n"
              "}\n"),
            templateContext.allDecls(),
            "inline void ", scope, "Builder::set", titleCase, "(", clientType, "&& cap) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<", type, ">::set(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), kj::mv(cap));\n"
            "}\n",
            templateContext.allDecls(),
            "inline void ", scope, "Builder::set", titleCase, "(", clientType, "& cap) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<", type, ">::set(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), cap);\n"
            "}\n",
            templateContext.allDecls(),
            "inline void ", scope, "Builder::adopt", titleCase, "(\n"
            "    ::capnp::Orphan<", type, ">&& value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<", type, ">::adopt(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), kj::mv(value));\n"
            "}\n",
            templateContext.allDecls(),
            "inline ::capnp::Orphan<", type, "> ", scope, "Builder::disown", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::disown(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
            "}\n"
            "#endif  // !CAPNP_LITE\n"
            "\n")
      };

    } else if (kind == FieldKind::ANY_POINTER) {
      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDecl),
            "  inline bool has", titleCase, "() const;\n"
            "  inline ::capnp::AnyPointer::Reader get", titleCase, "() const;\n"
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDecl),
            "  inline bool has", titleCase, "();\n"
            "  inline ::capnp::AnyPointer::Builder get", titleCase, "();\n"
            "  inline ::capnp::AnyPointer::Builder init", titleCase, "();\n"
            "\n"),

        kj::strTree(),

        kj::strTree(
            kj::mv(unionDiscrim.isDefs),
            templateContext.allDecls(),
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionDiscrim.has,
            "  return !_reader.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS).isNull();\n"
            "}\n",
            templateContext.allDecls(),
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionDiscrim.has,
            "  return !_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS).isNull();\n"
            "}\n",
            templateContext.allDecls(),
            "inline ::capnp::AnyPointer::Reader ", scope, "Reader::get", titleCase, "() const {\n",
            unionDiscrim.check,
            "  return ::capnp::AnyPointer::Reader(_reader.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
            "}\n",
            templateContext.allDecls(),
            "inline ::capnp::AnyPointer::Builder ", scope, "Builder::get", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::AnyPointer::Builder(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
            "}\n",
            templateContext.allDecls(),
            "inline ::capnp::AnyPointer::Builder ", scope, "Builder::init", titleCase, "() {\n",
            unionDiscrim.set,
            "  auto result = ::capnp::AnyPointer::Builder(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
            "  result.clear();\n"
            "  return result;\n"
            "}\n"
            "\n")
      };

    } else {
      // Blob, struct, list, or template param.  These have only minor differences.

      uint64_t typeId = field.getContainingStruct().getProto().getId();
      kj::String defaultParam = defaultOffset == 0 ? kj::str() : kj::str(
          ",\n        ::capnp::schemas::bp_", kj::hex(typeId), " + ", defaultOffset,
          defaultSize == 0 ? kj::strTree() : kj::strTree(", ", defaultSize));

      bool shouldIncludeStructInit =
          kind == FieldKind::STRUCT || kind == FieldKind::BRAND_PARAMETER;
      bool shouldIncludeSizedInit =
          kind != FieldKind::STRUCT || kind == FieldKind::BRAND_PARAMETER;
      bool shouldIncludePipelineGetter = !hasDiscriminantValue(proto) &&
          (kind == FieldKind::STRUCT || kind == FieldKind::BRAND_PARAMETER);
      bool shouldIncludeArrayInitializer = false;
      bool shouldExcludeInLiteMode = type.hasInterfaces();
      bool shouldTemplatizeInit = typeSchema.which() == schema::Type::ANY_POINTER &&
          kind != FieldKind::BRAND_PARAMETER;

      CppTypeName elementReaderType;
      if (typeSchema.isList()) {
        bool primitiveElement = false;
        bool interface = false;
        auto elementType = typeSchema.asList().getElementType();
        switch (elementType.which()) {
          case schema::Type::VOID:
          case schema::Type::BOOL:
          case schema::Type::INT8:
          case schema::Type::INT16:
          case schema::Type::INT32:
          case schema::Type::INT64:
          case schema::Type::UINT8:
          case schema::Type::UINT16:
          case schema::Type::UINT32:
          case schema::Type::UINT64:
          case schema::Type::FLOAT32:
          case schema::Type::FLOAT64:
          case schema::Type::ENUM:
            primitiveElement = true;
            shouldIncludeArrayInitializer = true;
            break;

          case schema::Type::TEXT:
          case schema::Type::DATA:
          case schema::Type::LIST:
            primitiveElement = false;
            shouldIncludeArrayInitializer = true;
            break;

          case schema::Type::ANY_POINTER:
            primitiveElement = false;
            shouldIncludeArrayInitializer = elementType.getBrandParameter() != nullptr;
            break;

          case schema::Type::INTERFACE:
            primitiveElement = false;
            interface = true;
            break;

          case schema::Type::STRUCT:
            primitiveElement = false;
            break;
        }
        elementReaderType = typeName(elementType, nullptr);
        if (!primitiveElement) {
          if (interface) {
            elementReaderType.addMemberType("Client");
          } else {
            elementReaderType.addMemberType("Reader");
          }
        }
      };

      CppTypeName readerType;
      CppTypeName builderType;
      CppTypeName pipelineType;
      if (kind == FieldKind::BRAND_PARAMETER) {
        readerType = CppTypeName::makeNamespace("capnp");
        readerType.addMemberTemplate("ReaderFor", kj::heapArray(&type, 1));
        builderType = CppTypeName::makeNamespace("capnp");
        builderType.addMemberTemplate("BuilderFor", kj::heapArray(&type, 1));
        pipelineType = CppTypeName::makeNamespace("capnp");
        pipelineType.addMemberTemplate("PipelineFor", kj::heapArray(&type, 1));
      } else {
        readerType = type;
        readerType.addMemberType("Reader");
        builderType = type;
        builderType.addMemberType("Builder");
        pipelineType = type;
        pipelineType.addMemberType("Pipeline");
      }

      #define COND(cond, ...) ((cond) ? kj::strTree(__VA_ARGS__) : kj::strTree())

      return FieldText {
        kj::strTree(
            kj::mv(unionDiscrim.readerIsDecl),
            "  inline bool has", titleCase, "() const;\n",
            COND(shouldExcludeInLiteMode, "#if !CAPNP_LITE\n"),
            "  inline ", readerType, " get", titleCase, "() const;\n",
            COND(shouldExcludeInLiteMode, "#endif  // !CAPNP_LITE\n"),
            "\n"),

        kj::strTree(
            kj::mv(unionDiscrim.builderIsDecl),
            "  inline bool has", titleCase, "();\n",
            COND(shouldExcludeInLiteMode, "#if !CAPNP_LITE\n"),
            "  inline ", builderType, " get", titleCase, "();\n"
            "  inline void set", titleCase, "(", readerType, " value);\n",
            COND(shouldIncludeArrayInitializer,
              "  inline void set", titleCase, "(::kj::ArrayPtr<const ", elementReaderType, "> value);\n"),
            COND(shouldIncludeStructInit,
              COND(shouldTemplatizeInit,
                "  template <typename T_>\n"
                "  inline ::capnp::BuilderFor<T_> init", titleCase, "As();\n"),
              COND(!shouldTemplatizeInit,
                "  inline ", builderType, " init", titleCase, "();\n")),
            COND(shouldIncludeSizedInit,
              COND(shouldTemplatizeInit,
                "  template <typename T_>\n"
                "  inline ::capnp::BuilderFor<T_> init", titleCase, "As(unsigned int size);\n"),
              COND(!shouldTemplatizeInit,
                "  inline ", builderType, " init", titleCase, "(unsigned int size);\n")),
            "  inline void adopt", titleCase, "(::capnp::Orphan<", type, ">&& value);\n"
            "  inline ::capnp::Orphan<", type, "> disown", titleCase, "();\n",
            COND(shouldExcludeInLiteMode, "#endif  // !CAPNP_LITE\n"),
            "\n"),

        kj::strTree(
            COND(shouldIncludePipelineGetter,
              "  inline ", pipelineType, " get", titleCase, "();\n")),

        kj::strTree(
            kj::mv(unionDiscrim.isDefs),
            templateContext.allDecls(),
            "inline bool ", scope, "Reader::has", titleCase, "() const {\n",
            unionDiscrim.has,
            "  return !_reader.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS).isNull();\n"
            "}\n",
            templateContext.allDecls(),
            "inline bool ", scope, "Builder::has", titleCase, "() {\n",
            unionDiscrim.has,
            "  return !_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS).isNull();\n"
            "}\n",
            COND(shouldExcludeInLiteMode, "#if !CAPNP_LITE\n"),
            templateContext.allDecls(),
            "inline ", readerType, " ", scope, "Reader::get", titleCase, "() const {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(_reader.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS)", defaultParam, ");\n"
            "}\n",
            templateContext.allDecls(),
            "inline ", builderType, " ", scope, "Builder::get", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::get(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS)", defaultParam, ");\n"
            "}\n",
            COND(shouldIncludePipelineGetter,
              "#if !CAPNP_LITE\n",
              templateContext.allDecls(),
              "inline ", pipelineType, " ", scope, "Pipeline::get", titleCase, "() {\n",
              "  return ", pipelineType, "(_typeless.getPointerField(", offset, "));\n"
              "}\n"
              "#endif  // !CAPNP_LITE\n"),
            templateContext.allDecls(),
            "inline void ", scope, "Builder::set", titleCase, "(", readerType, " value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<", type, ">::set(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), value);\n"
            "}\n",
            COND(shouldIncludeArrayInitializer,
              templateContext.allDecls(),
              "inline void ", scope, "Builder::set", titleCase, "(::kj::ArrayPtr<const ", elementReaderType, "> value) {\n",
              unionDiscrim.set,
              "  ::capnp::_::PointerHelpers<", type, ">::set(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), value);\n"
              "}\n"),
            COND(shouldIncludeStructInit,
              COND(shouldTemplatizeInit,
                templateContext.allDecls(),
                "template <typename T_>\n"
                "inline ::capnp::BuilderFor<T_> ", scope, "Builder::init", titleCase, "As() {\n",
                "  static_assert(::capnp::kind<T_>() == ::capnp::Kind::STRUCT,\n"
                "                \"", proto.getName(), " must be a struct\");\n",
                unionDiscrim.set,
                "  return ::capnp::_::PointerHelpers<T_>::init(_builder.getPointerField(\n"
                "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
                "}\n"),
              COND(!shouldTemplatizeInit,
                templateContext.allDecls(),
                "inline ", builderType, " ", scope, "Builder::init", titleCase, "() {\n",
                unionDiscrim.set,
                "  return ::capnp::_::PointerHelpers<", type, ">::init(_builder.getPointerField(\n"
                "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
                "}\n")),
            COND(shouldIncludeSizedInit,
              COND(shouldTemplatizeInit,
                templateContext.allDecls(),
                "template <typename T_>\n"
                "inline ::capnp::BuilderFor<T_> ", scope, "Builder::init", titleCase, "As(unsigned int size) {\n",
                "  static_assert(::capnp::kind<T_>() == ::capnp::Kind::LIST,\n"
                "                \"", proto.getName(), " must be a list\");\n",
                unionDiscrim.set,
                "  return ::capnp::_::PointerHelpers<T_>::init(_builder.getPointerField(\n"
                "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), size);\n"
                "}\n"),
              COND(!shouldTemplatizeInit,
                templateContext.allDecls(),
                "inline ", builderType, " ", scope, "Builder::init", titleCase, "(unsigned int size) {\n",
                unionDiscrim.set,
                "  return ::capnp::_::PointerHelpers<", type, ">::init(_builder.getPointerField(\n"
                "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), size);\n"
                "}\n")),
            templateContext.allDecls(),
            "inline void ", scope, "Builder::adopt", titleCase, "(\n"
            "    ::capnp::Orphan<", type, ">&& value) {\n",
            unionDiscrim.set,
            "  ::capnp::_::PointerHelpers<", type, ">::adopt(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS), kj::mv(value));\n"
            "}\n",
            COND(type.hasDisambiguatedTemplate(),
                "#ifndef _MSC_VER\n"
                "// Excluded under MSVC because bugs may make it unable to compile this method.\n"),
            templateContext.allDecls(),
            "inline ::capnp::Orphan<", type, "> ", scope, "Builder::disown", titleCase, "() {\n",
            unionDiscrim.check,
            "  return ::capnp::_::PointerHelpers<", type, ">::disown(_builder.getPointerField(\n"
            "      ::capnp::bounded<", offset, ">() * ::capnp::POINTERS));\n"
            "}\n",
            COND(type.hasDisambiguatedTemplate(), "#endif  // !_MSC_VER\n"),
            COND(shouldExcludeInLiteMode, "#endif  // !CAPNP_LITE\n"),
            "\n")
      };

      #undef COND
    }
  }

  // -----------------------------------------------------------------

  enum class AsGenericRole { READER, BUILDER, CLIENT };

  kj::StringTree makeAsGenericDef(AsGenericRole role, const TemplateContext& templateContext,
                                  kj::StringPtr type, kj::String innerType = kj::String()) {
    if (!templateContext.isGeneric()) {
      return kj::StringTree();
    }
    kj::StringTree self, up;
    if (templateContext.hasParams()) {
      auto returnType = [&]() {
        return kj::strTree(type, templateContext.args("2"), innerType);
      };
      kj::StringTree asGeneric = kj::strTree("as", innerType.size() ? type : "", "Generic()");
      switch (role) {
        case AsGenericRole::READER:
          self = kj::strTree(
              "  ", templateContext.decl(true, "2"),
              "  typename ", returnType(), "::Reader ", kj::mv(asGeneric), " {\n"
              "    return typename ", returnType(), "::Reader(_reader);\n"
              "  }\n"
              "\n");
          break;
        case AsGenericRole::BUILDER:
          self = kj::strTree(
              "  ", templateContext.decl(true, "2"),
              "  typename ", returnType(), "::Builder ", kj::mv(asGeneric), " {\n"
              "    return typename ", returnType(), "::Builder(_builder);\n"
              "  }\n"
              "\n");
          break;
        case AsGenericRole::CLIENT:
          self = kj::strTree(
              "  ", templateContext.decl(true, "2"),
              "  typename ", returnType(), "::Client ", kj::mv(asGeneric), " {\n"
              "    return castAs<", innerType.size() ? "typename " : "", returnType(), ">();\n"
              "  }\n"
              "\n");
          break;
      }
    }
    KJ_IF_MAYBE(p, templateContext.getParent()) {
      up = makeAsGenericDef(role, *p, p->getName(), kj::strTree(
          templateContext.hasParams() ? "::template " : "::", type,
          templateContext.args()).flatten());
    }
    return kj::strTree(kj::mv(self), kj::mv(up));
  }

  // -----------------------------------------------------------------

  struct StructText {
    kj::StringTree outerTypeDecl;
    kj::StringTree outerTypeDef;
    kj::StringTree readerBuilderDefs;
    kj::StringTree inlineMethodDefs;
    kj::StringTree sourceDefs;
  };

  kj::StringTree makeReaderDef(kj::StringPtr fullName, kj::StringPtr unqualifiedParentType,
                               const TemplateContext& templateContext, bool isUnion,
                               kj::Array<kj::StringTree>&& methodDecls) {
    return kj::strTree(
        templateContext.allDecls(),
        "class ", fullName, "::Reader {\n"
        "public:\n"
        "  typedef ", unqualifiedParentType, " Reads;\n"
        "\n"
        "  Reader() = default;\n"
        "  inline explicit Reader(::capnp::_::StructReader base): _reader(base) {}\n"
        "\n"
        "  inline ::capnp::MessageSize totalSize() const {\n"
        "    return _reader.totalSize().asPublic();\n"
        "  }\n"
        "\n"
        "#if !CAPNP_LITE\n"
        "  inline ::kj::StringTree toString() const {\n"
        "    return ::capnp::_::structString(_reader, *_capnpPrivate::brand());\n"
        "  }\n"
        "#endif  // !CAPNP_LITE\n"
        "\n",
        makeAsGenericDef(AsGenericRole::READER, templateContext, unqualifiedParentType),
        isUnion ? kj::strTree("  inline Which which() const;\n") : kj::strTree(),
        kj::mv(methodDecls),
        "private:\n"
        "  ::capnp::_::StructReader _reader;\n"
        "  template <typename, ::capnp::Kind>\n"
        "  friend struct ::capnp::ToDynamic_;\n"
        "  template <typename, ::capnp::Kind>\n"
        "  friend struct ::capnp::_::PointerHelpers;\n"
        "  template <typename, ::capnp::Kind>\n"
        "  friend struct ::capnp::List;\n"
        "  friend class ::capnp::MessageBuilder;\n"
        "  friend class ::capnp::Orphanage;\n"
        "};\n"
        "\n");
  }

  kj::StringTree makeBuilderDef(kj::StringPtr fullName, kj::StringPtr unqualifiedParentType,
                                const TemplateContext& templateContext, bool isUnion,
                                kj::Array<kj::StringTree>&& methodDecls) {
    return kj::strTree(
        templateContext.allDecls(),
        "class ", fullName, "::Builder {\n"
        "public:\n"
        "  typedef ", unqualifiedParentType, " Builds;\n"
        "\n"
        "  Builder() = delete;  // Deleted to discourage incorrect usage.\n"
        "                       // You can explicitly initialize to nullptr instead.\n"
        "  inline Builder(decltype(nullptr)) {}\n"
        "  inline explicit Builder(::capnp::_::StructBuilder base): _builder(base) {}\n"
        "  inline operator Reader() const { return Reader(_builder.asReader()); }\n"
        "  inline Reader asReader() const { return *this; }\n"
        "\n"
        "  inline ::capnp::MessageSize totalSize() const { return asReader().totalSize(); }\n"
        "#if !CAPNP_LITE\n"
        "  inline ::kj::StringTree toString() const { return asReader().toString(); }\n"
        "#endif  // !CAPNP_LITE\n"
        "\n",
        makeAsGenericDef(AsGenericRole::BUILDER, templateContext, unqualifiedParentType),
        isUnion ? kj::strTree("  inline Which which();\n") : kj::strTree(),
        kj::mv(methodDecls),
        "private:\n"
        "  ::capnp::_::StructBuilder _builder;\n"
        "  template <typename, ::capnp::Kind>\n"
        "  friend struct ::capnp::ToDynamic_;\n"
        "  friend class ::capnp::Orphanage;\n",
        "  template <typename, ::capnp::Kind>\n"
        "  friend struct ::capnp::_::PointerHelpers;\n"
        "};\n"
        "\n");
  }

  kj::StringTree makePipelineDef(kj::StringPtr fullName, kj::StringPtr unqualifiedParentType,
                                 const TemplateContext& templateContext, bool isUnion,
                                 kj::Array<kj::StringTree>&& methodDecls) {
    return kj::strTree(
        "#if !CAPNP_LITE\n",
        templateContext.allDecls(),
        "class ", fullName, "::Pipeline {\n"
        "public:\n"
        "  typedef ", unqualifiedParentType, " Pipelines;\n"
        "\n"
        "  inline Pipeline(decltype(nullptr)): _typeless(nullptr) {}\n"
        "  inline explicit Pipeline(::capnp::AnyPointer::Pipeline&& typeless)\n"
        "      : _typeless(kj::mv(typeless)) {}\n"
        "\n",
        kj::mv(methodDecls),
        "private:\n"
        "  ::capnp::AnyPointer::Pipeline _typeless;\n"
        "  friend class ::capnp::PipelineHook;\n"
        "  template <typename, ::capnp::Kind>\n"
        "  friend struct ::capnp::ToDynamic_;\n"
        "};\n"
        "#endif  // !CAPNP_LITE\n"
        "\n");
  }

  kj::StringTree makeGenericDeclarations(const TemplateContext& templateContext,
                                         bool hasBrandDependencies) {
    // Returns the declarations for the private members of a generic struct/interface;
    // paired with the definitions from makeGenericDefinitions().
    return kj::strTree(
        "    static const ::capnp::_::RawBrandedSchema::Scope brandScopes[];\n"
        "    static const ::capnp::_::RawBrandedSchema::Binding brandBindings[];\n",
        (!hasBrandDependencies ? "" :
            "    static const ::capnp::_::RawBrandedSchema::Dependency brandDependencies[];\n"),
        "    static const ::capnp::_::RawBrandedSchema specificBrand;\n"
        "    static constexpr ::capnp::_::RawBrandedSchema const* brand() { "
        "return ::capnp::_::ChooseBrand<_capnpPrivate, ", templateContext.allArgs(), ">::brand(); }\n");
  }

  kj::StringTree makeGenericDefinitions(
      const TemplateContext& templateContext, kj::StringPtr fullName, kj::StringPtr hexId,
      BrandInitializerText brandInitializers) {
    // Returns the definitions for the members from makeGenericDeclarations().
    bool hasBrandDependencies = (brandInitializers.dependencies.size() != 0);

    auto scopeCount = templateContext.getScopeMap().size();
    auto dependencyCount = brandInitializers.dependencyCount;

    kj::String templates = kj::str(templateContext.allDecls());

    return kj::strTree(
        templates, "const ::capnp::_::RawBrandedSchema::Scope ", fullName,
        "::_capnpPrivate::brandScopes[] = ", kj::mv(brandInitializers.scopes), ";\n",

        templates, "const ::capnp::_::RawBrandedSchema::Binding ", fullName,
        "::_capnpPrivate::brandBindings[] = ", kj::mv(brandInitializers.bindings), ";\n",

        (!hasBrandDependencies ? kj::strTree("") : kj::strTree(
            templates, "const ::capnp::_::RawBrandedSchema::Dependency ", fullName,
            "::_capnpPrivate::brandDependencies[] = ", kj::mv(brandInitializers.dependencies),
            ";\n")),

        templates, "const ::capnp::_::RawBrandedSchema ", fullName, "::_capnpPrivate::specificBrand = {\n",
        "  &::capnp::schemas::s_", hexId, ", brandScopes, ",
        (!hasBrandDependencies ? "nullptr" : "brandDependencies"), ",\n",
        "  ", scopeCount, ", ", dependencyCount,
        ", nullptr\n"
        "};\n");
  }

  StructText makeStructText(kj::StringPtr scope, kj::StringPtr name, StructSchema schema,
                            kj::Array<kj::StringTree> nestedTypeDecls,
                            const TemplateContext& templateContext) {
    auto proto = schema.getProto();
    KJ_IF_MAYBE(annotatedName, annotationValue(proto, NAME_ANNOTATION_ID)) {
      name = annotatedName->getText();
    }
    auto fullName = kj::str(scope, name, templateContext.args());
    auto subScope = kj::str(fullName, "::");
    auto fieldTexts = KJ_MAP(f, schema.getFields()) {
      return makeFieldText(subScope, f, templateContext);
    };

    auto structNode = proto.getStruct();
    uint discrimOffset = structNode.getDiscriminantOffset();
    auto hexId = kj::hex(proto.getId());

    kj::String templates = kj::str(templateContext.allDecls());  // Ends with a newline

    // Private members struct
    kj::StringTree declareText = kj::strTree(
         "  struct _capnpPrivate {\n"
         "    CAPNP_DECLARE_STRUCT_HEADER(", hexId, ", ", structNode.getDataWordCount(), ", ",
         structNode.getPointerCount(), ")\n");

    kj::StringTree defineText = kj::strTree(
        "// ", fullName, "\n",
        templates, "constexpr uint16_t ", fullName, "::_capnpPrivate::dataWordSize;\n",
        templates, "constexpr uint16_t ", fullName, "::_capnpPrivate::pointerCount;\n"
        "#if !CAPNP_LITE\n",
        templates, "constexpr ::capnp::Kind ", fullName, "::_capnpPrivate::kind;\n",
        templates, "constexpr ::capnp::_::RawSchema const* ", fullName, "::_capnpPrivate::schema;\n");

    if (templateContext.isGeneric()) {
      auto brandInitializers = makeBrandInitializers(templateContext, schema);
      bool hasDeps = (brandInitializers.dependencies.size() != 0);

      declareText = kj::strTree(kj::mv(declareText),
          "    #if !CAPNP_LITE\n",
          makeGenericDeclarations(templateContext, hasDeps),
          "    #endif  // !CAPNP_LITE\n");

      defineText = kj::strTree(kj::mv(defineText),
          makeGenericDefinitions(
              templateContext, fullName, kj::str(hexId), kj::mv(brandInitializers)));
    } else {
      declareText = kj::strTree(kj::mv(declareText),
          "    #if !CAPNP_LITE\n"
          "    static constexpr ::capnp::_::RawBrandedSchema const* brand() { return &schema->defaultBrand; }\n"
          "    #endif  // !CAPNP_LITE\n");
    }

    declareText = kj::strTree(kj::mv(declareText), "  };");
    defineText = kj::strTree(kj::mv(defineText), "#endif  // !CAPNP_LITE\n\n");

    // Name of the ::Which type, when applicable.
    CppTypeName whichName;
    if (structNode.getDiscriminantCount() != 0) {
      whichName = cppFullName(schema, nullptr);
      whichName.addMemberType("Which");
    }

    return StructText {
      kj::strTree(
          templateContext.hasParams() ? "  " : "", templateContext.decl(true),
          "  struct ", name, ";\n"),

      kj::strTree(
          templateContext.parentDecls(),
          templateContext.decl(scope == nullptr),
          "struct ", scope, name, " {\n",
          "  ", name, "() = delete;\n"
          "\n"
          "  class Reader;\n"
          "  class Builder;\n"
          "  class Pipeline;\n",
          structNode.getDiscriminantCount() == 0 ? kj::strTree() : kj::strTree(
              "  enum Which: uint16_t {\n",
              KJ_MAP(f, structNode.getFields()) {
                if (hasDiscriminantValue(f)) {
                  return kj::strTree("    ", toUpperCase(protoName(f)), ",\n");
                } else {
                  return kj::strTree();
                }
              },
              "  };\n"),
          KJ_MAP(n, nestedTypeDecls) { return kj::mv(n); },
          "\n",
          kj::mv(declareText), "\n",
          "};\n"
          "\n"),

      kj::strTree(
          makeReaderDef(fullName, name, templateContext, structNode.getDiscriminantCount() != 0,
                        KJ_MAP(f, fieldTexts) { return kj::mv(f.readerMethodDecls); }),
          makeBuilderDef(fullName, name, templateContext, structNode.getDiscriminantCount() != 0,
                         KJ_MAP(f, fieldTexts) { return kj::mv(f.builderMethodDecls); }),
          makePipelineDef(fullName, name, templateContext, structNode.getDiscriminantCount() != 0,
                          KJ_MAP(f, fieldTexts) { return kj::mv(f.pipelineMethodDecls); })),

      kj::strTree(
          structNode.getDiscriminantCount() == 0 ? kj::strTree() : kj::strTree(
              templateContext.allDecls(),
              "inline ", whichName, " ", fullName, "::Reader::which() const {\n"
              "  return _reader.getDataField<Which>(\n"
              "      ::capnp::bounded<", discrimOffset, ">() * ::capnp::ELEMENTS);\n"
              "}\n",
              templateContext.allDecls(),
              "inline ", whichName, " ", fullName, "::Builder::which() {\n"
              "  return _builder.getDataField<Which>(\n"
              "      ::capnp::bounded<", discrimOffset, ">() * ::capnp::ELEMENTS);\n"
              "}\n"
              "\n"),
          KJ_MAP(f, fieldTexts) { return kj::mv(f.inlineMethodDefs); }),

      kj::mv(defineText)
    };
  }

  // -----------------------------------------------------------------

  struct MethodText {
    kj::StringTree clientDecls;
    kj::StringTree serverDecls;
    kj::StringTree inlineDefs;
    kj::StringTree sourceDefs;
    kj::StringTree dispatchCase;
  };

  MethodText makeMethodText(kj::StringPtr interfaceName, InterfaceSchema::Method method,
                            const TemplateContext& templateContext) {
    auto proto = method.getProto();
    auto name = protoName(proto);
    auto titleCase = toTitleCase(name);
    auto paramSchema = method.getParamType();
    auto resultSchema = method.getResultType();
    auto identifierName = safeIdentifier(name);

    auto paramProto = paramSchema.getProto();
    auto resultProto = resultSchema.getProto();

    auto implicitParamsReader = proto.getImplicitParameters();
    auto implicitParamsBuilder = kj::heapArrayBuilder<CppTypeName>(implicitParamsReader.size());
    for (auto param: implicitParamsReader) {
      implicitParamsBuilder.add(CppTypeName::makeTemplateParam(param.getName()));
    }
    auto implicitParams = implicitParamsBuilder.finish();

    kj::String implicitParamsTemplateDecl;
    if (implicitParams.size() > 0) {
      implicitParamsTemplateDecl = kj::str(
          "template <", kj::StringTree(KJ_MAP(p, implicitParams) {
            return kj::strTree("typename ", p);
          }, ", "), ">\n");
    }


    CppTypeName interfaceTypeName = cppFullName(method.getContainingInterface(), nullptr);
    CppTypeName paramType;
    CppTypeName genericParamType;
    if (paramProto.getScopeId() == 0) {
      paramType = interfaceTypeName;
      if (implicitParams.size() == 0) {
        paramType.addMemberType(kj::str(titleCase, "Params"));
        genericParamType = paramType;
      } else {
        genericParamType = paramType;
        genericParamType.addMemberTemplate(kj::str(titleCase, "Params"), nullptr);
        paramType.addMemberTemplate(kj::str(titleCase, "Params"),
                                    kj::heapArray(implicitParams.asPtr()));
      }
    } else {
      paramType = cppFullName(paramSchema, method);
      genericParamType = cppFullName(paramSchema, nullptr);
    }
    CppTypeName resultType;
    CppTypeName genericResultType;
    if (resultProto.getScopeId() == 0) {
      resultType = interfaceTypeName;
      if (implicitParams.size() == 0) {
        resultType.addMemberType(kj::str(titleCase, "Results"));
        genericResultType = resultType;
      } else {
        genericResultType = resultType;
        genericResultType.addMemberTemplate(kj::str(titleCase, "Results"), nullptr);
        resultType.addMemberTemplate(kj::str(titleCase, "Results"),
                                     kj::heapArray(implicitParams.asPtr()));
      }
    } else {
      resultType = cppFullName(resultSchema, method);
      genericResultType = cppFullName(resultSchema, nullptr);
    }

    kj::String shortParamType = paramProto.getScopeId() == 0 ?
        kj::str(titleCase, "Params") : kj::str(genericParamType);
    kj::String shortResultType = resultProto.getScopeId() == 0 ?
        kj::str(titleCase, "Results") : kj::str(genericResultType);

    auto interfaceProto = method.getContainingInterface().getProto();
    uint64_t interfaceId = interfaceProto.getId();
    auto interfaceIdHex = kj::hex(interfaceId);
    uint16_t methodId = method.getIndex();

    // TODO(msvc):  Notice that the return type of this method's request function is supposed to be
    // `::capnp::Request<param, result>`. If the first template parameter to ::capnp::Request is a
    // template instantiation, MSVC will sometimes complain that it's unspecialized and can't be
    // used as a parameter in the return type (error C3203). It is not clear to me under what exact
    // conditions this bug occurs, but it commonly crops up in test.capnp.h.
    //
    // The easiest (and only) workaround I found is to use C++14's return type deduction here, thus
    // the `CAPNP_AUTO_IF_MSVC()` hackery in the return type declarations below. We're depending on
    // the fact that that this function has an inline implementation for the deduction to work.

    auto requestMethodImpl = kj::strTree(
        templateContext.allDecls(),
        implicitParamsTemplateDecl,
        templateContext.isGeneric() ? "CAPNP_AUTO_IF_MSVC(" : "",
        "::capnp::Request<", paramType, ", ", resultType, ">",
        templateContext.isGeneric() ? ")\n" : "\n",
        interfaceName, "::Client::", name, "Request(::kj::Maybe< ::capnp::MessageSize> sizeHint) {\n"
        "  return newCall<", paramType, ", ", resultType, ">(\n"
        "      0x", interfaceIdHex, "ull, ", methodId, ", sizeHint);\n"
        "}\n");

    return MethodText {
      kj::strTree(
          implicitParamsTemplateDecl.size() == 0 ? "" : "  ", implicitParamsTemplateDecl,
          templateContext.isGeneric() ? "  CAPNP_AUTO_IF_MSVC(" : "  ",
          "::capnp::Request<", paramType, ", ", resultType, ">",
          templateContext.isGeneric() ? ")" : "",
          " ", name, "Request(\n"
          "      ::kj::Maybe< ::capnp::MessageSize> sizeHint = nullptr);\n"),

      kj::strTree(
          paramProto.getScopeId() != 0 ? kj::strTree() : kj::strTree(
              "  typedef ", genericParamType, " ", titleCase, "Params;\n"),
          resultProto.getScopeId() != 0 ? kj::strTree() : kj::strTree(
              "  typedef ", genericResultType, " ", titleCase, "Results;\n"),
          "  typedef ::capnp::CallContext<", shortParamType, ", ", shortResultType, "> ",
                titleCase, "Context;\n"
          "  virtual ::kj::Promise<void> ", identifierName, "(", titleCase, "Context context);\n"),

      implicitParams.size() == 0 ? kj::strTree() : kj::mv(requestMethodImpl),

      kj::strTree(
          implicitParams.size() == 0 ? kj::mv(requestMethodImpl) : kj::strTree(),
          templateContext.allDecls(),
          "::kj::Promise<void> ", interfaceName, "::Server::", identifierName, "(", titleCase, "Context) {\n"
          "  return ::capnp::Capability::Server::internalUnimplemented(\n"
          "      \"", interfaceProto.getDisplayName(), "\", \"", name, "\",\n"
          "      0x", interfaceIdHex, "ull, ", methodId, ");\n"
          "}\n"),

      kj::strTree(
          "    case ", methodId, ":\n"
          "      return ", identifierName, "(::capnp::Capability::Server::internalGetTypedContext<\n"
          "          ", genericParamType, ", ", genericResultType, ">(context));\n")
    };
  }

  struct InterfaceText {
    kj::StringTree outerTypeDecl;
    kj::StringTree outerTypeDef;
    kj::StringTree clientServerDefs;
    kj::StringTree inlineMethodDefs;
    kj::StringTree sourceDefs;
  };

  struct ExtendInfo {
    CppTypeName typeName;
    uint64_t id;
  };

  void getTransitiveSuperclasses(InterfaceSchema schema, std::map<uint64_t, InterfaceSchema>& map) {
    if (map.insert(std::make_pair(schema.getProto().getId(), schema)).second) {
      for (auto sup: schema.getSuperclasses()) {
        getTransitiveSuperclasses(sup, map);
      }
    }
  }

  InterfaceText makeInterfaceText(kj::StringPtr scope, kj::StringPtr name, InterfaceSchema schema,
                                  kj::Array<kj::StringTree> nestedTypeDecls,
                                  const TemplateContext& templateContext) {
    auto fullName = kj::str(scope, name, templateContext.args());
    auto methods = KJ_MAP(m, schema.getMethods()) {
      return makeMethodText(fullName, m, templateContext);
    };

    auto proto = schema.getProto();
    auto hexId = kj::hex(proto.getId());

    auto superclasses = KJ_MAP(superclass, schema.getSuperclasses()) {
      return ExtendInfo { cppFullName(superclass, nullptr), superclass.getProto().getId() };
    };

    kj::Array<ExtendInfo> transitiveSuperclasses;
    {
      std::map<uint64_t, InterfaceSchema> map;
      getTransitiveSuperclasses(schema, map);
      map.erase(schema.getProto().getId());
      transitiveSuperclasses = KJ_MAP(entry, map) {
        return ExtendInfo { cppFullName(entry.second, nullptr), entry.second.getProto().getId() };
      };
    }

    CppTypeName typeName = cppFullName(schema, nullptr);

    CppTypeName clientName = typeName;
    clientName.addMemberType("Client");

    kj::String templates = kj::str(templateContext.allDecls());  // Ends with a newline

    // Private members struct
    kj::StringTree declareText = kj::strTree(
         "  #if !CAPNP_LITE\n"
         "  struct _capnpPrivate {\n"
         "    CAPNP_DECLARE_INTERFACE_HEADER(", hexId, ")\n");

    kj::StringTree defineText = kj::strTree(
        "// ", fullName, "\n",
        "#if !CAPNP_LITE\n",
        templates, "constexpr ::capnp::Kind ", fullName, "::_capnpPrivate::kind;\n",
        templates, "constexpr ::capnp::_::RawSchema const* ", fullName, "::_capnpPrivate::schema;\n");

    if (templateContext.isGeneric()) {
      auto brandInitializers = makeBrandInitializers(templateContext, schema);
      bool hasDeps = (brandInitializers.dependencies.size() != 0);

      declareText = kj::strTree(kj::mv(declareText),
          makeGenericDeclarations(templateContext, hasDeps));

      defineText = kj::strTree(kj::mv(defineText),
          makeGenericDefinitions(
              templateContext, fullName, kj::str(hexId), kj::mv(brandInitializers)));
    } else {
      declareText = kj::strTree(kj::mv(declareText),
        "    static constexpr ::capnp::_::RawBrandedSchema const* brand() { return &schema->defaultBrand; }\n");
    }

    declareText = kj::strTree(kj::mv(declareText), "  };\n  #endif  // !CAPNP_LITE");
    defineText = kj::strTree(kj::mv(defineText), "#endif  // !CAPNP_LITE\n\n");

    return InterfaceText {
      kj::strTree(
          templateContext.hasParams() ? "  " : "", templateContext.decl(true),
          "  struct ", name, ";\n"),

      kj::strTree(
          templateContext.parentDecls(),
          templateContext.decl(scope == nullptr),
          "struct ", scope, name, " {\n",
          "  ", name, "() = delete;\n"
          "\n"
          "#if !CAPNP_LITE\n"
          "  class Client;\n"
          "  class Server;\n"
          "#endif  // !CAPNP_LITE\n"
          "\n",
          KJ_MAP(n, nestedTypeDecls) { return kj::mv(n); },
          "\n",
          kj::mv(declareText), "\n"
          "};\n"
          "\n"),

      kj::strTree(
          "#if !CAPNP_LITE\n",
          templateContext.allDecls(),
          "class ", fullName, "::Client\n"
          "    : public virtual ::capnp::Capability::Client",
          KJ_MAP(s, superclasses) {
            return kj::strTree(",\n      public virtual ", s.typeName.strNoTypename(), "::Client");
          }, " {\n"
          "public:\n"
          "  typedef ", name, " Calls;\n"
          "  typedef ", name, " Reads;\n"
          "\n"
          "  Client(decltype(nullptr));\n"
          "  explicit Client(::kj::Own< ::capnp::ClientHook>&& hook);\n"
          "  template <typename _t, typename = ::kj::EnableIf< ::kj::canConvert<_t*, Server*>()>>\n"
          "  Client(::kj::Own<_t>&& server);\n"
          "  template <typename _t, typename = ::kj::EnableIf< ::kj::canConvert<_t*, Client*>()>>\n"
          "  Client(::kj::Promise<_t>&& promise);\n"
          "  Client(::kj::Exception&& exception);\n"
          "  Client(Client&) = default;\n"
          "  Client(Client&&) = default;\n"
          "  Client& operator=(Client& other);\n"
          "  Client& operator=(Client&& other);\n"
          "\n",
          makeAsGenericDef(AsGenericRole::CLIENT, templateContext, name),
          KJ_MAP(m, methods) { return kj::mv(m.clientDecls); },
          "\n"
          "protected:\n"
          "  Client() = default;\n"
          "};\n"
          "\n",
          templateContext.allDecls(),
          "class ", fullName, "::Server\n"
          "    : public virtual ::capnp::Capability::Server",
          KJ_MAP(s, superclasses) {
            return kj::strTree(",\n      public virtual ", s.typeName.strNoTypename(), "::Server");
          }, " {\n"
          "public:\n",
          "  typedef ", name, " Serves;\n"
          "\n"
          "  ::kj::Promise<void> dispatchCall(uint64_t interfaceId, uint16_t methodId,\n"
          "      ::capnp::CallContext< ::capnp::AnyPointer, ::capnp::AnyPointer> context)\n"
          "      override;\n"
          "\n"
          "protected:\n",
          KJ_MAP(m, methods) { return kj::mv(m.serverDecls); },
          "\n"
          "  inline ", clientName, " thisCap() {\n"
          "    return ::capnp::Capability::Server::thisCap()\n"
          "        .template castAs<", typeName, ">();\n"
          "  }\n"
          "\n"
          "  ::kj::Promise<void> dispatchCallInternal(uint16_t methodId,\n"
          "      ::capnp::CallContext< ::capnp::AnyPointer, ::capnp::AnyPointer> context);\n"
          "};\n"
          "#endif  // !CAPNP_LITE\n"
          "\n"),

      kj::strTree(
          "#if !CAPNP_LITE\n",
          templateContext.allDecls(),
          "inline ", fullName, "::Client::Client(decltype(nullptr))\n"
          "    : ::capnp::Capability::Client(nullptr) {}\n",
          templateContext.allDecls(),
          "inline ", fullName, "::Client::Client(\n"
          "    ::kj::Own< ::capnp::ClientHook>&& hook)\n"
          "    : ::capnp::Capability::Client(::kj::mv(hook)) {}\n",
          templateContext.allDecls(),
          "template <typename _t, typename>\n"
          "inline ", fullName, "::Client::Client(::kj::Own<_t>&& server)\n"
          "    : ::capnp::Capability::Client(::kj::mv(server)) {}\n",
          templateContext.allDecls(),
          "template <typename _t, typename>\n"
          "inline ", fullName, "::Client::Client(::kj::Promise<_t>&& promise)\n"
          "    : ::capnp::Capability::Client(::kj::mv(promise)) {}\n",
          templateContext.allDecls(),
          "inline ", fullName, "::Client::Client(::kj::Exception&& exception)\n"
          "    : ::capnp::Capability::Client(::kj::mv(exception)) {}\n",
          templateContext.allDecls(),
          "inline ", clientName, "& ", fullName, "::Client::operator=(Client& other) {\n"
          "  ::capnp::Capability::Client::operator=(other);\n"
          "  return *this;\n"
          "}\n",
          templateContext.allDecls(),
          "inline ", clientName, "& ", fullName, "::Client::operator=(Client&& other) {\n"
          "  ::capnp::Capability::Client::operator=(kj::mv(other));\n"
          "  return *this;\n"
          "}\n"
          "\n",
          KJ_MAP(m, methods) { return kj::mv(m.inlineDefs); },
          "#endif  // !CAPNP_LITE\n"),

      kj::strTree(
          "#if !CAPNP_LITE\n",
          KJ_MAP(m, methods) { return kj::mv(m.sourceDefs); },
          templateContext.allDecls(),
          "::kj::Promise<void> ", fullName, "::Server::dispatchCall(\n"
          "    uint64_t interfaceId, uint16_t methodId,\n"
          "    ::capnp::CallContext< ::capnp::AnyPointer, ::capnp::AnyPointer> context) {\n"
          "  switch (interfaceId) {\n"
          "    case 0x", kj::hex(proto.getId()), "ull:\n"
          "      return dispatchCallInternal(methodId, context);\n",
          KJ_MAP(s, transitiveSuperclasses) {
            return kj::strTree(
              "    case 0x", kj::hex(s.id), "ull:\n"
              "      return ", s.typeName.strNoTypename(),
                         "::Server::dispatchCallInternal(methodId, context);\n");
          },
          "    default:\n"
          "      return internalUnimplemented(\"", proto.getDisplayName(), "\", interfaceId);\n"
          "  }\n"
          "}\n",
          templateContext.allDecls(),
          "::kj::Promise<void> ", fullName, "::Server::dispatchCallInternal(\n"
          "    uint16_t methodId,\n"
          "    ::capnp::CallContext< ::capnp::AnyPointer, ::capnp::AnyPointer> context) {\n"
          "  switch (methodId) {\n",
          KJ_MAP(m, methods) { return kj::mv(m.dispatchCase); },
          "    default:\n"
          "      (void)context;\n"
          "      return ::capnp::Capability::Server::internalUnimplemented(\n"
          "          \"", proto.getDisplayName(), "\",\n"
          "          0x", kj::hex(proto.getId()), "ull, methodId);\n"
          "  }\n"
          "}\n"
          "#endif  // !CAPNP_LITE\n"
          "\n",
          kj::mv(defineText))
    };
  }

  // -----------------------------------------------------------------

  struct ConstText {
    bool needsSchema;
    kj::StringTree decl;
    kj::StringTree def;
  };

  ConstText makeConstText(kj::StringPtr scope, kj::StringPtr name, ConstSchema schema,
                          const TemplateContext& templateContext) {
    auto proto = schema.getProto();
    auto constProto = proto.getConst();
    auto type = schema.getType();
    auto typeName_ = typeName(type, nullptr);
    auto upperCase = toUpperCase(name);

    // Linkage qualifier for non-primitive types.
    const char* linkage = scope.size() == 0 ? "extern " : "static ";

    switch (type.which()) {
      case schema::Value::BOOL:
      case schema::Value::INT8:
      case schema::Value::INT16:
      case schema::Value::INT32:
      case schema::Value::INT64:
      case schema::Value::UINT8:
      case schema::Value::UINT16:
      case schema::Value::UINT32:
      case schema::Value::UINT64:
      case schema::Value::ENUM:
        return ConstText {
          false,
          kj::strTree("static constexpr ", typeName_, ' ', upperCase, " = ",
              literalValue(schema.getType(), constProto.getValue()), ";\n"),
          scope.size() == 0 ? kj::strTree() : kj::strTree(
              // TODO(msvc): MSVC doesn't like definitions of constexprs, but other compilers and
              //   the standard require them.
              "#ifndef _MSC_VER\n"
              "constexpr ", typeName_, ' ', scope, upperCase, ";\n"
              "#endif\n")
        };

      case schema::Value::VOID:
      case schema::Value::FLOAT32:
      case schema::Value::FLOAT64: {
        // TODO(msvc): MSVC doesn't like float- or class-typed constexprs. As soon as this is fixed,
        //   treat VOID, FLOAT32, and FLOAT64 the same as the other primitives.
        kj::String value = literalValue(schema.getType(), constProto.getValue()).flatten();
        return ConstText {
          false,
          kj::strTree("static KJ_CONSTEXPR(const) ", typeName_, ' ', upperCase,
              " CAPNP_NON_INT_CONSTEXPR_DECL_INIT(", value, ");\n"),
          scope.size() == 0 ? kj::strTree() : kj::strTree(
              "KJ_CONSTEXPR(const) ", typeName_, ' ', scope, upperCase,
              " CAPNP_NON_INT_CONSTEXPR_DEF_INIT(", value, ");\n")
        };
      }

      case schema::Value::TEXT: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstText<", schema.as<Text>().size(), ">").flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::DATA: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstData<", schema.as<Data>().size(), ">").flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::STRUCT: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstStruct<", typeName_, ">").flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::LIST: {
        kj::String constType = kj::strTree(
            "::capnp::_::ConstList<", typeName(type.asList().getElementType(), nullptr), ">")
            .flatten();
        return ConstText {
          true,
          kj::strTree(linkage, "const ", constType, ' ', upperCase, ";\n"),
          kj::strTree("const ", constType, ' ', scope, upperCase, "(::capnp::schemas::b_",
                      kj::hex(proto.getId()), ".words + ", schema.getValueSchemaOffset(), ");\n")
        };
      }

      case schema::Value::ANY_POINTER:
      case schema::Value::INTERFACE:
        return ConstText { false, kj::strTree(), kj::strTree() };
    }

    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------

  struct NodeText {
    kj::StringTree outerTypeDecl;
    kj::StringTree outerTypeDef;
    kj::StringTree readerBuilderDefs;
    kj::StringTree inlineMethodDefs;
    kj::StringTree capnpSchemaDecls;
    kj::StringTree capnpSchemaDefs;
    kj::StringTree sourceFileDefs;
  };

  NodeText makeNodeText(kj::StringPtr namespace_, kj::StringPtr scope,
                        kj::StringPtr name, Schema schema,
                        const TemplateContext& parentTemplateContext) {
    // `templateContext` is something like "template <typename T>\ntemplate <typename U>\n"
    // declaring template parameters for all parent scopes.

    auto proto = schema.getProto();
    KJ_IF_MAYBE(annotatedName, annotationValue(proto, NAME_ANNOTATION_ID)) {
      name = annotatedName->getText();
    }
    auto hexId = kj::hex(proto.getId());

    TemplateContext templateContext(parentTemplateContext, name, proto);

    auto subScope = kj::str(scope, name, templateContext.args(), "::");

    // Compute nested nodes, including groups.
    kj::Vector<NodeText> nestedTexts(proto.getNestedNodes().size());
    for (auto nested: proto.getNestedNodes()) {
      nestedTexts.add(makeNodeText(
          namespace_, subScope, nested.getName(), schemaLoader.getUnbound(nested.getId()),\
          templateContext));
    };

    if (proto.isStruct()) {
      for (auto field: proto.getStruct().getFields()) {
        if (field.isGroup()) {
          nestedTexts.add(makeNodeText(
              namespace_, subScope, toTitleCase(protoName(field)),
              schemaLoader.getUnbound(field.getGroup().getTypeId()),
              templateContext));
        }
      }
    } else if (proto.isInterface()) {
      for (auto method: proto.getInterface().getMethods()) {
        {
          Schema params = schemaLoader.getUnbound(method.getParamStructType());
          auto paramsProto = schemaLoader.getUnbound(method.getParamStructType()).getProto();
          if (paramsProto.getScopeId() == 0) {
            nestedTexts.add(makeNodeText(namespace_, subScope,
                toTitleCase(kj::str(protoName(method), "Params")), params, templateContext));
          }
        }
        {
          Schema results = schemaLoader.getUnbound(method.getResultStructType());
          auto resultsProto = schemaLoader.getUnbound(method.getResultStructType()).getProto();
          if (resultsProto.getScopeId() == 0) {
            nestedTexts.add(makeNodeText(namespace_, subScope,
                toTitleCase(kj::str(protoName(method), "Results")), results, templateContext));
          }
        }
      }
    }

    // Convert the encoded schema to a literal byte array.
    kj::ArrayPtr<const word> rawSchema = schema.asUncheckedMessage();
    auto schemaLiteral = kj::StringTree(KJ_MAP(w, rawSchema) {
      const byte* bytes = reinterpret_cast<const byte*>(&w);

      return kj::strTree(KJ_MAP(i, kj::range<uint>(0, sizeof(word))) {
        auto text = kj::toCharSequence(kj::implicitCast<uint>(bytes[i]));
        return kj::strTree(kj::repeat(' ', 4 - text.size()), text, ",");
      });
    }, "\n   ");

    auto schemaDecl = kj::strTree(
        "CAPNP_DECLARE_SCHEMA(", hexId, ");\n");

    std::set<uint64_t> deps;
    enumerateDeps(proto, deps);

    kj::Array<uint> membersByName;
    kj::Array<uint> membersByDiscrim;
    switch (proto.which()) {
      case schema::Node::STRUCT: {
        auto structSchema = schema.asStruct();
        membersByName = makeMembersByName(structSchema.getFields());
        auto builder = kj::heapArrayBuilder<uint>(structSchema.getFields().size());
        for (auto field: structSchema.getUnionFields()) {
          builder.add(field.getIndex());
        }
        for (auto field: structSchema.getNonUnionFields()) {
          builder.add(field.getIndex());
        }
        membersByDiscrim = builder.finish();
        break;
      }
      case schema::Node::ENUM:
        membersByName = makeMembersByName(schema.asEnum().getEnumerants());
        break;
      case schema::Node::INTERFACE:
        membersByName = makeMembersByName(schema.asInterface().getMethods());
        break;
      default:
        break;
    }

    auto brandDeps = makeBrandDepInitializers(
        makeBrandDepMap(templateContext, schema.getGeneric()));

    auto schemaDef = kj::strTree(
        "static const ::capnp::_::AlignedData<", rawSchema.size(), "> b_", hexId, " = {\n"
        "  {", kj::mv(schemaLiteral), " }\n"
        "};\n"
        "::capnp::word const* const bp_", hexId, " = b_", hexId, ".words;\n"
        "#if !CAPNP_LITE\n",
        deps.size() == 0 ? kj::strTree() : kj::strTree(
            "static const ::capnp::_::RawSchema* const d_", hexId, "[] = {\n",
            KJ_MAP(depId, deps) {
              return kj::strTree("  &s_", kj::hex(depId), ",\n");
            },
            "};\n"),
        membersByName.size() == 0 ? kj::strTree() : kj::strTree(
            "static const uint16_t m_", hexId, "[] = {",
            kj::StringTree(KJ_MAP(index, membersByName) { return kj::strTree(index); }, ", "),
            "};\n"),
        membersByDiscrim.size() == 0 ? kj::strTree() : kj::strTree(
            "static const uint16_t i_", hexId, "[] = {",
            kj::StringTree(KJ_MAP(index, membersByDiscrim) { return kj::strTree(index); }, ", "),
            "};\n"),
        brandDeps.size() == 0 ? kj::strTree() : kj::strTree(
            "KJ_CONSTEXPR(const) ::capnp::_::RawBrandedSchema::Dependency bd_", hexId, "[] = ",
            kj::mv(brandDeps), ";\n"),
        "const ::capnp::_::RawSchema s_", hexId, " = {\n"
        "  0x", hexId, ", b_", hexId, ".words, ", rawSchema.size(), ", ",
        deps.size() == 0 ? kj::strTree("nullptr") : kj::strTree("d_", hexId), ", ",
        membersByName.size() == 0 ? kj::strTree("nullptr") : kj::strTree("m_", hexId), ",\n",
        "  ", deps.size(), ", ", membersByName.size(), ", ",
        membersByDiscrim.size() == 0 ? kj::strTree("nullptr") : kj::strTree("i_", hexId),
        ", nullptr, nullptr, { &s_", hexId, ", nullptr, ",
        brandDeps.size() == 0 ? kj::strTree("nullptr, 0, 0") : kj::strTree(
            "bd_", hexId, ", 0, " "sizeof(bd_", hexId, ") / sizeof(bd_", hexId, "[0])"),
        ", nullptr }\n"
        "};\n"
        "#endif  // !CAPNP_LITE\n");

    NodeText top = makeNodeTextWithoutNested(
        namespace_, scope, name, schema,
        KJ_MAP(n, nestedTexts) { return kj::mv(n.outerTypeDecl); },
        templateContext);

    NodeText result = {
      kj::mv(top.outerTypeDecl),

      kj::strTree(
          kj::mv(top.outerTypeDef),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.outerTypeDef); }),

      kj::strTree(
          kj::mv(top.readerBuilderDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.readerBuilderDefs); }),

      kj::strTree(
          kj::mv(top.inlineMethodDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.inlineMethodDefs); }),

      kj::strTree(
          kj::mv(schemaDecl),
          kj::mv(top.capnpSchemaDecls),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpSchemaDecls); }),

      kj::strTree(
          kj::mv(schemaDef),
          kj::mv(top.capnpSchemaDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.capnpSchemaDefs); }),

      kj::strTree(
          kj::mv(top.sourceFileDefs),
          KJ_MAP(n, nestedTexts) { return kj::mv(n.sourceFileDefs); }),
    };

    if (templateContext.isGeneric()) {
      // This is a template, so move all source declarations into the header.
      result.inlineMethodDefs = kj::strTree(
          kj::mv(result.inlineMethodDefs), kj::mv(result.sourceFileDefs));
      result.sourceFileDefs = kj::strTree();
    }

    return result;
  }

  NodeText makeNodeTextWithoutNested(kj::StringPtr namespace_, kj::StringPtr scope,
                                     kj::StringPtr name, Schema schema,
                                     kj::Array<kj::StringTree> nestedTypeDecls,
                                     const TemplateContext& templateContext) {
    auto proto = schema.getProto();
    KJ_IF_MAYBE(annotatedName, annotationValue(proto, NAME_ANNOTATION_ID)) {
      name = annotatedName->getText();
    }
    auto hexId = kj::hex(proto.getId());

    switch (proto.which()) {
      case schema::Node::FILE:
        KJ_FAIL_REQUIRE("This method shouldn't be called on file nodes.");

      case schema::Node::STRUCT: {
        StructText structText =
            makeStructText(scope, name, schema.asStruct(), kj::mv(nestedTypeDecls),
                           templateContext);

        return NodeText {
          kj::mv(structText.outerTypeDecl),
          kj::mv(structText.outerTypeDef),
          kj::mv(structText.readerBuilderDefs),
          kj::mv(structText.inlineMethodDefs),

          kj::strTree(),
          kj::strTree(),

          kj::mv(structText.sourceDefs),
        };
      }

      case schema::Node::ENUM: {
        auto enumerants = schema.asEnum().getEnumerants();

        return NodeText {
          scope.size() == 0 ? kj::strTree() : kj::strTree(
              "  typedef ::capnp::schemas::", name, "_", hexId, " ", name, ";\n"
              "\n"),

          scope.size() > 0 ? kj::strTree() : kj::strTree(
              "typedef ::capnp::schemas::", name, "_", hexId, " ", name, ";\n"
              "\n"),

          kj::strTree(),
          kj::strTree(),

          kj::strTree(
              // We declare enums in the capnp::schemas namespace and then typedef them into
              // place because we don't want them to be parameterized for generics.
              "enum class ", name, "_", hexId, ": uint16_t {\n",
              KJ_MAP(e, enumerants) {
                return kj::strTree("  ", toUpperCase(protoName(e.getProto())), ",\n");
              },
              "};\n"
              "CAPNP_DECLARE_ENUM(", name, ", ", hexId, ");\n"),
          kj::strTree(
              "CAPNP_DEFINE_ENUM(", name, "_", hexId, ", ", hexId, ");\n"),

          kj::strTree(),
        };
      }

      case schema::Node::INTERFACE: {
        hasInterfaces = true;

        InterfaceText interfaceText =
            makeInterfaceText(scope, name, schema.asInterface(), kj::mv(nestedTypeDecls),
                              templateContext);

        return NodeText {
          kj::mv(interfaceText.outerTypeDecl),
          kj::mv(interfaceText.outerTypeDef),
          kj::mv(interfaceText.clientServerDefs),
          kj::mv(interfaceText.inlineMethodDefs),

          kj::strTree(),
          kj::strTree(),

          kj::mv(interfaceText.sourceDefs),
        };
      }

      case schema::Node::CONST: {
        auto constText = makeConstText(scope, name, schema.asConst(), templateContext);

        return NodeText {
          scope.size() == 0 ? kj::strTree() : kj::strTree("  ", kj::mv(constText.decl)),
          scope.size() > 0 ? kj::strTree() : kj::mv(constText.decl),
          kj::strTree(),
          kj::strTree(),

          kj::strTree(),
          kj::strTree(),

          kj::mv(constText.def),
        };
      }

      case schema::Node::ANNOTATION: {
        return NodeText {
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),
          kj::strTree(),

          kj::strTree(),
          kj::strTree(),

          kj::strTree(),
        };
      }
    }

    KJ_UNREACHABLE;
  }

  // -----------------------------------------------------------------

  struct FileText {
    kj::StringTree header;
    kj::StringTree source;
  };

  FileText makeFileText(Schema schema,
                        schema::CodeGeneratorRequest::RequestedFile::Reader request) {
    usedImports.clear();

    auto node = schema.getProto();
    auto displayName = node.getDisplayName();

    kj::Vector<kj::ArrayPtr<const char>> namespaceParts;
    kj::String namespacePrefix;

    for (auto annotation: node.getAnnotations()) {
      if (annotation.getId() == NAMESPACE_ANNOTATION_ID) {
        kj::StringPtr ns = annotation.getValue().getText();
        kj::StringPtr ns2 = ns;
        namespacePrefix = kj::str("::", ns);

        for (;;) {
          KJ_IF_MAYBE(colonPos, ns.findFirst(':')) {
            namespaceParts.add(ns.slice(0, *colonPos));
            ns = ns.slice(*colonPos);
            if (!ns.startsWith("::")) {
              context.exitError(kj::str(displayName, ": invalid namespace spec: ", ns2));
            }
            ns = ns.slice(2);
          } else {
            namespaceParts.add(ns);
            break;
          }
        }

        break;
      }
    }

    auto nodeTexts = KJ_MAP(nested, node.getNestedNodes()) {
      return makeNodeText(namespacePrefix, "", nested.getName(),
                          schemaLoader.getUnbound(nested.getId()), TemplateContext());
    };

    kj::String separator = kj::str("// ", kj::repeat('=', 87), "\n");

    kj::Vector<kj::StringPtr> includes;
    for (auto import: request.getImports()) {
      if (usedImports.count(import.getId()) > 0) {
        includes.add(import.getName());
      }
    }

    kj::StringTree sourceDefs = kj::strTree(
        KJ_MAP(n, nodeTexts) { return kj::mv(n.sourceFileDefs); });

    return FileText {
      kj::strTree(
          "// Generated by Cap'n Proto compiler, DO NOT EDIT\n"
          "// source: ", baseName(displayName), "\n"
          "\n"
          "#ifndef CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n",
          "#define CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n"
          "\n"
          "#include <capnp/generated-header-support.h>\n",
          hasInterfaces ? kj::strTree(
            "#if !CAPNP_LITE\n"
            "#include <capnp/capability.h>\n"
            "#endif  // !CAPNP_LITE\n"
          ) : kj::strTree(),
          "\n"
          "#if CAPNP_VERSION != ", CAPNP_VERSION, "\n"
          "#error \"Version mismatch between generated code and library headers.  You must "
              "use the same version of the Cap'n Proto compiler and library.\"\n"
          "#endif\n"
          "\n",
          KJ_MAP(path, includes) {
            if (path.startsWith("/")) {
              return kj::strTree("#include <", path.slice(1), ".h>\n");
            } else {
              return kj::strTree("#include \"", path, ".h\"\n");
            }
          },
          "\n"
          "namespace capnp {\n"
          "namespace schemas {\n"
          "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.capnpSchemaDecls); },
          "\n"
          "}  // namespace schemas\n"
          "}  // namespace capnp\n"
          "\n",

          KJ_MAP(n, namespaceParts) { return kj::strTree("namespace ", n, " {\n"); }, "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.outerTypeDef); },
          separator, "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.readerBuilderDefs); },
          separator, "\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.inlineMethodDefs); },
          KJ_MAP(n, namespaceParts) { return kj::strTree("}  // namespace\n"); }, "\n",
          "#endif  // CAPNP_INCLUDED_", kj::hex(node.getId()), "_\n"),

      kj::strTree(
          "// Generated by Cap'n Proto compiler, DO NOT EDIT\n"
          "// source: ", baseName(displayName), "\n"
          "\n"
          "#include \"", baseName(displayName), ".h\"\n"
          "\n"
          "namespace capnp {\n"
          "namespace schemas {\n",
          KJ_MAP(n, nodeTexts) { return kj::mv(n.capnpSchemaDefs); },
          "}  // namespace schemas\n"
          "}  // namespace capnp\n",
          sourceDefs.size() == 0 ? kj::strTree() : kj::strTree(
              "\n", separator, "\n",
              KJ_MAP(n, namespaceParts) { return kj::strTree("namespace ", n, " {\n"); }, "\n",
              kj::mv(sourceDefs), "\n",
              KJ_MAP(n, namespaceParts) { return kj::strTree("}  // namespace\n"); }, "\n"))
    };
  }

  // -----------------------------------------------------------------

  void makeDirectory(kj::StringPtr path) {
    KJ_IF_MAYBE(slashpos, path.findLast('/')) {
      // Make the parent dir.
      makeDirectory(kj::str(path.slice(0, *slashpos)));
    }

    if (kj::miniposix::mkdir(path.cStr(), 0777) < 0) {
      int error = errno;
      if (error != EEXIST) {
        KJ_FAIL_SYSCALL("mkdir(path)", error, path);
      }
    }
  }

  void writeFile(kj::StringPtr filename, const kj::StringTree& text) {
    if (!filename.startsWith("/")) {
      KJ_IF_MAYBE(slashpos, filename.findLast('/')) {
        // Make the parent dir.
        makeDirectory(kj::str(filename.slice(0, *slashpos)));
      }
    }

    int fd;
    KJ_SYSCALL(fd = open(filename.cStr(), O_CREAT | O_WRONLY | O_TRUNC, 0666), filename);
    kj::FdOutputStream out((kj::AutoCloseFd(fd)));

    text.visit(
        [&](kj::ArrayPtr<const char> text) {
          out.write(text.begin(), text.size());
        });
  }

  kj::MainBuilder::Validity run() {
    ReaderOptions options;
    options.traversalLimitInWords = 1 << 30;  // Don't limit.
    StreamFdMessageReader reader(STDIN_FILENO, options);
    auto request = reader.getRoot<schema::CodeGeneratorRequest>();

    auto capnpVersion = request.getCapnpVersion();

    if (capnpVersion.getMajor() != CAPNP_VERSION_MAJOR ||
        capnpVersion.getMinor() != CAPNP_VERSION_MINOR ||
        capnpVersion.getMicro() != CAPNP_VERSION_MICRO) {
      auto compilerVersion = request.hasCapnpVersion()
          ? kj::str(capnpVersion.getMajor(), '.', capnpVersion.getMinor(), '.',
                    capnpVersion.getMicro())
          : kj::str("pre-0.6");  // pre-0.6 didn't send the version.
      auto generatorVersion = kj::str(
          CAPNP_VERSION_MAJOR, '.', CAPNP_VERSION_MINOR, '.', CAPNP_VERSION_MICRO);

      KJ_LOG(WARNING,
          "You appear to be using different versions of 'capnp' (the compiler) and "
          "'capnpc-c++' (the code generator). This can happen, for example, if you built "
          "a custom version of 'capnp' but then ran it with '-oc++', which invokes "
          "'capnpc-c++' from your PATH (i.e. the installed version). To specify an alternate "
          "'capnpc-c++' executable, try something like '-o/path/to/capnpc-c++' instead.",
          compilerVersion, generatorVersion);
    }

    for (auto node: request.getNodes()) {
      schemaLoader.load(node);
    }

    kj::FdOutputStream rawOut(STDOUT_FILENO);
    kj::BufferedOutputStreamWrapper out(rawOut);

    for (auto requestedFile: request.getRequestedFiles()) {
      auto schema = schemaLoader.get(requestedFile.getId());
      auto fileText = makeFileText(schema, requestedFile);

      writeFile(kj::str(schema.getProto().getDisplayName(), ".h"), fileText.header);
      writeFile(kj::str(schema.getProto().getDisplayName(), ".c++"), fileText.source);
    }

    return true;
  }
};

}  // namespace
}  // namespace capnp

KJ_MAIN(capnp::CapnpcCppMain);
