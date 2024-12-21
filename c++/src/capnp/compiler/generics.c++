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

#include "generics.h"
#include "parser.h"  // for expressionString()

namespace capnp {
namespace compiler {

BrandedDecl::BrandedDecl(BrandedDecl& other)
    : body(other.body),
      source(other.source) {
  if (body.is<Resolver::ResolvedDecl>()) {
    brand = kj::addRef(*other.brand);
  }
}

BrandedDecl& BrandedDecl::operator=(BrandedDecl& other) {
  body = other.body;
  source = other.source;
  if (body.is<Resolver::ResolvedDecl>()) {
    brand = kj::addRef(*other.brand);
  }
  return *this;
}

kj::Maybe<BrandedDecl> BrandedDecl::applyParams(
    kj::Array<BrandedDecl> params, Expression::Reader subSource) {
  if (body.is<Resolver::ResolvedParameter>()) {
    return nullptr;
  } else {
    return brand->setParams(kj::mv(params), body.get<Resolver::ResolvedDecl>().kind, subSource)
        .map([&](kj::Own<BrandScope>&& scope) {
      BrandedDecl result = *this;
      result.brand = kj::mv(scope);
      result.source = subSource;
      return result;
    });
  }
}

kj::Maybe<BrandedDecl> BrandedDecl::getMember(
    kj::StringPtr memberName, Expression::Reader subSource) {
  if (body.is<Resolver::ResolvedParameter>()) {
    return nullptr;
  } else KJ_IF_MAYBE(r, body.get<Resolver::ResolvedDecl>().resolver->resolveMember(memberName)) {
    return brand->interpretResolve(*body.get<Resolver::ResolvedDecl>().resolver, *r, subSource);
  } else {
    return nullptr;
  }
}

kj::Maybe<Declaration::Which> BrandedDecl::getKind() {
  if (body.is<Resolver::ResolvedParameter>()) {
    return nullptr;
  } else {
    return body.get<Resolver::ResolvedDecl>().kind;
  }
}

kj::Maybe<BrandedDecl&> BrandedDecl::getListParam() {
  KJ_REQUIRE(body.is<Resolver::ResolvedDecl>());

  auto& decl = body.get<Resolver::ResolvedDecl>();
  KJ_REQUIRE(decl.kind == Declaration::BUILTIN_LIST);

  auto params = KJ_ASSERT_NONNULL(brand->getParams(decl.id));
  if (params.size() != 1) {
    return nullptr;
  } else {
    return params[0];
  }
}

Resolver::ResolvedParameter BrandedDecl::asVariable() {
  KJ_REQUIRE(body.is<Resolver::ResolvedParameter>());

  return body.get<Resolver::ResolvedParameter>();
}

bool BrandedDecl::compileAsType(
    ErrorReporter& errorReporter, schema::Type::Builder target) {
  KJ_IF_MAYBE(kind, getKind()) {
    switch (*kind) {
      case Declaration::ENUM: {
        auto enum_ = target.initEnum();
        enum_.setTypeId(getIdAndFillBrand([&]() { return enum_.initBrand(); }));
        return true;
      }

      case Declaration::STRUCT: {
        auto struct_ = target.initStruct();
        struct_.setTypeId(getIdAndFillBrand([&]() { return struct_.initBrand(); }));
        return true;
      }

      case Declaration::INTERFACE: {
        auto interface = target.initInterface();
        interface.setTypeId(getIdAndFillBrand([&]() { return interface.initBrand(); }));
        return true;
      }

      case Declaration::BUILTIN_LIST: {
        auto elementType = target.initList().initElementType();

        KJ_IF_MAYBE(param, getListParam()) {
          if (!param->compileAsType(errorReporter, elementType)) {
            return false;
          }
        } else {
          addError(errorReporter, "'List' requires exactly one parameter.");
          return false;
        }

        if (elementType.isAnyPointer()) {
          auto unconstrained = elementType.getAnyPointer().getUnconstrained();

          if (unconstrained.isAnyKind()) {
            addError(errorReporter, "'List(AnyPointer)' is not supported.");
            // Seeing List(AnyPointer) later can mess things up, so change the type to Void.
            elementType.setVoid();
            return false;
          } else if (unconstrained.isStruct()) {
            addError(errorReporter, "'List(AnyStruct)' is not supported.");
            // Seeing List(AnyStruct) later can mess things up, so change the type to Void.
            elementType.setVoid();
            return false;
          }
        }

        return true;
      }

      case Declaration::BUILTIN_VOID: target.setVoid(); return true;
      case Declaration::BUILTIN_BOOL: target.setBool(); return true;
      case Declaration::BUILTIN_INT8: target.setInt8(); return true;
      case Declaration::BUILTIN_INT16: target.setInt16(); return true;
      case Declaration::BUILTIN_INT32: target.setInt32(); return true;
      case Declaration::BUILTIN_INT64: target.setInt64(); return true;
      case Declaration::BUILTIN_U_INT8: target.setUint8(); return true;
      case Declaration::BUILTIN_U_INT16: target.setUint16(); return true;
      case Declaration::BUILTIN_U_INT32: target.setUint32(); return true;
      case Declaration::BUILTIN_U_INT64: target.setUint64(); return true;
      case Declaration::BUILTIN_FLOAT32: target.setFloat32(); return true;
      case Declaration::BUILTIN_FLOAT64: target.setFloat64(); return true;
      case Declaration::BUILTIN_TEXT: target.setText(); return true;
      case Declaration::BUILTIN_DATA: target.setData(); return true;

      case Declaration::BUILTIN_OBJECT:
        addError(errorReporter,
            "As of Cap'n Proto 0.4, 'Object' has been renamed to 'AnyPointer'.  Sorry for the "
            "inconvenience, and thanks for being an early adopter.  :)");
        KJ_FALLTHROUGH;
      case Declaration::BUILTIN_ANY_POINTER:
        target.initAnyPointer().initUnconstrained().setAnyKind();
        return true;
      case Declaration::BUILTIN_ANY_STRUCT:
        target.initAnyPointer().initUnconstrained().setStruct();
        return true;
      case Declaration::BUILTIN_ANY_LIST:
        target.initAnyPointer().initUnconstrained().setList();
        return true;
      case Declaration::BUILTIN_CAPABILITY:
        target.initAnyPointer().initUnconstrained().setCapability();
        return true;

      case Declaration::FILE:
      case Declaration::USING:
      case Declaration::CONST:
      case Declaration::ENUMERANT:
      case Declaration::FIELD:
      case Declaration::UNION:
      case Declaration::GROUP:
      case Declaration::METHOD:
      case Declaration::ANNOTATION:
      case Declaration::NAKED_ID:
      case Declaration::NAKED_ANNOTATION:
        addError(errorReporter, kj::str("'", toString(), "' is not a type."));
        return false;
    }

    KJ_UNREACHABLE;
  } else {
    // Oh, this is a type variable.
    auto var = asVariable();
    if (var.id == 0) {
      // This is actually a method implicit parameter.
      auto builder = target.initAnyPointer().initImplicitMethodParameter();
      builder.setParameterIndex(var.index);
      return true;
    } else {
      auto builder = target.initAnyPointer().initParameter();
      builder.setScopeId(var.id);
      builder.setParameterIndex(var.index);
      return true;
    }
  }
}

Resolver::ResolveResult BrandedDecl::asResolveResult(
    uint64_t scopeId, schema::Brand::Builder brandBuilder) {
  auto result = body;
  if (result.is<Resolver::ResolvedDecl>()) {
    // May need to compile our context as the "brand".

    result.get<Resolver::ResolvedDecl>().scopeId = scopeId;

    getIdAndFillBrand([&]() {
      result.get<Resolver::ResolvedDecl>().brand = brandBuilder.asReader();
      return brandBuilder;
    });
  }
  return result;
}

kj::String BrandedDecl::toString() {
  return expressionString(source);
}

kj::String BrandedDecl::toDebugString() {
  if (body.is<Resolver::ResolvedParameter>()) {
    auto variable = body.get<Resolver::ResolvedParameter>();
    return kj::str("variable(", variable.id, ", ", variable.index, ")");
  } else {
    auto decl = body.get<Resolver::ResolvedDecl>();
    return kj::str("decl(", decl.id, ", ", (uint)decl.kind, "')");
  }
}

BrandScope::BrandScope(ErrorReporter& errorReporter, uint64_t startingScopeId,
                       uint startingScopeParamCount, Resolver& startingScope)
    : errorReporter(errorReporter), parent(nullptr), leafId(startingScopeId),
      leafParamCount(startingScopeParamCount), inherited(true) {
  // Create all lexical parent scopes, all with no brand bindings.
  KJ_IF_MAYBE(p, startingScope.getParent()) {
    parent = kj::refcounted<BrandScope>(
        errorReporter, p->id, p->genericParamCount, *p->resolver);
  }
}

bool BrandScope::isGeneric() {
  if (leafParamCount > 0) return true;

  KJ_IF_MAYBE(p, parent) {
    return p->get()->isGeneric();
  } else {
    return false;
  }
}

kj::Own<BrandScope> BrandScope::push(uint64_t typeId, uint paramCount) {
  return kj::refcounted<BrandScope>(kj::addRef(*this), typeId, paramCount);
}

kj::Maybe<kj::Own<BrandScope>> BrandScope::setParams(
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

kj::Own<BrandScope> BrandScope::pop(uint64_t newLeafId) {
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

kj::Maybe<BrandedDecl> BrandScope::lookupParameter(
    Resolver& resolver, uint64_t scopeId, uint index) {
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

kj::Maybe<kj::ArrayPtr<BrandedDecl>> BrandScope::getParams(uint64_t scopeId) {
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

BrandedDecl BrandScope::interpretResolve(
    Resolver& resolver, Resolver::ResolveResult& result, Expression::Reader source) {
  if (result.is<Resolver::ResolvedDecl>()) {
    auto& decl = result.get<Resolver::ResolvedDecl>();

    auto scope = pop(decl.scopeId);
    KJ_IF_MAYBE(brand, decl.brand) {
      scope = scope->evaluateBrand(resolver, decl, brand->getScopes());
    } else {
      scope = scope->push(decl.id, decl.genericParamCount);
    }

    return BrandedDecl(decl, kj::mv(scope), source);
  } else {
    auto& param = result.get<Resolver::ResolvedParameter>();
    KJ_IF_MAYBE(p, lookupParameter(resolver, param.id, param.index)) {
      return *p;
    } else {
      return BrandedDecl(param, source);
    }
  }
}

kj::Own<BrandScope> BrandScope::evaluateBrand(
    Resolver& resolver, Resolver::ResolvedDecl decl,
    List<schema::Brand::Scope>::Reader brand, uint index) {
  auto result = kj::refcounted<BrandScope>(errorReporter, decl.id);
  result->leafParamCount = decl.genericParamCount;

  // Fill in `params`.
  if (index < brand.size()) {
    auto nextScope = brand[index];
    if (decl.id == nextScope.getScopeId()) {
      // Initialize our parameters.

      switch (nextScope.which()) {
        case schema::Brand::Scope::BIND: {
          auto bindings = nextScope.getBind();
          auto params = kj::heapArrayBuilder<BrandedDecl>(bindings.size());
          for (auto binding: bindings) {
            switch (binding.which()) {
              case schema::Brand::Binding::UNBOUND: {
                // Build an AnyPointer-equivalent.
                auto anyPointerDecl = resolver.resolveBuiltin(Declaration::BUILTIN_ANY_POINTER);
                params.add(BrandedDecl(anyPointerDecl,
                    kj::refcounted<BrandScope>(errorReporter, anyPointerDecl.scopeId),
                    Expression::Reader()));
                break;
              }

              case schema::Brand::Binding::TYPE:
                // Reverse this schema::Type back into a BrandedDecl.
                params.add(decompileType(resolver, binding.getType()));
                break;
            }
          }
          result->params = params.finish();
          break;
        }

        case schema::Brand::Scope::INHERIT:
          KJ_IF_MAYBE(p, getParams(decl.id)) {
            result->params = kj::heapArray(*p);
          } else {
            result->inherited = true;
          }
          break;
      }

      // Parent should start one level deeper in the list.
      ++index;
    }
  }

  // Fill in `parent`.
  KJ_IF_MAYBE(parent, decl.resolver->getParent()) {
    result->parent = evaluateBrand(resolver, *parent, brand, index);
  }

  return result;
}

BrandedDecl BrandScope::decompileType(
    Resolver& resolver, schema::Type::Reader type) {
  auto builtin = [&](Declaration::Which which) -> BrandedDecl {
    auto decl = resolver.resolveBuiltin(which);
    return BrandedDecl(decl,
        evaluateBrand(resolver, decl, List<schema::Brand::Scope>::Reader()),
        Expression::Reader());
  };

  switch (type.which()) {
    case schema::Type::VOID:    return builtin(Declaration::BUILTIN_VOID);
    case schema::Type::BOOL:    return builtin(Declaration::BUILTIN_BOOL);
    case schema::Type::INT8:    return builtin(Declaration::BUILTIN_INT8);
    case schema::Type::INT16:   return builtin(Declaration::BUILTIN_INT16);
    case schema::Type::INT32:   return builtin(Declaration::BUILTIN_INT32);
    case schema::Type::INT64:   return builtin(Declaration::BUILTIN_INT64);
    case schema::Type::UINT8:   return builtin(Declaration::BUILTIN_U_INT8);
    case schema::Type::UINT16:  return builtin(Declaration::BUILTIN_U_INT16);
    case schema::Type::UINT32:  return builtin(Declaration::BUILTIN_U_INT32);
    case schema::Type::UINT64:  return builtin(Declaration::BUILTIN_U_INT64);
    case schema::Type::FLOAT32: return builtin(Declaration::BUILTIN_FLOAT32);
    case schema::Type::FLOAT64: return builtin(Declaration::BUILTIN_FLOAT64);
    case schema::Type::TEXT:    return builtin(Declaration::BUILTIN_TEXT);
    case schema::Type::DATA:    return builtin(Declaration::BUILTIN_DATA);

    case schema::Type::ENUM: {
      auto enumType = type.getEnum();
      Resolver::ResolvedDecl decl = resolver.resolveId(enumType.getTypeId());
      return BrandedDecl(decl,
          evaluateBrand(resolver, decl, enumType.getBrand().getScopes()),
          Expression::Reader());
    }

    case schema::Type::INTERFACE: {
      auto interfaceType = type.getInterface();
      Resolver::ResolvedDecl decl = resolver.resolveId(interfaceType.getTypeId());
      return BrandedDecl(decl,
          evaluateBrand(resolver, decl, interfaceType.getBrand().getScopes()),
          Expression::Reader());
    }

    case schema::Type::STRUCT: {
      auto structType = type.getStruct();
      Resolver::ResolvedDecl decl = resolver.resolveId(structType.getTypeId());
      return BrandedDecl(decl,
          evaluateBrand(resolver, decl, structType.getBrand().getScopes()),
          Expression::Reader());
    }

    case schema::Type::LIST: {
      auto elementType = decompileType(resolver, type.getList().getElementType());
      return KJ_ASSERT_NONNULL(builtin(Declaration::BUILTIN_LIST)
          .applyParams(kj::heapArray(&elementType, 1), Expression::Reader()));
    }

    case schema::Type::ANY_POINTER: {
      auto anyPointer = type.getAnyPointer();
      switch (anyPointer.which()) {
        case schema::Type::AnyPointer::UNCONSTRAINED:
          return builtin(Declaration::BUILTIN_ANY_POINTER);

        case schema::Type::AnyPointer::PARAMETER: {
          auto param = anyPointer.getParameter();
          auto id = param.getScopeId();
          uint index = param.getParameterIndex();
          KJ_IF_MAYBE(binding, lookupParameter(resolver, id, index)) {
            return *binding;
          } else {
            return BrandedDecl(Resolver::ResolvedParameter {id, index}, Expression::Reader());
          }
        }

        case schema::Type::AnyPointer::IMPLICIT_METHOD_PARAMETER:
          KJ_FAIL_ASSERT("Alias pointed to implicit method type parameter?");
      }

      KJ_UNREACHABLE;
    }
  }

  KJ_UNREACHABLE;
}

kj::Maybe<BrandedDecl> BrandScope::compileDeclExpression(
    Expression::Reader source, Resolver& resolver,
    ImplicitParams implicitMethodParams) {
  switch (source.which()) {
    case Expression::UNKNOWN:
      // Error reported earlier.
      return nullptr;

    case Expression::POSITIVE_INT:
    case Expression::NEGATIVE_INT:
    case Expression::FLOAT:
    case Expression::STRING:
    case Expression::BINARY:
    case Expression::LIST:
    case Expression::TUPLE:
    case Expression::EMBED:
      errorReporter.addErrorOn(source, "Expected name.");
      return nullptr;

    case Expression::RELATIVE_NAME: {
      auto name = source.getRelativeName();
      auto nameValue = name.getValue();

      // Check implicit method params first.
      for (auto i: kj::indices(implicitMethodParams.params)) {
        if (implicitMethodParams.params[i].getName() == nameValue) {
          if (implicitMethodParams.scopeId == 0) {
            return BrandedDecl::implicitMethodParam(i);
          } else {
            return BrandedDecl(Resolver::ResolvedParameter {
                implicitMethodParams.scopeId, static_cast<uint16_t>(i) },
                Expression::Reader());
          }
        }
      }

      KJ_IF_MAYBE(r, resolver.resolve(nameValue)) {
        auto result = interpretResolve(resolver, *r, source);
        if (r->is<Resolver::ResolvedDecl>()){
          errorReporter.reportResolution(Resolution {
            /* .startByte = */ source.getStartByte(),
            /* .endByte = */ source.getEndByte(),
            /* .target = */ Resolution::Type { r->get<Resolver::ResolvedDecl>().id },
          });
        }
        return kj::mv(result);
      } else {
        errorReporter.addErrorOn(name, kj::str("Not defined: ", nameValue));
        return nullptr;
      }
    }

    case Expression::ABSOLUTE_NAME: {
      auto name = source.getAbsoluteName();
      KJ_IF_MAYBE(r, resolver.getTopScope().resolver->resolveMember(name.getValue())) {
        auto result = interpretResolve(resolver, *r, source);
        if (r->is<Resolver::ResolvedDecl>()){
          errorReporter.reportResolution(Resolution {
            /* .startByte = */ source.getStartByte(),
            /* .endByte = */ source.getEndByte(),
            /* .target = */ Resolution::Type { r->get<Resolver::ResolvedDecl>().id },
          });
        }
        return kj::mv(result);
      } else {
        errorReporter.addErrorOn(name, kj::str("Not defined: ", name.getValue()));
        return nullptr;
      }
    }

    case Expression::IMPORT: {
      auto filename = source.getImport();
      KJ_IF_MAYBE(decl, resolver.resolveImport(filename.getValue())) {
        errorReporter.reportResolution(Resolution {
          /* .startByte = */ source.getStartByte(),
          /* .endByte = */ source.getEndByte(),
          /* .target = */ Resolution::Type { decl->id },
        });
        // Import is always a root scope, so create a fresh BrandScope.
        return BrandedDecl(*decl, kj::refcounted<BrandScope>(
            errorReporter, decl->id, decl->genericParamCount, *decl->resolver), source);
      } else {
        errorReporter.addErrorOn(filename, kj::str("Import failed: ", filename.getValue()));
        return nullptr;
      }
    }

    case Expression::APPLICATION: {
      auto app = source.getApplication();
      KJ_IF_MAYBE(decl, compileDeclExpression(app.getFunction(), resolver, implicitMethodParams)) {
        // Compile all params.
        auto params = app.getParams();
        auto compiledParams = kj::heapArrayBuilder<BrandedDecl>(params.size());
        bool paramFailed = false;
        for (auto param: params) {
          if (param.isNamed()) {
            errorReporter.addErrorOn(param.getNamed(), "Named parameter not allowed here.");
          }

          KJ_IF_MAYBE(d, compileDeclExpression(param.getValue(), resolver, implicitMethodParams)) {
            compiledParams.add(kj::mv(*d));
          } else {
            // Param failed to compile. Error was already reported.
            paramFailed = true;
          }
        };

        if (paramFailed) {
          return kj::mv(*decl);
        }

        // Add the parameters to the brand.
        KJ_IF_MAYBE(applied, decl->applyParams(compiledParams.finish(), source)) {
          return kj::mv(*applied);
        } else {
          // Error already reported. Ignore parameters.
          return kj::mv(*decl);
        }
      } else {
        // error already reported
        return nullptr;
      }
    }

    case Expression::MEMBER: {
      auto member = source.getMember();
      KJ_IF_MAYBE(decl, compileDeclExpression(member.getParent(), resolver, implicitMethodParams)) {
        auto name = member.getName();
        KJ_IF_MAYBE(memberDecl, decl->getMember(name.getValue(), source)) {
          KJ_IF_MAYBE(id, memberDecl->getGenericTypeId()) {
            errorReporter.reportResolution(Resolution {
              /* .startByte = */ source.getStartByte(),
              /* .endByte = */ source.getEndByte(),
              /* .target = */ Resolution::Type { *id },
            });
          }
          return kj::mv(*memberDecl);
        } else {
          errorReporter.addErrorOn(name, kj::str(
              "'", expressionString(member.getParent()),
              "' has no member named '", name.getValue(), "'"));
          return nullptr;
        }
      } else {
        // error already reported
        return nullptr;
      }
    }
  }

  KJ_UNREACHABLE;
}

}  // namespace compiler
}  // namespace capnp
