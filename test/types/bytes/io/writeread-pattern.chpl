use CTypes;
use IO;

config const nRepeat = 10;

proc test(byteRange) {
  const byteRange = 0..255;
  const reverseRange = byteRange by -byteRange.stride;
  const nBytes = (byteRange.size + reverseRange.size)*nRepeat;
  var buf = allocate(uint(8), (nBytes+1).safeCast(c_size_t));

  var i = 0;
  for r in 0..#nRepeat {
    for b in byteRange {
      buf[i] = b:uint(8);
      i += 1;
    }
    for b in reverseRange {
      buf[i] = b:uint(8);
      i += 1;
    }
  }
  buf[nBytes] = 0;

  const randomBytes = bytes.createAdoptingBuffer(buf, length=nBytes,
                                                      size=nBytes+1);

  if randomBytes.size != nBytes {
    halt("Error creating bytes object with correct length");
  }

  var bytesChannel = openTempFile();

  {
    // write them to a channel
    var bytesWriter = bytesChannel.writer();
    bytesWriter.write(randomBytes);
    bytesWriter.close();
  }

  {
    // read them into a different object
    var bytesReader = bytesChannel.reader();
    var readBytes = bytesReader.readAll(bytes);
    bytesReader.close();
    // compare
    if readBytes == randomBytes {
      writeln("Success");
    }
    else {
      writeln("Failed: Bytes generated and read differ!");
    }
  }

  {
    // write them to a channel
    var bytesWriter = bytesChannel.writer();
    bytesWriter.writef("%|*s", randomBytes.size, randomBytes);
    bytesWriter.close();
  }

  {
    // read them into a different object
    var bytesReader = bytesChannel.reader();
    var readBytes = b"";
    var readLen = randomBytes.size;
    bytesReader.readf("%|*s", readLen, readBytes);
    bytesReader.close();
    // compare
    if readBytes == randomBytes {
      writeln("Success");
    }
    else {
      writeln("Failed: Bytes generated and read differ!");
    }
  }


}

test(0..255);
test(0..255 by 5);
test(0..255 by 7);
test(0..10);
