#!/bin/bash

if gcc -DDEBUG -O0 -g -rdynamic -std=c89 -pedantic -Wall src/*.c -o donbootstrap ; then

rm -rf bootstrapcache
XDG_CACHE_HOME=bootstrapcache ./donbootstrap $@

fi
