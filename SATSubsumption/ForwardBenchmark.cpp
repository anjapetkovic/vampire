#include "Inferences/InferenceEngine.hpp"
#include "Saturation/SaturationAlgorithm.hpp"
#include "Indexing/LiteralIndex.hpp"

#include "ForwardBenchmark.hpp"
#include "SATSubsumption/SATSubsumptionAndResolution.hpp"
#include "Lib/Timer.hpp"

#if !USE_SAT_SUBSUMPTION_FORWARD
#include "Indexing/LiteralMiniIndex.hpp"
#include "Kernel/ColorHelper.hpp"
#include "Debug/RuntimeStatistics.hpp"
#include "Kernel/MLMatcher.hpp"
#include "Debug/RuntimeStatistics.hpp"
#include "Kernel/MLMatcher.hpp"
#include "Kernel/ColorHelper.hpp"
#endif

namespace Inferences {
using namespace std;
using namespace Lib;
using namespace Kernel;
using namespace Indexing;
using namespace Saturation;
using namespace SATSubsumption;
using namespace std::chrono;


ForwardBenchmark::ForwardBenchmark(bool subsumptionResolution, bool log)
    : _subsumptionResolution(subsumptionResolution)
#if SAT_SR_IMPL != 0
    , _forward(subsumptionResolution, log)
#endif
{
#if ENABLE_ROUNDS && SAT_SR_IMPL == 0
  max_rounds = env.options->maxRounds();
#endif
}

#if SAT_SR_IMPL == 0
/*
  Copy-paste of the old implementation
*/
void ForwardBenchmark::attach(SaturationAlgorithm *salg)
{
  ForwardSimplificationEngine::attach(salg);
  _unitIndex = static_cast<UnitClauseLiteralIndex *>(
      _salg->getIndexManager()->request(FW_SUBSUMPTION_UNIT_CLAUSE_SUBST_TREE));
  _fwIndex = static_cast<FwSubsSimplifyingLiteralIndex *>(
      _salg->getIndexManager()->request(FW_SUBSUMPTION_SUBST_TREE));
}

void ForwardBenchmark::detach()
{
  _unitIndex = 0;
  _fwIndex = 0;
  _salg->getIndexManager()->release(FW_SUBSUMPTION_UNIT_CLAUSE_SUBST_TREE);
  _salg->getIndexManager()->release(FW_SUBSUMPTION_SUBST_TREE);
  ForwardSimplificationEngine::detach();
}

struct ClauseMatches {
  USE_ALLOCATOR(ClauseMatches);

private:
  // private and undefined operator= and copy constructor to avoid implicitly generated ones
  ClauseMatches(const ClauseMatches &);
  ClauseMatches &operator=(const ClauseMatches &);

public:
  ClauseMatches(Clause *cl) : _cl(cl), _zeroCnt(cl->length())
  {
    unsigned clen = _cl->length();
    _matches = static_cast<LiteralList **>(ALLOC_KNOWN(clen * sizeof(void *), "Inferences::ClauseMatches"));
    for (unsigned i = 0; i < clen; i++) {
      _matches[i] = 0;
    }
  }
  ~ClauseMatches()
  {
    unsigned clen = _cl->length();
    for (unsigned i = 0; i < clen; i++) {
      LiteralList::destroy(_matches[i]);
    }
    DEALLOC_KNOWN(_matches, clen * sizeof(void *), "Inferences::ClauseMatches");
  }

  void addMatch(Literal *baseLit, Literal *instLit)
  {
    addMatch(_cl->getLiteralPosition(baseLit), instLit);
  }
  void addMatch(unsigned bpos, Literal *instLit)
  {
    if (!_matches[bpos]) {
      _zeroCnt--;
    }
    LiteralList::push(instLit, _matches[bpos]);
  }
  void fillInMatches(LiteralMiniIndex *miniIndex)
  {
    unsigned blen = _cl->length();

    for (unsigned bi = 0; bi < blen; bi++) {
      LiteralMiniIndex::InstanceIterator instIt(*miniIndex, (*_cl)[bi], false);
      while (instIt.hasNext()) {
        Literal *matched = instIt.next();
        addMatch(bi, matched);
      }
    }
  }
  bool anyNonMatched() { return _zeroCnt; }

