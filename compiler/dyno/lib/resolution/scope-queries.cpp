/*
 * Copyright 2021-2022 Hewlett Packard Enterprise Development LP
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "chpl/resolution/scope-queries.h"

#include "chpl/parsing/parsing-queries.h"
#include "chpl/queries/ErrorMessage.h"
#include "chpl/queries/global-strings.h"
#include "chpl/queries/query-impl.h"
#include "chpl/uast/all-uast.h"

#include "scope-help.h"

#include <cstdio>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chpl {
namespace resolution {


using namespace uast;
using namespace types;

static void gather(DeclMap& declared, UniqueString name, const AstNode* d) {
  auto search = declared.find(name);
  if (search == declared.end()) {
    // add a new entry containing just the one ID
    declared.emplace_hint(search,
                          name,
                          OwnedIdsWithName(d->id()));
  } else {
    // found an entry, so add to it
    OwnedIdsWithName& val = search->second;
    val.appendId(d->id());
  }
}

struct GatherQueryDecls {
  DeclMap& declared;
  GatherQueryDecls(DeclMap& declared) : declared(declared) { }

  bool enter(const TypeQuery* ast) {
    gather(declared, ast->name(), ast);
    return true;
  }
  void exit(const TypeQuery* ast) { }

  bool enter(const AstNode* ast) {
    return true;
  }
  void exit(const AstNode* ast) { }
};

struct GatherDecls {
  DeclMap declared;
  bool containsUseImport = false;
  bool containsFunctionDecls = false;

  GatherDecls() { }

  // Add NamedDecls to the map
  bool enter(const NamedDecl* d) {
    bool skip = false;

    if (d->isRecord() && d->name() == USTR("_tuple")) {
      // skip gathering _tuple from the standard library
      // since dyno handles tuple types directly rather
      // than through a record.
      skip = true;
    }

    if (skip == false) {
      gather(declared, d->name(), d);
    }

    if (d->isFunction()) {
      // make a note if we encountered a function
      containsFunctionDecls = true;
    }

    // traverse inside to look for type queries &
    // add them to declared
    if (auto fml = d->toFormal()) {
      if (auto typeExpr = fml->typeExpression()) {
        GatherQueryDecls gatherQueryDecls(declared);
        typeExpr->traverse(gatherQueryDecls);
      }
    }

    return false;
  }
  void exit(const NamedDecl* d) { }

  // Traverse into TupleDecl and MultiDecl looking for NamedDecls
  bool enter(const TupleDecl* d) {
    return true;
  }
  void exit(const TupleDecl* d) { }

  bool enter(const MultiDecl* d) {
    return true;
  }
  void exit(const MultiDecl* d) { }

  // make note of use/import
  bool enter(const Use* d) {
    containsUseImport = true;
    return false;
  }
  void exit(const Use* d) { }
  bool enter(const Import* d) {
    containsUseImport = true;
    return false;
  }
  void exit(const Import* d) { }

  // consider 'include module' something that defines a name
  bool enter(const Include* d) {
    gather(declared, d->name(), d);
    return false;
  }
  void exit(const Include* d) { }

  // ignore other AST nodes
  bool enter(const AstNode* ast) {
    return false;
  }
  void exit(const AstNode* ast) { }
};

void gatherDeclsWithin(const uast::AstNode* ast,
                       DeclMap& declared,
                       bool& containsUseImport,
                       bool& containsFunctionDecls) {
  GatherDecls visitor;

  // Visit child nodes to e.g. look inside a Function
  // rather than collecting it as a NamedDecl
  // Or, look inside a Block for its declarations
  for (const AstNode* child : ast->children()) {
    child->traverse(visitor);
  }

  declared.swap(visitor.declared);
  containsUseImport = visitor.containsUseImport;
  containsFunctionDecls = visitor.containsFunctionDecls;
}

bool createsScope(asttags::AstTag tag) {
  return Builder::astTagIndicatesNewIdScope(tag)
         || asttags::isSimpleBlockLike(tag)
         || asttags::isLoop(tag)
         || asttags::isCobegin(tag)
         || asttags::isConditional(tag)
         || asttags::isSelect(tag)
         || asttags::isTry(tag);
}

static const Scope* const& scopeForIdQuery(Context* context, ID id);

static void populateScopeWithBuiltins(Context* context, Scope* scope) {
  std::unordered_map<UniqueString,const Type*> map;
  Type::gatherBuiltins(context, map);

  for (const auto& pair : map) {
    scope->addBuiltin(pair.first);
  }
}

// This query always constructs a scope
// (don't call it if the scope does not need to exist)
static const owned<Scope>& constructScopeQuery(Context* context, ID id) {
  QUERY_BEGIN(constructScopeQuery, context, id);

  Scope* result = nullptr;

  if (id.isEmpty()) {
    result = new Scope();
    // empty ID indicates to make the root scope
    // populate it with builtins
    populateScopeWithBuiltins(context, result);
  } else {
    const uast::AstNode* ast = parsing::idToAst(context, id);
    if (ast == nullptr) {
      assert(false && "could not find ast for id");
      result = new Scope();
    } else {
      ID parentId = parsing::idToParentId(context, id);
      const Scope* parentScope = scopeForIdQuery(context, parentId);

      bool autoUsesModules = false;
      if (ast->isModule()) {
        if (!parsing::idIsInInternalModule(context, ast->id())) {
          autoUsesModules = true;
        }
      }

      result = new Scope(ast, parentScope, autoUsesModules);
    }
  }

  auto ownedResult = toOwned(result);
  return QUERY_END(ownedResult);
}

static const Scope* const& scopeForIdQuery(Context* context, ID idIn) {
  QUERY_BEGIN(scopeForIdQuery, context, idIn);

  const Scope* result = nullptr;

  if (idIn.isEmpty()) {
    // use empty scope for top-level
    const owned<Scope>& newScope = constructScopeQuery(context, ID());
    result = newScope.get();
  } else {
    // decide whether or not to create a new scope
    bool newScope = false;

    ID id = idIn;
    const uast::AstNode* ast = parsing::idToAst(context, id);
    if (ast == nullptr) {
      assert(false && "could not find ast for id");
    } else if (ast->isInclude()) {
      // parse 'module include' and use the result of parsing instead
      // of the 'module include' itself.
      ast = parsing::getIncludedSubmodule(context, id);
      id = ast->id();
    }

    if (createsScope(ast->tag())) {
      if (Builder::astTagIndicatesNewIdScope(ast->tag())) {
        // always create a new scope for a Function etc
        newScope = true;
      } else {
        DeclMap declared;
        bool containsUseImport = false;
        bool containsFns = false;
        gatherDeclsWithin(ast, declared, containsUseImport, containsFns);

        // create a new scope if we found any decls/uses immediately in it
        newScope = !(declared.empty() && containsUseImport == false);
      }
    }

    if (newScope) {
      // Construct the new scope.
      const owned<Scope>& newScope = constructScopeQuery(context, id);
      result = newScope.get();
    } else {
      // find the scope for the parent node and return that.
      ID parentId = parsing::idToParentId(context, id);
      result = scopeForIdQuery(context, parentId);
    }
  }

  return QUERY_END(result);
}

const Scope* scopeForId(Context* context, ID id) {
  return scopeForIdQuery(context, id);
}

static bool doLookupInScope(Context* context,
                            const Scope* scope,
                            const ResolvedVisibilityScope* resolving,
                            UniqueString name,
                            LookupConfig config,
                            std::unordered_set<const Scope*>& checkedScopes,
                            std::vector<BorrowedIdsWithName>& result);

static const ResolvedVisibilityScope*
resolveVisibilityStmts(Context* context, const Scope* scope);

// Returns the scope for the automatically included standard module
static const Scope* const& scopeForAutoModule(Context* context) {
  QUERY_BEGIN(scopeForAutoModule, context);

  auto name = UniqueString::get(context, "ChapelStandard");
  const Module* mod = parsing::getToplevelModule(context, name);
  const Scope* result = nullptr;
  if (mod != nullptr) {
    result = scopeForIdQuery(context, mod->id());
  }

  return QUERY_END(result);
}

static bool doLookupInImports(Context* context,
                              const Scope* scope,
                              const ResolvedVisibilityScope* resolving,
                              UniqueString name,
                              bool onlyInnermost,
                              std::unordered_set<const Scope*>& checkedScopes,
                              std::vector<BorrowedIdsWithName>& result) {
  // Get the resolved visibility statements, if available
  const ResolvedVisibilityScope* r = nullptr;
  if (resolving && resolving->scope() == scope) {
    r = resolving;
  } else {
    r = resolveVisibilityStmts(context, scope);
  }

  if (r != nullptr) {
    // check to see if it's mentioned in names/renames
    for (const VisibilitySymbols& is: r->visibilityClauses()) {
      UniqueString from = name;
      bool named = is.lookupName(name, from);
      if (named && is.kind() == VisibilitySymbols::SYMBOL_ONLY) {
        result.push_back(BorrowedIdsWithName(is.scope()->id()));
        return true;
      } else if (named && is.kind() == VisibilitySymbols::CONTENTS_EXCEPT) {
        // mentioned in an except clause, so don't return it
      } else if (named || is.kind() == VisibilitySymbols::ALL_CONTENTS) {
        // find it in the contents
        const Scope* symScope = is.scope();
        LookupConfig newConfig = LOOKUP_DECLS |
                                 LOOKUP_IMPORT_AND_USE;
        if (onlyInnermost) {
          newConfig |= LOOKUP_INNERMOST;
        }

        // find it in that scope
        bool found = doLookupInScope(context, symScope, resolving,
                                     from, newConfig,
                                     checkedScopes, result);
        if (found && onlyInnermost)
          return true;
      }
    }
  }

  if (scope->autoUsesModules()) {
    const Scope* autoModScope = scopeForAutoModule(context);
    if (autoModScope) {
      LookupConfig newConfig = LOOKUP_DECLS |
                               LOOKUP_IMPORT_AND_USE;

      if (onlyInnermost) {
        newConfig |= LOOKUP_INNERMOST;
      }

      // find it in that scope
      bool found = doLookupInScope(context, autoModScope, resolving,
                                   name, newConfig,
                                   checkedScopes, result);
      if (found && onlyInnermost)
        return true;
    }
  }

  return false;
}

static bool doLookupInToplevelModules(Context* context,
                                      const Scope* scope,
                                      UniqueString name,
                                      std::vector<BorrowedIdsWithName>& result){
  const Module* mod = parsing::getToplevelModule(context, name);
  if (mod == nullptr)
    return false;

  result.push_back(BorrowedIdsWithName(mod->id()));
  return true;
}

// appends to result
static bool doLookupInScope(Context* context,
                            const Scope* scope,
                            const ResolvedVisibilityScope* resolving,
                            UniqueString name,
                            LookupConfig config,
                            std::unordered_set<const Scope*>& checkedScopes,
                            std::vector<BorrowedIdsWithName>& result) {

  bool checkDecls = (config & LOOKUP_DECLS) != 0;
  bool checkUseImport = (config & LOOKUP_IMPORT_AND_USE) != 0;
  bool checkParents = (config & LOOKUP_PARENTS) != 0;
  bool checkToplevel = (config & LOOKUP_TOPLEVEL) != 0;
  bool onlyInnermost = (config & LOOKUP_INNERMOST) != 0;

  // TODO: to include checking for symbol privacy,
  // add a findPrivate argument to doLookupInScope and set it
  // to false when traversing a use/import of a module not visible.
  // Adjust the checkedScopes set to be a map to bool, where
  // the bool indicates if findPrivate was true or not. If we
  // have visited but only got public symbols, we have to visit again
  // for private symbols. But we'd like to avoid splitting overloads into
  // two 'result' vector entries in that case...
  size_t startSize = result.size();

  auto pair = checkedScopes.insert(scope);
  if (pair.second == false) {
    // scope has already been visited by this function,
    // so don't try it again.
    return false;
  }

  if (checkDecls) {
    bool got = scope->lookupInScope(name, result);
    if (onlyInnermost && got) return true;
  }

  if (checkUseImport) {
    bool got = false;
    got = doLookupInImports(context, scope, resolving,
                            name, onlyInnermost,
                            checkedScopes, result);
    if (onlyInnermost && got) return true;
  }

  if (checkParents) {
    LookupConfig newConfig = LOOKUP_DECLS;
    if (checkUseImport) {
      newConfig |= LOOKUP_IMPORT_AND_USE;
    }
    if (onlyInnermost) {
      newConfig |= LOOKUP_INNERMOST;
    }

    const Scope* cur = nullptr;
    for (cur = scope->parentScope(); cur != nullptr; cur = cur->parentScope()) {
      bool got = doLookupInScope(context, cur, resolving, name, newConfig,
                                 checkedScopes, result);
      if (onlyInnermost && got) return true;

      // stop if we reach a Module scope
      if (asttags::isModule(cur->tag()))
        break;
    }

    // check also in the root scope if this isn't already the root scope
    const Scope* rootScope = nullptr;
    for (cur = scope->parentScope(); cur != nullptr; cur = cur->parentScope()) {
      if (cur->parentScope() == nullptr)
        rootScope = cur;
    }
    if (rootScope != nullptr) {
      bool got = doLookupInScope(context, rootScope, resolving, name, newConfig,
                                 checkedScopes, result);
      if (onlyInnermost && got) return true;
    }
  }

  if (checkToplevel) {
    bool got = doLookupInToplevelModules(context, scope, name, result);
    if (onlyInnermost && got) return true;
  }

  return result.size() > startSize;
}

enum VisibilityStmtKind {
  VIS_USE,    // the expr is the thing being use'd e.g. use A.B
  VIS_IMPORT, // the expr is the thing being imported e.g. import C.D
};

// It can return multiple results because that is important for 'import'.
// 'isFirstPart' is true for A in A.B.C but not for B or C.
// On return:
//   result contains the things with a matching name
static bool lookupInScopeViz(Context* context,
                             const Scope* scope,
                             const ResolvedVisibilityScope* resolving,
                             UniqueString name,
                             VisibilityStmtKind useOrImport,
                             bool isFirstPart,
                             std::vector<BorrowedIdsWithName>& result) {
  std::unordered_set<const Scope*> checkedScopes;

  LookupConfig config = LOOKUP_INNERMOST;

  if (isFirstPart == true) {
    // e.g. A in use A.B.C or import A.B.C
    if (useOrImport == VIS_USE) {
      // a top-level module name
      config |= LOOKUP_TOPLEVEL;

      // a submodule of the current module
      config |= LOOKUP_DECLS;

      // a module name in scope due to another use/import
      config |= LOOKUP_IMPORT_AND_USE;

      // a sibling module or parent module
      config |= LOOKUP_PARENTS;
    }
    if (useOrImport == VIS_IMPORT) {
      // a top-level module name
      config |= LOOKUP_TOPLEVEL;

      // a module name in scope due to another use/import
      config |= LOOKUP_IMPORT_AND_USE;
    }
  } else {
    // if it's not the first part, look in the scope for
    // declarations and use/import statements.
    config |= LOOKUP_IMPORT_AND_USE;
    config |= LOOKUP_DECLS;
  }

  bool got = doLookupInScope(context, scope, resolving,
                             name, config,
                             checkedScopes, result);

  return got;
}

std::vector<BorrowedIdsWithName> lookupNameInScope(Context* context,
                                                   const Scope* scope,
                                                   const Scope* receiverScope,
                                                   UniqueString name,
                                                   LookupConfig config) {
  std::unordered_set<const Scope*> checkedScopes;

  return lookupNameInScopeWithSet(context, scope, receiverScope, name,
                                  config, checkedScopes);
}

std::vector<BorrowedIdsWithName>
lookupNameInScopeWithSet(Context* context,
                         const Scope* scope,
                         const Scope* receiverScope,
                         UniqueString name,
                         LookupConfig config,
                         std::unordered_set<const Scope*>& visited) {
  std::vector<BorrowedIdsWithName> vec;

  if (receiverScope) {
    doLookupInScope(context, receiverScope,
                    /* resolving scope */ nullptr,
                    name, config, visited, vec);
  }

  doLookupInScope(context, scope,
                  /* resolving scope */ nullptr,
                  name, config, visited, vec);

  return vec;
}

