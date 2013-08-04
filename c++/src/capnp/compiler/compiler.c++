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

#include "compiler.h"
#include <kj/mutex.h>
#include <kj/arena.h>
#include <kj/vector.h>
#include <kj/debug.h>
#include <capnp/message.h>
#include <map>
#include <unordered_map>
#include "node-translator.h"
#include "md5.h"

namespace capnp {
namespace compiler {

class Compiler::Alias {
public:
  Alias(const Node& parent, const DeclName::Reader& targetName)
      : parent(parent), targetName(targetName) {}

  kj::Maybe<const Node&> getTarget() const;

private:
  const Node& parent;
  DeclName::Reader targetName;
  kj::Lazy<kj::Maybe<const Node&>> target;
};

class Compiler::Node: public NodeTranslator::Resolver {
  // Passes through four states:
  // - Stub:  On initial construction, the Node is just a placeholder object.  Its ID has been
  //     determined, and it is placed in its parent's member table as well as the compiler's
  //     nodes-by-ID table.
  // - Expanded:  Nodes have been constructed for all of this Node's nested children.  This happens
  //     the first time a lookup is performed for one of those children.
  // - Bootstrap:  A NodeTranslator has been built and advanced to the bootstrap phase.
  // - Finished:  A final Schema object has been constructed.

public:
  explicit Node(CompiledModule& module);
  // Create a root node representing the given file.  May

  Node(const Node& parent, const Declaration::Reader& declaration);
  // Create a child node.

  Node(kj::StringPtr name, Declaration::Body::Which kind);
  // Create a dummy node representing a built-in declaration, like "Int32" or "true".

  uint64_t getId() const { return id; }
  Declaration::Body::Which getKind() const { return kind; }

  kj::Maybe<const Node&> lookupMember(kj::StringPtr name) const;
  // Find a direct member of this node with the given name.

  kj::Maybe<const Node&> lookupLexical(kj::StringPtr name) const;
  // Look up the given name first as a member of this Node, then in its parent, and so on, until
  // it is found or there are no more parents to search.

  kj::Maybe<const Node&> lookup(const DeclName::Reader& name) const;
  // Resolve an arbitrary DeclName to a Node.

  kj::Maybe<Schema> getBootstrapSchema() const;
  kj::Maybe<schema::Node::Reader> getFinalSchema() const;

  void traverse(Compiler::Mode mode) const;
  // Get the final schema for this node, and also possibly traverse the node's children and
  // dependencies to ensure that they are loaded, depending on the mode.

  void addError(kj::StringPtr error) const;
  // Report an error on this Node.

  // implements NodeTranslator::Resolver -----------------------------
  kj::Maybe<ResolvedName> resolve(const DeclName::Reader& name) const override;
  kj::Maybe<Schema> resolveBootstrapSchema(uint64_t id) const override;
  kj::Maybe<schema::Node::Reader> resolveFinalSchema(uint64_t id) const override;

private:
  const CompiledModule* module;  // null iff isBuiltin is true
  kj::Maybe<const Node&> parent;

  Declaration::Reader declaration;
  // AST of the declaration parsed from the schema file.  May become invalid once the content
  // state has reached FINISHED.

  uint64_t id;
  // The ID of this node, either taken from the AST or computed based on the parent.  Or, a dummy
  // value, if duplicates were detected.

  kj::StringPtr displayName;
  // Fully-qualified display name for this node.  For files, this is just the file name, otherwise
  // it is "filename:Path.To.Decl".

  Declaration::Body::Which kind;
  // Kind of node.

  bool isBuiltin;
  // Whether this is a bulit-in declaration, like "Int32" or "true".

  uint32_t startByte;
  uint32_t endByte;
  // Start and end byte for reporting general errors.

  struct Content {
    inline Content(): state(STUB) {}

    enum State {
      STUB,
      EXPANDED,
      BOOTSTRAP,
      FINISHED
    };
    State state;
    // Indicates which fields below are valid.  Must update with atomic-release semantics.

    inline bool stateHasReached(State minimumState) const {
      return __atomic_load_n(&state, __ATOMIC_ACQUIRE) >= minimumState;
    }
    inline void advanceState(State newState) {
      __atomic_store_n(&state, newState, __ATOMIC_RELEASE);
    }

