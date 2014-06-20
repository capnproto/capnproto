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

#include "compiler.h"
#include "parser.h"      // only for generateChildId()
#include <kj/mutex.h>
#include <kj/arena.h>
#include <kj/vector.h>
#include <kj/debug.h>
#include <capnp/message.h>
#include <map>
#include <set>
#include <unordered_map>
#include "node-translator.h"
#include "md5.h"

namespace capnp {
namespace compiler {

class Compiler::Alias {
public:
  Alias(Node& parent, const DeclName::Reader& targetName)
      : parent(parent), targetName(targetName) {}

  kj::Maybe<Node&> getTarget();

private:
  Node& parent;
  DeclName::Reader targetName;
  bool initialized = false;
  kj::Maybe<Node&> target;
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

  Node(Node& parent, const Declaration::Reader& declaration);
  // Create a child node.

  Node(kj::StringPtr name, Declaration::Which kind);
  // Create a dummy node representing a built-in declaration, like "Int32" or "true".

  uint64_t getId() { return id; }
  Declaration::Which getKind() { return kind; }

  kj::Maybe<Node&> lookupMember(kj::StringPtr name);
  // Find a direct member of this node with the given name.

  kj::Maybe<Node&> lookupLexical(kj::StringPtr name);
  // Look up the given name first as a member of this Node, then in its parent, and so on, until
  // it is found or there are no more parents to search.

  kj::Maybe<Node&> lookup(const DeclName::Reader& name);
  // Resolve an arbitrary DeclName to a Node.

  kj::Maybe<Schema> getBootstrapSchema();
  kj::Maybe<schema::Node::Reader> getFinalSchema();
  void loadFinalSchema(const SchemaLoader& loader);

  void traverse(uint eagerness, std::unordered_map<Node*, uint>& seen,
                const SchemaLoader& finalLoader);
  // Get the final schema for this node, and also possibly traverse the node's children and
  // dependencies to ensure that they are loaded, depending on the mode.

  void addError(kj::StringPtr error);
  // Report an error on this Node.

  // implements NodeTranslator::Resolver -----------------------------
  kj::Maybe<ResolvedName> resolve(const DeclName::Reader& name) override;
  kj::Maybe<Schema> resolveBootstrapSchema(uint64_t id) override;
  kj::Maybe<schema::Node::Reader> resolveFinalSchema(uint64_t id) override;
  kj::Maybe<uint64_t> resolveImport(kj::StringPtr name) override;

private:
  CompiledModule* module;  // null iff isBuiltin is true
  kj::Maybe<Node&> parent;

  Declaration::Reader declaration;
  // AST of the declaration parsed from the schema file.  May become invalid once the content
  // state has reached FINISHED.

  uint64_t id;
  // The ID of this node, either taken from the AST or computed based on the parent.  Or, a dummy
  // value, if duplicates were detected.

  kj::StringPtr displayName;
  // Fully-qualified display name for this node.  For files, this is just the file name, otherwise
  // it is "filename:Path.To.Decl".

  Declaration::Which kind;
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
    // Indicates which fields below are valid.

    inline bool stateHasReached(State minimumState) {
      return state >= minimumState;
    }
    inline void advanceState(State newState) {
      state = newState;
    }

    // EXPANDED ------------------------------------

    typedef std::multimap<kj::StringPtr, kj::Own<Node>> NestedNodesMap;
    NestedNodesMap nestedNodes;
    kj::Vector<Node*> orderedNestedNodes;
    // Filled in when lookupMember() is first called.  multimap in case of duplicate member names --
    // we still want to compile them, even if it's an error.

    typedef std::multimap<kj::StringPtr, kj::Own<Alias>> AliasMap;
    AliasMap aliases;
    // The "using" declarations.  These are just links to nodes elsewhere.

    // BOOTSTRAP -----------------------------------

    NodeTranslator* translator;
    // Node translator, allocated in the bootstrap arena.

    kj::Maybe<Schema> bootstrapSchema;
    // The schema built in the bootstrap loader.  Null if the bootstrap loader threw an exception.

    // FINISHED ------------------------------------

    kj::Maybe<schema::Node::Reader> finalSchema;
    // The completed schema, ready to load into the real schema loader.

    kj::Array<schema::Node::Reader> auxSchemas;
    // Schemas for all auxiliary nodes built by the NodeTranslator.
  };

