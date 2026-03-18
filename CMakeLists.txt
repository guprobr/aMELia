cmake_minimum_required(VERSION 3.21)
project(amelia_qt6 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Concurrent)

qt_standard_project_setup()

qt_add_executable(amelia_qt6
    src/main.cpp
    src/appconfig.h
    src/appconfig.cpp
    src/chatcontroller.h
    src/chatcontroller.cpp
    src/llmclient.h
    src/ollamaclient.h
    src/ollamaclient.cpp
    src/mainwindow.h
    src/mainwindow.cpp
    src/bootstrapdialog.h
    src/bootstrapdialog.cpp
    src/notificationcenter.h
    src/notificationcenter.cpp
    src/policyengine.h
    src/policyengine.cpp
    src/ragindexer.h
    src/ragindexer.cpp
    src/searchbroker.h
    src/searchbroker.cpp
    src/toolexecutor.h
    src/toolexecutor.cpp
    src/embeddingclient.h
    src/embeddingclient.cpp
    src/outlineplanner.h
    src/outlineplanner.cpp
    src/qdrantclient.h
    src/qdrantclient.cpp
    src/storagemanager.h
    src/storagemanager.cpp
    src/memorymanager.h
    src/memorymanager.cpp
    src/sessionsummary.h
    src/sessionsummary.cpp
    assets/resources.qrc
)

target_link_libraries(amelia_qt6
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
)

install(TARGETS amelia_qt6
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES packaging/amelia_qt6.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES assets/amelia_logo.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    RENAME amelia_qt6.svg
)

install(FILES config/config.example.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/amelia_qt6/config
)