    // EXPANDED ------------------------------------

    typedef std::multimap<kj::StringPtr, kj::Own<Node>> NestedNodesMap;
    NestedNodesMap nestedNodes;
    kj::Vector<Node*> orderedNestedNodes;
    // Filled in when lookupMember() is first called.  multimap in case of duplicate member names --
    // we still want to compile them, even if it's an error.

    typedef std::multimap<kj::StringPtr, kj::Own<Alias>> AliasMap;
    AliasMap aliases;
    //

    // BOOTSTRAP -----------------------------------

    NodeTranslator* translator;
    // Node translator, allocated in the bootstrap arena.

    kj::Maybe<Schema> bootstrapSchema;
    // The schema built in the bootstrap loader.  Null if the bootstrap loader threw an exception.

    // FINISHED ------------------------------------

    kj::Maybe<Schema> finalSchema;
    // The complete schema as loaded by the compiler's main SchemaLoader.  Null if the final
    // loader threw an exception.
  };

  kj::MutexGuarded<Content> content;

  // ---------------------------------------------

  static uint64_t generateId(uint64_t parentId, kj::StringPtr declName,
                             Declaration::Id::Reader declId);
  // Extract the ID from the declaration, or if it has none, generate one based on the name and
  // parent ID.

  static kj::StringPtr joinDisplayName(const kj::Arena& arena, const Node& parent,
                                       kj::StringPtr declName);
  // Join the parent's display name with the child's unqualified name to construct the child's
  // display name.

  const Content& getContent(Content::State minimumState) const;
  // Advances the content to at least the given state and returns it.  Does not lock if the content
  // is already at or past the given state.
};

class Compiler::CompiledModule {
public:
  CompiledModule(const Compiler::Impl& compiler, const Module& parserModule);

  const Compiler::Impl& getCompiler() const { return compiler; }

  const ErrorReporter& getErrorReporter() const { return parserModule; }
  ParsedFile::Reader getParsedFile() const { return content.getReader(); }
  const Node& getRootNode() const { return rootNode; }
  kj::StringPtr getSourceName() const { return parserModule.getSourceName(); }

  kj::Maybe<const CompiledModule&> importRelative(kj::StringPtr importPath) const;

private:
  const Compiler::Impl& compiler;
  const Module& parserModule;
  MallocMessageBuilder contentArena;
  Orphan<ParsedFile> content;
  Node rootNode;
};

class Compiler::Impl: public SchemaLoader::LazyLoadCallback {
public:
  Impl();
  virtual ~Impl() noexcept(false);

  uint64_t add(const Module& module, Mode mode) const;
  kj::Maybe<uint64_t> lookup(uint64_t parent, kj::StringPtr childName) const;
  const CompiledModule& add(const Module& parsedModule) const;

  struct Workspace {
    // Scratch space where stuff can be allocated while working.  The Workspace is available
    // whenever nodes are actively being compiled, then is destroyed once control exits the
    // compiler.  Note that since nodes are compiled lazily, a new Workspace may have to be
    // constructed in order to compile more nodes later.

    MallocMessageBuilder message;
    Orphanage orphanage;
    // Orphanage for allocating temporary Cap'n Proto objects.

    kj::Arena arena;
    // Arena for allocating temporary native objects.  Note that objects in `arena` may contain
    // pointers into `message` that will be manipulated on destruction, so `arena` must be declared
    // after `message`.

    SchemaLoader bootstrapLoader;
    // Loader used to load bootstrap schemas.  The bootstrap schema nodes are similar to the final
    // versions except that any value expressions which depend on knowledge of other types (e.g.
    // default values for struct fields) are left unevaluated (the values in the schema are empty).
    // These bootstrap schemas can then be plugged into the dynamic API and used to evaluate these
    // remaining values.

    inline explicit Workspace(const SchemaLoader::LazyLoadCallback& loaderCallback)
        : orphanage(message.getOrphanage()),
          bootstrapLoader(loaderCallback) {}
  };

