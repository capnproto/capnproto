// Copyright (c) 2013-2020 Sandstorm Development Group, Inc. and contributors
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

CAPNP_BEGIN_HEADER

namespace capnp {
namespace compiler {

class BrandedDecl;
class BrandScope;

struct ImplicitParams {
  // Represents a set of implicit brand parameters visible in the current context.
  //
  // As of this writing, implicit parameters occur only in the context of RPC methods. That is,
  // like this:
  //
  //     makeBox @0 [T] (value :T) -> Box(T);
  //
  // Here, `T` is an implicit parameter.

  uint64_t scopeId;
  // If zero, then any reference to an implicit param in this context should be compiled to a
  // `implicitMethodParam` AnyPointer. If non-zero, it should be compiled to a `parameter`
  // AnyPointer using this scopeId. This comes into play when compiling the implicitly-generated
  // struct types corresponding to a method's params or results; these implicitly-generated types
  // themselves have *explicit* brand parameters corresponding to the *implicit* brand parameters
  // of the method.
  //
  // TODO(cleanup): Unclear why ImplicitParams is even used when compiling the implicit structs
  //   with explicit params. Missing abstraction?

  List<Declaration::BrandParameter>::Reader params;
  // Name and metadata about the parameter declaration.

  static inline ImplicitParams none() {
    // Convenience helper to create an empty `ImplicitParams`.
    return { 0, List<Declaration::BrandParameter>::Reader() };
  }
};

class BrandedDecl {
  // Represents a declaration possibly with generic parameter bindings.

public:
  inline BrandedDecl(Resolver::ResolvedDecl decl,
                     kj::Own<BrandScope>&& brand,
                     Expression::Reader source)
      : brand(kj::mv(brand)), source(source) {
    // `source`, is the expression which specified this branded decl. It is provided so that errors
    // can be reported against it. It is acceptable to pass a default-initialized reader if there's
    // no source expression; errors will then be reported at 0, 0.

    body.init<Resolver::ResolvedDecl>(kj::mv(decl));
  }
  inline BrandedDecl(Resolver::ResolvedParameter variable, Expression::Reader source)
      : source(source) {
    body.init<Resolver::ResolvedParameter>(kj::mv(variable));
  }
  inline BrandedDecl(decltype(nullptr)) {}
  inline BrandedDecl() {}  // exists only for ExternalMutexGuarded<BrandedDecl> to work...

  static BrandedDecl implicitMethodParam(uint index) {
    // Get a BrandedDecl referring to an implicit method parameter.
    // (As a hack, we internally represent this as a ResolvedParameter. Sorry.)
    return BrandedDecl(Resolver::ResolvedParameter { 0, index }, Expression::Reader());
  }

  BrandedDecl(BrandedDecl& other);
  BrandedDecl(BrandedDecl&& other) = default;

  BrandedDecl& operator=(BrandedDecl& other);
  BrandedDecl& operator=(BrandedDecl&& other) = default;

  kj::Maybe<BrandedDecl> applyParams(kj::Array<BrandedDecl> params, Expression::Reader subSource);
  // Treat the declaration as a generic and apply it to the given parameter list.

  kj::Maybe<BrandedDecl> getMember(kj::StringPtr memberName, Expression::Reader subSource);
  // Get a member of this declaration.

  kj::Maybe<Declaration::Which> getKind();
  // Returns the kind of declaration, or null if this is an unbound generic variable.

  template <typename InitBrandFunc>
  uint64_t getIdAndFillBrand(InitBrandFunc&& initBrand);
  // Returns the type ID of this node. `initBrand` is a zero-arg functor which returns
  // schema::Brand::Builder; this will be called if this decl has brand bindings, and
  // the returned builder filled in to reflect those bindings.
  //
  // It is an error to call this when `getKind()` returns null.

  kj::Maybe<BrandedDecl&> getListParam();
  // Only if the kind is BUILTIN_LIST: Get the list's type parameter.