  Content guardedContent;     // Read using getContent() only!
  bool inGetContent = false;  // True while getContent() is running; detects cycles.

  kj::Maybe<schema::Node::Reader> loadedFinalSchema;
  // Copy of `finalSchema` as loaded into the final schema loader.  This doesn't go away if the
  // workspace is destroyed.

  // ---------------------------------------------

  static uint64_t generateId(uint64_t parentId, kj::StringPtr declName,
                             Declaration::Id::Reader declId);
  // Extract the ID from the declaration, or if it has none, generate one based on the name and
  // parent ID.

  static kj::StringPtr joinDisplayName(kj::Arena& arena, Node& parent, kj::StringPtr declName);
  // Join the parent's display name with the child's unqualified name to construct the child's
  // display name.

  kj::Maybe<Content&> getContent(Content::State minimumState);
  // Advances the content to at least the given state and returns it.  Returns null if getContent()
  // is being called recursively and the given state has not yet been reached, as this indicates
  // that the declaration recursively depends on itself.

  void traverseNodeDependencies(const schema::Node::Reader& schemaNode, uint eagerness,
                                std::unordered_map<Node*, uint>& seen,
                                const SchemaLoader& finalLoader);
  void traverseType(const schema::Type::Reader& type, uint eagerness,
                    std::unordered_map<Node*, uint>& seen,
                    const SchemaLoader& finalLoader);
  void traverseAnnotations(const List<schema::Annotation>::Reader& annotations, uint eagerness,
                           std::unordered_map<Node*, uint>& seen,
                           const SchemaLoader& finalLoader);
  void traverseDependency(uint64_t depId, uint eagerness,
                          std::unordered_map<Node*, uint>& seen,
                          const SchemaLoader& finalLoader,
                          bool ignoreIfNotFound = false);
  // Helpers for traverse().
};

class Compiler::CompiledModule {
public:
  CompiledModule(Compiler::Impl& compiler, Module& parserModule);

  Compiler::Impl& getCompiler() { return compiler; }

  ErrorReporter& getErrorReporter() { return parserModule; }
  ParsedFile::Reader getParsedFile() { return content.getReader(); }
  Node& getRootNode() { return rootNode; }
  kj::StringPtr getSourceName() { return parserModule.getSourceName(); }

  kj::Maybe<CompiledModule&> importRelative(kj::StringPtr importPath);

  Orphan<List<schema::CodeGeneratorRequest::RequestedFile::Import>>
      getFileImportTable(Orphanage orphanage);

private:
  Compiler::Impl& compiler;
  Module& parserModule;
  MallocMessageBuilder contentArena;
  Orphan<ParsedFile> content;
  Node rootNode;
};

class Compiler::Impl: public SchemaLoader::LazyLoadCallback {
public:
  explicit Impl(AnnotationFlag annotationFlag);
  virtual ~Impl() noexcept(false);

  uint64_t add(Module& module);
  kj::Maybe<uint64_t> lookup(uint64_t parent, kj::StringPtr childName);
  Orphan<List<schema::CodeGeneratorRequest::RequestedFile::Import>>
      getFileImportTable(Module& module, Orphanage orphanage);
  void eagerlyCompile(uint64_t id, uint eagerness, const SchemaLoader& loader);
  CompiledModule& addInternal(Module& parsedModule);

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

  kj::Arena& getNodeArena() { return nodeArena; }
  // Arena where nodes and other permanent objects should be allocated.

  Workspace& getWorkspace() { return workspace; }
  // Temporary workspace that can be used to construct bootstrap objects.

  inline bool shouldCompileAnnotations() {
    return annotationFlag == AnnotationFlag::COMPILE_ANNOTATIONS;
  }

  void clearWorkspace();
  // Reset the temporary workspace.

  uint64_t addNode(uint64_t desiredId, Node& node);
  // Add the given node to the by-ID map under the given ID.  If another node with the same ID
  // already exists, choose a new one arbitrarily and use that instead.  Return the ID that was
  // finally used.

  kj::Maybe<Node&> findNode(uint64_t id);

  kj::Maybe<Node&> lookupBuiltin(kj::StringPtr name);

  void load(const SchemaLoader& loader, uint64_t id) const override;
  // SchemaLoader callback for the bootstrap loader.

