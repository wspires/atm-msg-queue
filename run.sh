#!/usr/bin/env bash

g++ -std=c++14 -O3 main.cpp 2>&1 | tee make.out && ./a.out
