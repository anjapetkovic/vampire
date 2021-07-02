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
 * @file LiteralFactoring.cpp
 * Defines class LiteralFactoring
 *
 */

#include "LiteralFactoring.hpp"
#include "Shell/Statistics.hpp"

#define DEBUG(...) // DBG(__VA_ARGS__)

namespace Inferences {
namespace IRC {

void LiteralFactoring::attach(SaturationAlgorithm* salg) 
{ }

void LiteralFactoring::detach() 
{ }

//  C \/ ±js1 + t1 <> 0 \/ ±ks2 + t2 <> 0
// ====================================================
// (C \/ ±js1 + t1 <> 0 \/ k t1 − j t2  ̸≈ 0) σ \/ Cnst
//
//
// where
// • uwa(s1,s2)=⟨σ,Cnst⟩
// • <> ∈ {>,≥,≈, /≈}
// • term(s1)σ is maximal in ({s1} ∪ terms(t1))σ
// • term(s2)σ is maximal in ({s2} ∪ terms(t2))σ
// • (±ks1 + t1 <> 0)σ is maximal in Hypσ <- TODO
// • (±ks2 + t2 <> 0)σ is maximal in Hypσ <- TODO




template<class NumTraits>
Clause* LiteralFactoring::applyRule(Clause* premise, 
    Literal* lit1, IrcLiteral<NumTraits> l1,  Monom<NumTraits> j_s1,
    //       ^^^^--> `±js1 + t1 <> 0` <--^^            ±js1 <--^^^^
    Literal* lit2, IrcLiteral<NumTraits> l2,  Monom<NumTraits> k_s2,
    //       ^^^^--> `±ks2 + t2 <> 0` <--^^            ±ks2 <--^^^^
    UwaResult uwa)
{
  auto sigma = [&](auto x){ return uwa.sigma.apply(x, /* varbank */ 0); };
  auto& cnst  = uwa.cnst;
  auto j = j_s1.numeral;
  auto k = k_s2.numeral;
  ASS_EQ(j.isPositive(), k.isPositive())

  Stack<Literal*> conclusion(premise->size() + cnst.size());

  // adding `(C \/ ±js1 + t1 <> 0)σ`
  { 
    auto lit2cnt = 0;
    for (auto lit : iterTraits(premise->getLiteralIterator())) {
      if (lit == lit2) {
        lit2cnt++;
      } else {
        ASS(lit != lit2)
        conclusion.push(sigma(lit));
      }
    }
    if (lit2cnt > 1) {
      conclusion.push(sigma(lit2));
    }
  }

  auto pivotSum = 
  //   ^^^^^^^^--> `k t1 − j t2`
    NumTraits::sum(iterTraits(getConcatenatedIterator(
      l1.term().iterSummands()
        .filter([&](auto t) { return t != j_s1; })
        .map([&](auto t) { return  (k * t).denormalize(); }),

      l2.term().iterSummands()
        .filter([&](auto t) { return t != k_s2; })
        .map([&](auto t) { return  (-j * t).denormalize(); })
        )));
  auto pivotLiteral = NumTraits::eq(false, pivotSum, NumTraits::zero());

  // adding `(k t1 − j t2  ̸≈ 0)σ`
  conclusion.push(sigma(pivotLiteral));

  // adding `Cnst`
  conclusion.loadFromIterator(uwa.cnstLiterals());

  Inference inf(GeneratingInference1(Kernel::InferenceRule::IRC_LITERAL_FACTORING, premise));

  env.statistics->ircLitFacCnt++;
  return Clause::fromStack(conclusion, inf);
}


template<class NumTraits>
ClauseIterator LiteralFactoring::generateClauses(Clause* premise, 
    Literal* lit1, IrcLiteral<NumTraits> l1, 
    Literal* lit2, IrcLiteral<NumTraits> l2,
    shared_ptr<Stack<MaxAtomicTerm<NumTraits>>> maxTerms
    )
{
  auto filterNonMax = [maxTerms](Stack<Monom<NumTraits>> terms, Literal* lit) {
    return iterTraits(terms.iterFifo())
      .filter([&](auto t) 
          { return iterTraits(maxTerms->iterFifo())
                         .any([&](auto maxT) 
                           { return maxT.literal == lit && t == maxT.self; }); })
      .template collect<Stack>();
  };
  return pvi(iterTraits(ownedArrayishIterator(filterNonMax(_shared->maxAtomicTerms(l1), lit1)))
    .flatMap([=](auto j_s1) {
      return pvi(iterTraits(ownedArrayishIterator(filterNonMax(_shared->maxAtomicTerms(l2), lit2)))
        .filter([=](auto k_s2) { return k_s2.numeral.isPositive() == j_s1.numeral.isPositive(); })
        .filterMap([=](auto k_s2) { 
            auto s1 = j_s1.factors->denormalize();
            auto s2 = k_s2.factors->denormalize();
            return _shared->unify(s1, s2)
              .andThen([&](auto&& sigma_cnst) -> Option<UwaResult> { 
                  auto maxAfterUnif = [&](auto term, auto literal) {
                    auto term_sigma    = _shared->normalize(TypedTermList(sigma_cnst.sigma.apply(term, 0), NumTraits::sort()))
                      .template downcast<NumTraits>().unwrap()
                      ->tryMonom().unwrap().factors;
                    auto literal_sigma = _shared->normalize(sigma_cnst.sigma.apply(literal.denormalize(), 0))
                                     .unwrap().template unwrap<IrcLiteral<NumTraits>>();
                    auto max = _shared->maxAtomicTerms(literal_sigma); // TODO can be done more efficient
                    return iterTraits(max.iterFifo()).any([&](auto monom) { return monom.factors == term_sigma; });
                  };

                  if (maxAfterUnif(s1, l1) && maxAfterUnif(s1, l1)) {
                    return Option<UwaResult>(std::move(sigma_cnst));
                  } else {
                    return Option<UwaResult>();
                  }
              })
              .map([&](auto sigma_cnst){ return applyRule(premise, lit1, l1, j_s1, 
                                                                   lit2, l2, k_s2, 
                                                                   std::move(sigma_cnst)); });
            }));
    }));
}

template<template<class> class Obj> class AllNumTraits;
template<class NumTraits, template<class> class Obj2> struct __getAllNumTraits {
  Obj2<NumTraits>     & operator()(AllNumTraits<Obj2>      &);
  Obj2<NumTraits>const& operator()(AllNumTraits<Obj2> const&);
};

template<template<class> class Obj>
class AllNumTraits {
  Obj< IntTraits> _int;
  Obj< RatTraits> _rat;
  Obj<RealTraits> _real;
public:
  AllNumTraits( Obj< IntTraits> intObj, Obj< RatTraits> ratObj, Obj<RealTraits> realObj)
   : _int(std::move(intObj))
   , _rat(std::move(ratObj))
   , _real(std::move(realObj)) 
  {}