  void loadFinal(const SchemaLoader& loader, uint64_t id);
  // Called from the SchemaLoader callback for the final loader.

private:
  AnnotationFlag annotationFlag;

  kj::Arena nodeArena;
  // Arena used to allocate nodes and other permanent objects.

  Workspace workspace;
  // The temporary workspace.

  std::unordered_map<Module*, kj::Own<CompiledModule>> modules;
  // Map of parser modules to compiler modules.

  std::unordered_map<uint64_t, Node*> nodesById;
  // Map of nodes by ID.

  std::map<kj::StringPtr, kj::Own<Node>> builtinDecls;
  // Map of built-in declarations, like "Int32" and "List", which make up the global scope.

  uint64_t nextBogusId = 1000;
  // Counter for assigning bogus IDs to nodes whose real ID is a duplicate.
};

// =======================================================================================

kj::Maybe<Compiler::Node&> Compiler::Alias::getTarget() {
  if (!initialized) {
    initialized = true;
    target = parent.lookup(targetName);
  }
  return target;
}

// =======================================================================================

Compiler::Node::Node(CompiledModule& module)
    : module(&module),
      parent(nullptr),
      declaration(module.getParsedFile().getRoot()),
      id(generateId(0, declaration.getName().getValue(), declaration.getId())),
      displayName(module.getSourceName()),
      kind(declaration.which()),
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

Compiler::Node::Node(Node& parent, const Declaration::Reader& declaration)
    : module(parent.module),
      parent(parent),
      declaration(declaration),
      id(generateId(parent.id, declaration.getName().getValue(), declaration.getId())),
      displayName(joinDisplayName(parent.module->getCompiler().getNodeArena(),
                                  parent, declaration.getName().getValue())),
      kind(declaration.which()),
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

Compiler::Node::Node(kj::StringPtr name, Declaration::Which kind)
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
  if (declId.isUid()) {
    return declId.getUid().getValue();
  }

  return generateChildId(parentId, declName);
}

kj::StringPtr Compiler::Node::joinDisplayName(
    kj::Arena& arena, Node& parent, kj::StringPtr declName) {
  kj::ArrayPtr<char> result = arena.allocateArray<char>(
      parent.displayName.size() + declName.size() + 2);

  size_t separatorPos = parent.displayName.size();
  memcpy(result.begin(), parent.displayName.begin(), separatorPos);
  result[separatorPos] = parent.parent == nullptr ? ':' : '.';
  memcpy(result.begin() + separatorPos + 1, declName.begin(), declName.size());
  result[result.size() - 1] = '\0';
  return kj::StringPtr(result.begin(), result.size() - 1);
}

