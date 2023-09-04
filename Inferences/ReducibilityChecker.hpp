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
 * @file ReducibilityChecker.hpp
 * Defines class ReducibilityChecker.
 */

#ifndef __ReducibilityChecker__
#define __ReducibilityChecker__

#include "Forwards.hpp"
#include "Indexing/TermIndex.hpp"

#include "InferenceEngine.hpp"

namespace Inferences {

using namespace Indexing;

// struct VariantHash
// {
//   // static bool equals(T o1, T o2);
//   static unsigned hash(Term* t);
// };

class ReducibilityChecker {
private:
  DHSet<Term*> _reducible;
  DHSet<Term*> _nonReducible;
  DHSet<Term*> _done;

  DemodulationLHSIndex* _index;
  const Ordering& _ord;
  const Options& _opt;

  bool checkTerm(Term* t, Term* tS, Term* rwTermS, ResultSubstitution* subst, bool result, bool& variant);
  bool checkTermReducible(Term* tS, TermList* tgtTermS, bool greater);
  bool checkLeftmostInnermost(Clause* cl, Term* rwTermS, ResultSubstitution* subst, bool result);
  bool checkSmaller(Clause* cl, TermList rwTerm, Term* rwTermS, TermList* tgtTermS, ResultSubstitution* subst, bool result, bool greater);
  bool checkSmallerSanity(Clause* cl, TermList rwTerm, Term* rwTermS, TermList* tgtTermS, ResultSubstitution* subst, bool result, vstringstream& exp);

public:
  CLASS_NAME(ReducibilityChecker);
  USE_ALLOCATOR(ReducibilityChecker);

  ReducibilityChecker(DemodulationLHSIndex* index, const Ordering& ord, const Options& opt);

  bool check(Clause* cl, TermList rwTerm, Term* rwTermS, TermList* tgtTermS, ResultSubstitution* subst, bool result, bool greater);
  void reset() { _nonReducible.reset(); }
  void resetDone() { _done.reset(); }
  bool isNonReducible(Term* t) { return _nonReducible.contains(t); }  
};

}

#endif