static
bool doIsWholeScopeVisibleFromScope(Context* context,
                                   const Scope* checkScope,
                                   const Scope* fromScope,
                                   std::unordered_set<const Scope*>& checked) {

  auto pair = checked.insert(fromScope);
  if (pair.second == false) {
    // scope has already been visited by this function,
    // so don't try it again.
    return false;
  }

  // go through parent scopes checking for a match
  for (const Scope* cur = fromScope; cur != nullptr; cur = cur->parentScope()) {
    if (checkScope == cur) {
      return true;
    }

    if (cur->containsUseImport()) {
      const ResolvedVisibilityScope* r = resolveVisibilityStmts(context, cur);

      if (r != nullptr) {
        // check for scope containment
        for (const VisibilitySymbols& is: r->visibilityClauses()) {
          if (is.kind() == VisibilitySymbols::ALL_CONTENTS) {
            // find it in the contents
            const Scope* usedScope = is.scope();
            // check it recursively
            bool found = doIsWholeScopeVisibleFromScope(context,
                                                        checkScope,
                                                        usedScope,
                                                        checked);
            if (found) {
              return true;
            }
          }
        }
      }
    }
  }

  return false;
}

bool isWholeScopeVisibleFromScope(Context* context,
                                  const Scope* checkScope,
                                  const Scope* fromScope) {

  std::unordered_set<const Scope*> checked;

  return doIsWholeScopeVisibleFromScope(context,
                                        checkScope,
                                        fromScope,
                                        checked);
}

