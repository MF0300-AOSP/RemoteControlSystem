TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

CONFIG += link_pkgconfig

PKGCONFIG += openssl libxml-2.0

INCLUDEPATH += $$PWD/../../boost_1_70_0
INCLUDEPATH += $$PWD/../../nlohmann_json_3_7_0/include

SOURCES += \
    location_finder.cpp

HEADERS += \
    tools_keycdn_com.crt.h

LIBS += -pthread