kj::Maybe<Compiler::Node::Content&> Compiler::Node::getContent(Content::State minimumState) {
  KJ_REQUIRE(!isBuiltin, "illegal method call for built-in declaration");

  auto& content = guardedContent;

  if (content.stateHasReached(minimumState)) {
    return content;
  }

  if (inGetContent) {
    addError("Declaration recursively depends on itself.");
    return nullptr;
  }

  inGetContent = true;
  KJ_DEFER(inGetContent = false);

  switch (content.state) {
    case Content::STUB: {
      if (minimumState <= Content::STUB) break;

      // Expand the child nodes.
      auto& arena = module->getCompiler().getNodeArena();

      for (auto nestedDecl: declaration.getNestedDecls()) {
        switch (nestedDecl.which()) {
          case Declaration::FILE:
          case Declaration::CONST:
          case Declaration::ANNOTATION:
          case Declaration::ENUM:
          case Declaration::STRUCT:
          case Declaration::INTERFACE: {
            kj::Own<Node> subNode = arena.allocateOwn<Node>(*this, nestedDecl);
            kj::StringPtr name = nestedDecl.getName().getValue();
            content.orderedNestedNodes.add(subNode);
            content.nestedNodes.insert(std::make_pair(name, kj::mv(subNode)));
            break;
          }

          case Declaration::USING: {
            kj::Own<Alias> alias = arena.allocateOwn<Alias>(
                *this, nestedDecl.getUsing().getTarget());
            kj::StringPtr name = nestedDecl.getName().getValue();
            content.aliases.insert(std::make_pair(name, kj::mv(alias)));
            break;
          }
          case Declaration::ENUMERANT:
          case Declaration::FIELD:
          case Declaration::UNION:
          case Declaration::GROUP:
          case Declaration::METHOD:
          case Declaration::NAKED_ID:
          case Declaration::NAKED_ANNOTATION:
            // Not a node.  Skip.
            break;
          default:
            KJ_FAIL_ASSERT("unknown declaration type", nestedDecl);
            break;
        }
      }

      content.advanceState(Content::EXPANDED);
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
      // TODO(cleanup):  Would be better if we could remember the prefix length from before we
      //   added this decl's name to the end.
      KJ_IF_MAYBE(lastDot, displayName.findLast('.')) {
        builder.setDisplayNamePrefixLength(*lastDot + 1);
      }
      KJ_IF_MAYBE(lastColon, displayName.findLast(':')) {
        if (*lastColon > builder.getDisplayNamePrefixLength()) {
          builder.setDisplayNamePrefixLength(*lastColon + 1);
        }
      }
      KJ_IF_MAYBE(p, parent) {
        builder.setScopeId(p->id);
      }

      auto nestedNodes = builder.initNestedNodes(content.orderedNestedNodes.size());
      auto nestedIter = nestedNodes.begin();
      for (auto node: content.orderedNestedNodes) {
        nestedIter->setName(node->declaration.getName().getValue());
        nestedIter->setId(node->id);
        ++nestedIter;
      }

      content.translator = &workspace.arena.allocate<NodeTranslator>(
          *this, module->getErrorReporter(), declaration, kj::mv(schemaNode),
          module->getCompiler().shouldCompileAnnotations());
      KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&](){
        auto nodeSet = content.translator->getBootstrapNode();
        for (auto& auxNode: nodeSet.auxNodes) {
          workspace.bootstrapLoader.loadOnce(auxNode);
        }
        content.bootstrapSchema = workspace.bootstrapLoader.loadOnce(nodeSet.node);
      })) {
        content.bootstrapSchema = nullptr;
        // Only bother to report validation failures if we think we haven't seen any errors.
        // Otherwise we assume that the errors caused the validation failure.
        if (!module->getErrorReporter().hadErrors()) {
          addError(kj::str("Internal compiler bug: Bootstrap schema failed validation:\n",
                           *exception));
        }
      }

      // If the Workspace is destroyed, revert the node to the EXPANDED state, because the
      // NodeTranslator is no longer valid in this case.
      workspace.arena.copy(kj::defer([&content]() {
        content.bootstrapSchema = nullptr;
        if (content.state > Content::EXPANDED) {
          content.state = Content::EXPANDED;
        }
      }));

      content.advanceState(Content::BOOTSTRAP);
      // no break
    }

    case Content::BOOTSTRAP: {
      if (minimumState <= Content::BOOTSTRAP) break;

      // Create the final schema.
      auto nodeSet = content.translator->finish();
      content.finalSchema = nodeSet.node;
      content.auxSchemas = kj::mv(nodeSet.auxNodes);

      content.advanceState(Content::FINISHED);
      // no break
    }

    case Content::FINISHED:
      break;
  }

  return content;
}

kj::Maybe<Compiler::Node&> Compiler::Node::lookupMember(kj::StringPtr name) {
  if (isBuiltin) return nullptr;

  KJ_IF_MAYBE(content, getContent(Content::EXPANDED)) {
    {
      auto iter = content->nestedNodes.find(name);
      if (iter != content->nestedNodes.end()) {
        return *iter->second;
      }
    }
    {
      auto iter = content->aliases.find(name);
      if (iter != content->aliases.end()) {
        return iter->second->getTarget();
      }
    }
  }
  return nullptr;
}

kj::Maybe<Compiler::Node&> Compiler::Node::lookupLexical(kj::StringPtr name) {
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

kj::Maybe<Compiler::Node&> Compiler::Node::lookup(const DeclName::Reader& name) {
  KJ_REQUIRE(!isBuiltin, "illegal method call for built-in declaration");

  Node* node = nullptr;

  auto base = name.getBase();
  switch (base.which()) {
    case DeclName::Base::ABSOLUTE_NAME: {
      auto absoluteName = base.getAbsoluteName();
      KJ_IF_MAYBE(n, module->getRootNode().lookupMember(absoluteName.getValue())) {
        node = &*n;
      } else {
        module->getErrorReporter().addErrorOn(
            absoluteName, kj::str("Not defined: ", absoluteName.getValue()));
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
            relativeName, kj::str("Not defined: ", relativeName.getValue()));
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
            importName, kj::str("Import failed: ", importName.getValue()));
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
          partName, kj::str("No such member: ", partName.getValue()));
      return nullptr;
    }
  }

  return *node;
}

