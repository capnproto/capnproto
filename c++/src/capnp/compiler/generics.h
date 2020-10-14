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
  // Represents a set of implicit parameters visible in the current context.

  uint64_t scopeId;
  // If zero, then any reference to an implciit param in this context should be compiled to a
  // `implicitMethodParam` AnyPointer. If non-zero, it should be compiled to a `parameter`
  // AnyPointer.

  List<Declaration::BrandParameter>::Reader params;

  static inline ImplicitParams none() {
    return { 0, List<Declaration::BrandParameter>::Reader() };
  }
};

class BrandedDecl {
  // Represents a declaration possibly with generic parameter bindings.
  //
  // TODO(cleaup): This is too complicated to live here. We should refactor this class and
  //   BrandScope out into their own file, independent of NodeTranslator.

public:
  inline BrandedDecl(Resolver::ResolvedDecl decl,
                     kj::Own<BrandScope>&& brand,
                     Expression::Reader source)
      : brand(kj::mv(brand)), source(source) {
    body.init<Resolver::ResolvedDecl>(kj::mv(decl));
  }
  inline BrandedDecl(Resolver::ResolvedParameter variable, Expression::Reader source)
      : source(source) {
    body.init<Resolver::ResolvedParameter>(kj::mv(variable));
  }
  inline BrandedDecl(decltype(nullptr)) {}

  static BrandedDecl implicitMethodParam(uint index) {
    // Get a BrandedDecl referring to an implicit method parameter.
    // (As a hack, we internally represent this as a ResolvedParameter. Sorry.)
    return BrandedDecl(Resolver::ResolvedParameter { 0, index }, Expression::Reader());
  }

  BrandedDecl(BrandedDecl& other);
  BrandedDecl(BrandedDecl&& other) = default;

  BrandedDecl& operator=(BrandedDecl& other);
  BrandedDecl& operator=(BrandedDecl&& other) = default;

  // TODO(cleanup): A lot of the methods below are actually only called within compileAsType(),
  //   which was originally a method on NodeTranslator, but now is a method here and thus doesn't
  //   need these to be public. We should privatize most of these.

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
  // Tracks the brand parameter bindings affecting the current scope. For example, if we are
  // interpreting the type expression "Foo(Text).Bar", we would start with the current scopes
  // BrandScope, create a new child BrandScope representing "Foo", add the "(Text)" parameter
  // bindings to it, then create a further child scope for "Bar". Thus the BrandScope for Bar
  // knows that Foo's parameter list has been bound to "(Text)".
  //
  // TODO(cleanup): This is too complicated to live here. We should refactor this class and
  //   BrandedDecl out into their own file, independent of NodeTranslator.

public:
  BrandScope(ErrorReporter& errorReporter, uint64_t startingScopeId,
             uint startingScopeParamCount, Resolver& startingScope)
      : errorReporter(errorReporter), parent(nullptr), leafId(startingScopeId),
        leafParamCount(startingScopeParamCount), inherited(true) {
    // Create all lexical parent scopes, all with no brand bindings.
    KJ_IF_MAYBE(p, startingScope.getParent()) {
      parent = kj::refcounted<BrandScope>(
          errorReporter, p->id, p->genericParamCount, *p->resolver);
    }
  }

  bool isGeneric() {
    if (leafParamCount > 0) return true;

    KJ_IF_MAYBE(p, parent) {
      return p->get()->isGeneric();
    } else {
      return false;
    }
  }

  kj::Own<BrandScope> push(uint64_t typeId, uint paramCount) {
    return kj::refcounted<BrandScope>(kj::addRef(*this), typeId, paramCount);
  }

