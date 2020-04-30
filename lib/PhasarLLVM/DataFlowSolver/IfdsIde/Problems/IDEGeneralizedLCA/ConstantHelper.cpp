/******************************************************************************
 * Copyright (c) 2020 Fabian Schiebel.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Fabian Schiebel and others
 *****************************************************************************/

#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Value.h"

#include "phasar/PhasarLLVM/DataFlowSolver/IfdsIde/Problems/IDEGeneralizedLCA/ConstantHelper.h"

namespace psr {

bool isConstant(const llvm::Value *val) {
  // is constantInt, constantFP or constant string
  if (llvm::isa<llvm::ConstantInt>(val)) // const int
    return true;
  if (llvm::isa<llvm::ConstantFP>(val)) // const fp
    return true;
  if (llvm::isa<llvm::ConstantPointerNull>(val)) // NULL
    return true;
  if (auto gep = llvm::dyn_cast<llvm::ConstantExpr>(val);
      gep && val->getType()->isPointerTy() &&
      val->getType()->getPointerElementType()->isIntegerTy()) {
    // const string
    // val isa GEP
    auto op1 = gep->getOperand(0); // op1 is pointer-operand
    if (auto glob = llvm::dyn_cast<llvm::GlobalVariable>(op1);
        glob && glob->hasInitializer()) {
      if (auto cdat =
              llvm::dyn_cast<llvm::ConstantDataArray>(glob->getInitializer())) {
        return true; // it is definitely a const string
      }
    }
  }
  return false;
}

} // namespace psr