  const kj::Arena& getNodeArena() const { return nodeArena; }
  // Arena where nodes and other permanent objects should be allocated.

  const SchemaLoader& getFinalLoader() const { return finalLoader; }
  // Schema loader containing final versions of schemas.

  const Workspace& getWorkspace() const { return workspace.getWithoutLock(); }
  // Temporary workspace that can be used to construct bootstrap objects.  We assume that the
  // caller already holds the workspace lock somewhere up the stack.

  void clearWorkspace();
  // Reset the temporary workspace.


  uint64_t addNode(uint64_t desiredId, Node& node) const;
  // Add the given node to the by-ID map under the given ID.  If another node with the same ID
  // already exists, choose a new one arbitrarily and use that instead.  Return the ID that was
  // finally used.

  kj::Maybe<const Node&> findNode(uint64_t id) const;

  kj::Maybe<const Node&> lookupBuiltin(kj::StringPtr name) const;

  void load(const SchemaLoader& loader, uint64_t id) const override;

private:
  kj::Arena nodeArena;
  // Arena used to allocate nodes and other permanent objects.

  SchemaLoader finalLoader;
  // The loader where we put final output of the compiler.

  kj::MutexGuarded<Workspace> workspace;
  // The temporary workspace.

  typedef std::unordered_map<const Module*, kj::Own<CompiledModule>> ModuleMap;
  kj::MutexGuarded<ModuleMap> modules;
  // Map of parser modules to compiler modules.

  typedef std::unordered_map<uint64_t, const Node*> NodeMap;
  kj::MutexGuarded<NodeMap> nodesById;
  // Map of nodes by ID.

  std::map<kj::StringPtr, kj::Own<Node>> builtinDecls;
  // Map of built-in declarations, like "Int32" and "List", which make up the global scope.

  mutable uint64_t nextBogusId = 1000;
};

// =======================================================================================

kj::Maybe<const Compiler::Node&> Compiler::Alias::getTarget() const {
  return target.get([this](kj::SpaceFor<kj::Maybe<const Node&>>& space) {
    return space.construct(parent.lookup(targetName));
  });
}

// =======================================================================================

Compiler::Node::Node(CompiledModule& module)
    : module(&module),
      parent(nullptr),
      declaration(module.getParsedFile().getRoot()),
      id(generateId(0, declaration.getName().getValue(), declaration.getId())),
      displayName(module.getSourceName()),
      kind(declaration.getBody().which()),
      isBuiltin(false) {
  auto name = declaration.getName();
  if (name.getValue().size() > 0) {
    startByte = name.getStartByte();
    endByte = name.getEndByte();
  } else {
    startByte = declaration.getStartByte();
    endByte = declaration.getEndByte();
  }

  id = module.getCompiler().addNode(id, *this);
}

Compiler::Node::Node(const Node& parent, const Declaration::Reader& declaration)
    : module(parent.module),
      parent(parent),
      declaration(declaration),
      id(generateId(parent.id, declaration.getName().getValue(), declaration.getId())),
      displayName(joinDisplayName(parent.module->getCompiler().getNodeArena(),
                                  parent, declaration.getName().getValue())),
      kind(declaration.getBody().which()),
      isBuiltin(false) {
  auto name = declaration.getName();
  if (name.getValue().size() > 0) {
    startByte = name.getStartByte();
    endByte = name.getEndByte();
  } else {
    startByte = declaration.getStartByte();
    endByte = declaration.getEndByte();
  }

  id = module->getCompiler().addNode(id, *this);
}

Compiler::Node::Node(kj::StringPtr name, Declaration::Body::Which kind)
    : module(nullptr),
      parent(nullptr),
      id(0),
      displayName(name),
      kind(kind),
      isBuiltin(true),
      startByte(0),
      endByte(0) {}

uint64_t Compiler::Node::generateId(uint64_t parentId, kj::StringPtr declName,
                                    Declaration::Id::Reader declId) {
  if (declId.which() == Declaration::Id::UID) {
    return declId.getUid().getValue();
  }

  // No explicit ID.  Compute it by MD5 hashing the concatenation of the parent ID and the
  // declaration name, and then taking the first 8 bytes.

  kj::byte parentIdBytes[sizeof(uint64_t)];
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    parentIdBytes[i] = (parentId >> (i * 8)) & 0xff;
  }