static void errorIfNameNotInScope(Context* context,
                                  const Scope* scope,
                                  const ResolvedVisibilityScope* resolving,
                                  UniqueString name,
                                  ID idForErr,
                                  VisibilityStmtKind useOrImport) {
  std::unordered_set<const Scope*> checkedScopes;
  std::vector<BorrowedIdsWithName> result;
  LookupConfig config = LOOKUP_INNERMOST |
                        LOOKUP_DECLS |
                        LOOKUP_IMPORT_AND_USE;
  bool got = doLookupInScope(context, scope, resolving,
                             name, config,
                             checkedScopes, result);

  if (got == false || result.size() == 0) {
    if (useOrImport == VIS_USE) {
      context->error(idForErr, "could not find '%s' for 'use'", name.c_str());
    } else {
      context->error(idForErr, "could not find '%s' for 'import'",
                     name.c_str());
    }
  }
}

static void
errorIfAnyLimitationNotInScope(Context* context,
                               const VisibilityClause* clause,
                               const Scope* scope,
                               const ResolvedVisibilityScope* resolving,
                               VisibilityStmtKind useOrImport) {
  for (const AstNode* e : clause->limitations()) {
    if (auto ident = e->toIdentifier()) {
      errorIfNameNotInScope(context, scope, resolving, ident->name(),
                            ident->id(), useOrImport);
    } else if (auto as = e->toAs()) {
      if (auto ident = as->symbol()->toIdentifier()) {
        errorIfNameNotInScope(context, scope, resolving, ident->name(),
                              ident->id(), useOrImport);
      }
    }
  }
}

