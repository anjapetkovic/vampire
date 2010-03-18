/**
 * @file CodeTree.cpp
 * Implements class CodeTree for code tree indexes.
 *
 */

#include "../Lib/Allocator.hpp"
#include "../Lib/DHMap.hpp"
#include "../Lib/Vector.hpp"

#include "../Kernel/Clause.hpp"
#include "../Kernel/FlatTerm.hpp"
#include "../Kernel/Term.hpp"

#if VDEBUG

#include "../Lib/Environment.hpp"
#include "../Lib/Int.hpp"
#include "../Kernel/Signature.hpp"

#endif

#include "CodeTree.hpp"

#define LOG_OP(x)
//#define LOG_OP(x) cout<<x<<endl

namespace Indexing
{

using namespace Lib;
using namespace Kernel;

#if VDEBUG

string CodeTree::OpCode::toString() const
{
  string res;
  switch(instr()) {
  case SUCCESS:
  case SUCCESS2:
    res+="suc";
    break;
  case CHECK_FUN:
    res+="chf:"+env.signature->functionName(arg());
    break;
  case ASSIGN_VAR:
    res+="asv:"+Int::toString(arg());
    break;
  case CHECK_VAR:
    res+="chv:"+Int::toString(arg());
    break;
  case FAIL:
    res+="fail";
    break;
  case NEXT_LIT:
    res+="nlit";
    break;
  }
  return res;
}

#endif

/**
 * Return true iff @b o is equal to the current object except
 * for the value of the @b alternative field
 */
inline bool CodeTree::OpCode::eqModAlt(const OpCode& o) const
{
#ifdef ARCH_X64
  if((data&3)==0) {
    //the operation is SUCCESS so all 64 bits are initialized
    return data==o.data;
  }
  else {
    //only the first 32 bits are initialized
    return (data&0xFFFFFFFF)==(o.data&0xFFFFFFFF);
  }
#else
  return result==o.result;
#endif
}


CodeTree::CodeTree()
: _maxVarCnt(0),
#if VDEBUG
  _initEContextCounter(0),
#endif
  _data(0)
{
  CALL("CodeTree::CodeTree");
}

void CodeTree::compile(Term* t, CodeStack& code, VarMap& varMap, unsigned& nextVarNum)
{
  CALL("CodeTree::compile(Term*...)");

  unsigned func=t->isLiteral() ? static_cast<Literal*>(t)->header() : t->functor();
  code.push(OpCode(CHECK_FUN, func));

  Term::SubtermIterator sti(t);
  while(sti.hasNext()) {
    TermList s=sti.next();
    if(s.isVar()) {
      unsigned var=s.var();
      unsigned* varNumPtr;
      if(varMap.getValuePtr(var,varNumPtr)) {
	*varNumPtr=nextVarNum++;
	code.push(OpCode(ASSIGN_VAR, *varNumPtr));
      }
      else {
	code.push(OpCode(CHECK_VAR, *varNumPtr));
      }
    }
    else {
      ASS(s.isTerm());
      code.push(OpCode(CHECK_FUN, s.term()->functor()));
    }
  }
}

/**
 * Build CodeBlock object from the last @b cnt instructions on the
 * @b code stack.
 */
CodeTree::CodeBlock* CodeTree::buildBlock(CodeStack& code, size_t cnt)
{
  CALL("CodeTree::buildBlock");

  size_t clen=code.length();
  ASS_LE(cnt,clen);

  CodeBlock* res=CodeBlock::allocate(cnt);
  size_t sOfs=clen-cnt;
  for(size_t i=0;i<cnt;i++) {
    ASS_EQ(code[i+sOfs].alternative,0); //the ops should not have an alternattive set yet
    (*res)[i]=code[i+sOfs];
  }
  return res;
}

void CodeTree::incorporate(CodeStack& code)
{
  CALL("CodeTree::incorporate");

  if(!_data) {
    _data=buildBlock(code, code.length());
    return;
  }

  size_t clen=code.length();
  OpCode* treeOp=&(*_data)[0];

  for(size_t i=0;i<clen;i++) {
    while(!code[i].eqModAlt(*treeOp) && treeOp->alternative) {
      treeOp=treeOp->alternative;
    }
    if(!code[i].eqModAlt(*treeOp)) {
      ASS(!treeOp->alternative);
      CodeBlock* rem=buildBlock(code, clen-i);
      treeOp->alternative=&(*rem)[0];
      LOG_OP(rem->toString()<<" incorporated at "<<treeOp->toString()<<" caused by "<<code[i].toString());
      return;
    }
    //we can safely do increase because as long as we match and something
    //remains in the @b code stack, we aren't at the end of the CodeBlock
    //either
    treeOp++;
  }
  //if we are here, we are inserting a clause/term multiple times
  ASS_EQ(treeOp->instr()&3,SUCCESS);

  //we insert it anyway becouse later we will be removing it multiple
  //times as well
  while(treeOp->alternative) {
    treeOp=treeOp->alternative;
  }
  CodeBlock* rem=buildBlock(code, 1);
  treeOp->alternative=&(*rem)[0];
  LOG_OP(rem->toString()<<" incorporated");
}

void CodeTree::EContext::init(CodeTree* tree)
{
  CALL("CodeTree::EContext::init");
  ASS(tree->_data); //the tree must already contain something

#if VDEBUG
  tree->_initEContextCounter++;
#endif

  fresh=true;
  tp=0;
  op=&(*tree->_data)[0];
  btStack.reset();
  bindings.ensure(tree->_maxVarCnt);

}

void CodeTree::EContext::deinit(CodeTree* tree)
{
  CALL("CodeTree::EContext::deinit");

#if VDEBUG
  ASS_G(tree->_initEContextCounter,0);
  tree->_initEContextCounter--;
#endif
}

inline bool CodeTree::EContext::backtrack()
{
  if(btStack.isEmpty()) {
    return false;
  }
  load(btStack.pop());
  return true;
}

inline bool CodeTree::EContext::doCheckFun()
{
  ASS_EQ(op->instr(), CHECK_FUN);

  unsigned functor=op->arg();
  FlatTerm::Entry fte=(*ft)[tp];
  if(fte.tag()!=FlatTerm::FUN || fte.number()!=functor) {
    return false;
  }
  else {
    tp+=FlatTerm::functionEntryCount;
  }
  return true;
}

inline void CodeTree::EContext::doAssignVar()
{
  ASS_EQ(op->instr(), ASSIGN_VAR);

  unsigned var=op->arg();
  const FlatTerm::Entry* fte=&(*ft)[tp];
  if(fte->tag()==FlatTerm::VAR) {
    bindings[var]=TermList(fte->number(),false);
    tp++;
  }
  else {
    ASS_EQ(fte->tag(), FlatTerm::FUN);
    fte++;
    ASS_EQ(fte->tag(), FlatTerm::FUN_TERM_PTR);
    ASS(fte->ptr());
    bindings[var]=TermList(fte->ptr());
    fte++;
    ASS_EQ(fte->tag(), FlatTerm::FUN_RIGHT_OFS);
    tp+=fte->number();
  }
}

inline bool CodeTree::EContext::doCheckVar()
{
  ASS_EQ(op->instr(), CHECK_VAR);

  unsigned var=op->arg();
  const FlatTerm::Entry* fte=&(*ft)[tp];
  if(fte->tag()==FlatTerm::VAR) {
    if(bindings[var]!=TermList(fte->number(),false)) {
      return false;
    }
    tp++;
  }
  else {
    ASS_EQ(fte->tag(), FlatTerm::FUN);
    fte++;
    ASS_EQ(fte->tag(), FlatTerm::FUN_TERM_PTR);
    if(bindings[var]!=TermList(fte->ptr())) {
      return false;
    }
    fte++;
    ASS_EQ(fte->tag(), FlatTerm::FUN_RIGHT_OFS);
    tp+=fte->number();
  }
  return true;
}



/////////////////////////

void TermCodeTree::compile(TermList t, CodeStack& code)
{
  CALL("TermCodeTree::compile(TermList...)");

  unsigned nextVarNum=0;

  if(t.isVar()) {
    code.push(OpCode(ASSIGN_VAR, nextVarNum++));
  }
  else {
    static VarMap varMap;
    varMap.reset();

    CodeTree::compile(t.term(), code, varMap, nextVarNum);
  }
  code.push(OpCode(SUCCESS));

  //update the max. number of variables, if necessary
  if(nextVarNum>_maxVarCnt) {
    _maxVarCnt=nextVarNum;
  }
}

void TermCodeTree::compile(Term* t, CodeStack& code)
{
  CALL("TermCodeTree::compile(TermList...)");

  unsigned nextVarNum=0;

  static VarMap varMap;
  varMap.reset();

  CodeTree::compile(t, code, varMap, nextVarNum);
  code.push(OpCode(SUCCESS));

  //update the max. number of variables, if necessary
  if(nextVarNum>_maxVarCnt) {
    _maxVarCnt=nextVarNum;
  }
}

void TermCodeTree::TermEContext::init(TermList t, TermCodeTree* tree)
{
  CALL("TermCodeTree::TermEContext::init(TermList...)");

  EContext::init(tree);

  ft=FlatTerm::create(t);
  _ownFlatTerm=true;
}

void TermCodeTree::TermEContext::init(Term* t, TermCodeTree* tree)
{
  CALL("TermCodeTree::TermEContext::init(Term*...)");

  EContext::init(tree);

  ft=FlatTerm::create(t);
  _ownFlatTerm=true;
}

void TermCodeTree::TermEContext::init(FlatTerm* flatTerm, TermCodeTree* tree)
{
  CALL("TermCodeTree::TermEContext::init(FlatTerm*...)");

  EContext::init(tree);

  ft=flatTerm;
  _ownFlatTerm=false;
}

void TermCodeTree::TermEContext::deinit(TermCodeTree* tree)
{
  CALL("TermCodeTree::TermEContext::deinit");

  if(_ownFlatTerm) {
    ft->destroy();
  }

  EContext::deinit(tree);
}

/**
 * Perform the operations of the code tree until a SUCCESS
 * operation is reached or the end of the code is reached
 */
bool TermCodeTree::next(TermEContext& ctx, void*& res)
{
  CALL("TermCodeTree::next");

  if(!ctx.fresh) {
    //we backtrack from what we found in the previous run
    if(!ctx.backtrack()) {
      return false;
    }
  }

  ctx.fresh=false;

  bool backtrack=false;
  for(;;) {
    if(ctx.op->alternative) {
      LOG_OP("alt at "<<ctx.tp);
      ctx.btStack.push(BTPoint(ctx.tp, ctx.op->alternative));
    }
    LOG_OP(ctx.tp<<':'<<ctx.op->toString());
    switch(ctx.op->instr()) {
    case SUCCESS:
    case SUCCESS2:
      res=ctx.op->result;
      return true;
    case CHECK_FUN:
      backtrack=!ctx.doCheckFun();
      break;
    case ASSIGN_VAR:
      ctx.doAssignVar();
      break;
    case CHECK_VAR:
      backtrack=!ctx.doCheckVar();
      break;
    case FAIL:
      backtrack=true;
      break;
    case NEXT_LIT:
      //the NEXT_LIT operation shuold not appear in a TermCodeTree
      ASSERTION_VIOLATION;
    }
    if(backtrack) {
      if(!ctx.backtrack()) {
	LOG_OP("not found");
	return false;
      }
      LOG_OP(ctx.tp<<"<-bt");
      backtrack=false;
    }
    else {
      //in each CodeBlock there is always either operation SUCCESS or FAIL,
      //so as we haven't encountered one yet, we may safely increase the
      //operation pointer
      ctx.op++;
    }
  }
}


/////////////////////////

void ClauseCodeTree::compile(Clause* c, CodeStack& code)
{
  CALL("ClauseCodeTree::compile(Clause*...)");

  unsigned clen=c->length();
  static DArray<Literal*> lits;
  lits.initFromArray(clen, *c);

  //here we can perform literal reordering

  static VarMap varMap;
  varMap.reset();
  unsigned nextVarNum=0;

  for(unsigned i=0;i<clen;i++) {
    code.push(OpCode(NEXT_LIT));
    CodeTree::compile(lits[i], code, varMap, nextVarNum);
  }
  code.push(OpCode(SUCCESS));

  //update the max. number of variables, if necessary
  if(nextVarNum>_maxVarCnt) {
    _maxVarCnt=nextVarNum;
  }
}





}












































