/**
 * @file MainLoop.cpp
 * Implements class MainLoop.
 */


#include "Lib/Environment.hpp"
#include "Lib/SmartPtr.hpp"

#include "Inferences/Condensation.hpp"
#include "Inferences/DistinctEqualitySimplifier.hpp"
#include "Inferences/FastCondensation.hpp"
#include "Inferences/InferenceEngine.hpp"
#include "Inferences/InterpretedEvaluation.hpp"
#include "Inferences/TautologyDeletionISE.hpp"

#include "InstGen/IGAlgorithm.hpp"

#include "Saturation/SaturationAlgorithm.hpp"

#include "Tabulation/TabulationAlgorithm.hpp"

#include "Shell/BFNTMainLoop.hpp"
#include "Shell/Options.hpp"

#include "BDD.hpp"
#include "Clause.hpp"
#include "Problem.hpp"

#include "MainLoop.hpp"

using namespace Kernel;
using namespace InstGen;
using namespace Saturation;
using namespace Tabulation;

void MainLoopResult::updateStatistics()
{
  CALL("MainLoopResult::updateStatistics");

  env -> statistics->terminationReason = terminationReason;
  env -> statistics->refutation = refutation;
  env -> statistics->saturatedSet = saturatedSet;
}

/**
 * Run the solving algorithm
 */
MainLoopResult MainLoop::run()
{
  CALL("MainLoop::run");

  try {
    init();
    return runImpl();
  }
  catch(RefutationFoundException& rs)
  {
    return MainLoopResult(Statistics::REFUTATION, rs.refutation);
  }
  catch(TimeLimitExceededException&)
  {
    return MainLoopResult(Statistics::TIME_LIMIT);
  }
  catch(MainLoopFinishedException& e)
  {
    return e.result;
  }
}

/**
 * Return true iff clause @b c is refutation clause.
 *
 * Deriving a refutation clause means that the saturation algorithm can
 * terminate with success.
 */
bool MainLoop::isRefutation(Clause* cl)
{
  CALL("MainLoop::isRefutation");

  // No prop part
  //BDD* bdd=BDD::instance();
  return cl->isEmpty() && cl->noSplits(); //&& (!cl->prop() || bdd->isFalse(cl->prop()));
}

/**
 * Create local clause simplifier for problem @c prb according to options @c opt
 */
ImmediateSimplificationEngine* MainLoop::createISE(Problem& prb, const Options& opt)
{
  CALL("MainLoop::createImmediateSE");

  CompositeISE* res=new CompositeISE();

  switch(opt.condensation()) {
  case Options::CONDENSATION_ON:
    res->addFront(new Condensation());
    break;
  case Options::CONDENSATION_FAST:
    res->addFront(new FastCondensation());
    break;
  case Options::CONDENSATION_OFF:
    break;
  }

  if(prb.hasEquality()) {
    res->addFront(new DistinctEqualitySimplifier());
  }
  if(prb.hasInterpretedOperations()) {
    res->addFront(new InterpretedEvaluation());
  }
  if(prb.hasEquality()) {
    res->addFront(new TrivialInequalitiesRemovalISE());
  }
  res->addFront(new TautologyDeletionISE());
  res->addFront(new DuplicateLiteralRemovalISE());

  return res;
}

MainLoop* MainLoop::createFromOptions(Problem& prb, const Options& opt)
{
  CALL("MainLoop::createFromOptions");

  if(opt.bfnt()) {
    return new BFNTMainLoop(prb, opt);
  }

  MainLoop* res;

  switch (opt.saturationAlgorithm()) {
  case Options::TABULATION:
    res = new TabulationAlgorithm(prb, opt);
    break;
  case Options::INST_GEN:
    res = new IGAlgorithm(prb, opt);
    break;
  default:
    res = SaturationAlgorithm::createFromOptions(prb, opt);
    break;
  }

  return res;
}