static std::vector<std::pair<UniqueString,UniqueString>>
emptyNames() {
  std::vector<std::pair<UniqueString,UniqueString>> empty;
  return empty;
}

static std::vector<std::pair<UniqueString,UniqueString>>
convertOneName(UniqueString name) {
  std::vector<std::pair<UniqueString,UniqueString>> ret;
  ret.push_back(std::make_pair(name, name));
  return ret;
}

static std::vector<std::pair<UniqueString,UniqueString>>
convertOneRename(UniqueString oldName, UniqueString newName) {
  std::vector<std::pair<UniqueString,UniqueString>> ret;
  ret.push_back(std::make_pair(oldName, newName));
  return ret;
}

static std::vector<std::pair<UniqueString,UniqueString>>
convertLimitations(Context* context, const VisibilityClause* clause) {
  std::vector<std::pair<UniqueString,UniqueString>> ret;
  for (const AstNode* e : clause->limitations()) {
    if (auto ident = e->toIdentifier()) {
      UniqueString name = ident->name();
      ret.push_back(std::make_pair(name, name));
    } else if (auto dot = e->toDot()) {
      context->error(dot, "dot expression not supported here");
      continue;
    } else if (auto as = e->toAs()) {
      UniqueString name;
      UniqueString rename;
      auto s = as->symbol();
      if (auto symId = s->toIdentifier()) {
        name = symId->name();
      } else {
        context->error(s, "expression type not supported for 'as'");
        continue;
      }

      // Expect an identifier by construction.
      auto ident = as->rename()->toIdentifier();
      assert(ident);

      rename = ident->name();

      ret.push_back(std::make_pair(name, rename));
    }
  }
  return ret;
}

