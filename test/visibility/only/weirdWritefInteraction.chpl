//== weirdWritefInteraction.chpl ==
// This test failed with 1.18.0 but not with 1.19.0 (pre-release, Feb 11 2019).
// Track it in case the problem was only hidden, not resolved.
use IO;

//use mydef;              // OK
use mydef only Mytype;    // error

proc main()
{
    var ownA = new owned Mytype();
    var a = ownA.borrow();
    writeln( a );

    var file = openWriter( "test.dat" );

    file.writef( "%10.3dr\n", 1.23 );
    // file.writeln( 1.23 );
}
