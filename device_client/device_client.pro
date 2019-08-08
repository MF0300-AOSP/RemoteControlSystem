TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += $$PWD/../common
INCLUDEPATH += $$PWD/../../boost_1_70_0

SOURCES += \
        main.cpp

HEADERS += \
    command_processor.hpp \
    device_connection.hpp \
    update_android_info_request.hpp \
    upload_file_reply.hpp

LIBS += -pthread
