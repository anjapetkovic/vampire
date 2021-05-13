/*
 * This file is part of the source code of the software program
 * Vampire. It is protected by applicable
 * copyright laws.
 *
 * This source code is distributed under the licence found here
 * https://vprover.github.io/license.html
 * and in the source directory
 */

#include "Test/UnitTesting.hpp"
#include "Test/SyntaxSugar.hpp"
#include "Indexing/TermSharing.hpp"
#include "Inferences/IRC/Totality.hpp"
#include "Inferences/InterpretedEvaluation.hpp"
#include "Kernel/Ordering.hpp"
#include "Inferences/PolynomialEvaluation.hpp"
#include "Inferences/Cancellation.hpp"

#include "Test/SyntaxSugar.hpp"
#include "Test/TestUtils.hpp"
#include "Lib/Coproduct.hpp"
#include "Test/SimplificationTester.hpp"
#include "Test/GenerationTester.hpp"
#include "Kernel/KBO.hpp"
#include "Indexing/TermSubstitutionTree.hpp"
#include "Inferences/PolynomialEvaluation.hpp"

using namespace std;
using namespace Kernel;
using namespace Inferences;
using namespace Test;
using namespace Indexing;
using namespace Inferences::IRC;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////// TEST CASES 
/////////////////////////////////////

#define SUGAR(Num)                                                                                            \
  NUMBER_SUGAR(Num)                                                                                           \
  DECL_DEFAULT_VARS                                                                                           \
  DECL_FUNC(f, {Num}, Num)                                                                                    \
  DECL_FUNC(g, {Num, Num}, Num)                                                                               \
  DECL_CONST(a, Num)                                                                                          \
  DECL_CONST(b, Num)                                                                                          \
  DECL_CONST(c, Num)                                                                                          \
  DECL_PRED(r, {Num,Num})                                                                                     \

#define MY_SYNTAX_SUGAR SUGAR(Rat)


inline Stack<Indexing::Index*> indices() 
{ 
  auto uwa = Options::UnificationWithAbstraction::ONE_INTERP;
  return {
    new InequalityResolutionIndex( new TermSubstitutionTree(uwa, true))
  };
}

Totality testTotality(
    Options::UnificationWithAbstraction uwa = Options::UnificationWithAbstraction::ONE_INTERP
    )
{ return Totality(testIrcState(uwa)); }

REGISTER_GEN_TESTER(Test::Generation::GenerationTester<Totality>(testTotality()))

/////////////////////////////////////////////////////////
// Basic tests
//////////////////////////////////////

TEST_GENERATION(basic01,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  a >= 0 )  }) )
      .context ({ clause({ selected( -a >= 0 )  }) })
      .expected(exactly(
            clause({ a == 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic02,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  a + -1 >= 0 )  }) )
      .context ({ clause({ selected( -a +  1 >= 0 )  }) })
      .expected(exactly(
            clause({ a + -1 == 0, num(1) + num(-1) != 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic03,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected( 3 * a + -1 >= 0 )  }) )
      .context ({ clause({ selected( 2 * a + -1 >= 0 )  }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic04a,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  c - b >= 0 )  }) )
      .context ({ clause({ selected( -c + a >= 0 )  }) })
      .expected(exactly(
            clause({ c - b == 0, a - b != 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic04b,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  a - b >= 0 )  }) )
      .context ({ clause({ selected( -a + c >= 0 )  }) })
      .expected(exactly( /* c > b > a */ ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic05,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  f(x) - b >= 0 )  }) )
      .context ({ clause({ selected( -f(a) + c >= 0 )  }) })
      .expected(exactly(
            clause({ f(a) - b == 0, c - b != 0  })
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic06,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  f(x) - b >= 0 )  }) )
      .context ({ clause({ selected(  f(a) + c >= 0 )  }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic07,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  f(b) >= 0 )  }) )
      .context ({ clause({ selected( -f(a) >= 0 )  }) })
      .expected(exactly(
      ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic08,
    Generation::TestCase()
      .indices(indices())
      .input   (         clause({ selected(  3 * f(b) >= 0 )  }) )
      .context ({        clause({ selected( -2 * f(x) >= 0 )  }) })
      .expected(exactly( clause({            3 * f(b) == 0    }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(basic09,
    Generation::TestCase()
      .indices(indices())
      .input   (         clause({ selected(  3 * f(b) + 7 >= 0 )  }) )
      .context ({        clause({ selected( -2 * f(x) + a >= 0 )  }) })
      .expected(exactly( clause({ 3 * f(b) + 7 == 0, 14 + 3 * a != 0  }) ))
      .premiseRedundant(false)
    )

TEST_GENERATION(uwa,
    Generation::TestCase()
      .indices(indices())
      .input   (  clause({ selected(  f(x + a) - b >= 0 )  }) )
      .context ({ clause({ selected( -f(b) + c >= 0 )  }) })
      .expected(exactly(
            clause({ f(x + a) - b == 0, c - b != 0, x + a != b  })
      ))
      .premiseRedundant(false)
    )
