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
 * @file Coherence.hpp
 * Defines class Coherence
 *
 */

#ifndef __LASCA_Coherence__
#define __LASCA_Coherence__

#include "Forwards.hpp"

#include "Inferences/InferenceEngine.hpp"
#include "Inferences/LASCA/Superposition.hpp"
#include "Kernel/NumTraits.hpp"
#include "Kernel/Ordering.hpp"
#include "Indexing/LascaIndex.hpp"
#include "BinInf.hpp"
#include "Shell/Options.hpp"

#define DEBUG(...) // DBG(__VA_ARGS__)

namespace Inferences {
namespace LASCA {

using namespace Kernel;
using namespace Indexing;
using namespace Saturation;

template<class NumTraits>
struct CoherenceConf
{
public:
  struct Lhs
  {
    LASCA::SuperpositionConf::Lhs _self;
    TermList _summand;

    static const char* name() { return "lasca coherence lhs"; }
    static IndexType indexType() { return Indexing::LASCA_COHERENCE_LHS_SUBST_TREE; }

    TypedTermList key() const { return TypedTermList(_summand, NumTraits::sort()); }
    Clause* clause() const { return _self.clause(); }

    static auto iter(LascaState& shared, Clause* cl)
    {
      return iterTraits(LASCA::Superposition::Lhs::iter(shared, cl))
        .filterMap([&shared](auto lhs) { return NumTraits::ifFloor(lhs.key(), 
              [&shared, lhs](auto...) { 
                auto ts = shared.normalize(lhs.smallerSide())
                        .template wrapPoly<NumTraits>();
                // TODO we can choose *any* summand for the rule to work. which summand is important though as it is our primary filter to preselect the number of potential applications in indexing. Try out which terms are good here!!!
                auto atoms = ts->iterSummands()
                  .map([](auto m) { return m.factors->denormalize(); })
                  .filter([](auto f) { return !f.isVar(); });
                auto selectedAtom = atoms.tryNext();
                return selectedAtom.map([lhs](auto t) { return Lhs { lhs, t }; });
              }).flatten(); 
            });
    }

      

    friend std::ostream& operator<<(std::ostream& out, Lhs const& self)
    { return out << self._self << "[" << self._summand << "]"; }

    auto asTuple() const { return std::tie(_self, _summand); }
    IMPL_COMPARISONS_FROM_TUPLE(Lhs)
  };


  struct Rhs 
  {
    LASCA::Superposition::Rhs _self;
    Monom<NumTraits> _summand;

    static const char* name() { return "lasca coherence rhs"; }
    static IndexType indexType() { return Indexing::LASCA_COHERENCE_RHS_SUBST_TREE; }

    TypedTermList key() const { return TypedTermList(_summand.denormalize(), NumTraits::sort()); }

    static auto iter(LascaState& shared, Clause* cl)
    {
      return iterTraits(LASCA::Superposition::Rhs::iter(shared, cl))
        .filterMap([&shared](auto rhs) { return NumTraits::ifFloor(rhs.key(), 
              [&shared, rhs](auto t) { return 
                shared.normalize(t).template wrapPoly<NumTraits>()
                      ->iterSummands()
                      .map([rhs](auto& m) { return Rhs{rhs, m}; })
              ; }); 
            })
        .flatten();
    }

      

    friend std::ostream& operator<<(std::ostream& out, Rhs const& self)
    { return out << self._self << "[" << self._summand << "]"; }

    auto asTuple() const { return std::tie(_self, _summand); }
    IMPL_COMPARISONS_FROM_TUPLE(Rhs)
  };


  Option<Clause*> applyRule(
      Lhs const& lhs, unsigned lhsVarBank,
      Rhs const& rhs, unsigned rhsVarBank,
      AbstractingUnifier& uwa
      ) const 
  {
    DBG(lhs)
    DBG(rhs)
    ASSERTION_VIOLATION
  }
};

template<class NumTraits>
struct Coherence : public BinInf<CoherenceConf<NumTraits>> {
  Coherence(std::shared_ptr<LascaState> shared) 
    : BinInf<CoherenceConf<NumTraits>>(shared, {}) 
    {}
};

#undef DEBUG
} // namespace LASCA 
} // namespace Inferences

#endif /*__LASCA_Coherence__*/
