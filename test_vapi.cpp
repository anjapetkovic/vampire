/**
 * @file test_vapi.cpp
 * Source of the test executable for the libvapi.
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "Api/FormulaBuilder.hpp"
#include "Api/Problem.hpp"

using namespace std;
using namespace Api;

void printProblem(Problem p)
{
  cout<<"____"<<endl;
  AnnotatedFormulaIterator fit=p.formulas();
  while(fit.hasNext()) {
    cout<<fit.next()<<endl;
  }
  cout<<"^^^^"<<endl;
}

void clausifyTest(const char* fname)
{
  ifstream fs(fname);
  Problem p;
  p.addFromStream(fs);

  Problem p2=p.clausify();
}

int main(int argc, char* argv [])
{

  if(argc==2) {
    clausifyTest(argv[1]);
  }

  FormulaBuilder api(true);

  Var xv = api.var("X"); // variable x
  Var yv = api.var("Y"); // variable y
  Term x =  api.varTerm(xv); // term x
  Term y =  api.varTerm(yv); // term y
  Function f = api.function("f",1);
  Term fx = api.term(f,x); // f(x)
  Term fy = api.term(f,y); // f(y)
  Formula lhs = api.equality(fx,fy); // f(x) = f(y)
  Predicate p=api.predicate("p",3);
  Formula rhs = api.formula(p,x,fx,fy); // p(X0,f(X0),f(X1))

  Formula form = api.formula(FormulaBuilder::IFF,lhs,rhs);
  AnnotatedFormula af = api.annotatedFormula(form, FormulaBuilder::CONJECTURE);


  cout<<af<<endl;

  Problem p1;
  p1.addFormula(af);
  printProblem(p1);

  string fs=af.toString();

  stringstream sstr(fs);

  Problem p2;
  p2.addFromStream(sstr);
  printProblem(p2);

  Problem p3=p2.clausify();
  printProblem(p3);

  AnnotatedFormulaIterator fit=p3.formulas();
  fit.hasNext();
  cout<<"deleting "<<fit.next()<<endl;
  fit.del();

  printProblem(p3);

  ifstream finp("Problems/PUZ/PUZ001+1.p");
  if(!finp.fail()) {
    Problem p4;
    p4.addFromStream(finp);
    printProblem(p4);

    Problem p5=p4.clausify();
    printProblem(p5);
  }

  return 0;
}
