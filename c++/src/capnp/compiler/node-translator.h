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

#pragma once

#include <capnp/orphan.h>
#include <capnp/compiler/grammar.capnp.h>
#include <capnp/schema.capnp.h>
#include <capnp/dynamic.h>
#include <kj/vector.h>
#include <kj/one-of.h>
#include "error-reporter.h"
#include "resolver.h"
#include "generics.h"
#include <map>

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

class NodeTranslator {
  // Translates one node in the schema from AST form to final schema form.  A "node" is anything
  // that has a unique ID, such as structs, enums, constants, and annotations, but not fields,
  // unions, enumerants, or methods (the latter set have 16-bit ordinals but not 64-bit global IDs).
public:
  NodeTranslator(Resolver& resolver, ErrorReporter& errorReporter,
                 const Declaration::Reader& decl, Orphan<schema::Node> wipNode,
                 bool compileAnnotations);
  // Construct a NodeTranslator to translate the given declaration.  The wipNode starts out with
  // `displayName`, `id`, `scopeId`, and `nestedNodes` already initialized.  The `NodeTranslator`
  // fills in the rest.

  ~NodeTranslator() noexcept(false);

  struct NodeSet {
    schema::Node::Reader node;
    // The main node.

    kj::Array<schema::Node::Reader> auxNodes;
    // Auxiliary nodes that were produced when translating this node and should be loaded along
    // with it.  In particular, structs that contain groups (or named unions) spawn extra nodes
    // representing those, and interfaces spawn struct nodes representing method params/results.

    kj::Array<schema::Node::SourceInfo::Reader> sourceInfo;
    // The SourceInfo for the node and all aux nodes.
  };

  NodeSet getBootstrapNode();
  // Get an incomplete version of the node in which pointer-typed value expressions have not yet
  // been translated.  Instead, for all `schema.Value` objects representing pointer-type values,
  // the value is set to an appropriate "empty" value.  This version of the schema can be used to
  // bootstrap the dynamic API which can then in turn be used to encode the missing complex values.
  //
  // If the final node has already been built, this will actually return the final node (in fact,
  // it's the same node object).

  NodeSet finish(Schema selfUnboundBootstrap);
  // Finish translating the node (including filling in all the pieces that are missing from the
  // bootstrap node) and return it.
  //
  // `selfUnboundBootstrap` is a Schema build using the Node returned by getBootstrapNode(), and
  // with generic parameters "unbound", i.e. it was returned by SchemaLoader::getUnbound().

  static kj::Maybe<Resolver::ResolveResult> compileDecl(
      uint64_t scopeId, uint scopeParameterCount, Resolver& resolver, ErrorReporter& errorReporter,
      Expression::Reader expression, schema::Brand::Builder brandBuilder);
  // Compile a one-off declaration expression without building a NodeTranslator. Used for
  // evaluating aliases.
  //
  // `brandBuilder` may be used to construct a message which will fill in ResolvedDecl::brand in
  // the result.

private:
  class DuplicateNameDetector;
  class DuplicateOrdinalDetector;
  class StructLayout;
  class StructTranslator;

  Resolver& resolver;
  ErrorReporter& errorReporter;
  Orphanage orphanage;
  bool compileAnnotations;
  kj::Own<BrandScope> localBrand;

  Orphan<schema::Node> wipNode;
  // The work-in-progress schema node.

  Orphan<schema::Node::SourceInfo> sourceInfo;
  // Doc comments and other source info for this node.

  struct AuxNode {
    Orphan<schema::Node> node;
    Orphan<schema::Node::SourceInfo> sourceInfo;
  };

  kj::Vector<AuxNode> groups;
  // If this is a struct node and it contains groups, these are the nodes for those groups,  which
  // must be loaded together with the top-level node.

  kj::Vector<AuxNode> paramStructs;
  // If this is an interface, these are the auto-generated structs representing params and results.

  struct UnfinishedValue {
    Expression::Reader source;
    schema::Type::Reader type;
    kj::Maybe<Schema> typeScope;
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