// Returns the Scope for something use/imported.
// This routine exists to support Dot expressions
// but it just takes in a name. The passed id is only used to
// anchor errors.
// 'isFirstPart' is true for A in A.B.C but not for B or C.
// Returns nullptr in the event of an error.
static const Scope* findScopeViz(Context* context,
                                 const Scope* scope,
                                 UniqueString nameInScope,
                                 const ResolvedVisibilityScope* resolving,
                                 ID idForErrs,
                                 VisibilityStmtKind useOrImport,
                                 bool isFirstPart) {

  // lookup 'field' in that scope
  std::vector<BorrowedIdsWithName> vec;
  bool got = lookupInScopeViz(context, scope, resolving,
                              nameInScope, useOrImport, isFirstPart, vec);

  if (got == false || vec.size() == 0) {
    if (useOrImport == VIS_USE)
      context->error(idForErrs, "could not find target of 'use'");
    else
      context->error(idForErrs, "could not find target of 'import'");

    return nullptr;

  } else if (vec.size() > 1 || vec[0].numIds() > 1) {
    if (useOrImport == VIS_USE)
      context->error(idForErrs, "ambiguity in finding target of 'use'");
    else
      context->error(idForErrs, "ambiguity in finding target of 'import'");

    return nullptr;
  }

  ID foundId = vec[0].id(0);
  AstTag tag = parsing::idToTag(context, foundId);
  if (isModule(tag) || isInclude(tag) ||
      (useOrImport == VIS_USE && isEnum(tag))) {
    return scopeForModule(context, foundId);
  }

  context->error(idForErrs, "does not refer to a module");
  return nullptr;
}

