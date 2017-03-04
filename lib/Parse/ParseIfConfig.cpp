//===--- ParseIfConfig.cpp - Swift Language Parser for #if directives -----===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Conditional Compilation Block Parsing and AST Building
//
//===----------------------------------------------------------------------===//

#include "swift/Parse/Parser.h"
#include "swift/Basic/Defer.h"
#include "swift/Basic/LangOptions.h"
#include "swift/Basic/Version.h"
#include "swift/Parse/Lexer.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace swift;

/// Parse and populate a list of #if/#elseif/#else/#endif clauses.
/// Delegate callback function to parse elements in the blocks.
template <typename ElemTy, unsigned N>
static ParserStatus parseIfConfig(
    Parser &P, SmallVectorImpl<IfConfigClause<ElemTy>> &Clauses,
    SourceLoc &EndLoc, bool HadMissingEnd,
    llvm::function_ref<void(SmallVectorImpl<ElemTy> &, bool)> parseElements) {
  Parser::StructureMarkerRAII ParsingDecl(
      P, P.Tok.getLoc(), Parser::StructureMarkerKind::IfConfig);

  bool foundActive = false;
  ConditionalCompilationExprState ConfigState;
  while (1) {
    bool isElse = P.Tok.is(tok::pound_else);
    SourceLoc ClauseLoc = P.consumeToken();
    Expr *Condition = nullptr;

    // Parse and evaluate the directive.
    if (isElse) {
      ConfigState.setConditionActive(!foundActive);
    } else {
      llvm::SaveAndRestore<bool> S(P.InPoundIfEnvironment, true);
      ParserResult<Expr> Result = P.parseExprSequence(diag::expected_expr,
                                                      /*isBasic*/true,
                                                      /*isForDirective*/true);
      if (Result.isNull())
        return makeParserError();

      Condition = Result.get();

      // Evaluate the condition, to validate it.
      ConfigState = P.classifyConditionalCompilationExpr(Condition, P.Context,
                                                         P.Diags);
      if (foundActive)
        ConfigState.setConditionActive(false);
    }

    foundActive |= ConfigState.isConditionActive();

    if (!P.Tok.isAtStartOfLine() && P.Tok.isNot(tok::eof)) {
      P.diagnose(P.Tok.getLoc(),
                 diag::extra_tokens_conditional_compilation_directive);
    }

    // Parse elements
    SmallVector<ElemTy, N> Elements;
    if (ConfigState.shouldParse()) {
      parseElements(Elements, ConfigState.isConditionActive());
    } else {
      DiagnosticTransaction DT(P.Diags);
      P.skipUntilConditionalBlockClose();
      DT.abort();
    }

    Clauses.push_back(IfConfigClause<ElemTy>(ClauseLoc, Condition,
                                             P.Context.AllocateCopy(Elements),
                                             ConfigState.isConditionActive()));

    if (P.Tok.isNot(tok::pound_elseif, tok::pound_else))
      break;

    if (isElse)
      P.diagnose(P.Tok, diag::expected_close_after_else_directive);
  }

  HadMissingEnd = P.parseEndIfDirective(EndLoc);
  return makeParserSuccess();
}

ParserResult<IfConfigDecl> Parser::parseDeclIfConfig(ParseDeclOptions Flags) {
  SmallVector<IfConfigClause<Decl *>, 4> Clauses;
  SourceLoc EndLoc;
  bool HadMissingEnd = false;
  auto Status = parseIfConfig<Decl *, 8>(
      *this, Clauses, EndLoc, HadMissingEnd,
      [&](SmallVectorImpl<Decl *> &Decls, bool IsActive) {
    Optional<Scope> scope;
    if (!IsActive)
      scope.emplace(this, getScopeInfo().getCurrentScope()->getKind(),
                    /*inactiveConfigBlock=*/true);

    ParserStatus Status;
    bool PreviousHadSemi = true;
    while (Tok.isNot(tok::pound_else, tok::pound_endif, tok::pound_elseif,
                      tok::eof)) {
      if (Tok.is(tok::r_brace)) {
        diagnose(Tok.getLoc(),
                  diag::unexpected_rbrace_in_conditional_compilation_block);
        // If we see '}', following declarations don't look like belong to
        // the current decl context; skip them.
        skipUntilConditionalBlockClose();
        break;
      }
      Status |= parseDeclItem(PreviousHadSemi, Flags,
                              [&](Decl *D) {Decls.push_back(D);});
    }
  });
  if (Status.isError())
    return makeParserErrorResult<IfConfigDecl>();

  IfConfigDecl *ICD = new (Context) IfConfigDecl(CurDeclContext,
                                                 Context.AllocateCopy(Clauses),
                                                 EndLoc, HadMissingEnd);
  return makeParserResult(ICD);
}