  Clause *_cl;
  unsigned _zeroCnt;
  LiteralList **_matches;

  class ZeroMatchLiteralIterator {
  public:
    ZeroMatchLiteralIterator(ClauseMatches *cm)
        : _lits(cm->_cl->literals()), _mlists(cm->_matches), _remaining(cm->_cl->length())
    {
      if (!cm->_zeroCnt) {
        _remaining = 0;
      }
    }
    bool hasNext()
    {
      while (_remaining > 0 && *_mlists) {
        _lits++;
        _mlists++;
        _remaining--;
      }
      return _remaining;
    }
    Literal *next()
    {
      _remaining--;
      _mlists++;
      return *(_lits++);
    }

  private:
    Literal **_lits;
    LiteralList **_mlists;
    unsigned _remaining;
  };
};

typedef Stack<ClauseMatches *> CMStack;

Clause *ForwardBenchmark::generateSubsumptionResolutionClause(Clause *cl, Literal *lit, Clause *baseClause)
{
  int clen = cl->length();
  int nlen = clen - 1;

  Clause *res = new (nlen) Clause(nlen,
                                  SimplifyingInference2(InferenceRule::SUBSUMPTION_RESOLUTION, cl, baseClause));

  int next = 0;
  bool found = false;
  for (int i = 0; i < clen; i++) {
    Literal *curr = (*cl)[i];
    // As we will apply subsumption resolution after duplicate literal
    // deletion, the same literal should never occur twice.
    ASS(curr != lit || !found);
    if (curr != lit || found) {
      (*res)[next++] = curr;
    }
    else {
      found = true;
    }
  }

  return res;
}

bool checkForSubsumptionResolution(Clause *cl, ClauseMatches *cms, Literal *resLit)
{
  Clause *mcl = cms->_cl;
  unsigned mclen = mcl->length();

  ClauseMatches::ZeroMatchLiteralIterator zmli(cms);
  if (zmli.hasNext()) {
    while (zmli.hasNext()) {
      Literal *bl = zmli.next();
      //      if( !resLit->couldBeInstanceOf(bl, true) ) {
      if (!MatchingUtils::match(bl, resLit, true)) {
        return false;
      }
    }
  }
  else {
    bool anyResolvable = false;
    for (unsigned i = 0; i < mclen; i++) {
      //      if(resLit->couldBeInstanceOf((*mcl)[i], true)) {
      if (MatchingUtils::match((*mcl)[i], resLit, true)) {
        anyResolvable = true;
        break;
      }
    }
    if (!anyResolvable) {
      return false;
    }
  }

  return MLMatcher::canBeMatched(mcl, cl, cms->_matches, resLit);
}

bool ForwardBenchmark::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{

#if ENABLE_ROUNDS
  env.statistics->forwardSubsumptionRounds++;
  if (max_rounds && env.statistics->forwardSubsumptionRounds > max_rounds) {
    env.statistics->forwardSubsumptionRounds--;
    env.statistics->terminationReason = Shell::Statistics::TIME_LIMIT;
    Timer::setLimitEnforcement(false);
    throw TimeLimitExceededException();
  }
#endif

  Clause *resolutionClause = 0;

  unsigned clen = cl->length();
  if (clen == 0) {
    return false;
  }

  TIME_TRACE("forward subsumption");

  bool result = false;

  Clause::requestAux();

  static CMStack cmStore(64);
  ASS(cmStore.isEmpty());

  for (unsigned li = 0; li < clen; li++) {
    SLQueryResultIterator rit = _unitIndex->getGeneralizations((*cl)[li], false, false);
    while (rit.hasNext()) {
      Clause *premise = rit.next().clause;
      if (ColorHelper::compatible(cl->color(), premise->color())) {
        premises = pvi(getSingletonIterator(premise));
        env.statistics->forwardSubsumed++;
        result = true;
        goto fin;
      }
    }
  }

  {
    LiteralMiniIndex miniIndex(cl);

    for (unsigned li = 0; li < clen; li++) {
      SLQueryResultIterator rit = _fwIndex->getGeneralizations((*cl)[li], false, false);
      while (rit.hasNext()) {
        SLQueryResult res = rit.next();
        Clause *mcl = res.clause;
        if (mcl->hasAux()) {
          // we've already checked this clause
          continue;
        }
        ASS_G(mcl->length(), 1);

        ClauseMatches *cms = new ClauseMatches(mcl);
        mcl->setAux(cms);
        cmStore.push(cms);
        cms->fillInMatches(&miniIndex);

        if (cms->anyNonMatched()) {
          continue;
        }

        if (MLMatcher::canBeMatched(mcl, cl, cms->_matches, 0) && ColorHelper::compatible(cl->color(), mcl->color())) {
          premises = pvi(getSingletonIterator(mcl));
          env.statistics->forwardSubsumed++;
          result = true;
          goto fin;
        }
      }
    }

    if (!_subsumptionResolution) {
      goto fin;
    }

    {
      TIME_TRACE("forward subsumption resolution");

      for (unsigned li = 0; li < clen; li++) {
        Literal *resLit = (*cl)[li];
        SLQueryResultIterator rit = _unitIndex->getGeneralizations(resLit, true, false);
        while (rit.hasNext()) {
          Clause *mcl = rit.next().clause;
          if (ColorHelper::compatible(cl->color(), mcl->color())) {
            resolutionClause = generateSubsumptionResolutionClause(cl, resLit, mcl);
            env.statistics->forwardSubsumptionResolution++;
            premises = pvi(getSingletonIterator(mcl));
            replacement = resolutionClause;
            result = true;
            goto fin;
          }
        }
      }

      {
        CMStack::Iterator csit(cmStore);
        while (csit.hasNext()) {
          ClauseMatches *cms = csit.next();
          for (unsigned li = 0; li < clen; li++) {
            Literal *resLit = (*cl)[li];
            if (checkForSubsumptionResolution(cl, cms, resLit) && ColorHelper::compatible(cl->color(), cms->_cl->color())) {
              resolutionClause = generateSubsumptionResolutionClause(cl, resLit, cms->_cl);
              env.statistics->forwardSubsumptionResolution++;
              premises = pvi(getSingletonIterator(cms->_cl));
              replacement = resolutionClause;
              result = true;
              goto fin;
            }
          }
          ASS_EQ(cms->_cl->getAux<ClauseMatches>(), cms);
          cms->_cl->setAux(nullptr);
        }
      }

      for (unsigned li = 0; li < clen; li++) {
        Literal *resLit = (*cl)[li]; // resolved literal
        SLQueryResultIterator rit = _fwIndex->getGeneralizations(resLit, true, false);
        while (rit.hasNext()) {
          SLQueryResult res = rit.next();
          Clause *mcl = res.clause;

          ClauseMatches *cms = nullptr;
          if (mcl->hasAux()) {
            // We have seen the clause already, try to re-use the literal matches.
            // (Note that we can't just skip the clause: if our previous check
            // failed to detect subsumption resolution, it might still work out
            // with a different resolved literal.)
            cms = mcl->getAux<ClauseMatches>();
            // Already handled in the loop over cmStore above.
            if (!cms) {
              continue;
            }
          }
          if (!cms) {
            cms = new ClauseMatches(mcl);
            mcl->setAux(cms);
            cmStore.push(cms);
            cms->fillInMatches(&miniIndex);
          }

          if (checkForSubsumptionResolution(cl, cms, resLit) && ColorHelper::compatible(cl->color(), cms->_cl->color())) {
            resolutionClause = generateSubsumptionResolutionClause(cl, resLit, cms->_cl);
            env.statistics->forwardSubsumptionResolution++;
            premises = pvi(getSingletonIterator(cms->_cl));
            replacement = resolutionClause;
            result = true;
            goto fin;
          }
        }
      }
    }
  }

fin:
  Clause::releaseAux();
  while (cmStore.isNonEmpty()) {
    delete cmStore.pop();
  }
  return result;
}
#else
// Configuration 4
void ForwardBenchmark::attach(SaturationAlgorithm *salg)
{
  _forward.attach(salg);

  cout << "Forward benchmark: ";
#if SAT_SR_IMPL == 1
  _forward.forceDirectEncodingForSubsumptionResolution();
  cout << "direct encoding";
#elif SAT_SR_IMPL == 2
  _forward.forceIndirectEncodingForSubsumptionResolution();
  cout << "indirect encoding";
#else
  cout << "dynamic encoding";
#endif
#if USE_OPTIMIZED_FORWARD
  _forward.setOptimizedLoop(true);
  cout << " - optimized loop";
#else
  _forward.setOptimizedLoop(false);
#endif
}

void ForwardBenchmark::detach()
{
  _forward.detach();
}

bool ForwardBenchmark::perform(Clause *cl, Clause *&replacement, ClauseIterator &premises)
{
  TIME_TRACE("forward subsumption");
  return _forward.perform(cl, replacement, premises);
}
#endif




#if SAT_SR_IMPL == 0

SubsumptionReplayResult ForwardBenchmark::replay(SubsumptionBenchmark const& b, bool do_subsumption_resolution)
{
  SubsumptionReplayResult r;
  ALWAYS(r.subsumptions == 0);
  ALWAYS(r.subsumption_resolutions == 0);
  ALWAYS(r.errors == 0);

  if (do_subsumption_resolution) {
    USER_ERROR("cannot replay subsumption resolutions with old algorithm");
  }

  CMStack cmStore(64);

  COMPILER_FENCE;
  auto const start = benchmark_clock::now();
  COMPILER_FENCE;

  for (auto const& l : b.fwd_loops) {

    Clause* cl = l.main_premise;
    unsigned clen = cl->length();
    if (clen == 0) {
      continue;
    }

    Clause::requestAux();
    ASS(cmStore.isEmpty());

    LiteralMiniIndex miniIndex(cl);

    for (auto const& i : l.instances) {

      if (i.do_subsumption) {
        r.subsumptions++;
        Clause *mcl = i.side_premise;

        auto check_subsumption = [&]() -> bool {
          if (mcl->hasAux()) {
            // we've already checked this clause
            return false;
          }
          ASS_G(mcl->length(), 1);

          ClauseMatches *cms = new ClauseMatches(mcl);
          mcl->setAux(cms);
          cmStore.push(cms);
          cms->fillInMatches(&miniIndex);

          if (cms->anyNonMatched()) {
            return false;
          }

          return MLMatcher::canBeMatched(mcl, cl, cms->_matches, 0);
        };

        bool const result = check_subsumption();
        if (i.subsumption_result != result) {
          r.errors++;
        }
      }

      if (do_subsumption_resolution && i.do_subsumption_resolution) {
        ASS(false);
      }

    }  // l.instances

    Clause::releaseAux();
    while (cmStore.isNonEmpty()) {
      delete cmStore.pop();
    }

  }  // b.fwd_loops

  COMPILER_FENCE;
  auto const stop = benchmark_clock::now();
  COMPILER_FENCE;

  r.duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);

