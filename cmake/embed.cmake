# Turns a file into a C++ byte array.
#   cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -P embed.cmake
# Defines  const unsigned char <name>[]  (with a trailing 0, so text can be used as a C
# string) and  const unsigned int <name>Size  (the file size, without that 0).
file(READ "${INPUT}" HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BYTES "${HEX}")
string(REGEX REPLACE "((0x..,){32})" "\\1\n" BYTES "${BYTES}")
file(WRITE "${OUTPUT}.tmp"
  "// Generated from ${INPUT} - do not edit.\n"
  "extern const unsigned char ${SYMBOL}[] = {\n${BYTES}0x00};\n"
  "extern const unsigned int ${SYMBOL}Size = sizeof(${SYMBOL}) - 1;\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