ParserResult<Stmt> Parser::parseStmtIfConfig(BraceItemListKind Kind) {
  SmallVector<IfConfigClause<ASTNode>, 4> Clauses;
  SourceLoc EndLoc;
  bool HadMissingEnd = false;
  auto Status = parseIfConfig<ASTNode, 16>(
      *this, Clauses, EndLoc, HadMissingEnd,
      [&](SmallVectorImpl<ASTNode> &Elements, bool IsActive) {
    parseBraceItems(Elements, Kind, IsActive
                      ? BraceItemListKind::ActiveConditionalBlock
                      : BraceItemListKind::InactiveConditionalBlock);
  });
  if (Status.isError())
    return makeParserErrorResult<Stmt>();

  auto *ICS = new (Context) IfConfigStmt(Context.AllocateCopy(Clauses),
                                         EndLoc, HadMissingEnd);
  return makeParserResult(ICS);
}

// Evaluate a subset of expression types suitable for build configuration
// conditional expressions.  The accepted expression types are:
//  - The magic constants "true" and "false".
//  - Named decl ref expressions ("FOO")
//  - Parenthesized expressions ("(FOO)")
//  - Binary "&&" or "||" operations applied to other build configuration
//    conditional expressions
//  - Unary "!" expressions applied to other build configuration conditional
//    expressions
//  - Single-argument call expressions, where the function being invoked is a
//    supported target configuration (currently "os", "arch", and
//    "_compiler_version"), and whose argument is a named decl ref expression
ConditionalCompilationExprState
Parser::classifyConditionalCompilationExpr(Expr *condition,
                                           ASTContext &Context,
                                           DiagnosticEngine &D) {
  assert(condition && "Cannot classify a NULL condition expression!");

  // Evaluate a ParenExpr.
  if (auto *PE = dyn_cast<ParenExpr>(condition))
    return classifyConditionalCompilationExpr(PE->getSubExpr(), Context, D);

  // Evaluate a "&&" or "||" expression.
  if (auto *SE = dyn_cast<SequenceExpr>(condition)) {
    // Check for '&&' or '||' as the expression type.
    if (SE->getNumElements() < 3) {
      D.diagnose(SE->getLoc(),
                 diag::unsupported_conditional_compilation_binary_expression);
      return ConditionalCompilationExprState::error();
    }
    // Before type checking, chains of binary expressions will not be fully
    // parsed, so associativity has not yet been encoded in the subtree.
    auto elements = SE->getElements();
    auto numElements = SE->getNumElements();
    size_t iOperator = 1;
    size_t iOperand = 2;

    auto result = classifyConditionalCompilationExpr(elements[0], Context, D);

    while (iOperand < numElements) {

      if (auto *UDREOp = dyn_cast<UnresolvedDeclRefExpr>(elements[iOperator])) {
        auto name = UDREOp->getName().getBaseName().str();

        if (name.equals("||") || name.equals("&&")) {
          auto rhs = classifyConditionalCompilationExpr(elements[iOperand],
                                                        Context, D);

          if (name.equals("||")) {
            result = result || rhs;
            if (result.isConditionActive())
              break;
          }

          if (name.equals("&&")) {
            result = result && rhs;
            if (!result.isConditionActive())
              break;
          }
        } else {
          D.diagnose(
              SE->getLoc(),
              diag::unsupported_conditional_compilation_binary_expression);
          return ConditionalCompilationExprState::error();
        }
      } else {
        // Swift3 didn't have this branch. the operator and the RHS are
        // silently ignored.
        if (!Context.isSwiftVersion3()) {
          D.diagnose(
              elements[iOperator]->getLoc(),
              diag::unsupported_conditional_compilation_expression_type);
          return ConditionalCompilationExprState::error();
        } else {
          SourceRange ignoredRange(elements[iOperator]->getLoc(),
                                   elements[iOperand]->getEndLoc());
          D.diagnose(
              elements[iOperator]->getLoc(),
              diag::swift3_unsupported_conditional_compilation_expression_type)
            .highlight(ignoredRange);
        }
      }

      iOperator += 2;
      iOperand += 2;
    }

    return result;
  }

  // Evaluate a named reference expression.
  if (auto *UDRE = dyn_cast<UnresolvedDeclRefExpr>(condition)) {
    auto name = UDRE->getName().getBaseName().str();
    return {Context.LangOpts.isCustomConditionalCompilationFlagSet(name),
      ConditionalCompilationExprKind::DeclRef};
  }

  // Evaluate a Boolean literal.
  if (auto *boolLit = dyn_cast<BooleanLiteralExpr>(condition)) {
    return {boolLit->getValue(), ConditionalCompilationExprKind::Boolean};
  }

  // Evaluate a negation (unary "!") expression.
  if (auto *PUE = dyn_cast<PrefixUnaryExpr>(condition)) {
    // If the PUE is not a negation expression, return false
    auto name =
    cast<UnresolvedDeclRefExpr>(PUE->getFn())->getName().getBaseName().str();
    if (name != "!") {
      D.diagnose(PUE->getLoc(),
                 diag::unsupported_conditional_compilation_unary_expression);
      return ConditionalCompilationExprState::error();
    }

    return !classifyConditionalCompilationExpr(PUE->getArg(), Context, D);
  }

  // Evaluate a target config call expression.
  if (auto *CE = dyn_cast<CallExpr>(condition)) {
    // look up target config, and compare value
    auto fnNameExpr = dyn_cast<UnresolvedDeclRefExpr>(CE->getFn());

    // Get the arg, which should be in a paren expression.
    if (!fnNameExpr) {
      D.diagnose(CE->getLoc(), diag::unsupported_platform_condition_expression);
      return ConditionalCompilationExprState::error();
    }

    auto fnName = fnNameExpr->getName().getBaseName().str();

    auto *PE = dyn_cast<ParenExpr>(CE->getArg());
    if (!PE) {
      auto diag = D.diagnose(CE->getLoc(),
                             diag::platform_condition_expected_one_argument);
      return ConditionalCompilationExprState::error();
    }

    if (!fnName.equals("arch") && !fnName.equals("os") &&
        !fnName.equals("_endian") &&
        !fnName.equals("_runtime") &&
        !fnName.equals("swift") &&
        !fnName.equals("_compiler_version")) {
      D.diagnose(CE->getLoc(), diag::unsupported_platform_condition_expression);
      return ConditionalCompilationExprState::error();
    }

    if (fnName.equals("_compiler_version")) {
      if (auto SLE = dyn_cast<StringLiteralExpr>(PE->getSubExpr())) {
        if (SLE->getValue().empty()) {
          D.diagnose(CE->getLoc(), diag::empty_version_string);
          return ConditionalCompilationExprState::error();
        }
        auto versionRequirement =
        version::Version::parseCompilerVersionString(SLE->getValue(),
                                                     SLE->getLoc(),
                                                     &D);
        auto thisVersion = version::Version::getCurrentCompilerVersion();
        auto VersionNewEnough = thisVersion >= versionRequirement;
        return {VersionNewEnough,
          ConditionalCompilationExprKind::CompilerVersion};
      } else {
        D.diagnose(CE->getLoc(), diag::unsupported_platform_condition_argument,
                 "string literal");
        return ConditionalCompilationExprState::error();
      }
    } else if (fnName.equals("swift")) {
      auto PUE = dyn_cast<PrefixUnaryExpr>(PE->getSubExpr());
      if (!PUE) {
        D.diagnose(PE->getSubExpr()->getLoc(),
                   diag::unsupported_platform_condition_argument,
                   "a unary comparison, such as '>=2.2'");
        return ConditionalCompilationExprState::error();
      }

      auto prefix = dyn_cast<UnresolvedDeclRefExpr>(PUE->getFn());
      auto versionArg = PUE->getArg();
      auto versionStartLoc = versionArg->getStartLoc();
      auto endLoc = Lexer::getLocForEndOfToken(Context.SourceMgr,
                                               versionArg->getSourceRange().End);
      CharSourceRange versionCharRange(Context.SourceMgr, versionStartLoc,
                                       endLoc);
      auto versionString = Context.SourceMgr.extractText(versionCharRange);

      auto versionRequirement =
        version::Version::parseVersionString(versionString,
                                             versionStartLoc,
                                             &D);

      if (!versionRequirement.hasValue())
        return ConditionalCompilationExprState::error();

      auto thisVersion = Context.LangOpts.EffectiveLanguageVersion;

      if (!prefix->getName().getBaseName().str().equals(">=")) {
        D.diagnose(PUE->getFn()->getLoc(),
                   diag::unexpected_version_comparison_operator)
          .fixItReplace(PUE->getFn()->getLoc(), ">=");
        return ConditionalCompilationExprState::error();
      }

      auto VersionNewEnough = thisVersion >= versionRequirement.getValue();
      return {VersionNewEnough,
        ConditionalCompilationExprKind::LanguageVersion};
    } else {
      if (auto UDRE = dyn_cast<UnresolvedDeclRefExpr>(PE->getSubExpr())) {
        // The sub expression should be an UnresolvedDeclRefExpr (we won't
        // tolerate extra parens).
        auto argumentIdent = UDRE->getName().getBaseName();
        auto argument = argumentIdent.str();
        PlatformConditionKind Kind =
          llvm::StringSwitch<PlatformConditionKind>(fnName)
          .Case("os", PlatformConditionKind::OS)
          .Case("arch", PlatformConditionKind::Arch)
          .Case("_endian", PlatformConditionKind::Endianness)
          .Case("_runtime", PlatformConditionKind::Runtime);

        // FIXME: Perform the replacement macOS -> OSX elsewhere.
        if (Kind == PlatformConditionKind::OS && argument == "macOS")
          argument = "OSX";

        std::vector<StringRef> suggestions;
        if (!LangOptions::checkPlatformConditionSupported(Kind, argument,
                                                          suggestions)) {
          if (Kind == PlatformConditionKind::Runtime) {
            // Error for _runtime()
            D.diagnose(UDRE->getLoc(),
                       diag::unsupported_platform_runtime_condition_argument);
            return ConditionalCompilationExprState::error();
          }
          StringRef DiagName;
          switch (Kind) {
          case PlatformConditionKind::OS:
            DiagName = "operating system"; break;
          case PlatformConditionKind::Arch:
            DiagName = "architecture"; break;
          case PlatformConditionKind::Endianness:
            DiagName = "endianness"; break;
          case PlatformConditionKind::Runtime:
            llvm_unreachable("handled above");
          }
          D.diagnose(UDRE->getLoc(), diag::unknown_platform_condition_argument,
                     DiagName, fnName);
          for (const StringRef &suggestion : suggestions) {
            D.diagnose(UDRE->getLoc(), diag::note_typo_candidate, suggestion)
              .fixItReplace(UDRE->getSourceRange(), suggestion);
          }
        }

        auto target = Context.LangOpts.getPlatformConditionValue(Kind);
        return {target == argument, ConditionalCompilationExprKind::DeclRef};
      } else {
        D.diagnose(CE->getLoc(), diag::unsupported_platform_condition_argument,
                   "identifier");
        return ConditionalCompilationExprState::error();
      }
    }
  }

  // "#if 0" isn't valid, but it is common, so recognize it and handle it
  // with a fixit elegantly.
  if (auto *IL = dyn_cast<IntegerLiteralExpr>(condition))
    if (IL->getDigitsText() == "0" || IL->getDigitsText() == "1") {
      StringRef replacement = IL->getDigitsText() == "0" ? "false" :"true";
      D.diagnose(IL->getLoc(), diag::unsupported_conditional_compilation_integer,
                 IL->getDigitsText(), replacement)
       .fixItReplace(IL->getLoc(), replacement);
      return {IL->getDigitsText() == "1",
        ConditionalCompilationExprKind::Integer};
    }


  // If we've gotten here, it's an unsupported expression type.
  D.diagnose(condition->getLoc(),
             diag::unsupported_conditional_compilation_expression_type);
  return ConditionalCompilationExprState::error();
}
