// Copyright (c) 2013, Kenton Varda <temporal@gmail.com>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#ifndef CAPNP_COMPILER_NODE_TRANSLATOR_H_
#define CAPNP_COMPILER_NODE_TRANSLATOR_H_

#include <capnp/orphan.h>
#include <capnp/compiler/grammar.capnp.h>
#include <capnp/schema.capnp.h>
#include <capnp/schema-loader.h>
#include <capnp/dynamic.h>
#include <kj/vector.h>
#include "error-reporter.h"

namespace capnp {
namespace compiler {

class NodeTranslator {
  // Translates one node in the schema from AST form to final schema form.  A "node" is anything
  // that has a unique ID, such as structs, enums, constants, and annotations, but not fields,
  // unions, enumerants, or methods (the latter set have 16-bit ordinals but not 64-bit global IDs).

public:
  class Resolver {
    // Callback class used to find other nodes relative to this one.

  public:
    struct ResolvedName {
      uint64_t id;
      Declaration::Body::Which kind;
    };

    virtual kj::Maybe<ResolvedName> resolve(const DeclName::Reader& name) const = 0;
    // Look up the given name, relative to this node, and return basic information about the
    // target.

    virtual Schema resolveMaybeBootstrapSchema(uint64_t id) const = 0;
    // Get the schema for the given ID.  Returning either a bootstrap schema or a final schema
    // is acceptable.  Throws an exception if the id is not one that was found by calling resolve()
    // or by traversing other schemas.

    virtual Schema resolveFinalSchema(uint64_t id) const = 0;
    // Get the final schema for the given ID.  A bootstrap schema is not acceptable.  Throws an
    // exception if the id is not one that was found by calling resolve() or by traversing other
    // schemas.
  };

  NodeTranslator(const Resolver& resolver, const ErrorReporter& errorReporter,
                 const Declaration::Reader& decl, Orphan<schema::Node> wipNode);
  // Construct a NodeTranslator to translate the given declaration.  The wipNode starts out with
  // `displayName`, `id`, `scopeId`, and `nestedNodes` already initialized.  The `NodeTranslator`
  // fills in the rest.

  schema::Node::Reader getBootstrapNode() { return wipNode.getReader(); }
  // Get an incomplete version of the node in which pointer-typed value expressions have not yet
  // been translated.  Instead, for all `schema.Value` objects representing pointer-type values,
  // the value is set to an appropriate "empty" value.  This version of the schema can be used to
  // bootstrap the dynamic API which can then in turn be used to encode the missing complex values.
  //
  // If the final node has already been built, this will actually return the final node (in fact,
  // it's the same node object).

  schema::Node::Reader finish();
  // Finish translating the node (including filling in all the pieces that are missing from the
  // bootstrap node) and return it.

private:
  const Resolver& resolver;
  const ErrorReporter& errorReporter;

  Orphan<schema::Node> wipNode;
  // The work-in-progress schema node.

  struct UnfinishedValue {
    ValueExpression::Reader source;
    schema::Type::Reader type;
    schema::Value::Builder target;
  };
  kj::Vector<UnfinishedValue> unfinishedValues;
  // List of values in `wipNode` which have not yet been interpreted, because they are structs
  // or lists and as such interpreting them require using the types' schemas (to take advantage
  // of the dynamic API).  Once bootstrap schemas have been built, they can be used to interpret
  // these values.

  void compileNode(Declaration::Reader decl, schema::Node::Builder builder);

  void checkMembers(List<Declaration>::Reader nestedDecls, Declaration::Body::Which parentKind);
  // Check the given member list for errors, including detecting duplicate names and detecting
  // out-of-place declarations.

  void disallowNested(List<Declaration>::Reader nestedDecls);
  // Complain if the nested decl list is non-empty.

  void compileFile(Declaration::Reader decl, schema::FileNode::Builder builder);
  void compileConst(Declaration::Const::Reader decl, schema::ConstNode::Builder builder);
  void compileAnnotation(Declaration::Annotation::Reader decl,
                         schema::AnnotationNode::Builder builder);

  class DuplicateOrdinalDetector;
  class StructLayout;
  class StructTranslator;

  void compileEnum(Declaration::Enum::Reader decl, List<Declaration>::Reader members,
                   schema::EnumNode::Builder builder);
  void compileStruct(Declaration::Struct::Reader decl, List<Declaration>::Reader members,
                     schema::StructNode::Builder builder);
  void compileInterface(Declaration::Interface::Reader decl, List<Declaration>::Reader members,
                        schema::InterfaceNode::Builder builder);
  // The `members` arrays contain only members with ordinal numbers, in code order.  Other members
  // are handled elsewhere.

  bool compileType(TypeExpression::Reader source, schema::Type::Builder target);
  // Returns false if there was a problem, in which case value expressions of this type should
  // not be parsed.

  void compileDefaultDefaultValue(schema::Type::Reader type, schema::Value::Builder target);
  // Initializes `target` to contain the "default default" value for `type`.

  void compileBootstrapValue(ValueExpression::Reader source, schema::Type::Reader type,
                             schema::Value::Builder target);
  // Calls compileValue() if this value should be interpreted at bootstrap time.  Otheriwse,
  // adds the value to `unfinishedValues` for later evaluation.

  void compileValue(ValueExpression::Reader source, schema::Type::Reader type,
                    schema::Value::Builder target, bool isBootstrap);
  // Interprets the value expression and initializes `target` with the result.

  class DynamicSlot;

  void compileValue(ValueExpression::Reader src, DynamicSlot& dst, bool isBootstrap);
  // Fill in `dst` (which effectively points to a struct field or list element) with the given
  // value.

  void compileValueInner(ValueExpression::Reader src, DynamicSlot& dst, bool isBootstrap);
  // Helper for compileValue().

  void copyValue(schema::Value::Reader src, schema::Type::Reader srcType,
                 schema::Value::Builder dst, schema::Type::Reader dstType,
                 ValueExpression::Reader errorLocation);
  // Copy a value from one schema to another, possibly coercing the type if compatible, or
  // reporting an error otherwise.

  kj::Maybe<DynamicValue::Reader> readConstant(DeclName::Reader name, bool isBootstrap,
                                               ValueExpression::Reader errorLocation);
  // Get the value of the given constant.

  ListSchema makeListSchemaOf(schema::Type::Reader elementType);
  // Construct a list schema representing a list of elements of the given type.

  Orphan<List<schema::Annotation>> compileAnnotationApplications(
      List<Declaration::AnnotationApplication>::Reader annotations,
      kj::StringPtr targetsFlagName);
};

}  // namespace compiler
}  // namespace capnp

#endif  // CAPNP_COMPILER_NODE_TRANSLATOR_H_