kj::Maybe<Schema> Compiler::Node::getBootstrapSchema() {
  KJ_IF_MAYBE(schema, loadedFinalSchema) {
    // We don't need to rebuild the bootstrap schema if we already have a final schema.
    return module->getCompiler().getWorkspace().bootstrapLoader.loadOnce(*schema);
  } else KJ_IF_MAYBE(content, getContent(Content::BOOTSTRAP)) {
    if (content->state == Content::FINISHED && content->bootstrapSchema == nullptr) {
      // The bootstrap schema was discarded.  Copy it from the final schema.
      // (We can't just return the final schema because using it could trigger schema loader
      // callbacks that would deadlock.)
      KJ_IF_MAYBE(finalSchema, content->finalSchema) {
        return module->getCompiler().getWorkspace().bootstrapLoader.loadOnce(*finalSchema);
      } else {
        return nullptr;
      }
    } else {
      return content->bootstrapSchema;
    }
  } else {
    return nullptr;
  }
}
kj::Maybe<schema::Node::Reader> Compiler::Node::getFinalSchema() {
  KJ_IF_MAYBE(schema, loadedFinalSchema) {
    return *schema;
  } else KJ_IF_MAYBE(content, getContent(Content::FINISHED)) {
    return content->finalSchema;
  } else {
    return nullptr;
  }
}
void Compiler::Node::loadFinalSchema(const SchemaLoader& loader) {
  KJ_IF_MAYBE(content, getContent(Content::FINISHED)) {
    KJ_IF_MAYBE(exception, kj::runCatchingExceptions([&](){
      KJ_IF_MAYBE(finalSchema, content->finalSchema) {
        KJ_MAP(auxSchema, content->auxSchemas) {
          return loader.loadOnce(auxSchema);
        };
        loadedFinalSchema = loader.loadOnce(*finalSchema).getProto();
      }
    })) {
      // Schema validation threw an exception.

      // Don't try loading this again.
      content->finalSchema = nullptr;

      // Only bother to report validation failures if we think we haven't seen any errors.
      // Otherwise we assume that the errors caused the validation failure.
      if (!module->getErrorReporter().hadErrors()) {
        addError(kj::str("Internal compiler bug: Schema failed validation:\n", *exception));
      }
    }
  }
}

void Compiler::Node::traverse(uint eagerness, std::unordered_map<Node*, uint>& seen,
                              const SchemaLoader& finalLoader) {
  uint& slot = seen[this];
  if ((slot & eagerness) == eagerness) {
    // We've already covered this node.
    return;
  }
  slot |= eagerness;

  KJ_IF_MAYBE(content, getContent(Content::FINISHED)) {
    loadFinalSchema(finalLoader);

    KJ_IF_MAYBE(schema, getFinalSchema()) {
      if (eagerness / DEPENDENCIES != 0) {
        // For traversing dependencies, discard the bits lower than DEPENDENCIES and replace
        // them with the bits above DEPENDENCIES shifted over.
        uint newEagerness = (eagerness & ~(DEPENDENCIES - 1)) | (eagerness / DEPENDENCIES);

        traverseNodeDependencies(*schema, newEagerness, seen, finalLoader);
        for (auto& aux: content->auxSchemas) {
          traverseNodeDependencies(aux, newEagerness, seen, finalLoader);
        }
      }
    }
  }

  if (eagerness & PARENTS) {
    KJ_IF_MAYBE(p, parent) {
      p->traverse(eagerness, seen, finalLoader);
    }
  }

  if (eagerness & CHILDREN) {
    KJ_IF_MAYBE(content, getContent(Content::EXPANDED)) {
      for (auto& child: content->orderedNestedNodes) {
        child->traverse(eagerness, seen, finalLoader);
      }
    }
  }
}