  Resolver::ResolvedParameter asVariable();
  // If this is an unbound generic variable (i.e. `getKind()` returns null), return information
  // about the variable.
  //
  // It is an error to call this when `getKind()` does not return null.

  bool compileAsType(ErrorReporter& errorReporter, schema::Type::Builder target);
  // Compile this decl to a schema::Type.

  inline void addError(ErrorReporter& errorReporter, kj::StringPtr message) {
    errorReporter.addErrorOn(source, message);
  }

  Resolver::ResolveResult asResolveResult(uint64_t scopeId, schema::Brand::Builder brandBuilder);
  // Reverse this into a ResolveResult. If necessary, use `brandBuilder` to fill in
  // ResolvedDecl.brand.

  kj::String toString();
  kj::String toDebugString();

private:
  Resolver::ResolveResult body;
  kj::Own<BrandScope> brand;  // null if parameter
  Expression::Reader source;
};

class BrandScope: public kj::Refcounted {
  // Tracks the brand parameter bindings affecting the scope specified by some expression. For
  // example, if we are interpreting the type expression "Foo(Text).Bar", we would start with the
  // current scope's BrandScope, create a new child BrandScope representing "Foo", add the "(Text)"
  // parameter bindings to it, then create a further child scope for "Bar". Thus the BrandScope for
  // Bar knows that Foo's parameter list has been bound to "(Text)".

public:
  BrandScope(ErrorReporter& errorReporter, uint64_t startingScopeId,
             uint startingScopeParamCount, Resolver& startingScope);
  // TODO(bug): Passing an `errorReporter` to the constructor of `BrandScope` turns out not to
  //   make a ton of sense, as an `errorReporter` is meant to report errors in a specific module,
  //   but `BrandScope` might be constructed while compiling one module but then used when
  //   compiling a different module, or not compiling a module at all. Note, though, that it DOES
  //   make sense for BrandedDecl to have an ErrorReporter, specifically associated with its
  //   `source` expression.

  bool isGeneric();
  // Returns true if this scope or any parent scope is a generic (has brand parameters).

  kj::Own<BrandScope> push(uint64_t typeId, uint paramCount);
  // Creates a new child scope with the given type ID and number of brand parameters.

  kj::Maybe<kj::Own<BrandScope>> setParams(
      kj::Array<BrandedDecl> params, Declaration::Which genericType, Expression::Reader source);
  // Create a new BrandScope representing the same scope, but with parameters filled in.
  //
  // This should only be called on the generic version of the scope. If called on a branded
  // version, an error will be reported.
  //
  // Returns null if an error occurred that prevented creating the BrandScope; the error will have
  // been reported to the ErrorReporter.

  kj::Own<BrandScope> pop(uint64_t newLeafId);
  // Return the parent scope.

  kj::Maybe<BrandedDecl> lookupParameter(Resolver& resolver, uint64_t scopeId, uint index);
  // Search up the scope chain for the scope matching `scopeId`, and return its `index`th parameter
  // binding. Returns null if the parameter is from a scope that we are currently compiling, and
  // hasn't otherwise been bound to any argument (see Brand.Scope.inherit in schema.capnp).
  //
  // In the case that a parameter wasn't specified, but isn't part of the current scope, this
  // returns the declaration for `AnyPointer`.
  //
  // TODO(cleanup): Should be called lookupArgument()?

  kj::Maybe<kj::ArrayPtr<BrandedDecl>> getParams(uint64_t scopeId);
  // Get the whole list of parameter bindings at the given scope. Returns null if the scope is
  // currently be compiled and the parameters are unbound.
  //
  // Note that it's possible that not all declared parameters were actually specified for a given
  // scope. For example, if you declare a generic `Foo(T, U)`, and then you intiantiate it
  // somewhere as `Foo(Text)`, then `U` is unspecified -- this is not an error, because Cap'n
  // Proto allows new type parameters to be added over time. `U` should be treated as `AnyPointer`
  // in this case, but `getParams()` doesn't know how many parameters are expected, so it will
  // return an array that only contains one item. Use `lookupParameter()` if you want unspecified
  // parameters to be filled in with `AnyPointer` automatically.
  //
  // TODO(cleanup): Should be called getArguments()?

