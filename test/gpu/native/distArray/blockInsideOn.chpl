use BlockDist;
use GpuDiagnostics;


config const n = 10;

startGpuDiagnostics();
on here.gpus[0] {
  var space = {1..n};
  var dom = space dmapped Block(space, targetLocales=[here,]);
  var arr: [dom] int;

  forall i in dom do
    arr[i] = 1;

  /* The following does not work yet:

    forall a in arr do
      a = 1;

  */

  writeln(arr);
}

stopGpuDiagnostics();

assertGpuDiags(kernel_launch_um=4, kernel_launch_aod=6);
