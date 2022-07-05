use IO; //test iostyle? (now deprecated)

@unstable "HDFS support in Chapel currently requires the use of CHPL_TASKS=fifo. There is a compatibility problem with qthreads...iomode.cwr and iomode.rw are not supported with HDFS files due to limitations in HDFS itself. iomode.r and iomode.cw are the only modes supported with HDFS."
use HDFS; //Hadoop Distributed File System.

//https://chapel-lang.org/docs/main/modules/packages/HDFS.html
hdfs fs = hdfsConnect("default", 0);
const char path = /Users/Nikki/Desktop;
hdfsFile writeFile = hdfsOpenFile(fs, writePath, O_WRONLY|O_CREAT, 0, 0, 0);
if(!writeFile) {
    writeln(stderr, "Failed to open %s for writing!", writePath);
    exit(-1);
}
char* buffer = "Hello, World!";
hdfsCloseFile(fs, writeFile);

/*
HDFS
not one of the things aiming to stabilize for 2.0
technically could be marked as unstable but what makes it unstable is its team position, not that the user is missing something/not installed

IOSTYLE
deprecated - don't rely on it it's going away (will certainly not exist)
unstable - could exist going forward (there's a possibility)
see deprecation warning for compiling unstable

*/