/*
This tests the situation where a reduce intent and/or in intent
occurs in a forall loop where the parallel iterator itself
contains a forall loop.

See also:
  test/reductions/bharshbarg/reduceWithoutTag.chpl
  other tests in this directory esp. reduce*with-forall-in-par-iter.chpl
*/

config const locs = 3;
config const mm = 3;

//
// myiter() iterator ensures deterministic execution (using cnt$)
// while still creating bona fide parallel tasks
//

var MyDomain = LocaleSpace;
if numLocales == 1 then MyDomain = {1..locs};
var MyLocales: [MyDomain] locale = for i in MyDomain do Locales[i%numLocales];
//writeln(MyLocales);

iter myiter() {
  yield 555;
}

iter myiter(param tag: iterKind) where tag == iterKind.standalone {
  var cnt$: sync int = 1;
  coforall curloc in MyLocales do on curloc {
      const current = cnt$.readFE();
    writef("myiter start %i\n", current);
    for jjj in 1..mm {
      yield current * 100 + jjj;
    }
    writef("myiter done  %i\n", current);
    cnt$.writeEF(current + 1);
  }
}

//
// pariter1 and pariter2 are the same except 1 vs. 2 yields
// test1 and test2 are the same except testing pariter1 vs. pariter2
//

iter pariter1() {
  yield 333;
}

iter pariter1(param tag: iterKind) {
  writeln("starting pariter1");
  forall kkk in myiter() {
    yield kkk;
  }
  writeln("finishing pariter1");
}

iter pariter2() {
  yield 333;
}

iter pariter2(param tag: iterKind) {
  writeln("starting pariter2");
  forall kkk in myiter() {
    yield kkk;
    yield 10000 + kkk;
  }
  writeln("finishing pariter2");
}

type ART = [1..3] int;

proc test1 {
  var result1:ART = 10000, result2:ART = 7;
  forall iii in pariter1() with (in result1, + reduce result2) {
    const before1 = result1;
    const before2 = result2;
    result1 += iii;
    result2 += iii;
    writef("loop %?    %? -> %?    %? -> %?\n",
           iii, before1, result1, before2, result2);
  }
  writeln("result1 = ", result1);
  writeln("result2 = ", result2);
}

proc test2 {
  var result1:ART = 20000, result2:ART = 9;
  forall iii in pariter2() with (in result1, + reduce result2) {
    const before1 = result1;
    const before2 = result2;
    result1 += iii;
    result2 += iii;
    writef("loop %?    %? -> %?    %? -> %?\n",
           iii, before1, result1, before2, result2);
  }
  writeln("result1 = ", result1);
  writeln("result2 = ", result2);
}

proc main {
  test1;
  test2;
  writeln(+ reduce pariter1());
  writeln(+ reduce pariter2());
}