  kj::Maybe<kj::Own<BrandScope>> setParams(
      kj::Array<BrandedDecl> params, Declaration::Which genericType, Expression::Reader source) {
    if (this->params.size() != 0) {
      errorReporter.addErrorOn(source, "Double-application of generic parameters.");
      return nullptr;
    } else if (params.size() > leafParamCount) {
      if (leafParamCount == 0) {
        errorReporter.addErrorOn(source, "Declaration does not accept generic parameters.");
      } else {
        errorReporter.addErrorOn(source, "Too many generic parameters.");
      }
      return nullptr;
    } else if (params.size() < leafParamCount) {
      errorReporter.addErrorOn(source, "Not enough generic parameters.");
      return nullptr;
    } else {
      if (genericType != Declaration::BUILTIN_LIST) {
        for (auto& param: params) {
          KJ_IF_MAYBE(kind, param.getKind()) {
            switch (*kind) {
              case Declaration::BUILTIN_LIST:
              case Declaration::BUILTIN_TEXT:
              case Declaration::BUILTIN_DATA:
              case Declaration::BUILTIN_ANY_POINTER:
              case Declaration::STRUCT:
              case Declaration::INTERFACE:
                break;

              default:
                param.addError(errorReporter,
                    "Sorry, only pointer types can be used as generic parameters.");
                break;
            }
          }
        }
      }

      return kj::refcounted<BrandScope>(*this, kj::mv(params));
    }
  }

  kj::Own<BrandScope> pop(uint64_t newLeafId) {
    if (leafId == newLeafId) {
      return kj::addRef(*this);
    }
    KJ_IF_MAYBE(p, parent) {
      return (*p)->pop(newLeafId);
    } else {
      // Looks like we're moving into a whole top-level scope.
      return kj::refcounted<BrandScope>(errorReporter, newLeafId);
    }
  }

  kj::Maybe<BrandedDecl> lookupParameter(Resolver& resolver, uint64_t scopeId, uint index) {
    // Returns null if the param should be inherited from the client scope.

    if (scopeId == leafId) {
      if (index < params.size()) {
        return params[index];
      } else if (inherited) {
        return nullptr;
      } else {
        // Unbound and not inherited, so return AnyPointer.
        auto decl = resolver.resolveBuiltin(Declaration::BUILTIN_ANY_POINTER);
        return BrandedDecl(decl,
            evaluateBrand(resolver, decl, List<schema::Brand::Scope>::Reader()),
            Expression::Reader());
      }
    } else KJ_IF_MAYBE(p, parent) {
      return p->get()->lookupParameter(resolver, scopeId, index);
    } else {
      KJ_FAIL_REQUIRE("scope is not a parent");
    }
  }

  kj::Maybe<kj::ArrayPtr<BrandedDecl>> getParams(uint64_t scopeId) {
    // Returns null if params at the requested scope should be inherited from the client scope.

    if (scopeId == leafId) {
      if (inherited) {
        return nullptr;
      } else {
        return params.asPtr();
      }
    } else KJ_IF_MAYBE(p, parent) {
      return p->get()->getParams(scopeId);
    } else {
      KJ_FAIL_REQUIRE("scope is not a parent");
    }
  }

  template <typename InitBrandFunc>
  void compile(InitBrandFunc&& initBrand) {
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

  kj::Maybe<BrandedDecl> compileDeclExpression(
      Expression::Reader source, Resolver& resolver,
      ImplicitParams implicitMethodParams);

  BrandedDecl interpretResolve(
      Resolver& resolver, Resolver::ResolveResult& result, Expression::Reader source);

  kj::Own<BrandScope> evaluateBrand(
      Resolver& resolver, Resolver::ResolvedDecl decl,
      List<schema::Brand::Scope>::Reader brand, uint index = 0);

  BrandedDecl decompileType(Resolver& resolver, schema::Type::Reader type);

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

  template <typename T, typename... Params>
  friend kj::Own<T> kj::refcounted(Params&&... params);
};

template <typename InitBrandFunc>
uint64_t BrandedDecl::getIdAndFillBrand(InitBrandFunc&& initBrand) {
  KJ_REQUIRE(body.is<Resolver::ResolvedDecl>());

  brand->compile(kj::fwd<InitBrandFunc>(initBrand));
  return body.get<Resolver::ResolvedDecl>().id;
}

}  // namespace compiler
}  // namespace capnp

CAPNP_END_HEADER
