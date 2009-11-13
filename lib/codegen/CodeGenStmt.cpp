//===-- codege/CodeGenStmt.cpp -------------------------------- -*- C++ -*-===//
//
// This file is distributed under the MIT license. See LICENSE.txt for details.
//
// Copyright (C) 2009, Stephen Wilson
//
//===----------------------------------------------------------------------===//

#include "CodeGenRoutine.h"
#include "CodeGenTypes.h"
#include "CommaRT.h"
#include "comma/ast/Decl.h"
#include "comma/ast/Expr.h"
#include "comma/ast/Pragma.h"
#include "comma/ast/Stmt.h"

using namespace comma;

using llvm::dyn_cast;
using llvm::cast;
using llvm::isa;

void CodeGenRoutine::emitStmt(Stmt *stmt)
{
    switch (stmt->getKind()) {

    default:
        assert(false && "Cannot codegen stmt yet!");

    case Ast::AST_ProcedureCallStmt:
        emitProcedureCallStmt(cast<ProcedureCallStmt>(stmt));
        break;

    case Ast::AST_AssignmentStmt:
        emitAssignmentStmt(cast<AssignmentStmt>(stmt));
        break;

    case Ast::AST_StmtSequence:
        emitStmtSequence(cast<StmtSequence>(stmt));
        break;

    case Ast::AST_BlockStmt:
        emitBlockStmt(cast<BlockStmt>(stmt));
        break;

    case Ast::AST_IfStmt:
        emitIfStmt(cast<IfStmt>(stmt));
        break;

    case Ast::AST_WhileStmt:
        emitWhileStmt(cast<WhileStmt>(stmt));
        break;

    case Ast::AST_ReturnStmt:
        emitReturnStmt(cast<ReturnStmt>(stmt));
        break;

    case Ast::AST_PragmaStmt:
        emitPragmaStmt(cast<PragmaStmt>(stmt));
        break;
    }
}

void CodeGenRoutine::emitReturnStmt(ReturnStmt *ret)
{
    if (!ret->hasReturnExpr()) {
        // We should be emitting for a procedure.  Simply branch to the return
        // block.
        assert(returnValue == 0 && "Empty return from function!");
        Builder.CreateBr(returnBB);
        return;
    }

    FunctionDecl *fdecl = cast<FunctionDecl>(srCompletion);
    FunctionType *fTy = fdecl->getType();
    Type *targetTy = fTy->getReturnType();
    Expr *expr = ret->getReturnExpr();

    if (ArrayType *arrTy = dyn_cast<ArrayType>(targetTy)) {
        // Currently, only statically constrained arrays are supported as return
        // values.
        assert(arrTy->isStaticallyConstrained());

        // The return slot corresponds to an sret return parameter.  Simply
        // evaluate the return value into this slot.
        if (FunctionCallExpr *call = dyn_cast<FunctionCallExpr>(expr))
            emitCompositeCall(call, returnValue);
        else
            emitArrayExpr(expr, returnValue, false);
    }
    else {
        // Non-composite return values can be simply evaluated and stored into
        // this subroutines return slot.
        llvm::Value *res = emitValue(ret->getReturnExpr());
        Builder.CreateStore(res, returnValue);
    }

    // Branch to the return block.
    Builder.CreateBr(returnBB);
}

void CodeGenRoutine::emitStmtSequence(StmtSequence *seq)
{
    for (StmtSequence::StmtIter iter = seq->beginStatements();
         iter != seq->endStatements(); ++iter)
        emitStmt(*iter);
}

llvm::BasicBlock *CodeGenRoutine::emitBlockStmt(BlockStmt *block,
                                                llvm::BasicBlock *predecessor)
{
    assert(block && "NULL block statement!");
    std::string label;

    if (block->hasLabel())
        label = block->getLabel()->getString();

    llvm::BasicBlock *BB = makeBasicBlock(label);

    if (predecessor == 0) {
        // If we are not supplied a predecessor, terminate the current
        // insertion block with a direct branch to the new one.
        predecessor = Builder.GetInsertBlock();

        assert(!predecessor->getTerminator() &&
               "Current insertion block already terminated!");
        Builder.CreateBr(BB);
    }

    BB->moveAfter(predecessor);
    Builder.SetInsertPoint(BB);

    // Generate any object declarations provided by this block, followed by the
    // blocks sequence of statements.
    typedef DeclRegion::DeclIter iterator;
    for (iterator I = block->beginDecls(); I != block->endDecls(); ++I)
        if (ObjectDecl *objDecl = dyn_cast<ObjectDecl>(*I))
            emitObjectDecl(objDecl);
    emitStmtSequence(block);
    return BB;
}

