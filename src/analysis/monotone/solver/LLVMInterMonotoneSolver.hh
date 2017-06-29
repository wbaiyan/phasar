/*
 * LLVMInterMonotoneSolver.hh
 *
 *  Created on: 19.06.2017
 *      Author: philipp
 */

#ifndef LLVMINTERMONOTONESOLVER_HH_
#define LLVMINTERMONOTONESOLVER_HH_

#ifndef SRC_ANALYSIS_MONOTONE_SOLVER_LLVMINTERMONOTONESOLVER_HH_
#define SRC_ANALYSIS_MONOTONE_SOLVER_LLVMINTERMONOTONESOLVER_HH_

#include <iostream>
#include <llvm/IR/Instruction.h>

#include "../InterMonotoneProblem.hh"
#include "InterMonotoneSolver.hh"
using namespace std;

template <typename D, typename I>
class LLVMInterMonotoneSolver
    : public InterMonotoneSolver<const llvm::Instruction *, D,
                                 const llvm::Function *, I> {
protected:
  bool DUMP_RESULTS;

public:
  LLVMInterMonotoneSolver();
  virtual ~LLVMInterMonotoneSolver() = default;

  LLVMInterMonotoneSolver(
      InterMonotoneProblem<const llvm::Instruction *, D, const llvm::Function *,
                           I> &problem,
      bool dumpResults = false)
      : InterMonotoneSolver<const llvm::Instruction *, D,
                            const llvm::Function *, I>(problem),
        DUMP_RESULTS(dumpResults) {}

  virtual void solve() override {
    // do the solving of the analaysis problem
    InterMonotoneSolver<const llvm::Instruction *, D, const llvm::Function *,
                        I>::solve();
    if (DUMP_RESULTS)
      dumpResults();
  }

  void dumpResults() {
    cout << "Monotone solver results:\n";
    for (auto &node_entry :
         InterMonotoneSolver<const llvm::Instruction *, D,
                             const llvm::Function *, I>::Analysis) {
      for (auto &ctx_entry : node_entry.second) {
        cout << "Instruction:\n";
        node_entry.first->dump();
        cout << "Context:\n";
        //        cout << ctx_entry.first;
        cout << "Facts:\n";
        if (ctx_entry.second.empty()) {
          cout << "\tEMPTY\n";
        } else {
          for (auto fact : ctx_entry.second) {
            fact->dump();
          }
        }
        cout << "\n\n";
      }
    }
  }
};

#endif /* SRC_ANALYSIS_MONOTONE_SOLVER_LLVMINTERMONOTONESOLVER_HH_ */

#endif
