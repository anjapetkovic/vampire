/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */
/**
 * @file GoalRewriting.hpp
 * Defines class GoalRewriting
 *
 */

#ifndef __GoalRewriting__
#define __GoalRewriting__

#include "Forwards.hpp"

#include "InferenceEngine.hpp"

namespace Inferences
{

using Position = Stack<unsigned>;

using namespace Kernel;
using namespace Indexing;
using namespace Saturation;

TermList replaceOccurrence(Term* t, const Term* orig, TermList repl, const Position& pos);
VirtualIterator<std::pair<Term*,Position>> getPositions(TermList t, const Term* st);

class PositionalNonVariableNonTypeIterator
  : public IteratorCore<std::pair<Term*,Position>>
{
public:
  PositionalNonVariableNonTypeIterator(const PositionalNonVariableNonTypeIterator&);

  PositionalNonVariableNonTypeIterator(Term* term) : _stack(8)
  {
    _stack.push(std::make_pair(term,Position()));
  }

  /** true if there exists at least one subterm */
  bool hasNext() { return !_stack.isEmpty(); }
  std::pair<Term*,Position> next();
private:
  /** available non-variable subterms */
  Stack<std::pair<Term*,Position>> _stack;
}; // PositionalNonVariableNonTypeIterator

class GoalRewriting
: public GeneratingInferenceEngine
{
public:
  void attach(SaturationAlgorithm* salg) override;
  void detach() override;
  ClauseIterator generateClauses(Clause* premise) override;

private:
  Clause* perform(Clause* rwClause, Literal* rwLit, Term* rwSide, const Term* rwTerm, Position&& pos,
    Clause* eqClause, Literal* eqLit, TermList eqLhs, ResultSubstitution* subst, bool eqIsResult);

  TermIndex<TermLiteralClause>* _lhsIndex;
  TermIndex<TermLiteralClause>* _subtermIndex;
};

}

#endif /*__GoalRewriting__*/