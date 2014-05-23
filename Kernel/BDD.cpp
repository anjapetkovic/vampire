/**
 * @file Clause.cpp
 * Implements class BDD for binary decision diagrams
 */

#include <utility>

#include "Lib/Cache.hpp"
#include "Lib/Environment.hpp"
#include "Lib/Exception.hpp"
#include "Lib/Deque.hpp"
#include "Lib/DHMap.hpp"
#include "Lib/DHSet.hpp"
#include "Lib/Int.hpp"
#include "Lib/List.hpp"
#include "Lib/Stack.hpp"
#include "Lib/Timer.hpp"
#include "Lib/TimeCounter.hpp"

#include "Formula.hpp"
#include "Signature.hpp"
#include "Term.hpp"

#include "SAT/Preprocess.hpp"
#include "SAT/SingleWatchSAT.hpp"

#include "Shell/Options.hpp"
#include "Shell/Statistics.hpp"

#include "BDD.hpp"


namespace Kernel {

using namespace Lib;
using namespace SAT;

///////////////////////
// BDDNode
//

bool BDDNode::isTrue() const
{
  CALL("BDDNode::isTrue");
  return BDD::instance()->isTrue(this);
}

bool BDDNode::isFalse() const
{
  CALL("BDDNode::isFalse");
  return BDD::instance()->isFalse(this);
}


///////////////////////
// BDD
//

/**
 * Return the singleton instance of the BDD class
 */
BDD* BDD::instance()
{
  CALL("BDD::instance");

  static BDD* inst=0;
  if(!inst) {
    inst=new BDD();
  }
  return inst;
}

/**
 * Create a new BDD object
 */
BDD::BDD()
: _trueNode(0,0,0), _falseNode(0,0,0),
  _bddEvalPredicate(0), _nextNodeNum(1),
  _allowDefinitionOutput(true), _newVar(1)
{
  _trueNode._depth=0;
  _falseNode._depth=0;
}

/**
 * Destroy a BDD object
 */
BDD::~BDD()
{
  CALL("BDD::~BDD");

  ASS_REP(_allowDefinitionOutput, "Definition output has to be allowed at the BDD object destruction");

  NodeSet::Iterator nit(_nodes);
  while(nit.hasNext()) {
    BDDNode* node=nit.next();
    delete node;
  }
}

/**
 * Return a new BDD variable that will represent propositional
 * predicate symbol @b pred
 */
int BDD::getNewVar(unsigned pred)
{
  CALL("BDD::getNewVar(unsigned)");
  ASS_EQ(env -> signature->predicateArity(pred), 0);

  int res=getNewVar();
  _predicateSymbols.insert(res, pred);
  return res;
}

/**
 * Return a propositional predicate name that can be used to represent
 * BDD variable @b var
 */
string BDD::getPropositionalPredicateName(int var)
{
  CALL("BDD::getPropositionalPredicateName");

  string name;
  if(!getNiceName(var, name)) {
    string prefix(BDD_PREDICATE_PREFIX);
    prefix+=env -> options->namePrefix();
    name = prefix + Int::toString(var);

    //We do not want the predicate to be already present!
    //(But we also don't want to insert it into signature,
    //as it would grow too much.)
    ASS(!env -> signature->isPredicateName(name, 0));
  }

  return name;
}

/**
 * If BDD variable has a corresponding propositional predicate symbol,
 * return true, and assign its name to @b res. Otherwise return false.
 */
bool BDD::getNiceName(int var, string& res)
{
  CALL("BDD::getNiceName");

  unsigned pred;
  bool found=_predicateSymbols.find(var, pred);
  if(found) {
    res=env -> signature->predicateName(pred);
  }
  return found;
}

/**
 * If BDD variable has a corresponding propositional predicate symbol,
 * return pointer to its Signature::Symbol object. Otherwise return 0.
 */
Signature::Symbol* BDD::getSymbol(int var)
{
  CALL("BDD::getSymbol");

  unsigned pred;
  if(_predicateSymbols.find(var, pred)) {
    return env -> signature->getPredicate(pred);
  }
  return 0;
}

/**
 * Return BDD representing an atomic
 */
BDDNode* BDD::getAtomic(int varNum, bool positive)
{
  CALL("BDD::getAtomic");
  ASS_G(varNum,0);

  if(varNum>=_newVar) {
    _newVar=varNum+1;
  }

  if(positive) {
    return getNode(varNum, getTrue(), getFalse());
  } else {
    return getNode(varNum, getFalse(), getTrue());
  }
}

/**
 * If @b node is an atomic BDD (contains exactly one variable),
 * assign its variable into @b var, into @b positive assign
 * the polarity of the variable, and return true. If @b node is
 * not atomic, return false.
 */
bool BDD::parseAtomic(BDDNode* node, unsigned& var, bool& positive)
{
  CALL("BDD::parseAtomic");

  if(isConstant(node)) {
    return false;
  }
  if(!isConstant(node->_pos) || !isConstant(node->_neg)) {
    return false;
  }
  if(isTrue(node->_pos)!=isFalse(node->_neg)) {
    return false;
  }

  var=node->_var;
  positive=isTrue(node->_pos);
  return true;
}

/**
 * Find variables that have a trivial role in a non-atomic BDD @c n --- either
 * those implied (i.e. the formula can be written as (v1 & ... & vN & F) for
 * some F) or those that imply the formula (i.e. the formula can be written as
 * (v1=>...=>vN=>F), resp as (~v1 | ... | ~vN | F)).
 *
 * This function is complete in the sense that all implied and implying
 * variables are found.
 *
 * @param n BDD to be examined. It must be non-atomic.
 *
 * @param areImplied if true, we will look for implied variables, otherwise
 *             we will look for implying ones.
 *
 * @param acc where the trivial variables will be put in the form of atomic
 *            BDD nodes. Initially must be empty.
 *
 * Theorem:
 * In a non-atomic BDD there can be either implied or implying variables,
 * but not both at the same time.
 */
bool BDD::findTrivial(BDDNode* n, bool& areImplied, Stack<BDDNode*>& acc)
{
  CALL("BDD::findTrivial");
  ASS(acc.isEmpty());
  //n must be non-atomic
  ASS(!n->isAtomic());

  if(n->isConst()) {
    return false;
  }

  //invariant: only non-constant BDDs are put into the queue
  static Deque<BDDNode*> que;
  que.reset();

  que.push_back(n);
  que.push_back(0);

  bool foundSome = false;

  bool haveTrueAside = false;
  bool haveFalseAside = false;

  unsigned nextVar = n->getVar();

  while(que.size()>1) {
    unsigned currVar = nextVar;
    nextVar = 0;

    bool canBeImpliedTrue = !haveTrueAside;
    bool canBeImpliedFalse = !haveTrueAside;
    bool canBeImplying = !haveFalseAside;
    bool canNegBeImplying = !haveFalseAside;

    //from this point haveTrueAside and haveFalseAside will be updated to
    //reflect situation on the next level

    while(que.front()!=0) {
      BDDNode* curr = que.pop_front();
      ASS(curr);
      if(curr->getVar()==currVar) {
	BDDNode* pos = curr->getPos();
	BDDNode* neg = curr->getNeg();

	if(pos->isFalse() || neg->isFalse()) {
	  haveFalseAside = true;
	}
	if(pos->isTrue() || neg->isTrue()) {
	  haveTrueAside = true;
	}

	canBeImpliedTrue &= neg->isFalse();
	canBeImpliedFalse &= pos->isFalse();
	canBeImplying &= neg->isTrue();
	canNegBeImplying &= pos->isTrue();

	if(!pos->isConst()) {
	  if(nextVar<pos->getVar()) { nextVar = pos->getVar(); }
	  que.push_back(pos);
	}
	if(!neg->isConst()) {
	  if(nextVar<neg->getVar()) { nextVar = neg->getVar(); }
	  que.push_back(neg);
	}
      }
      else {
	ASS_L(curr->getVar(),currVar);

	canBeImpliedTrue = false;
	canBeImpliedFalse = false;
	canBeImplying = false;
	canNegBeImplying = false;

	if(nextVar<curr->getVar()) { nextVar = curr->getVar(); }
	que.push_back(curr);
      }
    }
    ALWAYS(que.pop_front()==0);
    que.push_back(0);

    ASS(~canBeImpliedTrue || ~canBeImpliedFalse);
    if(canBeImpliedTrue) {
      ASS(!foundSome || areImplied);
      foundSome = true;
      areImplied = true;
      acc.push(getAtomic(currVar, true));
    }
    if(canBeImpliedFalse) {
      ASS(!foundSome || areImplied);
      foundSome = true;
      areImplied = true;
      acc.push(getAtomic(currVar, false));
    }
    ASS(~canBeImplying || ~canNegBeImplying);
    if(canBeImplying) {
      ASS(!foundSome || !areImplied);
      foundSome = true;
      areImplied = false;
      acc.push(getAtomic(currVar, true));
    }
    if(canNegBeImplying) {
      ASS(!foundSome || !areImplied);
      foundSome = true;
      areImplied = false;
      acc.push(getAtomic(currVar, false));
    }
  }
  COND_TRACE("bdd_triv_vars", foundSome,
      tout << "found trivial (" << (areImplied ? "implied" : "implying") << ") variables in BDD." << endl;
      tout << "  BDD: " << toTPTPString(n,"n") << endl;
      tout << "  vars: ";
      Stack<BDDNode*>::BottomFirstIterator vit(acc);
      while(vit.hasNext()) {
	tout << toTPTPString(vit.next(), "n");
	if(vit.hasNext()) {
	  tout << ", ";
	}
      }
      tout << endl;
      );
  ASS(!foundSome || assignValue(n, acc.top()->getVar(), !acc.top()->getPos()->isTrue())->isConst());
  return foundSome;
}

/**
 * Return conjunction of @b n1 and @b n2
 */
BDDNode* BDD::conjunction(BDDNode* n1, BDDNode* n2)
{
  CALL("BDD::conjunction");
  BDDNode* res=getBinaryFnResult(n1,n2, ConjunctionFn(this));

  if(isRefuted(n1) && isRefuted(n2)) {
    markRefuted(res);
  }

  return res;
}

/**
 * Return disjunction of @b n1 and @b n2
 */
BDDNode* BDD::disjunction(BDDNode* n1, BDDNode* n2)
{
  CALL("BDD::disjunction");
  BDDNode* res=getBinaryFnResult(n1,n2, DisjunctionFn(this));

  if(isRefuted(n1) || isRefuted(n2)) {
    markRefuted(res);
  }

  return res;
}

/**
 * Return result of @b x | ~ @b y
 */
BDDNode* BDD::xOrNonY(BDDNode* x, BDDNode* y)
{
  CALL("BDD::xOrNonY");
  return getBinaryFnResult(x,y, XOrNonYFn(this));
}

BDDNode* BDD::assignValue(BDDNode* n, unsigned var, bool value)
{
  CALL("BDD::assignValue");

  BDDNode* asgnArg = getAtomic(var, value);
  return getBinaryFnResult(asgnArg, n, AssignFn(this));
}

/**
 * Return true iff @b x | ~ @b y is a constant formula with truth value
 * equal to @b resValue.
 */
bool BDD::isXOrNonYConstant(BDDNode* x, BDDNode* y, bool resValue)
{
  CALL("BDD::isXOrNonYConstant");
  if(resValue) {
    bool res=hasConstantResult<true>(x,y, XOrNonYFn(this));

    if(res && isRefuted(y)) {
      markRefuted(x);
    }

    return res;
  }
  else {
    return hasConstantResult<false>(x,y, XOrNonYFn(this));
  }
}

/**
 * Return result of applying the binary function specified by the
 * BinBoolFn functor to @b n1 and @b n2. The binary functor BinBoolFn
 * must allow to be called as
 *
 * @code
 * BDDNode* BinBoolFn(BDDNode* m1, BDDNode* m2)
 * @endcode
 *
 * and return either the result of applying the represented binary
 * function to @b m1 and @b m2, or 0 in case the result cannot
 * be determined locally by examining just the BDDNode objects pointed
 * by @b m1 and @b m2. It must not return 0 if both arguments are
 * either true or false formulas.
 */
template<class BinBoolFn>
BDDNode* BDD::getBinaryFnResult(BDDNode* n1, BDDNode* n2, BinBoolFn fn)
{
  CALL("BDD::getBinaryFnResult");
  ASS(n1);
  ASS(n2);

  TimeCounter tc(TC_BDD);

  int counter=0;

  static Stack<BDDNode*> toDo(8);
  //Results stack contains zeroes and proper pointers standing for
  //intermediate results.
  //It can be viewed as a prefix of an expression in prefix notation
  //with 0 being a binary function and non-zeroes constants.
  //The expression is being simplified every time a well formed
  //subexpression (i.e. zero followed by two non-zeroes) appears.
  //
  //For the purpose of caching, each 0 is preceeded by two pointers that
  //will be used as a key in the cache to store the result when it is
  //assembled.
  static Stack<BDDNode*> results(8);
  //Variable numbers to be used for building intermediate results in
  //the results stack.
  static Stack<int> vars(8);

  static Cache<pair<BDDNode*,BDDNode*>, BDDNode*, PtrPairSimpleHash > cache;
  //if the cache was not reset, too much memory would be consumed
  cache.resetEvictionCounter();

  for(;;) {
    counter++;
    if(counter==50000) {
      counter=0;
      //time to check if we aren't over the time limit
      if(env -> timeLimitReached()) {
	throw TimeLimitExceededException();
      }
    }
    if(BinBoolFn::commutative) {
      if(n1>n2) {
	swap(n1,n2);
      }
    }
    BDDNode* res=fn(n1,n2);
    if(res || cache.find(make_pair(n1, n2), res)) {
      while(results.isNonEmpty() && results.top()!=0) {
	BDDNode* pos=results.pop();
	BDDNode* neg=res;
	unsigned var=vars.pop();
	if(pos==neg) {
	  res=pos;
	} else {
	  res=getNode(var, pos, neg);
	}
	ASS_EQ(results.top(),0);
	results.pop();
	BDDNode* arg1=results.pop();
	BDDNode* arg2=results.pop();
	cache.insert(make_pair(arg1, arg2), res);

	if(BinBoolFn::op==DISJUNCTION) {
	  if(isRefuted(arg1) || isRefuted(arg2)) {
	    markRefuted(res);
	  }
	}

      }
      results.push(res);
    } else {
      //we split at variables with higher numbers first
      unsigned splitVar=max(n1->_var, n2->_var);
      ASS_G(splitVar,0);
      toDo.push((n2->_var==splitVar) ? n2->_neg : n2);
      toDo.push((n1->_var==splitVar) ? n1->_neg : n1);
      toDo.push((n2->_var==splitVar) ? n2->_pos : n2);
      toDo.push((n1->_var==splitVar) ? n1->_pos : n1);
      results.push(n2);
      results.push(n1);
      results.push(0);
      //now push arguments at the stack, so that we know store the
      //answer into the cache
      vars.push(splitVar);
    }

    if(toDo.isEmpty()) {
      break;
    }
    n1=toDo.pop();
    n2=toDo.pop();
  }

  ASS(toDo.isEmpty());
  ASS_EQ(results.length(),1);
  return results.pop();
}

/**
 * Return true iff the result of applying the binary function specified
 * by the BinBoolFn functor to @b n1 and @b n2 is a constant formula with truth
 * value equal to @b ResValue.
 *
 * The binary functor BinBoolFn must allow to be called as
 *
 * @code
 * BDDNode* BinBoolFn(BDDNode* m1, BDDNode* m2)
 * @endcode
 *
 * and return either the result of applying the represented binary
 * function to @b m1 and @b m2, or 0 in case the result cannot
 * be determined locally by examining just the BDDNode objects pointed
 * by @b m1 and @b m2. It must not return 0 if both arguments are
 * either true or false formulas.
 *
 * @b ResValue must be a template argument, so that we would have separate
 * caches for ResValue==true and ResValue==false.
 */
template<bool ResValue, class BinBoolFn>
bool BDD::hasConstantResult(BDDNode* n1, BDDNode* n2, BinBoolFn fn)
{
  CALL("BDD::hasConstantResult");
  ASS(n1);
  ASS(n2);

  TimeCounter tc(TC_BDD);

  int counter=0;

  static Stack<BDDNode*> toDo(8);
  toDo.reset();

  static Stack<pair<BDDNode*,BDDNode*> > current(8);
  current.reset();

  static Cache<pair<BDDNode*,BDDNode*>, EmptyStruct, PtrPairSimpleHash > examined;
  examined.resetEvictionCounter();

  for(;;) {
    counter++;
    if(counter==50000) {
      counter=0;
      //time to check if we aren't over the time limit
      if(env -> timeLimitReached()) {
	throw TimeLimitExceededException();
      }
    }
    BDDNode* res=fn(n1,n2);
    if(res) {
      if( (ResValue && !isTrue(res)) ||
	      (!ResValue && !isFalse(res))) {
	return false;
      }
    }
    else {
      if(!examined.find(make_pair(n1, n2)))
      {
	current.push(make_pair(n1, n2));
	toDo.push(0);

	//we split at variables with higher numbers first
	unsigned splitVar=max(n1->_var, n2->_var);
	ASS_G(splitVar,0);
	toDo.push((n2->_var==splitVar) ? n2->_neg : n2);
	toDo.push((n1->_var==splitVar) ? n1->_neg : n1);
	toDo.push((n2->_var==splitVar) ? n2->_pos : n2);
	toDo.push((n1->_var==splitVar) ? n1->_pos : n1);
      }
    }
    while(!toDo.isEmpty() && toDo.top()==0) {
      toDo.pop();
      ASS(current.isNonEmpty());
      if(counter%4) {
	examined.insert(current.top());
      }
      current.pop();
    }
    if(toDo.isEmpty()) {
      break;
    }
    n1=toDo.pop();
    n2=toDo.pop();
    ASS(n1);
    ASS(n2);
  }

  return true;
}

/**
 * If it is possible to locally determine the result of the conjunction
 * of @b n1 and @b n2, return the result, otherwise return 0.
 */
BDDNode* BDD::ConjunctionFn::operator()(BDDNode* n1, BDDNode* n2)
{
  if(_parent->isFalse(n1) || _parent->isFalse(n2)) {
    return _parent->getFalse();
  }
  if(_parent->isTrue(n1)) {
    return n2;
  }
  if(_parent->isTrue(n2)) {
    return n1;
  }
  if(n1==n2) {
    return n1;
  }
  return 0;
}

/**
 * If it is possible to locally determine the result of the disjunction
 * of @b n1 and @b n2, return the result, otherwise return 0.
 */
BDDNode* BDD::DisjunctionFn::operator()(BDDNode* n1, BDDNode* n2)
{
  if(n1==n2) {
    return n1;
  }
  if(_parent->isTrue(n1) || _parent->isTrue(n2)) {
    return _parent->getTrue();
  }
  if(_parent->isFalse(n1)) {
    return n2;
  }
  if(_parent->isFalse(n2)) {
    return n1;
  }
  return 0;
}

/**
 * If it is possible to locally determine the result of the operation
 * @b n1 | ~ @b n2, return the result, otherwise return 0.
 */
BDDNode* BDD::XOrNonYFn::operator()(BDDNode* n1, BDDNode* n2)
{
  if(n1==n2) {
    return _parent->getTrue();
  }
  if(_parent->isTrue(n1) || _parent->isFalse(n2)) {
    return _parent->getTrue();
  }
  if(_parent->isTrue(n2)) {
    return n1;
  }
  return 0;
}

/**
 * @b n1 is the assigned variable, or false in case we're already
 *   below the assignment variable's level
 */
BDDNode* BDD::AssignFn::operator()(BDDNode* n1, BDDNode* n2)
{
  CALL("BDD::AssignFn::operator ()");

  if(n1->isConst() || n2->isConst()) {
    //we're below the assignment level
    return n2;
  }
  if(n1->getVar()!=n2->getVar()) {
    //we're still above the decision level
    ASS_L(n1->getVar(),n2->getVar());
    return 0;
  }
  unsigned var;
  bool asgn;
  ALWAYS(_parent->parseAtomic(n1, var, asgn));
  ASS_EQ(var, n2->getVar());
  if(asgn) {
    return n2->getPos();
  }
  else {
    return n2->getNeg();
  }
}

/**
 * Return a BDD node containing variable @b varNum that points
 * positively to @b pos and negatively to @b neg BDD node.
 */
BDDNode* BDD::getNode(int varNum, BDDNode* pos, BDDNode* neg)
{
  CALL("BDD::getNode");
  ASS_G(varNum,0);
  ASS_L(varNum,_newVar);
  ASS_NEQ(pos,neg);

  //newNode contains either 0 or pointer to a BDDNode that
  //hasn't been used yet.
  static BDDNode* newNode=0;

  if(newNode==0) {
    newNode=new BDDNode();
    env -> statistics->bddMemoryUsage += sizeof(BDDNode);
  }

  newNode->_var=varNum;
  newNode->_pos=pos;
  newNode->_neg=neg;

  BDDNode* res=_nodes.insert(newNode);
  if(res==newNode) {
    newNode=0;
    res->_depth=max(pos->depth(), neg->depth())+1;
  }
  return res;
}


/**
 * Return a string representation of the formula represented by @b node.
 */
string BDD::toString(BDDNode* node)
{
  return getDefinition(node);
/*  string res="";
  static Stack<BDDNode*> nodes(8);
  nodes.push(node);
  while(nodes.isNonEmpty()) {
    BDDNode* n=nodes.pop();
    bool canPrintSeparator=true;
    if(n==0) {
      res+=") ";
    } else if(isTrue(n)) {
      res+="$true ";
    } else if(isFalse(n)) {
      res+="$false ";
    } else {
      res+=string("( ")+getPropositionalPredicateName(n->_var)+" ? ";
      nodes.push(0);
      nodes.push(n->_neg);
      nodes.push(n->_pos);
      canPrintSeparator=false;
    }
    if(canPrintSeparator && nodes.isNonEmpty() && nodes.top()) {
      res+=": ";
    }
  }
  return res;*/
}

/**
 * Return the formula represented by @b node in a TPTP compatible format.
 * The @b bddPrefix string will be added as a prefix to the each BDD
 * variable number to form a predicate symbol name.
 *
 * @warning A recursion is used in this methos, which can lead to
 *   problems with very large BDDs.
 */
string BDD::toTPTPString(BDDNode* node, string bddPrefix)
{
  if(isTrue(node)) {
    return "$true";
  } else if(isFalse(node)) {
    return "$false";
  } else if(isTrue(node->_pos) && isFalse(node->_neg)) {
    return bddPrefix+Int::toString(node->_var);
  } else if(isFalse(node->_pos) && isTrue(node->_neg)) {
    return "~"+bddPrefix+Int::toString(node->_var);
  } else {
    return string("( ( ")+bddPrefix+Int::toString(node->_var)+" => "+toTPTPString(node->_pos, bddPrefix)+
      ") & ( ~"+bddPrefix+Int::toString(node->_var)+" => "+toTPTPString(node->_neg, bddPrefix)+" ) )";
  }
}

/**
 * Return the formula represented by @b node in a TPTP compatible format.
 *
 * @warning A recursion is used in this method, which can lead to
 *   problems with very large BDDs.
 */
string BDD::toTPTPString(BDDNode* node)
{
  if(isTrue(node)) {
    return "$true";
  }
  else if(isFalse(node)) {
    return "$false";
  }
  else {
    return string("( ( ")+getPropositionalPredicateName(node->_var)+" => "+toTPTPString(node->_pos)+
      ") & ( ~"+getPropositionalPredicateName(node->_var)+" => "+toTPTPString(node->_neg)+" ) )";
  }
}


string BDD::getDefinition(BDDNode* node)
{
  //predicate and function symbols are mixed here, but it's how I understood it should be done
  if(isTrue(node)) {
    return "$true";
  }
  if(isFalse(node)) {
    return "$false";
  }

  string name;
  if(_nodeNames.find(node, name)) {
    return name;
  }

  string propPred=getPropositionalPredicateName(node->_var);
  if(isTrue(node->_pos) && isFalse(node->_neg)) {
    return propPred;
  }
  else if(isFalse(node->_pos) && isTrue(node->_neg)) {
    return "~"+propPred;
  }
  else if(isTrue(node->_pos)) {
    return "("+propPred+" | "+getDefinition(node->_neg)+")";
  }
  else if(isFalse(node->_neg)) {
    return "("+propPred+" & "+getDefinition(node->_pos)+")";
  }
  else if(isFalse(node->_pos)) {
    return "(~"+propPred+" & "+getDefinition(node->_neg)+")";
  }
  else if(isTrue(node->_neg)) {
    return "(~"+propPred+" | "+getDefinition(node->_pos)+")";
  }
  else {
    string posDef=getDefinition(node->_pos); //recursion here
    string negDef=getDefinition(node->_neg); //recursion here
    return introduceName(node, "("+propPred+" ? "+posDef+" : "+negDef+")");
  }

}

string BDD::introduceName(BDDNode* node, string definition)
{
  ASS(!_nodeNames.find(node));
  string name="$bddnode"+Int::toString(_nextNodeNum++);
  string report="BDD definition: "+name+" = "+definition;
  outputDefinition(report);
  ALWAYS(_nodeNames.insert(node, name));

  return name;
}

void BDD::allowDefinitionOutput(bool allow)
{
  _allowDefinitionOutput=allow;
  if(allow && _postponedDefinitions.isNonEmpty()) {
    unsigned stLen=_postponedDefinitions.size();
    env -> beginOutput();
    for(unsigned i=0;i<stLen;i++) {
      env -> out()<<_postponedDefinitions[i]<<endl;
    }
    env -> endOutput();
    _postponedDefinitions.reset();
  }
}

void BDD::outputDefinition(string def)
{
  if(_allowDefinitionOutput) {
    env -> beginOutput();
    env -> out()<<def<<endl;
    env -> endOutput();
  }
  else {
    _postponedDefinitions.push(def);
  }

}


string BDD::getName(BDDNode* node)
{
  string name;
  if(!_nodeNames.find(node, name)) {
    string def=getDefinition(node);
    //the name could have been introduced by the getDefinition
    if(!_nodeNames.find(node, name)) {
      name=introduceName(node, def);
    }
  }
  return name;
}

TermList BDD::getConstant(BDDNode* node)
{
  TermList res;
  if(!_nodeConstants.find(node, res)) {
    string name=getName(node);
    unsigned func;
    bool added;

    func=env -> signature->addFunction(name, 0, added);
    while(!added) {
      name+="_1";
      func=env -> signature->addFunction(name, 0, added);
      if(added) {
        string report="Name collision, BDD node now uses name "+name;
	outputDefinition(report);
        _nodeNames.set(node, name);
      }
    }
    res=TermList(Term::create(func, 0, 0));
    _nodeConstants.insert(node, res);
  }
  return res;
}

/**
 * Check whether two BDDNode objects are equal
 */
bool BDD::equals(const BDDNode* n1,const BDDNode* n2)
{
  return n1->_var==n2->_var && n1->_pos==n2->_pos && n1->_neg==n2->_neg;
}
/**
 * Return hash value of a BDDNode object
 */
unsigned BDD::hash(const BDDNode* n)
{
  CALL("BDD::hash");

  unsigned res=Hash::hash(n->_var);
  res=Hash::hash(n->_pos, res);
  res=Hash::hash(n->_neg, res);
  return res;
}

/**
 * Convert a BDD to formula
 *
 * @warning Currently the function uses recursion, so there can
 * be problems for very large variable counts.
 */
Formula* BDD::toFormula(BDDNode* node)
{
  if(isTrue(node)) {
    static Formula* tf=new Formula(true);
    return tf;
  } else if(isFalse(node)) {
    static Formula* ff=new Formula(false);
    return ff;
  }

  if(!_bddEvalPredicate) {
    string name="$bddEval";
    bool added;
    _bddEvalPredicate=env -> signature->addPredicate(name, 1, added);
    while(!added) {
      name+="_1";
      _bddEvalPredicate=env -> signature->addPredicate(name, 1, added);
    }
    ASS(_bddEvalPredicate);
  }
  TermList bddConst=getConstant(node);
  Literal* lit=Literal::create(_bddEvalPredicate,1,true,false,&bddConst);
  return new AtomicFormula(lit);

/*  unsigned var=node->_var;
  unsigned predNum;
  if(!_predicateSymbols.find(var, predNum)) {
    string name=getPropositionalPredicateName(var);
    bool added;
    predNum=env -> signature->addPredicate(name, 0, added);
    ASS(added);
    _predicateSymbols.insert(var, predNum);
  }
  Literal* posLit=Literal::create(predNum, 0, true, false, 0);
  Literal* negLit=Literal::create(predNum, 0, false, false, 0);

  FormulaList* args=0;
  FormulaList::push(new BinaryFormula(IMP, new AtomicFormula(posLit) ,toFormula(node->_pos)), args);
  FormulaList::push(new BinaryFormula(IMP, new AtomicFormula(negLit) ,toFormula(node->_neg)), args);

  return new JunctionFormula(AND, args);*/
}


}
