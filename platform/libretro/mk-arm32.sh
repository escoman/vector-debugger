#!/bin/bash
make platform=linux-portable CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ V=1 $*
