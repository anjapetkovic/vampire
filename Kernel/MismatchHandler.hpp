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
 * @file MismatchHandler.hpp
 * Defines class MismatchHandler.
 *
 */

#ifndef __MismatchHandler__
#define __MismatchHandler__

#include "Forwards.hpp"
#include "Term.hpp"
#include "Lib/Metaiterators.hpp"
#include "Lib/Option.hpp"
#include "RobSubstitution.hpp"
#include "Indexing/ResultSubstitution.hpp"
#include "Kernel/Signature.hpp"
#include "Lib/Reflection.hpp"
#include "Shell/Options.hpp"

namespace Kernel
{


class UnificationConstraintStack
{
  Stack<UnificationConstraint> _cont;
public:
  CLASS_NAME(UnificationConstraintStack)
  USE_ALLOCATOR(UnificationConstraintStack)
  UnificationConstraintStack() : _cont() {}
  UnificationConstraintStack(UnificationConstraintStack&&) = default;
  UnificationConstraintStack& operator=(UnificationConstraintStack&&) = default;

  auto iter() const
  { return iterTraits(_cont.iter()); }

  Recycled<Stack<Literal*>> literals(RobSubstitution& s);

  auto literalIter(RobSubstitution& s)
  { return iterTraits(_cont.iter())
              .filterMap([&](auto& c) { return c.toLiteral(s); }); }

  friend std::ostream& operator<<(std::ostream& out, UnificationConstraintStack const& self)
  { return out << self._cont; }

  void reset()
  { _cont.reset(); }

  bool isEmpty() const
  { return _cont.isEmpty(); }

  void add(UnificationConstraint c, Option<BacktrackData&> bd);
};

using Action = std::function<bool(unsigned, TermSpec)>;
using SpecialVar = unsigned;
using WaitingMap = DHMap<SpecialVar, Action>;

class MismatchHandler final
{
  Shell::Options::UnificationWithAbstraction const _mode;
public:
  MismatchHandler(Shell::Options::UnificationWithAbstraction mode) : _mode(mode) {}
  ~MismatchHandler() {}

  struct EqualIf { 
    Recycled<Stack<UnificationConstraint>> unify; 
    Recycled<Stack<UnificationConstraint>> constraints; 

    EqualIf( std::initializer_list<UnificationConstraint> unify,
             std::initializer_list<UnificationConstraint> constraints
        ) : unify(unify)
          , constraints(constraints) {  }

    EqualIf( Recycled<Stack<UnificationConstraint>> unify,
             Recycled<Stack<UnificationConstraint>> constraints
        ) : unify(std::move(unify))
          , constraints(std::move(constraints)) {  }

    friend std::ostream& operator<<(std::ostream& out, EqualIf const& self)
    { return out << "EqualIf(unify: " << self.unify << ", constr: " << self.constraints <<  ")"; }
  };

  struct NeverEqual {
    friend std::ostream& operator<<(std::ostream& out, NeverEqual const&)
    { return out << "NeverEqual"; } 
  };

  using AbstractionResult = Coproduct<NeverEqual, EqualIf>;


  /** TODO document */
  Option<AbstractionResult> tryAbstract(
      AbstractingUnifier* au,
      TermSpec t1,
      TermSpec t2) const;

  // /** TODO document */
  // virtual bool recheck(TermSpec l, TermSpec r) const = 0;

  static unique_ptr<MismatchHandler> create();
  static unique_ptr<MismatchHandler> createOnlyHigherOrder();

private:
  // for old non-alasca uwa modes
  bool isInterpreted(unsigned f) const;
  bool canAbstract(
      AbstractingUnifier* au,
      TermSpec t1,
      TermSpec t2) const;
};

class AbstractingUnifier {
  Recycled<RobSubstitution> _subs;
  Recycled<UnificationConstraintStack> _constr;
  Option<BacktrackData&> _bd;
  MismatchHandler const* _uwa;
  friend class RobSubstitution;
public:
  // DEFAULT_CONSTRUCTORS(AbstractingUnifier)
  AbstractingUnifier(MismatchHandler const* uwa) : _subs(), _constr(), _bd(), _uwa(uwa) 
  { }

  bool isRecording() { return _subs->bdIsRecording(); }

  void add(UnificationConstraint c) 
  { _constr->add(std::move(c), _subs->bdIsRecording() ? Option<BacktrackData&>(_subs->bdGet())
                                                      : Option<BacktrackData&>()              ); }

  bool unify(TermList t1, unsigned bank1, TermList t2, unsigned bank2);
  // { 
  //   return _subs->unify(t1, bank1, t2, bank2, _uwa, this); 
  // }


  UnificationConstraintStack& constr() { return *_constr; }
  Recycled<Stack<Literal*>> constraintLiterals() { return _constr->literals(*_subs); }

  RobSubstitution      & subs()       { return *_subs; }
  RobSubstitution const& subs() const { return *_subs; }
  void bdRecord(BacktrackData& bd) { _subs->bdRecord(bd); }
  void bdDone() { _subs->bdDone(); }
  bool usesUwa() const { return _uwa != nullptr; }

  friend std::ostream& operator<<(std::ostream& out, AbstractingUnifier const& self)
  { return out << "(" << self._subs << ", " << self._constr << ")"; }
};

} // namespace Kernel
#endif /*__MismatchHandler__*/
