/*  This is the Chapel version of Random Access HPC benchmark.
 *
 */  
use Math;

param POLY:uint(64) = 7;

config const verify = true;
config const ErrorTolerance = 0.01;
config const showtiming = false;

config const TotalMemSize:int = 100000;
const LogTableSize:int  = log2(TotalMemSize/2);
const TableSize:uint(64) = (1 << LogTableSize):uint(64);

const NumUpdates:uint(64) = 4*TableSize;
const NumStreams:int = 1 << 9;
const BigStep:int = (NumUpdates:int)/NumStreams;

const TableDomain = {0..TableSize-1};
var Table: [TableDomain] uint(64);

const RandStepsDomain = {0..63};
var RandomSteps: [RandStepsDomain] uint(64);

const StreamDomain = {0..NumStreams-1};
const BigStepDomain = {0..BigStep-1};
const UpdateDomain: domain(2) = {0..NumStreams-1,0..BigStep-1};

var RealTime:real;
var GUPs:real;

proc main() {

  use Time;
  var t:stopwatch;

  writeRAdata();

  InitRandomSteps();

  t.clear();
  t.start();

  RandomAccessUpdate();

  t.stop();

  RealTime = t.elapsed();

  GUPs = (if (RealTime > 0.0) then (1.0 / RealTime) else -1.0);
  GUPs *= 1.0e-9*NumUpdates;

  if (showtiming) then writeRAresults();

  if (verify) then VerifyResults();
}

proc RandomAccessUpdate() {

  [i in TableDomain] Table(i) = i;
  
  for j in StreamDomain {
    var ran:uint(64) = RandomStart(BigStep*j);
    for i in BigStepDomain {
      ran = (ran << 1) ^ (if (ran:int(64) < 0) then POLY else 0);
      Table(ran & (TableSize-1)) ^= ran;
    }
  }
}

proc RandomStart(step0:int):uint(64) {

  var i:int;
  var ran:uint(64) = 2;

  if (step0 ==0) then 
    return 1;
  else
    i = log2(step0);
  while (i > 0) do {
    var temp:uint(64) = 0;
    for j in RandStepsDomain {
      if (( ran >> j) & 1) then temp ^= RandomSteps(j);
    }
    ran = temp;
    i -= 1;
    if (( step0 >> i) & 1) then
      ran = (ran << 1) ^ (if (ran:int(64) < 0) then POLY else 0);
  }
  return ran;
}

proc InitRandomSteps() {
  
  var temp:uint(64) = 1;

  for i in RandStepsDomain {
    RandomSteps(i) = temp;
    temp = (temp << 1) ^ (if (temp:int(64) < 0) then POLY else 0);
    temp = (temp << 1) ^ (if (temp:int(64) < 0) then POLY else 0);
  }
}

proc VerifyResults() {

  var temp: uint(64) = 1;  
  for i in UpdateDomain {
    temp = (temp << 1) ^ (if (temp:int(64) < 0) then POLY else 0);
    Table(temp & (TableSize-1)) ^= temp;  
  }

  var NumErrors = 0;
  for i in TableDomain {
    if (Table(i) != i) then
      NumErrors += 1;
  }

  write("Found ", NumErrors, " errors in ", TableSize, " locations ");
  if (NumErrors <= ErrorTolerance*TableSize) {
    writeln("(passed)");
  } else {
    writeln("(failed)");
  }
}

proc writeRAdata() {
  writeln("Main table size = 2^",LogTableSize," = ",TableSize," words");
  writeln("Number of updates = ",NumUpdates);
  writeln("Number of random streams = ",NumStreams);
  writeln("Length of each random stream = ",BigStep);
  writeln();
}

proc writeRAresults() {

  writeln("Real time used = ", RealTime, " seconds");
  writeln(GUPs, " Billion(10^9) Updates    per second [GUP/s]");
  writeln();
}
