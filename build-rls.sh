#!/bin/sh

rm -rf ./build
mkdir -p ./build

# cmake --build ./build/ --target clean

cmake -S . -B ./build -DCMAKE_BUILD_TYPE=Release # -DCMAKE_PREFIX_PATH=../volepsi
cmake --build ./build 