  Md5 md5;
  md5.update(kj::arrayPtr(parentIdBytes, KJ_ARRAY_SIZE(parentIdBytes)));
  md5.update(declName);

  kj::ArrayPtr<const kj::byte> resultBytes = md5.finish();

  uint64_t result = 0;
  for (uint i = 0; i < sizeof(uint64_t); i++) {
    result = (result << 8) | resultBytes[i];
  }

  return result | (1ull << 63);
}

kj::StringPtr Compiler::Node::joinDisplayName(
    const kj::Arena& arena, const Node& parent, kj::StringPtr declName) {
  kj::ArrayPtr<char> result = arena.allocateArray<char>(
      parent.displayName.size() + declName.size() + 2);

  size_t separatorPos = parent.displayName.size();
  memcpy(result.begin(), parent.displayName.begin(), separatorPos);
  result[separatorPos] = parent.parent == nullptr ? ':' : '.';
  memcpy(result.begin() + separatorPos + 1, declName.begin(), declName.size());
  result[result.size() - 1] = '\0';
  return kj::StringPtr(result.begin(), result.size() - 1);
}

const Compiler::Node::Content& Compiler::Node::getContent(Content::State minimumState) const {
  KJ_REQUIRE(!isBuiltin, "illegal method call for built-in declaration");

  if (content.getWithoutLock().stateHasReached(minimumState)) {
    return content.getWithoutLock();
  }

  auto locked = content.lockExclusive();

  switch (locked->state) {
    case Content::STUB: {
      if (minimumState <= Content::STUB) break;

      // Expand the child nodes.
      auto& arena = module->getCompiler().getNodeArena();

      for (auto nestedDecl: declaration.getNestedDecls()) {
        switch (nestedDecl.getBody().which()) {
          case Declaration::Body::FILE_DECL:
          case Declaration::Body::CONST_DECL:
          case Declaration::Body::ANNOTATION_DECL:
          case Declaration::Body::ENUM_DECL:
          case Declaration::Body::STRUCT_DECL:
          case Declaration::Body::INTERFACE_DECL: {
            kj::Own<Node> subNode = arena.allocateOwn<Node>(*this, nestedDecl);
            kj::StringPtr name = nestedDecl.getName().getValue();
            locked->orderedNestedNodes.add(subNode);
            locked->nestedNodes.insert(std::make_pair(name, kj::mv(subNode)));
            break;
          }

          case Declaration::Body::USING_DECL: {
            kj::Own<Alias> alias = arena.allocateOwn<Alias>(
                *this, nestedDecl.getBody().getUsingDecl().getTarget());
            kj::StringPtr name = nestedDecl.getName().getValue();
            locked->aliases.insert(std::make_pair(name, kj::mv(alias)));
            break;
          }
          case Declaration::Body::ENUMERANT_DECL:
          case Declaration::Body::FIELD_DECL:
          case Declaration::Body::UNION_DECL:
          case Declaration::Body::GROUP_DECL:
          case Declaration::Body::METHOD_DECL:
          case Declaration::Body::NAKED_ID:
          case Declaration::Body::NAKED_ANNOTATION:
            // Not a node.  Skip.
            break;
          default:
            KJ_FAIL_ASSERT("unknown declaration type", nestedDecl);
            break;
        }
      }

      locked->advanceState(Content::EXPANDED);
      // no break
    }

    case Content::EXPANDED: {
      if (minimumState <= Content::EXPANDED) break;

      // Construct the NodeTranslator.
      auto& workspace = module->getCompiler().getWorkspace();

      auto schemaNode = workspace.orphanage.newOrphan<schema::Node>();
      auto builder = schemaNode.get();
      builder.setId(id);
      builder.setDisplayName(displayName);
      KJ_IF_MAYBE(p, parent) {
        builder.setScopeId(p->id);
      }

      auto nestedNodes = builder.initNestedNodes(locked->orderedNestedNodes.size());
      auto nestedIter = nestedNodes.begin();
      for (auto node: locked->orderedNestedNodes) {
        nestedIter->setName(node->declaration.getName().getValue());
        nestedIter->setId(node->id);
        ++nestedIter;
      }

      locked->translator = &workspace.arena.allocate<NodeTranslator>(
          *this, module->getErrorReporter(), declaration, kj::mv(schemaNode));
      KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&](){
        locked->bootstrapSchema = workspace.bootstrapLoader.loadOnce(
            locked->translator->getBootstrapNode());
      })) {
        locked->bootstrapSchema = nullptr;
        // Only bother to report validation failures if we think we haven't seen any errors.
        // Otherwise we assume that the errors caused the validation failure.
        if (!module->getErrorReporter().hadErrors()) {
          addError(kj::str("Internal compiler bug: Bootstrap schema failed validation:\n",
                           *exception));
        }
      }