  template<class NumTraits, template<class> class Obj2> friend struct __getAllNumTraits;

  template<class NumTraits> Obj<NumTraits>      & get()       { return __getAllNumTraits<NumTraits, Obj>{}(*this); }
  template<class NumTraits> Obj<NumTraits> const& get() const { return __getAllNumTraits<NumTraits, Obj>{}(*this); }
private:
  Obj< IntTraits> const&  getInt() const { return _int;  }
  Obj< RatTraits> const&  getRat() const { return _rat;  }
  Obj<RealTraits> const& getReal() const { return _real; }

  Obj< IntTraits>&  getInt() { return _int;  }
  Obj< RatTraits>&  getRat() { return _rat;  }
  Obj<RealTraits>& getReal() { return _real; }
};


template<template<class> class Obj> 
struct __getAllNumTraits< IntTraits, Obj> {
  Obj< IntTraits>     & operator()(AllNumTraits<Obj>      & self) { return self. getInt(); }
  Obj< IntTraits>const& operator()(AllNumTraits<Obj> const& self) { return self. getInt(); }
};

template<template<class> class Obj> 
struct __getAllNumTraits< RatTraits, Obj> {
  Obj< RatTraits>     & operator()(AllNumTraits<Obj>      & self) { return self. getRat(); }
  Obj< RatTraits>const& operator()(AllNumTraits<Obj> const& self) { return self. getRat(); }
};


template<template<class> class Obj> 
struct __getAllNumTraits<RealTraits, Obj> {
  Obj<RealTraits>     & operator()(AllNumTraits<Obj>      & self) { return self.getReal(); }
  Obj<RealTraits>const& operator()(AllNumTraits<Obj> const& self) { return self.getReal(); }
};


