#!/bin/bash

export SYSROOT=$HOME/Documents/musl-cross-make/output/arm-linux-musleabi
export PATH=$HOME/Documents/musl-cross-make/output/bin:$PATH

export PKG_CONFIG_DIR=
export PKG_CONFIG_LIBDIR=${SYSROOT}/lib/pkgconfig:${SYSROOT}/usr/lib/pkgconfig:${SYSROOT}/usr/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=${SYSROOT}

arm-linux-musleabi-g++ --sysroot=$SYSROOT -pipe -std=c++11 -O2 -s -DBOOST_ASIO_DISABLE_STD_FUTURE -I$PWD/../../boost_1_70_0 -I$PWD/../../nlohmann_json_3_7_0/include $(pkg-config --cflags openssl libxml-2.0 zlib) -o location_finder location_finder.cpp -pthread $(pkg-config --libs openssl libxml-2.0 zlib) -static