      // If the Workspace is destroyed while this Node is still in the BOOTSTRAP state,
      // revert it to the EXPANDED state, because the NodeTranslator is no longer valid in this
      // case.
      Content* contentPtr = locked.get();
      workspace.arena.copy(kj::defer([contentPtr]() {
        contentPtr->bootstrapSchema = nullptr;
        if (contentPtr->state == Content::BOOTSTRAP) {
          contentPtr->state = Content::EXPANDED;
        }
      }));

      locked->advanceState(Content::BOOTSTRAP);
      // no break
    }

    case Content::BOOTSTRAP: {
      if (minimumState <= Content::BOOTSTRAP) break;

      // Create the final schema.
      KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&](){
        locked->finalSchema = module->getCompiler().getFinalLoader().loadOnce(
            locked->translator->finish());
      })) {
        locked->finalSchema = nullptr;

        // Only bother to report validation failures if we think we haven't seen any errors.
        // Otherwise we assume that the errors caused the validation failure.
        if (!module->getErrorReporter().hadErrors()) {
          addError(kj::str("Internal compiler bug: Schema failed validation:\n",
                           *exception));
        }
      }

      locked->advanceState(Content::FINISHED);
      // no break
    }

    case Content::FINISHED:
      break;
  }

  return *locked;
}

kj::Maybe<const Compiler::Node&> Compiler::Node::lookupMember(kj::StringPtr name) const {
  if (isBuiltin) return nullptr;

  auto& content = getContent(Content::EXPANDED);
  {
    auto iter = content.nestedNodes.find(name);
    if (iter != content.nestedNodes.end()) {
      return *iter->second;
    }
  }
  {
    auto iter = content.aliases.find(name);
    if (iter != content.aliases.end()) {
      return iter->second->getTarget();
    }
  }
  return nullptr;
}

kj::Maybe<const Compiler::Node&> Compiler::Node::lookupLexical(kj::StringPtr name) const {
  KJ_REQUIRE(!isBuiltin, "illegal method call for built-in declaration");

  auto result = lookupMember(name);
  if (result == nullptr) {
    KJ_IF_MAYBE(p, parent) {
      result = p->lookupLexical(name);
    } else {
      result = module->getCompiler().lookupBuiltin(name);
    }
  }
  return result;
}

kj::Maybe<const Compiler::Node&> Compiler::Node::lookup(const DeclName::Reader& name) const {
  KJ_REQUIRE(!isBuiltin, "illegal method call for built-in declaration");

  const Node* node = nullptr;

  auto base = name.getBase();
  switch (base.which()) {
    case DeclName::Base::ABSOLUTE_NAME: {
      auto absoluteName = base.getAbsoluteName();
      KJ_IF_MAYBE(n, module->getRootNode().lookupMember(absoluteName.getValue())) {
        node = &*n;
      } else {
        module->getErrorReporter().addErrorOn(
            absoluteName, kj::str("not defined: ", absoluteName.getValue()));
        return nullptr;
      }
      break;
    }
    case DeclName::Base::RELATIVE_NAME: {
      auto relativeName = base.getRelativeName();
      KJ_IF_MAYBE(n, lookupLexical(relativeName.getValue())) {
        node = &*n;
      } else {
        module->getErrorReporter().addErrorOn(
            relativeName, kj::str("not defined: ", relativeName.getValue()));
        return nullptr;
      }
      break;
    }
    case DeclName::Base::IMPORT_NAME: {
      auto importName = base.getImportName();
      KJ_IF_MAYBE(m, module->importRelative(importName.getValue())) {
        node = &m->getRootNode();
      } else {
        module->getErrorReporter().addErrorOn(
            importName, kj::str("import failed: ", importName.getValue()));
        return nullptr;
      }
      break;
    }
  }

  KJ_ASSERT(node != nullptr);

  for (auto partName: name.getMemberPath()) {
    KJ_IF_MAYBE(member, node->lookupMember(partName.getValue())) {
      node = &*member;
    } else {
      module->getErrorReporter().addErrorOn(
          partName, kj::str("no such member: ", partName.getValue()));
      return nullptr;
    }
  }

  return *node;
}