void Compiler::Node::traverseNodeDependencies(
    const schema::Node::Reader& schemaNode, uint eagerness,
    std::unordered_map<Node*, uint>& seen,
    const SchemaLoader& finalLoader) {
  switch (schemaNode.which()) {
    case schema::Node::STRUCT:
      for (auto field: schemaNode.getStruct().getFields()) {
        switch (field.which()) {
          case schema::Field::SLOT:
            traverseType(field.getSlot().getType(), eagerness, seen, finalLoader);
            break;
          case schema::Field::GROUP:
            // Aux node will be scanned later.
            break;
        }

        traverseAnnotations(field.getAnnotations(), eagerness, seen, finalLoader);
      }
      break;

    case schema::Node::ENUM:
      for (auto enumerant: schemaNode.getEnum().getEnumerants()) {
        traverseAnnotations(enumerant.getAnnotations(), eagerness, seen, finalLoader);
      }
      break;

    case schema::Node::INTERFACE: {
      auto interface = schemaNode.getInterface();
      for (auto extend: interface.getExtends()) {
        if (extend != 0) {  // if zero, we reported an error earlier
          traverseDependency(extend, eagerness, seen, finalLoader);
        }
      }
      for (auto method: interface.getMethods()) {
        traverseDependency(method.getParamStructType(), eagerness, seen, finalLoader, true);
        traverseDependency(method.getResultStructType(), eagerness, seen, finalLoader, true);
        traverseAnnotations(method.getAnnotations(), eagerness, seen, finalLoader);
      }
      break;
    }

    default:
      break;
  }

  traverseAnnotations(schemaNode.getAnnotations(), eagerness, seen, finalLoader);
}

void Compiler::Node::traverseType(const schema::Type::Reader& type, uint eagerness,
                                  std::unordered_map<Node*, uint>& seen,
                                  const SchemaLoader& finalLoader) {
  uint64_t id = 0;
  switch (type.which()) {
    case schema::Type::STRUCT:
      id = type.getStruct().getTypeId();
      break;
    case schema::Type::ENUM:
      id = type.getEnum().getTypeId();
      break;
    case schema::Type::INTERFACE:
      id = type.getInterface().getTypeId();
      break;
    case schema::Type::LIST:
      traverseType(type.getList().getElementType(), eagerness, seen, finalLoader);
      return;
    default:
      return;
  }

  traverseDependency(id, eagerness, seen, finalLoader);
}

void Compiler::Node::traverseDependency(uint64_t depId, uint eagerness,
                                        std::unordered_map<Node*, uint>& seen,
                                        const SchemaLoader& finalLoader,
                                        bool ignoreIfNotFound) {
  KJ_IF_MAYBE(node, module->getCompiler().findNode(depId)) {
    node->traverse(eagerness, seen, finalLoader);
  } else if (!ignoreIfNotFound) {
    KJ_FAIL_ASSERT("Dependency ID not present in compiler?", depId);
  }
}

void Compiler::Node::traverseAnnotations(const List<schema::Annotation>::Reader& annotations,
                                         uint eagerness,
                                         std::unordered_map<Node*, uint>& seen,
                                         const SchemaLoader& finalLoader) {
  for (auto annotation: annotations) {
    KJ_IF_MAYBE(node, module->getCompiler().findNode(annotation.getId())) {
      node->traverse(eagerness, seen, finalLoader);
    }
  }
}


void Compiler::Node::addError(kj::StringPtr error) {
  module->getErrorReporter().addError(startByte, endByte, error);
}

kj::Maybe<NodeTranslator::Resolver::ResolvedName> Compiler::Node::resolve(
    const DeclName::Reader& name) {
  return lookup(name).map([](Node& node) {
    return ResolvedName { node.id, node.kind };
  });
}

kj::Maybe<Schema> Compiler::Node::resolveBootstrapSchema(uint64_t id) {
  KJ_IF_MAYBE(node, module->getCompiler().findNode(id)) {
    return node->getBootstrapSchema();
  } else {
    KJ_FAIL_REQUIRE("Tried to get schema for ID we haven't seen before.");
  }
}

kj::Maybe<schema::Node::Reader> Compiler::Node::resolveFinalSchema(uint64_t id) {
  KJ_IF_MAYBE(node, module->getCompiler().findNode(id)) {
    return node->getFinalSchema();
  } else {
    KJ_FAIL_REQUIRE("Tried to get schema for ID we haven't seen before.");
  }
}

kj::Maybe<uint64_t> Compiler::Node::resolveImport(kj::StringPtr name) {
  KJ_IF_MAYBE(m, module->importRelative(name)) {
    return m->getRootNode().getId();
  } else {
    return nullptr;
  }
}

