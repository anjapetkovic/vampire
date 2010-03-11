/**
 * @file SWBSplitterWithBDDs.hpp
 * Defines class SWBSplitterWithBDDs.
 */


#ifndef __SWBSplitterWithBDDs__
#define __SWBSplitterWithBDDs__

#include "../Forwards.hpp"

#include "SWBSplitter.hpp"

namespace Saturation {

using namespace Lib;
using namespace Kernel;
using namespace Indexing;

class SWBSplitterWithBDDs : public SWBSplitter
{
protected:
  void buildAndInsertComponents(Clause* cl, CompRec* comps, unsigned compCnt, bool firstIsMaster);

  bool handleNoSplit(Clause* cl);

private:
  Clause* getComponent(Clause* cl, CompRec cr, int& name, bool& newComponent);

  int nameComponent(Clause* comp);
  BDDNode* getNameProp(int name);

  /** Names assigned to clauses stored in @b _variantIndex */
  DHMap<Clause*, int> _clauseNames;

  /**
   * Names for ground literals whose opposite counterparts haven't
   * been named yet
   *
   * See @b Splitter::nameComponent function.
   */
  DHMap<Literal*, int> _groundNames;
};

};

#endif /* __SWBSplitterWithBDDs__ */
