# Usage: cmake -DIN=<file> -DOUT=<cpp> -DSYM=<name> -P embed.cmake
file(READ "${IN}" _hex HEX)
string(LENGTH "${_hex}" _len)
math(EXPR _size "${_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _body "${_hex}")
file(WRITE "${OUT}" "// generated - do not edit\n#include <cstddef>\nextern const unsigned char ${SYM}[] = {${_body}0};\nextern const std::size_t ${SYM}_size = ${_size};\n")
