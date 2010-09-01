/**
 * @file TheoryAxioms.cpp
 * Implements class TheoryAxioms.
 */

#include "Lib/Environment.hpp"
#include "Lib/Stack.hpp"

#include "Kernel/Clause.hpp"
#include "Kernel/EqHelper.hpp"
#include "Kernel/Formula.hpp"
#include "Kernel/FormulaUnit.hpp"
#include "Kernel/Inference.hpp"
#include "Kernel/Signature.hpp"
#include "Kernel/Term.hpp"
#include "Kernel/TermIterators.hpp"
#include "Kernel/Theory.hpp"

#include "AxiomGenerator.hpp"
#include "Property.hpp"
#include "SymCounter.hpp"

#include "TheoryAxioms.hpp"

namespace Shell
{
using namespace Lib;
using namespace Kernel;

struct TheoryAxioms::Arithmetic
: public AxiomGenerator
{
  void inclusionImplications()
  {
//    if(has(Theory::LESS_EQUAL)) {
//      include(Theory::PLUS);
//    }
//    if(has(Theory::GREATER_EQUAL) || has(Theory::LESS) || has(Theory::GREATER)) {
//      include(Theory::LESS_EQUAL);
//    }
    if(has(Theory::INT_GREATER_EQUAL) || has(Theory::INT_LESS) || has(Theory::INT_GREATER)) {
      include(Theory::INT_LESS_EQUAL);
    }
    if(has(Theory::INT_LESS_EQUAL)) {
      include(Theory::PLUS);
    }

    if(has(Theory::MINUS)) {
      include(Theory::PLUS);
    }
    if(has(Theory::UNARY_MINUS)) {
       include(Theory::PLUS);
    }
    if(has(Theory::PLUS)) {
      include(Theory::UNARY_MINUS);
      include(Theory::INT_GREATER);
    }


    if(has(Theory::INT_DIVIDE)) {
      include(Theory::MINUS);
      include(Theory::PLUS);
      include(Theory::MULTIPLY);

      include(Theory::INT_GREATER);
      include(Theory::INT_GREATER_EQUAL);
      include(Theory::INT_LESS);
      include(Theory::INT_LESS_EQUAL);
    }
//    if(has(Theory::DIVIDE)) {
//      include(Theory::MULTIPLY);
//    }
  }
  void enumerate()
  {
    if(has(Theory::PLUS)) {
      ASS(has(Theory::INT_GREATER));

      //group axioms
      axiom( (X0+X1)+X2==X0+(X1+X2) );
      axiom( X0+zero==X0 );
      axiom( -(X0+X1)==(-X0)+(-X1) );
      axiom( X0+(-X0)==zero );

      //commutativity
      axiom( X0+X1==X1+X0 );


      //order axioms
      axiom( ile(X0,X0) );
      axiom( (ile(X0,X1) & ile(X1,X2)) --> ile(X0,X2) );

      //total order
      axiom( (ile(X0,X1)) | ile(X1,X0) );

      //connects groups and order
      axiom( ile(X1,X2) --> ile(X1+X0,X2+X0) );

      //specific for arithmetic
      axiom( ile(zero,one) );
      axiom( ilt(X0,X1) --> ile(X0+one,X1) );

      //connect strict and non-strict inequality
      axiom( (ile(X0,X1)) --> ((X0==X1) | ilt(X0,X1)) );

    }
#if VDEBUG
    else {
      ASS(!has(Theory::INT_GREATER));
    }
#endif

//    if(has(Theory::GREATER)) {
//      axiom( !(X0>X0) );
//      axiom( ((X0>X1) & (X1>X2)) --> (X0>X2) );
//
//      //total order
//      axiom( (!(X0>X1)) | !(X1>X0) );
//
//      //specific for arithmetic
//      axiom( one>zero );
//
////      axiom( (!(X0>X1)) --> ((X0==X1) | (X1>X0)) );
//
//      if(has(Theory::PLUS)) {
//	axiom( (X1>X2) --> (X1+X0>X2+X0) );
//      }
//      if(has(Theory::SUCCESSOR)) {
//	axiom( X0++>X0 );
//      }
//    }
//
    if(has(Theory::MULTIPLY)) {
      axiom( X0*X1==X1*X0 );
      axiom( (X0*X1)*X2==X0*(X1*X2) );
      axiom( X0*one==X0 );
      axiom( X0*zero==zero );

      if(has(Theory::PLUS)) {
        axiom( X0*(X1++)==(X0*X1)+X0 );
        axiom( (X0+X1)*(X2+X3) == (X0*X2 + X0*X3 + X1*X2 + X1*X3) );
//	axiom( X0*(X1+X2) == (X0*X1 + X0*X2) );
      }
    }
    if(has(Theory::INT_DIVIDE)) {
      axiom( (ige(X0,zero) & igt(X1,zero)) --> ( ilt(X0-X1, idiv(X0,X1)*X1) & ile(idiv(X0,X1)*X1, X0) ) );
      axiom( (ilt(X0,zero) & ilt(X1,zero)) --> ( igt(X0-X1, idiv(X0,X1)*X1) & ige(idiv(X0,X1)*X1, X0) ) );
      axiom( (ige(X0,zero) & ilt(X1,zero)) --> ( ilt(X0+X1, idiv(X0,X1)*X1) & ile(idiv(X0,X1)*X1, X0) ) );
      axiom( (ilt(X0,zero) & igt(X1,zero)) --> ( igt(X0+X1, idiv(X0,X1)*X1) & ige(idiv(X0,X1)*X1, X0) ) );
      axiom( (ilt(X0,zero) & igt(X1,zero)) --> ( igt(X0+X1, idiv(X0,X1)*X1) & ige(idiv(X0,X1)*X1, X0) ) );

      axiom( (X1!=zero) --> (idiv(X0,X1)+X2==idiv(X0+(X1*X2),X1)) );
    }
//    if(has(Theory::DIVIDE)) {
//      axiom( (X1!=zero) --> ( (X0/X1==X2) -=- (X1*X2==X0) ) );
//    }
  }
};

/**
 * Add theory axioms to the @b units list that are relevant to
 * units present in the list. Update the property object @b prop.
 * Replace in each formula instances of X-Y by X+(-Y) and X++ by
 * X+1 and <,<=,>= by >.
 */
void TheoryAxioms::apply(UnitList*& units, Property* prop)
{
  CALL("TheoryAxioms::apply");

  if(!env.signature->anyInterpretedSymbols()) {
    //If we don't have any interpreted symbols (besides equality)
    //there won't be any theory axioms added anyway
    return;
  }

  Arithmetic axGen;

  //find out which symbols are used in the problem
  SymCounter sctr(*env.signature);
  sctr.count(units,1);
  for(unsigned i=0;i<Theory::interpretationElementCount; i++) {
    Interpretation interp=static_cast<Interpretation>(i);
    if(!env.signature->haveInterpretingSymbol(interp)) {
      continue;
    }
    if(Theory::isFunction(interp)) {
      unsigned fn=env.signature->getInterpretingSymbol(interp);
      if(sctr.getFun(fn).occ()) {
	axGen.include(interp);
      }
    }
    else {
      unsigned pred=env.signature->getInterpretingSymbol(interp);
      SymCounter::Pred* pc=&sctr.getPred(pred);
      if(pc->pocc() || pc->nocc() || pc->docc()) {
	axGen.include(interp);
      }
    }
  }

  UnitList* newAxioms=axGen.getAxioms();

  if(newAxioms) {
    prop->scan(newAxioms);
  }

  units=UnitList::concat(newAxioms, units);

  //replace some function and predicate definitions
  if( axGen.has(Theory::MINUS) || axGen.has(Theory::SUCCESSOR) ||
      axGen.has(Theory::INT_LESS_EQUAL) || axGen.has(Theory::INT_LESS) ||
      axGen.has(Theory::INT_GREATER_EQUAL) ) {
    UnitList::DelIterator us(units);
    while (us.hasNext()) {
      Unit* u = us.next();
      Unit* v = replaceFunctions(u);
      if (v != u) {
	us.replace(v);
      }
    }
  }

}

/**
 * Replace some functions and predicates by their definitions
 */
Unit* TheoryAxioms::replaceFunctions(Unit* u)
{
  CALL("TheoryAxioms::replaceFunctions(Unit*)");

  if(!u->isClause()) {
    Formula* f=static_cast<FormulaUnit*>(u)->formula();
    Formula* g=replaceFunctions(f);
    if(f==g) {
      return u;
    }
    return new FormulaUnit(g, new Inference1(Inference::INTERPRETED_SIMPLIFICATION, u) , u->inputType());
  }

  Clause* cl=static_cast<Clause*>(u);
  unsigned clen=cl->length();

  static LiteralStack lits;
  lits.reset();
  bool modified=false;
  for(unsigned i=0;i<clen;i++) {
    Literal* l=(*cl)[i];
    Literal* m=replaceFunctions(l);
    lits.push(m);
    if(l!=m) {
      modified=true;
    }
  }

  if(!modified) {
    return u;
  }
  return Clause::fromIterator(LiteralStack::Iterator(lits), u->inputType(),
      new Inference1(Inference::INTERPRETED_SIMPLIFICATION, u));
}

/**
 * Replace some functions and predicates by their definitions
 */
Formula* TheoryAxioms::replaceFunctions(Formula* f)
{
  CALL("TheoryAxioms::replaceFunctions(Formula*)");

  switch (f->connective()) {
  case LITERAL:
    {
      Literal* lit = replaceFunctions(f->literal());
      return lit == f->literal() ? f : new AtomicFormula(lit);
    }

  case AND:
  case OR:
    {
      FormulaList* newArgs = replaceFunctions(f->args());
      if (newArgs == f->args()) {
	return f;
      }
      return new JunctionFormula(f->connective(), newArgs);
    }

  case IMP:
  case IFF:
  case XOR:
    {
      Formula* l = replaceFunctions(f->left());
      Formula* r = replaceFunctions(f->right());
      if (l == f->left() && r == f->right()) {
	return f;
      }
      return new BinaryFormula(f->connective(), l, r);
    }

  case NOT:
    {
      Formula* arg = replaceFunctions(f->uarg());
      if (f->uarg() == arg) {
	return f;
      }
      return new NegatedFormula(arg);
    }

  case FORALL:
  case EXISTS:
    {
      Formula* arg = replaceFunctions(f->qarg());
      if (arg == f->qarg()) {
	return f;
      }
      return new QuantifiedFormula(f->connective(),f->vars(),arg);
    }

  case TRUE:
  case FALSE:
    return f;

  default:
    ASSERTION_VIOLATION;
  }
}

/**
 * Replace some functions and predicates by their definitions
 */
FormulaList* TheoryAxioms::replaceFunctions(FormulaList* fs)
{
  CALL("TheoryAxioms::replaceFunctions(FormulaList*)");

  if (fs->isEmpty()) {
    return fs;
  }
  Formula* f = fs->head();
  FormulaList* tail = fs->tail();
  Formula* g = replaceFunctions(f);
  FormulaList* gs = replaceFunctions(tail);

  if (f == g && tail == gs) {
    return fs;
  }
  return new FormulaList(g,gs);

}

/**
 * Replace some functions and predicates by their definitions
 */
Literal* TheoryAxioms::replaceFunctions(Literal* l)
{
  CALL("TheoryAxioms::replaceFunctions(Literal*)");

  //Term to be replaced
  //The terms are put on the stack in a 'parents first' manner,
  //so if we replace minuses in parent terms first, we do not
  //need to rescan repeatedly the term for new minus-term occurences.
  static Stack<TermList> rTerms;
  rTerms.reset();

  SubtermIterator sit(l);
  while(sit.hasNext()) {
    TermList t=sit.next();
    if(theory->isInterpretedFunction(t, Theory::MINUS) ||
	theory->isInterpretedFunction(t, Theory::SUCCESSOR)) {
      rTerms.push(t);
    }
  }

  //now let's do the replacing
  Stack<TermList>::BottomFirstIterator rit(rTerms);
  while(rit.hasNext()) {
    TermList orig=rit.next();
    Term* ot=orig.term();
    TermList repl;
    if(theory->isInterpretedFunction(ot, Theory::MINUS)) {
      ASS_EQ(ot->arity(),2);
      TermList arg2Neg=TermList(theory->fun1(Theory::UNARY_MINUS, *ot->nthArgument(1)));
      repl=TermList(theory->fun2(Theory::PLUS, *ot->nthArgument(0), arg2Neg));
    }
    else {
      ASS(theory->isInterpretedFunction(ot, Theory::SUCCESSOR));
      ASS_EQ(ot->arity(),1);
      repl=TermList(theory->fun2(Theory::PLUS, *ot->nthArgument(0), theory->one()));
    }
    l=EqHelper::replace(l, orig, repl);
  }

  if(theory->isInterpretedPredicate(l)) {
    Interpretation itpPred=theory->interpretPredicate(l);
    //we want to transform all integer inequalities to INT_LESS_EQUAL
    if(itpPred==Theory::INT_GREATER || itpPred==Theory::INT_GREATER_EQUAL || itpPred==Theory::INT_LESS) {
      bool polarity=l->polarity();
      if(itpPred==Theory::INT_GREATER || itpPred==Theory::INT_LESS) {
	polarity^=1;
      }
      TermList arg1=*l->nthArgument(0);
      TermList arg2=*l->nthArgument(1);
      if(itpPred==Theory::INT_LESS || itpPred==Theory::INT_GREATER_EQUAL) {
	swap(arg1, arg2);
      }

      l=theory->pred2(Theory::INT_LESS_EQUAL, polarity, arg1, arg2);
    }
  }

  return l;
}

}