// Handle this/super and submodules
// e.g. M.N.S is represented as
//   Dot( Dot(M, N), S)
// Returns in foundName the final name in a Dot expression, e.g. S in the above
static const Scope*
findUseImportTarget(Context* context,
                    const Scope* scope,
                    const ResolvedVisibilityScope* resolving,
                    const AstNode* expr,
                    VisibilityStmtKind useOrImport,
                    UniqueString& foundName) {
  if (auto ident = expr->toIdentifier()) {
    foundName = ident->name();
    if (ident->name() == USTR("super")) {
      return scope->parentScope()->moduleScope();
    } else if (ident->name() == USTR("this")) {
      return scope->moduleScope();
    } else {
      return findScopeViz(context, scope, ident->name(), resolving,
                          expr->id(), useOrImport, /* isFirstPart */ true);
    }
  } else if (auto dot = expr->toDot()) {
    UniqueString ignoredFoundName;
    const Scope* innerScope = findUseImportTarget(context, scope, resolving,
                                                  dot->receiver(), useOrImport,
                                                  ignoredFoundName);
    if (innerScope != nullptr) {
      UniqueString nameInScope = dot->field();
      // find nameInScope in innerScope
      foundName = nameInScope;
      return findScopeViz(context, innerScope, nameInScope, resolving,
                          expr->id(), useOrImport, /* isFirstPart */ false);
    }
  } else {
    assert(false && "case not handled");
  }

  return nullptr;
}


static void
doResolveUseStmt(Context* context, const Use* use,
                 const Scope* scope,
                 ResolvedVisibilityScope* r) {
  bool isPrivate = true;
  if (use->visibility() == Decl::PUBLIC)
    isPrivate = false;

  for (auto clause : use->visibilityClauses()) {
    // Figure out what was use'd
    const AstNode* expr = clause->symbol();
    UniqueString oldName;
    UniqueString newName;

    if (auto as = expr->toAs()) {
      auto newIdent = as->rename()->toIdentifier();
      if (newIdent != nullptr) {
        // search for the original name
        expr = as->symbol();
        newName = newIdent->name();
      } else {
        context->error(expr, "this form of as is not yet supported");
        continue; // move on to the next visibility clause
      }
    }

    const Scope* foundScope = findUseImportTarget(context, scope, r,
                                                  expr, VIS_USE, oldName);
    if (foundScope != nullptr) {
      // First, add VisibilitySymbols entry for the symbol itself
      if (newName.isEmpty()) {
        r->addVisibilityClause(foundScope, VisibilitySymbols::SYMBOL_ONLY,
                               isPrivate, convertOneName(oldName));
      } else {
        r->addVisibilityClause(foundScope, VisibilitySymbols::SYMBOL_ONLY,
                               isPrivate, convertOneRename(oldName, newName));
      }

      // Then, add the entries for anything imported
      VisibilitySymbols::Kind kind = VisibilitySymbols::ALL_CONTENTS;
      switch (clause->limitationKind()) {
        case VisibilityClause::EXCEPT:
          kind = VisibilitySymbols::CONTENTS_EXCEPT;
          // check that we do not have 'except A as B'
          for (const AstNode* e : clause->limitations()) {
            if (!e->isIdentifier()) {
              context->error(e, "'as' cannot be used with 'except'");
            }
          }
          // add the visibility clause for only/except
          r->addVisibilityClause(foundScope, kind, isPrivate,
                                 convertLimitations(context, clause));
          break;
        case VisibilityClause::ONLY:
          kind = VisibilitySymbols::ONLY_CONTENTS;
          // check that symbols named with 'only' actually exist
          errorIfAnyLimitationNotInScope(context, clause, foundScope,
                                         r, VIS_USE);
          // add the visibility clause for only/except
          r->addVisibilityClause(foundScope, kind, isPrivate,
                                 convertLimitations(context, clause));
          break;
        case VisibilityClause::NONE:
          kind = VisibilitySymbols::ALL_CONTENTS;
          r->addVisibilityClause(foundScope, kind, isPrivate, emptyNames());
          break;
        case VisibilityClause::BRACES:
          assert(false && "Should not be possible");
          break;
      }
    }
  }
}