// =======================================================================================

Compiler::CompiledModule::CompiledModule(Compiler::Impl& compiler, Module& parserModule)
    : compiler(compiler), parserModule(parserModule),
      content(parserModule.loadContent(contentArena.getOrphanage())),
      rootNode(*this) {}

kj::Maybe<Compiler::CompiledModule&> Compiler::CompiledModule::importRelative(
    kj::StringPtr importPath) {
  return parserModule.importRelative(importPath).map(
      [this](Module& module) -> Compiler::CompiledModule& {
        return compiler.addInternal(module);
      });
}

static void findImports(DeclName::Reader name, std::set<kj::StringPtr>& output) {
  if (name.getBase().isImportName()) {
    output.insert(name.getBase().getImportName().getValue());
  }
}

static void findImports(TypeExpression::Reader type, std::set<kj::StringPtr>& output) {
  findImports(type.getName(), output);
  for (auto param: type.getParams()) {
    findImports(param, output);
  }
}

static void findImports(Declaration::Reader decl, std::set<kj::StringPtr>& output) {
  switch (decl.which()) {
    case Declaration::USING:
      findImports(decl.getUsing().getTarget(), output);
      break;
    case Declaration::CONST:
      findImports(decl.getConst().getType(), output);
      break;
    case Declaration::FIELD:
      findImports(decl.getField().getType(), output);
      break;
    case Declaration::INTERFACE:
      for (auto extend: decl.getInterface().getExtends()) {
        findImports(extend, output);
      }
      break;
    case Declaration::METHOD: {
      auto method = decl.getMethod();

      auto params = method.getParams();
      if (params.isNamedList()) {
        for (auto param: params.getNamedList()) {
          findImports(param.getType(), output);
          for (auto ann: param.getAnnotations()) {
            findImports(ann.getName(), output);
          }
        }
      } else {
        findImports(params.getType(), output);
      }

      if (method.getResults().isExplicit()) {
        auto results = method.getResults().getExplicit();
        if (results.isNamedList()) {
          for (auto param: results.getNamedList()) {
            findImports(param.getType(), output);
            for (auto ann: param.getAnnotations()) {
              findImports(ann.getName(), output);
            }
          }
        } else {
          findImports(results.getType(), output);
        }
      }
      break;
    }
    default:
      break;
  }

  for (auto ann: decl.getAnnotations()) {
    findImports(ann.getName(), output);
  }

  for (auto nested: decl.getNestedDecls()) {
    findImports(nested, output);
  }
}

Orphan<List<schema::CodeGeneratorRequest::RequestedFile::Import>>
    Compiler::CompiledModule::getFileImportTable(Orphanage orphanage) {
  std::set<kj::StringPtr> importNames;
  findImports(content.getReader().getRoot(), importNames);

  auto result = orphanage.newOrphan<List<schema::CodeGeneratorRequest::RequestedFile::Import>>(
      importNames.size());
  auto builder = result.get();

  uint i = 0;
  for (auto name: importNames) {
    // We presumably ran this import before, so it shouldn't throw now.
    auto entry = builder[i++];
    entry.setId(KJ_ASSERT_NONNULL(importRelative(name)).rootNode.getId());
    entry.setName(name);
  }

  return result;
}

// =======================================================================================

Compiler::Impl::Impl(AnnotationFlag annotationFlag)
    : annotationFlag(annotationFlag), workspace(*this) {
  // Reflectively interpret the members of Declaration.body.  Any member prefixed by "builtin"
  // defines a builtin declaration visible in the global scope.

  StructSchema declSchema = Schema::from<Declaration>();
  for (auto field: declSchema.getFields()) {
    auto fieldProto = field.getProto();
    if (fieldProto.getDiscriminantValue() != schema::Field::NO_DISCRIMINANT) {
      auto name = fieldProto.getName();
      if (name.startsWith("builtin")) {
        kj::StringPtr symbolName = name.slice(strlen("builtin"));
        builtinDecls[symbolName] = nodeArena.allocateOwn<Node>(
            symbolName, static_cast<Declaration::Which>(fieldProto.getDiscriminantValue()));
      }
    }
  }
}

Compiler::Impl::~Impl() noexcept(false) {}