void CodeGenRoutine::emitIfStmt(IfStmt *ite)
{
    llvm::Value *condition = emitValue(ite->getCondition());
    llvm::BasicBlock *thenBB = makeBasicBlock("then");
    llvm::BasicBlock *mergeBB = makeBasicBlock("merge");
    llvm::BasicBlock *elseBB;

    if (ite->hasElsif())
        elseBB = makeBasicBlock("elsif");
    else if (ite->hasAlternate())
        elseBB = makeBasicBlock("else");
    else
        elseBB = mergeBB;

    Builder.CreateCondBr(condition, thenBB, elseBB);
    Builder.SetInsertPoint(thenBB);
    emitStmt(ite->getConsequent());

    // If generation of the consequent did not result in a terminator, create a
    // branch to the merge block.
    if (!Builder.GetInsertBlock()->getTerminator())
        Builder.CreateBr(mergeBB);

    for (IfStmt::iterator I = ite->beginElsif(); I != ite->endElsif(); ++I) {
        Builder.SetInsertPoint(elseBB);
        IfStmt::iterator J = I;
        if (++J != ite->endElsif())
            elseBB = makeBasicBlock("elsif");
        else if (ite->hasAlternate())
            elseBB = makeBasicBlock("else");
        else
            elseBB = mergeBB;
        llvm::BasicBlock *bodyBB = makeBasicBlock("body");
        llvm::Value *pred = emitValue(I->getCondition());
        Builder.CreateCondBr(pred, bodyBB, elseBB);
        Builder.SetInsertPoint(bodyBB);
        emitStmt(I->getConsequent());
        if (!Builder.GetInsertBlock()->getTerminator())
            Builder.CreateBr(mergeBB);
    }

    if (ite->hasAlternate()) {
        Builder.SetInsertPoint(elseBB);
        emitStmt(ite->getAlternate());
        if (!Builder.GetInsertBlock()->getTerminator())
            Builder.CreateBr(mergeBB);
    }

    Builder.SetInsertPoint(mergeBB);
}

void CodeGenRoutine::emitAssignmentStmt(AssignmentStmt *stmt)
{
    Expr *rhs = stmt->getAssignedExpr();

    if (DeclRefExpr *ref = dyn_cast<DeclRefExpr>(stmt->getTarget())) {
        Type *refTy = ref->getType();

        if (isa<ArrayType>(refTy)) {
            // Evaluate the rhs into the storage provided by the lhs.
            llvm::Value *dst = lookupDecl(ref->getDeclaration());
            if (FunctionCallExpr *call = dyn_cast<FunctionCallExpr>(rhs))
                emitCompositeCall(call, dst);
            else
                emitArrayExpr(rhs, dst, false);
        }
        else {
            // The left hand side is a simple variable reference.  Just emit the
            // left and right hand sides and form a store.
            llvm::Value *target = emitVariableReference(ref);
            llvm::Value *source = emitValue(rhs);
            Builder.CreateStore(source, target);
        }
    }
    else {
        // Otherwise, the target must be an IndexedArrayExpr.  Get a reference
        // to the needed component and again, store in the right hand side.
        IndexedArrayExpr *arrIdx = cast<IndexedArrayExpr>(stmt->getTarget());
        llvm::Value *target = emitIndexedArrayRef(arrIdx);
        llvm::Value *source = emitValue(rhs);
        Builder.CreateStore(source, target);
    }
}

void CodeGenRoutine::emitWhileStmt(WhileStmt *stmt)
{
    llvm::BasicBlock *entryBB = makeBasicBlock("while.top");
    llvm::BasicBlock *bodyBB = makeBasicBlock("while.entry");
    llvm::BasicBlock *mergeBB = makeBasicBlock("while.merge");

    // Branch unconditionally from the current insertion point to the entry
    // block and set up our new context.  Emit the loop condition into the entry
    // block.
    Builder.CreateBr(entryBB);
    Builder.SetInsertPoint(entryBB);
    llvm::Value *condition = emitValue(stmt->getCondition());

    // Branch into bodyBB if condition evaluates to true, mergeBB otherwise.
    Builder.CreateCondBr(condition, bodyBB, mergeBB);

    // Generate the body.
    Builder.SetInsertPoint(bodyBB);
    emitStmt(stmt->getBody());

    // If the current insertion point does not have a terminator,
    // unconditionally branch to entryBB.
    if (!Builder.GetInsertBlock()->getTerminator())
        Builder.CreateBr(entryBB);

    // Finally, set mergeBB the current insertion point.
    Builder.SetInsertPoint(mergeBB);
}

void CodeGenRoutine::emitPragmaStmt(PragmaStmt *stmt)
{
    // Currently, only pragma Assert is supported.
    Pragma *pragma = stmt->getPragma();

    switch (pragma->getKind()) {

    default:
        assert(false && "Cannot codegen pragma yet!");
        break;

    case pragma::Assert:
        emitPragmaAssert(cast<PragmaAssert>(pragma));
        break;
    };
}
