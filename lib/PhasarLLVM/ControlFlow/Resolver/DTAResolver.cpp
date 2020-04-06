/******************************************************************************
 * Copyright (c) 2017 Philipp Schubert.
 * All rights reserved. This program and the accompanying materials are made
 * available under the terms of LICENSE.txt.
 *
 * Contributors:
 *     Philipp Schubert and others
 *****************************************************************************/

/*
 *  DTAResolver.cpp
 *
 *  Created on: 20.07.2018
 *      Author: nicolas bellec
 */

#include "llvm/IR/CallSite.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Module.h"

#include "phasar/PhasarLLVM/ControlFlow/Resolver/DTAResolver.h"
#include "phasar/PhasarLLVM/TypeHierarchy/LLVMTypeHierarchy.h"
#include "phasar/Utils/LLVMShorthands.h"
#include "phasar/Utils/Logger.h"
#include "phasar/Utils/Utilities.h"

using namespace std;
using namespace psr;

DTAResolver::DTAResolver(ProjectIRDB &IRDB, LLVMTypeHierarchy &TH)
    : CHAResolver(IRDB, TH) {}

bool DTAResolver::heuristic_anti_contructor_this_type(
    const llvm::BitCastInst *BitCast) {
  // We check if the caller is a constructor, and if the this argument has the
  // same type as the source type of the bitcast. If it is the case, it returns
  // false, true otherwise.

  if (auto Caller = BitCast->getFunction()) {
    if (isConstructor(Caller->getName().str())) {
      if (auto FuncTy = Caller->getFunctionType()) {
        if (auto ThisTy = FuncTy->getParamType(0)) {
          if (ThisTy == BitCast->getSrcTy()) {
            return false;
          }
        }
      }
    }
  }

  return true;
}

bool DTAResolver::heuristic_anti_contructor_vtable_pos(
    const llvm::BitCastInst *BitCast) {
  // Better heuristic than the previous one, can handle the CRTP. Based on the
  // previous one.

  if (heuristic_anti_contructor_this_type(BitCast))
    return true;

  // We know that we are in a constructor and the source type of the bitcast is
  // the same as the this argument. We then check where the bitcast is against
  // the store instruction of the vtable.
  auto StructTy = stripPointer(BitCast->getSrcTy());
  if (StructTy == nullptr)
    throw runtime_error("StructTy == nullptr in the heuristic_anti_contructor");

  // If it doesn't contain vtable, there is no reason to call this class in the
  // DTA graph, so no need to add it
  if (StructTy->isStructTy()) {
    if (Resolver::TH->hasVFTable(llvm::dyn_cast<llvm::StructType>(StructTy))) {
      return false;
    }
  }

  // So there is a vtable, the question is, where is it compared to the bitcast
  // instruction Carefull, there can be multiple vtable storage, we want to get
  // the last one vtable storage typically are : store i32 (...)** bitcast (i8**
  // getelementptr inbounds ({ [3 x i8*], [3 x i8*] }, { [3 x i8*], [3 x i8*] }*
  // @_ZTV2AA, i32 0, inrange i32 1, i32 3) to i32 (...)**), i32 (...)*** %17,
  // align 8
  // WARNING: May break when changing llvm version or using clang with version
  // > 5.0.1

  auto Caller = BitCast->getFunction();
  if (Caller == nullptr) {
    throw runtime_error("A bitcast instruction has no associated function");
  }

  int Idx = 0, VtableNum = 0, BitcastNum = 0;

  for (auto I = llvm::inst_begin(Caller), E = llvm::inst_end(Caller); I != E;
       ++I, ++Idx) {
    const auto &Inst = *I;

    if (auto Store = llvm::dyn_cast<llvm::StoreInst>(&Inst)) {
      // We got a store instruction, now we are checking if it is a vtable
      // storage
      if (auto BitcastExpr =
              llvm::dyn_cast<llvm::ConstantExpr>(Store->getValueOperand())) {
        if (BitcastExpr->isCast()) {
          if (auto ConstGep = llvm::dyn_cast<llvm::ConstantExpr>(
                  BitcastExpr->getOperand(0))) {
            auto GepAsInst = ConstGep->getAsInstruction();
            if (auto Gep = llvm::dyn_cast<llvm::GetElementPtrInst>(GepAsInst)) {
              if (auto Vtable = llvm::dyn_cast<llvm::Constant>(
                      Gep->getPointerOperand())) {
                // We can here assume that we found a vtable
                VtableNum = Idx;
              }
            }
            GepAsInst->deleteValue();
          }
        }
      }
    }

    if (&Inst == BitCast)
      BitcastNum = Idx;
  }

  return (BitcastNum > VtableNum);
}

void DTAResolver::otherInst(const llvm::Instruction *Inst) {
  if (auto BitCast = llvm::dyn_cast<llvm::BitCastInst>(Inst)) {
    // We add the connection between the two types in the DTA graph
    auto Src = BitCast->getSrcTy();
    auto Dest = BitCast->getDestTy();

    auto SrcStructType = llvm::dyn_cast<llvm::StructType>(stripPointer(Src));
    auto DestStructType = llvm::dyn_cast<llvm::StructType>(stripPointer(Dest));

    if (SrcStructType && DestStructType &&
        heuristic_anti_contructor_vtable_pos(BitCast))
      typegraph.addLink(DestStructType, SrcStructType);
  }
}

set<const llvm::Function *>
DTAResolver::resolveVirtualCall(llvm::ImmutableCallSite CS) {
  set<const llvm::Function *> PossibleCallTargets;
  auto &LG = lg::get();

  LOG_IF_ENABLE(BOOST_LOG_SEV(LG, DEBUG)
                << "Call virtual function: "
                << llvmIRToString(CS.getInstruction()));

  auto VtableIndex = getVFTIndex(CS);
  if (VtableIndex < 0) {
    // An error occured
    LOG_IF_ENABLE(BOOST_LOG_SEV(LG, DEBUG)
                  << "Error with resolveVirtualCall : impossible to retrieve "
                     "the vtable index\n"
                  << llvmIRToString(CS.getInstruction()) << "\n");
    return {};
  }

  LOG_IF_ENABLE(BOOST_LOG_SEV(LG, DEBUG)
                << "Virtual function table entry is: " << VtableIndex);

  auto ReceiverType = getReceiverType(CS);

  auto PossibleTypes = typegraph.getTypes(ReceiverType);

  // WARNING We deactivated the check on allocated because it is
  // unabled to get the types allocated in the used libraries
  // auto allocated_types = IRDB.getAllocatedTypes();
  // auto end_it = allocated_types.end();
  for (auto PossibleType : PossibleTypes) {
    if (auto PossibleTypeStruct =
            llvm::dyn_cast<llvm::StructType>(PossibleType)) {
      // if ( allocated_types.find(possible_type_struct) != end_it ) {
      auto Target =
          getNonPureVirtualVFTEntry(PossibleTypeStruct, VtableIndex, CS);
      if (Target) {
        PossibleCallTargets.insert(Target);
      }
    }
  }

  if (PossibleCallTargets.empty())
    PossibleCallTargets = CHAResolver::resolveVirtualCall(CS);

  LOG_IF_ENABLE(BOOST_LOG_SEV(LG, DEBUG) << "Possible targets are:");
  for (auto Entry : PossibleCallTargets) {
    LOG_IF_ENABLE(BOOST_LOG_SEV(LG, DEBUG) << Entry);
  }

  return PossibleCallTargets;
}
