#!/bin/bash
g++ -std=c++14 -O2 -I ../common -I ../../boost_1_70_0 -I ../../nlohmann_json_3_7_0/include -s -o rcserver main.cpp -pthread
[[ $? -eq 0 ]] || exit 1
nohup ./rcserver &