kj::Maybe<Schema> Compiler::Node::getBootstrapSchema() const {
  auto& content = getContent(Content::BOOTSTRAP);

  if (__atomic_load_n(&content.state, __ATOMIC_ACQUIRE) == Content::FINISHED &&
      content.bootstrapSchema == nullptr) {
    // The bootstrap schema was discarded.  Copy it from the final schema.
    // (We can't just return the final schema because using it could trigger schema loader
    // callbacks that would deadlock.)
    KJ_IF_MAYBE(finalSchema, content.finalSchema) {
      return module->getCompiler().getWorkspace().bootstrapLoader.loadOnce(finalSchema->getProto());
    } else {
      return nullptr;
    }
  } else {
    return content.bootstrapSchema;
  }
}
kj::Maybe<schema::Node::Reader> Compiler::Node::getFinalSchema() const {
  return getContent(Content::FINISHED).finalSchema.map(
      [](const Schema& schema) { return schema.getProto(); });
}

void Compiler::Node::traverse(Compiler::Mode mode) const {
  getFinalSchema();

  if (mode == EAGER) {
    for (auto& child: getContent(Content::EXPANDED).nestedNodes) {
      child.second->traverse(mode);
    }
  }
}

void Compiler::Node::addError(kj::StringPtr error) const {
  module->getErrorReporter().addError(startByte, endByte, error);
}

kj::Maybe<NodeTranslator::Resolver::ResolvedName> Compiler::Node::resolve(
    const DeclName::Reader& name) const {
  return lookup(name).map([](const Node& node) {
    return ResolvedName { node.id, node.kind };
  });
}

kj::Maybe<Schema> Compiler::Node::resolveBootstrapSchema(uint64_t id) const {
  KJ_IF_MAYBE(node, module->getCompiler().findNode(id)) {
    return node->getBootstrapSchema();
  } else {
    KJ_FAIL_REQUIRE("Tried to get schema for ID we haven't seen before.");
  }
}

kj::Maybe<schema::Node::Reader> Compiler::Node::resolveFinalSchema(uint64_t id) const {
  KJ_IF_MAYBE(node, module->getCompiler().findNode(id)) {
    return node->getFinalSchema();
  } else {
    KJ_FAIL_REQUIRE("Tried to get schema for ID we haven't seen before.");
  }
}

// =======================================================================================

Compiler::CompiledModule::CompiledModule(
    const Compiler::Impl& compiler, const Module& parserModule)
    : compiler(compiler), parserModule(parserModule),
      content(parserModule.loadContent(contentArena.getOrphanage())),
      rootNode(*this) {}

kj::Maybe<const Compiler::CompiledModule&> Compiler::CompiledModule::importRelative(
    kj::StringPtr importPath) const {
  return parserModule.importRelative(importPath).map(
      [this](const Module& module) -> const Compiler::CompiledModule& {
        return compiler.add(module);
      });
}

// =======================================================================================

Compiler::Impl::Impl(): finalLoader(*this), workspace(*this) {
  // Reflectively interpret the members of Declaration.body.  Any member prefixed by "builtin"
  // defines a builtin declaration visible in the global scope.
  StructSchema::Union declBodySchema =
      Schema::from<Declaration>().getMemberByName("body").asUnion();
  for (auto member: declBodySchema.getMembers()) {
    auto name = member.getProto().getName();
    if (name.startsWith("builtin")) {
      kj::StringPtr symbolName = name.slice(strlen("builtin"));
      builtinDecls[symbolName] = nodeArena.allocateOwn<Node>(
          symbolName, static_cast<Declaration::Body::Which>(member.getIndex()));
    }
  }
}

