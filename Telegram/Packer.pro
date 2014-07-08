QT += core 

CONFIG(debug, debug|release) {
    DEFINES += _DEBUG
    OBJECTS_DIR = ./../Linux/DebugIntermediatePacker
    MOC_DIR = ./GeneratedFiles/Debug
    DESTDIR = ./../Linux/DebugPacker
}
CONFIG(release, debug|release) {
    OBJECTS_DIR = ./../Linux/ReleaseIntermediatePacker
    MOC_DIR = ./GeneratedFiles/Release
    DESTDIR = ./../Linux/ReleasePacker
}

macx {
    QMAKE_INFO_PLIST = ./SourceFiles/_other/Packer.plist
    QMAKE_LFLAGS += -framework Cocoa
}

SOURCES += \
    ./SourceFiles/_other/packer.cpp \

HEADERS += \
    ./SourceFiles/_other/packer.h \

INCLUDEPATH += ./../../Libraries/QtStatic/qtbase/include/QtGui/5.3.0/QtGui\
               ./../../Libraries/QtStatic/qtbase/include/QtCore/5.3.0/QtCore\
               ./../../Libraries/QtStatic/qtbase/include\
               ./../../Libraries/lzma/C

LIBS += -lcrypto -lssl -lz

