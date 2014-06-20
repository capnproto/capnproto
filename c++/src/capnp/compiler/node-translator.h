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

#ifndef CAPNP_COMPILER_NODE_TRANSLATOR_H_
#define CAPNP_COMPILER_NODE_TRANSLATOR_H_

#include <capnp/orphan.h>
#include <capnp/compiler/grammar.capnp.h>
#include <capnp/schema.capnp.h>
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
      Declaration::Which kind;
    };

    virtual kj::Maybe<ResolvedName> resolve(const DeclName::Reader& name) = 0;
    // Look up the given name, relative to this node, and return basic information about the
    // target.

    virtual kj::Maybe<Schema> resolveBootstrapSchema(uint64_t id) = 0;
    // Get the schema for the given ID.  If a schema is returned, it must be safe to traverse its
    // dependencies using Schema::getDependency().  A schema that is only at the bootstrap stage
    // is acceptable.
    //
    // Throws an exception if the id is not one that was found by calling resolve() or by
    // traversing other schemas.  Returns null if the ID is recognized, but the corresponding
    // schema node failed to be built for reasons that were already reported.

    virtual kj::Maybe<schema::Node::Reader> resolveFinalSchema(uint64_t id) = 0;
    // Get the final schema for the given ID.  A bootstrap schema is not acceptable.  A raw
    // node reader is returned rather than a Schema object because using a Schema object built
    // by the final schema loader could trigger lazy initialization of dependencies which could
    // lead to a cycle and deadlock.
    //
    // Throws an exception if the id is not one that was found by calling resolve() or by
    // traversing other schemas.  Returns null if the ID is recognized, but the corresponding
    // schema node failed to be built for reasons that were already reported.

    virtual kj::Maybe<uint64_t> resolveImport(kj::StringPtr name) = 0;
    // Get the ID of an imported file given the import path.
  };

  NodeTranslator(Resolver& resolver, ErrorReporter& errorReporter,
                 const Declaration::Reader& decl, Orphan<schema::Node> wipNode,
                 bool compileAnnotations);
  // Construct a NodeTranslator to translate the given declaration.  The wipNode starts out with
  // `displayName`, `id`, `scopeId`, and `nestedNodes` already initialized.  The `NodeTranslator`
  // fills in the rest.

  struct NodeSet {
    schema::Node::Reader node;
    // The main node.

    kj::Array<schema::Node::Reader> auxNodes;
    // Auxiliary nodes that were produced when translating this node and should be loaded along
    // with it.  In particular, structs that contain groups (or named unions) spawn extra nodes
    // representing those, and interfaces spawn struct nodes representing method params/results.
  };

  NodeSet getBootstrapNode();
  // Get an incomplete version of the node in which pointer-typed value expressions have not yet
  // been translated.  Instead, for all `schema.Value` objects representing pointer-type values,
  // the value is set to an appropriate "empty" value.  This version of the schema can be used to
  // bootstrap the dynamic API which can then in turn be used to encode the missing complex values.
  //
  // If the final node has already been built, this will actually return the final node (in fact,
  // it's the same node object).

  NodeSet finish();
  // Finish translating the node (including filling in all the pieces that are missing from the
  // bootstrap node) and return it.

private:
  Resolver& resolver;
  ErrorReporter& errorReporter;
  Orphanage orphanage;
  bool compileAnnotations;

  Orphan<schema::Node> wipNode;
  // The work-in-progress schema node.

  kj::Vector<Orphan<schema::Node>> groups;
  // If this is a struct node and it contains groups, these are the nodes for those groups,  which
  // must be loaded together with the top-level node.

  kj::Vector<Orphan<schema::Node>> paramStructs;
  // If this is an interface, these are the auto-generated structs representing params and results.

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

  void compileConst(Declaration::Const::Reader decl, schema::Node::Const::Builder builder);
  void compileAnnotation(Declaration::Annotation::Reader decl,
                         schema::Node::Annotation::Builder builder);

  class DuplicateNameDetector;
  class DuplicateOrdinalDetector;
  class StructLayout;
  class StructTranslator;

  void compileEnum(Void decl, List<Declaration>::Reader members,
                   schema::Node::Builder builder);
  void compileStruct(Void decl, List<Declaration>::Reader members,
                     schema::Node::Builder builder);
  void compileInterface(Declaration::Interface::Reader decl,
                        List<Declaration>::Reader members,
                        schema::Node::Builder builder);
  // The `members` arrays contain only members with ordinal numbers, in code order.  Other members
  // are handled elsewhere.

  uint64_t compileParamList(kj::StringPtr methodName, uint16_t ordinal, bool isResults,
                            Declaration::ParamList::Reader paramList);
  // Compile a param (or result) list and return the type ID of the struct type.

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

  kj::Maybe<DynamicValue::Reader> readConstant(DeclName::Reader name, bool isBootstrap);
  // Get the value of the given constant.  May return null if some error occurs, which will already
  // have been reported.

  kj::Maybe<ListSchema> makeListSchemaOf(schema::Type::Reader elementType);
  // Construct a list schema representing a list of elements of the given type.  May return null if
  // some error occurs, which will already have been reported.

  Orphan<List<schema::Annotation>> compileAnnotationApplications(
      List<Declaration::AnnotationApplication>::Reader annotations,
      kj::StringPtr targetsFlagName);
};

class ValueTranslator {
public:
  class Resolver {
  public:
    virtual kj::Maybe<Schema> resolveType(uint64_t id) = 0;
    virtual kj::Maybe<DynamicValue::Reader> resolveConstant(DeclName::Reader name) = 0;
  };

  ValueTranslator(Resolver& resolver, ErrorReporter& errorReporter, Orphanage orphanage)
      : resolver(resolver), errorReporter(errorReporter), orphanage(orphanage) {}

  kj::Maybe<Orphan<DynamicValue>> compileValue(
      ValueExpression::Reader src, schema::Type::Reader type);

private:
  Resolver& resolver;
  ErrorReporter& errorReporter;
  Orphanage orphanage;

  Orphan<DynamicValue> compileValueInner(ValueExpression::Reader src, schema::Type::Reader type);
  // Helper for compileValue().

  void fillStructValue(DynamicStruct::Builder builder,
                       List<ValueExpression::FieldAssignment>::Reader assignments);
  // Interprets the given assignments and uses them to fill in the given struct builder.

  kj::String makeNodeName(uint64_t id);
  kj::String makeTypeName(schema::Type::Reader type);

  kj::Maybe<ListSchema> makeListSchemaOf(schema::Type::Reader elementType);
};

}  // namespace compiler
}  // namespace capnp

#endif  // CAPNP_COMPILER_NODE_TRANSLATOR_H_