void Compiler::Impl::clearWorkspace() {
  // Make sure we reconstruct the workspace even if destroying it throws an exception.
  KJ_DEFER(kj::ctor(workspace, *this));
  kj::dtor(workspace);
}

Compiler::CompiledModule& Compiler::Impl::addInternal(Module& parsedModule) {
  kj::Own<CompiledModule>& slot = modules[&parsedModule];
  if (slot.get() == nullptr) {
    slot = kj::heap<CompiledModule>(*this, parsedModule);
  }

  return *slot;
}

uint64_t Compiler::Impl::addNode(uint64_t desiredId, Node& node) {
  for (;;) {
    auto insertResult = nodesById.insert(std::make_pair(desiredId, &node));
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
    desiredId = nextBogusId++;
  }
}

kj::Maybe<Compiler::Node&> Compiler::Impl::findNode(uint64_t id) {
  auto iter = nodesById.find(id);
  if (iter == nodesById.end()) {
    return nullptr;
  } else {
    return *iter->second;
  }
}

kj::Maybe<Compiler::Node&> Compiler::Impl::lookupBuiltin(kj::StringPtr name) {
  auto iter = builtinDecls.find(name);
  if (iter == builtinDecls.end()) {
    return nullptr;
  } else {
    return *iter->second;
  }
}

uint64_t Compiler::Impl::add(Module& module) {
  return addInternal(module).getRootNode().getId();
}

kj::Maybe<uint64_t> Compiler::Impl::lookup(uint64_t parent, kj::StringPtr childName) {
  // Looking up members does not use the workspace, so we don't need to lock it.
  KJ_IF_MAYBE(parentNode, findNode(parent)) {
    KJ_IF_MAYBE(child, parentNode->lookupMember(childName)) {
      return child->getId();
    } else {
      return nullptr;
    }
  } else {
    KJ_FAIL_REQUIRE("lookup()s parameter 'parent' must be a known ID.", parent);
  }
}

Orphan<List<schema::CodeGeneratorRequest::RequestedFile::Import>>
    Compiler::Impl::getFileImportTable(Module& module, Orphanage orphanage) {
  return addInternal(module).getFileImportTable(orphanage);
}

void Compiler::Impl::eagerlyCompile(uint64_t id, uint eagerness,
                                    const SchemaLoader& finalLoader) {
  KJ_IF_MAYBE(node, findNode(id)) {
    std::unordered_map<Node*, uint> seen;
    node->traverse(eagerness, seen, finalLoader);
  } else {
    KJ_FAIL_REQUIRE("id did not come from this Compiler.", id);
  }
}

void Compiler::Impl::load(const SchemaLoader& loader, uint64_t id) const {
  // We know that this load() is only called from the bootstrap loader which is already protected
  // by our mutex, so we can drop thread-safety.
  auto& self = const_cast<Compiler::Impl&>(*this);

  KJ_IF_MAYBE(node, self.findNode(id)) {
    node->getBootstrapSchema();
  }
}

void Compiler::Impl::loadFinal(const SchemaLoader& loader, uint64_t id) {
  KJ_IF_MAYBE(node, findNode(id)) {
    node->loadFinalSchema(loader);
  }
}

// =======================================================================================

Compiler::Compiler(AnnotationFlag annotationFlag)
    : impl(kj::heap<Impl>(annotationFlag)),
      loader(*this) {}
Compiler::~Compiler() noexcept(false) {}

uint64_t Compiler::add(Module& module) const {
  return impl.lockExclusive()->get()->add(module);
}

kj::Maybe<uint64_t> Compiler::lookup(uint64_t parent, kj::StringPtr childName) const {
  return impl.lockExclusive()->get()->lookup(parent, childName);
}

Orphan<List<schema::CodeGeneratorRequest::RequestedFile::Import>>
    Compiler::getFileImportTable(Module& module, Orphanage orphanage) const {
  return impl.lockExclusive()->get()->getFileImportTable(module, orphanage);
}

void Compiler::eagerlyCompile(uint64_t id, uint eagerness) const {
  impl.lockExclusive()->get()->eagerlyCompile(id, eagerness, loader);
}

void Compiler::clearWorkspace() const {
  impl.lockExclusive()->get()->clearWorkspace();
}

void Compiler::load(const SchemaLoader& loader, uint64_t id) const {
  impl.lockExclusive()->get()->loadFinal(loader, id);
}

}  // namespace compiler
}  // namespace capnp
