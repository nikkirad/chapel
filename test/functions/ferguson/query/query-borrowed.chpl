class MyClass {
  var x;
}

class OtherClass {
  var y;
}

class Concrete {
  var z:int;
}

proc g(arg:borrowed MyClass(?t)) {
  writeln("g ", t:string);
}
proc h(arg:borrowed MyClass(borrowed OtherClass(?t))) {
  writeln("h ", t:string);
}

proc i(arg:borrowed) {
  writeln("i ", arg.type:string);
}
proc j(arg:borrowed MyClass) {
  writeln("j ", arg.type:string);
}
proc k(arg:borrowed MyClass(borrowed OtherClass)) {
  writeln("k ", arg.type:string);
}

proc ll(arg:borrowed MyClass) {
  writeln("ll borrowed MyClass ", arg.type:string);
}
proc ll(arg) {
  writeln("ll generic ", arg.type:string);
}

proc m(type t : borrowed MyClass) {
  writeln("m borrowed MyClass");
}
proc m(type t : borrowed RootClass) {
  writeln("m RootClass");
}

proc n(arg : borrowed MyClass) {
  writeln("n borrowed MyClass");
}
proc n(arg : borrowed RootClass) {
  writeln("n RootClass");
}



proc test() {
  var ownInner = new owned OtherClass(1);
  var ownA = new owned MyClass(ownInner.borrow());
  var a = ownA.borrow();
  var ownB = new owned Concrete();
  var b = ownB.borrow();
  g(a);
  h(a);
  i(a);
  j(a);
  k(a);
  ll(a);
  ll(b);
  m(borrowed MyClass(int));
  m(borrowed Concrete);
  n(a);
  n(b);
}

test();