  void compileEnum(Void decl, List<Declaration>::Reader members,
                   schema::Node::Builder builder);
  void compileStruct(Void decl, List<Declaration>::Reader members,
                     schema::Node::Builder builder);
  void compileInterface(Declaration::Interface::Reader decl,
                        List<Declaration>::Reader members,
                        schema::Node::Builder builder);
  // The `members` arrays contain only members with ordinal numbers, in code order.  Other members
  // are handled elsewhere.

  template <typename InitBrandFunc>
  uint64_t compileParamList(kj::StringPtr methodName, uint16_t ordinal, bool isResults,
                            Declaration::ParamList::Reader paramList,
                            typename List<Declaration::BrandParameter>::Reader implicitParams,
                            InitBrandFunc&& initBrand);
  // Compile a param (or result) list and return the type ID of the struct type.

  kj::Maybe<BrandedDecl> compileDeclExpression(
      Expression::Reader source, ImplicitParams implicitMethodParams);
  // Compile an expression which is expected to resolve to a declaration or type expression.

  bool compileType(Expression::Reader source, schema::Type::Builder target,
                   ImplicitParams implicitMethodParams);
  // Returns false if there was a problem, in which case value expressions of this type should
  // not be parsed.

  void compileDefaultDefaultValue(schema::Type::Reader type, schema::Value::Builder target);
  // Initializes `target` to contain the "default default" value for `type`.

  void compileBootstrapValue(
      Expression::Reader source, schema::Type::Reader type, schema::Value::Builder target,
      kj::Maybe<Schema> typeScope = nullptr);
  // Calls compileValue() if this value should be interpreted at bootstrap time.  Otherwise,
  // adds the value to `unfinishedValues` for later evaluation.
  //
  // If `type` comes from some other node, `typeScope` is the schema for that node. Otherwise the
  // scope of the type expression is assumed to be this node (meaning, in particular, that no
  // generic type parameters are bound).

  void compileValue(Expression::Reader source, schema::Type::Reader type,
                    Schema typeScope, schema::Value::Builder target, bool isBootstrap);
  // Interprets the value expression and initializes `target` with the result.

  kj::Maybe<DynamicValue::Reader> readConstant(Expression::Reader name, bool isBootstrap);
  // Get the value of the given constant.  May return null if some error occurs, which will already
  // have been reported.

  kj::Maybe<kj::Array<const byte>> readEmbed(LocatedText::Reader filename);
  // Read a raw file for embedding.

  Orphan<List<schema::Annotation>> compileAnnotationApplications(
      List<Declaration::AnnotationApplication>::Reader annotations,
      kj::StringPtr targetsFlagName);
};

class ValueTranslator {
public:
  class Resolver {
  public:
    virtual kj::Maybe<DynamicValue::Reader> resolveConstant(Expression::Reader name) = 0;
    virtual kj::Maybe<kj::Array<const byte>> readEmbed(LocatedText::Reader filename) = 0;
  };

  ValueTranslator(Resolver& resolver, ErrorReporter& errorReporter, Orphanage orphanage)
      : resolver(resolver), errorReporter(errorReporter), orphanage(orphanage) {}

  kj::Maybe<Orphan<DynamicValue>> compileValue(Expression::Reader src, Type type);

  void fillStructValue(DynamicStruct::Builder builder,
                       List<Expression::Param>::Reader assignments);
  // Interprets the given assignments and uses them to fill in the given struct builder.

private:
  Resolver& resolver;
  ErrorReporter& errorReporter;
  Orphanage orphanage;

  Orphan<DynamicValue> compileValueInner(Expression::Reader src, Type type);
  bool matchesType(Expression::Reader src, Type type, Orphan<DynamicValue>& result);
  // Helpers for compileValue().

  kj::String makeNodeName(Schema node);
  kj::String makeTypeName(Type type);

  kj::Maybe<ListSchema> makeListSchemaOf(schema::Type::Reader elementType);
};

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