Compiler::Impl::~Impl() {}

void Compiler::Impl::clearWorkspace() {
  auto lock = workspace.lockExclusive();

  // Make sure we reconstruct the workspace even if destroying it throws an exception.
  KJ_DEFER(kj::ctor(*lock, *this));
  kj::dtor(*lock);
}

const Compiler::CompiledModule& Compiler::Impl::add(const Module& parsedModule) const {
  auto locked = modules.lockExclusive();

  kj::Own<CompiledModule>& slot = (*locked)[&parsedModule];
  if (slot.get() == nullptr) {
    slot = kj::heap<CompiledModule>(*this, parsedModule);
  }

  return *slot;
}

uint64_t Compiler::Impl::addNode(uint64_t desiredId, Node& node) const {
  auto lock = nodesById.lockExclusive();
  for (;;) {
    auto insertResult = lock->insert(std::make_pair(desiredId, &node));
    if (insertResult.second) {
      return desiredId;
    }

    // Only report an error if this ID is not bogus.  Actual IDs specified in the original source
    // code are required to have the upper bit set.  Anything else must have been manufactured
    // at some point to cover up an error.
    if (desiredId & (1ull << 63)) {
      node.addError(kj::str("Duplicate ID @0x", kj::hex(desiredId), "."));
      insertResult.first->second->addError(
          kj::str("ID @0x", kj::hex(desiredId), " originally used here."));
    }

    // Assign a new bogus ID.
    desiredId = __atomic_fetch_add(&nextBogusId, 1, __ATOMIC_RELAXED);
  }
}

kj::Maybe<const Compiler::Node&> Compiler::Impl::findNode(uint64_t id) const {
  auto lock = nodesById.lockShared();
  auto iter = lock->find(id);
  if (iter == lock->end()) {
    return nullptr;
  } else {
    return *iter->second;
  }
}

kj::Maybe<const Compiler::Node&> Compiler::Impl::lookupBuiltin(kj::StringPtr name) const {
  auto iter = builtinDecls.find(name);
  if (iter == builtinDecls.end()) {
    return nullptr;
  } else {
    return *iter->second;
  }
}

uint64_t Compiler::Impl::add(const Module& module, Mode mode) const {
  auto lock = this->workspace.lockShared();
  auto& node = add(module).getRootNode();
  if (mode != LAZY) {
    node.traverse(mode);
  }
  return node.getId();
}

kj::Maybe<uint64_t> Compiler::Impl::lookup(uint64_t parent, kj::StringPtr childName) const {
  // We know this won't use the workspace, so we need not lock it.
  KJ_IF_MAYBE(parentNode, findNode(parent)) {
    return parentNode->lookupMember(childName).map([](const Node& n) { return n.getId(); });
  } else {
    KJ_FAIL_REQUIRE("lookup()s parameter 'parent' must be a known ID.", parent);
  }
}

void Compiler::Impl::load(const SchemaLoader& loader, uint64_t id) const {
  KJ_IF_MAYBE(node, findNode(id)) {
    if (&loader == &finalLoader) {
      auto lock = this->workspace.lockShared();
      node->getFinalSchema();
    } else {
      // Must be the bootstrap loader.
      node->getBootstrapSchema();
    }
  }
}

// =======================================================================================

Compiler::Compiler(): impl(kj::heap<Impl>()) {}
Compiler::~Compiler() {}

uint64_t Compiler::add(const Module& module, Mode mode) const {
  return impl->add(module, mode);
}

kj::Maybe<uint64_t> Compiler::lookup(uint64_t parent, kj::StringPtr childName) const {
  return impl->lookup(parent, childName);
}

const SchemaLoader& Compiler::getLoader() const {
  return impl->getFinalLoader();
}

void Compiler::clearWorkspace() {
  impl->clearWorkspace();
}

}  // namespace compiler
}  // namespace capnp
