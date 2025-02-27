// Allow access to stderr, stdout, ioMode, divceil
private use IO, Math;

//
// Configuration params/types
// (Override defaults on compiler line using -s<cfg>=<val>)
//
config type pixelType = int;

config param bitsPerColor = 8;

//
// Define config-dependent types and params.
//
param colorMask = (0x1 << bitsPerColor) - 1;

type colorType = uint(bitsPerColor);


//
// set helper params for colors
//
param red = 0,        // names for referring to colors
      green = 1,
      blue = 2,
      numColors = 3;

//
// Verify that config-specified pixelType is appropriate for storing colors
//
if (isIntegral(pixelType)) {
  if (numColors*bitsPerColor > numBits(pixelType)) then
    compilerError("pixelType '" + pixelType:string +
                  "' isn't big enough to store " +
                  bitsPerColor:string + " bits per color");
} else {
  compilerError("pixelType must be an integral type");
}

//
// supported image types
//
enum imageType {ppm, bmp};

//
// Detect the image format based on the extension of 'filename'.
// Default to .ppm if using 'stdout'
//
proc extToFmt(filename) {
  if filename == "stdout" then {
    stderr.writeln("Using .ppm format for 'stdout' (override with --format)");
    return imageType.ppm;
  } else {
    for ext in imageType do
      if filename.toLower().endsWith(ext:string) then
        return ext;
  }

  halt("Unsupported image format for output file '", filename, "'");
}

//
// how far to shift a color component when packing into a pixelType
//
inline proc colorOffset(param color) param {
  return color * bitsPerColor;
}

//
// write the image to the output file
//
proc writeImage(image, format, pixels: [] pixelType) {
  // the output file channel
  const outfile = if image == "stdout" then stdout
                                       else open(image, ioMode.cw).writer();
  if image != "stdout" then
    writeln("Writing image to ", image);
  select format {
    when imageType.ppm do
      writeImagePPM(outfile, pixels);
    when imageType.bmp do
      writeImageBMP(outfile, pixels);
    otherwise
      halt("Don't know how to write images of type: ", format);
  }
}

//
// write the image as a PPM file (a simple file format, but not always
// the best supported)
//
proc writeImagePPM(outfile, pixels) {
  outfile.writeln("P6");
  outfile.writeln(pixels.domain.dim(1).size, " ", pixels.domain.dim(0).size);
  outfile.writeln(255);
  for p in pixels do
    for param c in 0..numColors-1 do
      outfile.writeBinary(((p >> colorOffset(c)) & colorMask):int(8));
}

//
// write the image as a BMP file (a more complex file format, but much
// more portable)
//
proc writeImageBMP(outfile, pixels) {
  const rows = pixels.domain.dim(0).size,
        cols = pixels.domain.dim(1).size,

        headerSize = 14,
        dibHeaderSize = 40,  // always use old BITMAPINFOHEADER
        bitsPerPixel = numColors*bitsPerColor,

        // row size in bytes. Pad each row out to 4 bytes.
        rowQuads = divceil(bitsPerPixel * cols, 32),
        rowSize = 4 * rowQuads,
        rowSizeBits = 8 * rowSize,

        pixelsSize = rowSize * rows,
        size = headerSize + dibHeaderSize + pixelsSize,

        offsetToPixelData = headerSize + dibHeaderSize;

  // Write the BMP image header
  outfile.writef("BM");
  outfile.writeBinary(size:uint(32), ioendian.little);
  outfile.writeBinary(0:uint(16), ioendian.little); /* reserved1 */
  outfile.writeBinary(0:uint(16), ioendian.little); /* reserved2 */
  outfile.writeBinary(offsetToPixelData:uint(32), ioendian.little);

  // Write the DIB header BITMAPINFOHEADER
  outfile.writeBinary(dibHeaderSize:uint(32), ioendian.little);
  outfile.writeBinary(cols:int(32), ioendian.little);
  outfile.writeBinary(-rows:int(32), ioendian.little); /*neg for swap*/
  outfile.writeBinary(1:uint(16), ioendian.little); /* 1 color plane */
  outfile.writeBinary(bitsPerPixel:uint(16), ioendian.little);
  outfile.writeBinary(0:uint(32), ioendian.little); /* no compression */
  outfile.writeBinary(pixelsSize:uint(32), ioendian.little);
  outfile.writeBinary(2835:uint(32), ioendian.little); /*pixels/meter print resolution=72dpi*/
  outfile.writeBinary(2835:uint(32), ioendian.little); /*pixels/meter print resolution=72dpi*/
  outfile.writeBinary(0:uint(32), ioendian.little); /* colors in palette */
  outfile.writeBinary(0:uint(32), ioendian.little); /* "important" colors */

  for i in pixels.domain.dim(0) {
    var nbits = 0;
    for j in pixels.domain.dim(1) {
      var p = pixels[i,j];
      var redv = (p >> colorOffset(red)) & colorMask;
      var greenv = (p >> colorOffset(green)) & colorMask;
      var bluev = (p >> colorOffset(blue)) & colorMask;

      // write 24-bit color value
      outfile.writeBits(bluev, bitsPerColor);
      outfile.writeBits(greenv, bitsPerColor);
      outfile.writeBits(redv, bitsPerColor);
      nbits += numColors * bitsPerColor;
    }
    // write the padding.
    // The padding is only rounding up to 4 bytes so
    // can be written in a single writeBits call.
    outfile.writeBits(0:uint, (rowSizeBits-nbits):int(8));
  }
}
