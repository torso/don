#!/bin/bash

#if gcc -O3 -ggdb3 -std=c89 -pedantic -Wno-error=unused-parameter -Wno-error=unused-variable -Wno-error=unused-function -Wall -Wextra -Wformat=2 -Winit-self -Wmissing-include-dirs -Wswitch-enum -Wsync-nand -Wunused -Wstrict-overflow=5 -Wfloat-equal -Wundef -Wshadow -Wbad-function-cast -Wc++-compat -Wcast-align -Wwrite-strings -Wconversion -Wlogical-op -Waggregate-return -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wmissing-declarations -Wmissing-noreturn -Wmissing-format-attribute -Wnormalized=nfc -Wpacked -Wpacked-bitfield-compat -Wredundant-decls -Wnested-externs -Winline -Winvalid-pch -Wdisabled-optimization -Wstack-protector -pipe -march=native src/*.c -o builder ; then
if gcc -DDEBUG -O0 -ggdb3 -rdynamic -std=c89 -pedantic -Werror -Wno-error=unused-parameter -Wno-error=unused-variable -Wno-error=unused-function -Wall -Wextra -Wformat=2 -Winit-self -Wmissing-include-dirs -Wswitch-enum -Wsync-nand -Wunused -Wstrict-overflow=5 -Wfloat-equal -Wundef -Wshadow -Wunsafe-loop-optimizations -Wbad-function-cast -Wc++-compat -Wcast-align -Wwrite-strings -Wconversion -Wlogical-op -Waggregate-return -Wstrict-prototypes -Wold-style-definition -Wmissing-prototypes -Wmissing-declarations -Wmissing-noreturn -Wmissing-format-attribute -Wnormalized=nfc -Wpacked -Wpacked-bitfield-compat -Wredundant-decls -Wnested-externs -Wunreachable-code -Wno-error=unreachable-code -Winline -Winvalid-pch -Wdisabled-optimization -Wstack-protector -pipe -march=native src/*.c -o builder ; then

for i in test/* ; do
  echo -n "$i: "
  ./builder -i $i
done

valgrind -q --leak-check=full --show-reachable=yes ./builder -i input

fi