static void
doResolveImportStmt(Context* context, const Import* imp,
                    const Scope* scope,
                    ResolvedVisibilityScope* r) {
  bool isPrivate = true;
  if (imp->visibility() == Decl::PUBLIC)
    isPrivate = false;

  for (auto clause : imp->visibilityClauses()) {
    // Figure out what was imported
    const AstNode* expr = clause->symbol();
    UniqueString oldName;
    UniqueString newName;
    UniqueString dotName;

    if (auto as = expr->toAs()) {
      auto newIdent = as->rename()->toIdentifier();
      if (newIdent != nullptr) {
        // search for the original name
        expr = as->symbol();
        newName = newIdent->name();
      } else {
        context->error(expr, "this form of as is not yet supported");
        continue; // move on to the next visibility clause
      }
    }

    // For import, because 'import M.f' should handle the case that 'f'
    // is an overloaded function, we handle the outermost Dot expression
    // here instead of using findUseImportTarget on it (which would insist
    // on it matching just one thing).
    // But, we don't do that for 'import M.f.{a,b,c}'
    if (auto dot = expr->toDot()) {
      if (clause->limitationKind() != VisibilityClause::BRACES) {
        expr = dot->receiver();
        dotName = dot->field();
      }
    }

    const Scope* foundScope = findUseImportTarget(context, scope, r,
                                                  expr, VIS_IMPORT, oldName);
    if (foundScope != nullptr) {
      VisibilitySymbols::Kind kind = VisibilitySymbols::ONLY_CONTENTS;

      if (!dotName.isEmpty()) {
        // e.g. 'import M.f' - dotName is f and foundScope is for M
        // Note that 'f' could refer to multiple symbols in the case
        // of an overloaded function.
        switch (clause->limitationKind()) {
          case VisibilityClause::EXCEPT:
          case VisibilityClause::ONLY:
            assert(false && "Should not be possible");
            break;
          case VisibilityClause::NONE:
            kind = VisibilitySymbols::ONLY_CONTENTS;
            errorIfNameNotInScope(context, foundScope, r,
                                  dotName, clause->id(), VIS_IMPORT);
            if (newName.isEmpty()) {
              // e.g. 'import M.f'
              r->addVisibilityClause(foundScope, kind, isPrivate,
                                     convertOneName(dotName));
            } else {
              // e.g. 'import M.f as g'
              r->addVisibilityClause(foundScope, kind, isPrivate,
                                     convertOneRename(dotName, newName));
            }
            break;
          case VisibilityClause::BRACES:
            // this case should be ruled out above
            // (dotName should not be set)
            assert(false && "should not be reachable");
            break;
        }
      } else {
        // e.g. 'import OtherModule'
        switch (clause->limitationKind()) {
          case VisibilityClause::EXCEPT:
          case VisibilityClause::ONLY:
            assert(false && "Should not be possible");
            break;
          case VisibilityClause::NONE:
            kind = VisibilitySymbols::SYMBOL_ONLY;
            if (newName.isEmpty()) {
              // e.g. 'import OtherModule'
              r->addVisibilityClause(foundScope, kind, isPrivate,
                                     convertOneName(oldName));
            } else {
              // e.g. 'import OtherModule as Foo'
              r->addVisibilityClause(foundScope, kind, isPrivate,
                                     convertOneRename(oldName, newName));
            }
            break;
          case VisibilityClause::BRACES:
            // e.g. 'import OtherModule.{a,b,c}'
            kind = VisibilitySymbols::ONLY_CONTENTS;
            // check that symbols named in the braces actually exist
            errorIfAnyLimitationNotInScope(context, clause, foundScope,
                                           r, VIS_IMPORT);

            // add the visibility clause with the named symbols
            r->addVisibilityClause(foundScope, kind, isPrivate,
                                   convertLimitations(context, clause));
            break;
        }
      }
    }
  }
}

static void
doResolveVisibilityStmt(Context* context,
                        const AstNode* ast,
                        ResolvedVisibilityScope* r) {
  if (ast != nullptr) {
    if (ast->isUse() || ast->isImport()) {
      // figure out the scope of the use/import
      const Scope* scope = scopeForIdQuery(context, ast->id());

      if (const Use* use = ast->toUse()) {
        doResolveUseStmt(context, use, scope, r);
        return;
      } else if (const Import* imp = ast->toImport()) {
        doResolveImportStmt(context, imp, scope, r);
        return;
      }
    }
  }

  // this code should never run
  assert(false && "should not be reached");
}

