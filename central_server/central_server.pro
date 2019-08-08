TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += $$PWD/../common
INCLUDEPATH += $$PWD/../../boost_1_70_0
INCLUDEPATH += $$PWD/../../nlohmann_json_3_7_0/include

SOURCES += \
        main.cpp

HEADERS += \
    device_commands.hpp \
    device_connection.hpp \
    device_manager.hpp \
    device_protocol.h \
    device_requests.hpp \
    http_session.hpp \
    tcp_server.hpp \
    web_api_handler.hpp

LIBS += -pthread