  template<class NumTraits> using MaxTermStack = Stack<MaxAtomicTerm<NumTraits>>;
  template<class NumTraits> using SharedMaxTermStack = shared_ptr<MaxTermStack<NumTraits>>;
ClauseIterator LiteralFactoring::generateClauses(Clause* premise) 
{
  // static_assert(std::is_same<ArrayishObjectIterator<Clause>, decltype(ownedArrayishIterator(_shared->maxLiterals(premise)))>::value, "we assume that the first numSelected() literals are the selected ones");

  DEBUG("in: ", *premise)

  auto selected = make_shared(new Stack<Literal*>(_shared->maxLiterals(premise)));
  auto normalize = [this,selected](unsigned i) {
    using Opt = Option<Indexed<pair<Literal*, AnyIrcLiteral>>>;
    auto lit = (*selected)[i];
    auto norm = _shared->normalizer.normalize(lit);
    return norm.isSome() && !norm.unwrap().overflowOccurred
      ? Opt(indexed(i, make_pair(lit, norm.unwrap().value)))
      : Opt();
  };

  auto maxTerms = AllNumTraits<SharedMaxTermStack>(
      make_shared(new MaxTermStack< IntTraits>(_shared->maxAtomicTermsNonVar< IntTraits>(premise))),
      make_shared(new MaxTermStack< RatTraits>(_shared->maxAtomicTermsNonVar< RatTraits>(premise))),
      make_shared(new MaxTermStack<RealTraits>(_shared->maxAtomicTermsNonVar<RealTraits>(premise)))
  );

  return pvi(iterTraits(getRangeIterator((unsigned)0, (unsigned)selected->size()))
    .filterMap([=](unsigned i) 
      { return normalize(i); })

    .flatMap([=](auto lit1_l1) {
      auto lit1 = lit1_l1->first;
      auto l1 = lit1_l1->second;
      return pvi(iterTraits(getRangeIterator(lit1_l1.idx + 1, (unsigned)selected->size()))

        .filterMap([=](unsigned j)
          { return normalize(j); })

        .filterMap([=](auto lit2_l2) -> Option<ClauseIterator> { 
          auto lit2 = lit2_l2->first;
          auto l2 = lit2_l2->second;

          return l1.apply([&](auto l1) { 
              using NumTraits = typename decltype(l1)::NumTraits;
              /* check whether l1 and l2 are of the same number sort */
              return l2.template as<decltype(l1)>()

                /* check whether l1 and l2 are of the same inequality symbol */
                .filter([=](auto l2) { return l1.symbol() == l2.symbol(); })

                /* actually apply the rule */
                .map([=](auto l2){ return generateClauses(premise, lit1, l1, lit2, l2, maxTerms.template get<NumTraits>()); });
          }); 
        })
        .flatten());

  }));
}

  

#if VDEBUG
void LiteralFactoring::setTestIndices(Stack<Indexing::Index*> const&) 
{

}
#endif

} // namespace IRC 
} // namespace Inferences 
