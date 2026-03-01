#!/bin/bash
echo Updating compiler_commands.json for clangd...
make clean
bear -- ./mk.sh