  return r;
}

#else

SubsumptionReplayResult ForwardBenchmark::replay(SubsumptionBenchmark const& b, bool do_subsumption_resolution)
{
  SubsumptionReplayResult r;
  ALWAYS(r.subsumptions == 0);
  ALWAYS(r.subsumption_resolutions == 0);
  ALWAYS(r.errors == 0);

  SATSubsumptionAndResolution satSubs;

  COMPILER_FENCE;
  auto const start = benchmark_clock::now();
  COMPILER_FENCE;

  for (auto const& l : b.fwd_loops) {
    for (auto const& i : l.instances) {
      bool const do_s = i.do_subsumption;
      bool const do_sr = do_subsumption_resolution && i.do_subsumption_resolution;
      if (do_s) {
        r.subsumptions++;
        bool const result = satSubs.checkSubsumption(i.side_premise, l.main_premise, do_sr);
        if (i.subsumption_result != result)
          r.errors++;
      }
      if (do_sr) {
        r.subsumption_resolutions++;
        Clause* const conclusion = satSubs.checkSubsumptionResolution(i.side_premise, l.main_premise, do_s);
        bool const result = !!conclusion;
        if (i.subsumption_resolution_result != result)
          r.errors++;
      }
    }
  }

  COMPILER_FENCE;
  auto const stop = benchmark_clock::now();
  COMPILER_FENCE;

  r.duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);

  return r;
}

#endif





} // namespace Inferences