static
const owned<ResolvedVisibilityScope>& resolveVisibilityStmtsQuery(
                                                      Context* context,
                                                      const Scope* scope)
{
  QUERY_BEGIN(resolveVisibilityStmtsQuery, context, scope);

  owned<ResolvedVisibilityScope> result;
  const AstNode* ast = parsing::idToAst(context, scope->id());
  assert(ast != nullptr);
  if (ast != nullptr) {
    result = toOwned(new ResolvedVisibilityScope(scope));
    auto r = result.get();
    // Visit child nodes to find use/import statements therein
    for (const AstNode* child : ast->children()) {
      if (child->isUse() || child->isImport()) {
        doResolveVisibilityStmt(context, child, r);
      }
    }
  }

  return QUERY_END(result);
}

static const ResolvedVisibilityScope*
resolveVisibilityStmts(Context* context, const Scope* scope) {
  if (!scope->containsUseImport()) {
    // stop early if this scope has no use/import statements
    return nullptr;
  }

  if (context->isQueryRunning(resolveVisibilityStmtsQuery,
                              std::make_tuple(scope))) {
    // ignore use/imports if we are currently resolving uses/imports
    // for this scope
    return nullptr;
  }

  const owned<ResolvedVisibilityScope>& r =
    resolveVisibilityStmtsQuery(context, scope);
  return r.get();
}


static
const owned<PoiScope>& constructPoiScopeQuery(Context* context,
                                              const Scope* scope,
                                              const PoiScope* parentPoiScope) {
  QUERY_BEGIN(constructPoiScopeQuery, context, scope, parentPoiScope);

  owned<PoiScope> result = toOwned(new PoiScope(scope, parentPoiScope));

  return QUERY_END(result);
}

static const PoiScope* const&
pointOfInstantiationScopeQuery(Context* context,
                               const Scope* scope,
                               const PoiScope* parentPoiScope) {
  QUERY_BEGIN(pointOfInstantiationScopeQuery, context, scope, parentPoiScope);

  // figure out which POI scope to create.
  const Scope* useScope = nullptr;
  const PoiScope* usePoi = nullptr;

  // Scopes that do not contain function declarations or use/import
  // thereof can be collapsed away.
  for (useScope = scope;
       useScope != nullptr;
       useScope = useScope->parentScope()) {
    if (useScope->containsUseImport() || useScope->containsFunctionDecls()) {
      break;
    }
  }

  // PoiScopes do not need to consider scopes that are visible from
  // the call site itself. These can be collapsed away.
  for (usePoi = parentPoiScope;
       usePoi != nullptr;
       usePoi = usePoi->inFnPoi()) {

    bool collapse = isWholeScopeVisibleFromScope(context,
                                                 usePoi->inScope(),
                                                 scope);
    if (collapse == false) {
      break;
    }
  }

  // get the poi scope for scope+usePoi
  const owned<PoiScope>& ps = constructPoiScopeQuery(context, useScope, usePoi);
  const PoiScope* result = ps.get();

  return QUERY_END(result);
}

const PoiScope* pointOfInstantiationScope(Context* context,
                                          const Scope* scope,
                                          const PoiScope* parentPoiScope) {
  return pointOfInstantiationScopeQuery(context, scope, parentPoiScope);
}

const InnermostMatch& findInnermostDecl(Context* context,
                                     const Scope* scope,
                                     UniqueString name)
{
  QUERY_BEGIN(findInnermostDecl, context, scope, name);

  ID id;
  InnermostMatch::MatchesFound count = InnermostMatch::ZERO;

  LookupConfig config = LOOKUP_DECLS |
                        LOOKUP_IMPORT_AND_USE |
                        LOOKUP_PARENTS |
                        LOOKUP_INNERMOST;

  std::vector<BorrowedIdsWithName> vec =
    lookupNameInScope(context, scope,
                      /* receiver scope */ nullptr,
                      name, config);

  if (vec.size() > 0) {
    const BorrowedIdsWithName& r = vec[0];
    if (r.numIds() > 1)
      count = InnermostMatch::MANY;
    else
      count = InnermostMatch::ONE;

    id = r.id(0);
  }

  auto result = InnermostMatch(id, count);
  return QUERY_END(result);
}

const Scope* scopeForModule(Context* context, ID id) {
  return scopeForId(context, id);
}


} // end namespace resolution
} // end namespace chpl
