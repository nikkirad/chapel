class myclass {
  var x: int;
  var y: real;
}

override proc myclass.writeThis(f) throws {
  f.write(x, " ", y);
}

var ownA = new owned myclass();
var a: borrowed myclass = ownA.borrow();
var ownB = new owned myclass();
var b: borrowed myclass = ownB.borrow();

writeln("a is: ", a, ", b is: ", b);

a.x = 1;
a.y = 2.3;

b.x = 4;
b.y = 5.6;

writeln("a is: ", a, ", b is: ", b);

a = b;

a.x = 7;
b.y = 8.9;

writeln("a is: ", a, ", b is: ", b);
