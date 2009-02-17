/**
 * @file TermSubstitutionTree.hpp
 * Defines class TermSubstitutionTree.
 */


#ifndef __TermSubstitutionTree__
#define __TermSubstitutionTree__


#include "../Kernel/Renaming.hpp"
#include "../Lib/SkipList.hpp"

#include "Index.hpp"
#include "TermIndexingStructure.hpp"
#include "SubstitutionTree.hpp"

#if COMPIT_GENERATOR

#include "../Test/CompitOutput.hpp"

#endif

namespace Indexing {

class TermSubstitutionTree
: public TermIndexingStructure, SubstitutionTree
{
public:
  TermSubstitutionTree();

  void insert(TermList t, Literal* lit, Clause* cls);
  void remove(TermList t, Literal* lit, Clause* cls);

  bool generalizationExists(TermList t);


  TermQueryResultIterator getUnifications(TermList t,
	  bool retrieveSubstitutions);

  TermQueryResultIterator getGeneralizations(TermList t,
	  bool retrieveSubstitutions);

  TermQueryResultIterator getInstances(TermList t,
	  bool retrieveSubstitutions);

private:
  void handleTerm(TermList t, Literal* lit, Clause* cls, bool insert);

  class TermQueryResultFn;

  template<class Iterator>
  TermQueryResultIterator getResultIterator(Term* term,
	  bool retrieveSubstitutions);

  struct LDToTermQueryResultFn;
  struct LDToTermQueryResultWithSubstFn;
  struct LeafToLDIteratorFn;
  struct UnifyingContext;

  template<class LDIt>
  TermQueryResultIterator ldIteratorToTQRIterator(LDIt ldIt,
	  TermList queryTerm, bool retrieveSubstitutions);

  TermQueryResultIterator getAllUnifyingIterator(TermList var,
	  bool retrieveSubstitutions);

  inline
  unsigned getRootNodeIndex(Term* t)
  {
    return t->functor();
  }


  typedef SkipList<LeafData,LDComparator> LDSkipList;
  LDSkipList _vars;
};

#if COMPIT_GENERATOR

using namespace Test;

class CompitUnificationRecordingTermSubstitutionTree
: public TermSubstitutionTree
{
  void insert(TermList t, Literal* lit, Clause* cls)
  {
    TermSubstitutionTree::insert(t,lit,cls);
    Renaming norm;
    norm.normalizeVariables(t);
    CompitOutput::print(CompitOutput::INSERT, norm.apply(t));
  }
  void remove(TermList t, Literal* lit, Clause* cls)
  {
    TermSubstitutionTree::insert(t,lit,cls);
    Renaming norm;
    norm.normalizeVariables(t);
    CompitOutput::print(CompitOutput::DELETE, norm.apply(t));
  }

  TermQueryResultIterator getUnifications(TermList t, bool retrieveSubstitutions)
  {
    TermQueryResultIterator res=TermSubstitutionTree::getUnifications(t,retrieveSubstitutions);
    Renaming norm;
    norm.normalizeVariables(t);
    if(res.hasNext()) {
      CompitOutput::print(CompitOutput::SUCCESSFUL_QUERY, norm.apply(t));
    } else {
      CompitOutput::print(CompitOutput::UNSUCCESSFUL_QUERY, norm.apply(t));
    }
    return res;
  }

};

#endif

};

#endif /* __TermSubstitutionTree__ */