  template <typename InitBrandFunc>
  void compile(InitBrandFunc&& initBrand);
  // Constructs the schema::Brand corresponding to this brand scope.
  //
  // `initBrand` is a zero-arg functor which returns an empty schema::Brand::Builder, into which
  // the brand is constructed. If no generics are present, then `initBrand` is never called.
  //
  // TODO(cleanup): Should this return Maybe<Orphan<schema::Brand>> instead?

  kj::Maybe<BrandedDecl> compileDeclExpression(
      Expression::Reader source, Resolver& resolver,
      ImplicitParams implicitMethodParams);
  // Interpret a type expression within this branded scope.

  BrandedDecl interpretResolve(
      Resolver& resolver, Resolver::ResolveResult& result, Expression::Reader source);
  // After using a Resolver to resolve a symbol, call interpretResolve() to interpret the result
  // within the current brand scope. For example, if a name resolved to a brand parameter, this
  // replaces it with the appropriate argument from the scope.

  inline uint64_t getScopeId() { return leafId; }

private:
  ErrorReporter& errorReporter;
  kj::Maybe<kj::Own<BrandScope>> parent;
  uint64_t leafId;                     // zero = this is the root
  uint leafParamCount;                 // number of generic parameters on this leaf
  bool inherited;
  kj::Array<BrandedDecl> params;

  BrandScope(kj::Own<BrandScope> parent, uint64_t leafId, uint leafParamCount)
      : errorReporter(parent->errorReporter),
        parent(kj::mv(parent)), leafId(leafId), leafParamCount(leafParamCount),
        inherited(false) {}
  BrandScope(BrandScope& base, kj::Array<BrandedDecl> params)
      : errorReporter(base.errorReporter),
        leafId(base.leafId), leafParamCount(base.leafParamCount),
        inherited(false), params(kj::mv(params)) {
    KJ_IF_MAYBE(p, base.parent) {
      parent = kj::addRef(**p);
    }
  }
  BrandScope(ErrorReporter& errorReporter, uint64_t scopeId)
      : errorReporter(errorReporter), leafId(scopeId), leafParamCount(0), inherited(false) {}

  kj::Own<BrandScope> evaluateBrand(
      Resolver& resolver, Resolver::ResolvedDecl decl,
      List<schema::Brand::Scope>::Reader brand, uint index = 0);

  BrandedDecl decompileType(Resolver& resolver, schema::Type::Reader type);

  template <typename T, typename... Params>
  friend kj::Own<T> kj::refcounted(Params&&... params);
  friend class BrandedDecl;
};

template <typename InitBrandFunc>
uint64_t BrandedDecl::getIdAndFillBrand(InitBrandFunc&& initBrand) {
  KJ_REQUIRE(body.is<Resolver::ResolvedDecl>());

  brand->compile(kj::fwd<InitBrandFunc>(initBrand));
  return body.get<Resolver::ResolvedDecl>().id;
}

template <typename InitBrandFunc>
void BrandScope::compile(InitBrandFunc&& initBrand) {
  kj::Vector<BrandScope*> levels;
  BrandScope* ptr = this;
  for (;;) {
    if (ptr->params.size() > 0 || (ptr->inherited && ptr->leafParamCount > 0)) {
      levels.add(ptr);
    }
    KJ_IF_MAYBE(p, ptr->parent) {
      ptr = *p;
    } else {
      break;
    }
  }

  if (levels.size() > 0) {
    auto scopes = initBrand().initScopes(levels.size());
    for (uint i: kj::indices(levels)) {
      auto scope = scopes[i];
      scope.setScopeId(levels[i]->leafId);

      if (levels[i]->inherited) {
        scope.setInherit();
      } else {
        auto bindings = scope.initBind(levels[i]->params.size());
        for (uint j: kj::indices(bindings)) {
          levels[i]->params[j].compileAsType(errorReporter, bindings[j].initType());
        }
      }
    }
  }
}

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
