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
<<<<<<< HEAD
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

=======
    src/app/main.cpp
    src/core/appconfig.h
    src/core/appconfig.cpp
    src/core/appversion.h
    src/ui/bootstrapdialog.h
    src/ui/bootstrapdialog.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/ui/notificationcenter.h
    src/ui/notificationcenter.cpp
    src/backend/chatcontroller.h
    src/backend/chatcontroller.cpp
    src/backend/llmclient.h
    src/backend/ollamaclient.h
    src/backend/ollamaclient.cpp
    src/backend/outlineplanner.h
    src/backend/outlineplanner.cpp
    src/backend/policyengine.h
    src/backend/policyengine.cpp
    src/backend/toolexecutor.h
    src/backend/toolexecutor.cpp
    src/rag/embeddingclient.h
    src/rag/embeddingclient.cpp
    src/rag/qdrantclient.h
    src/rag/qdrantclient.cpp
    src/rag/ragindexer.h
    src/rag/ragindexer.cpp
    src/rag/searchbroker.h
    src/rag/searchbroker.cpp
    src/core/memorymanager.h
    src/core/memorymanager.cpp
    src/core/sessionsummary.h
    src/core/sessionsummary.cpp
    src/core/storagemanager.h
    src/core/storagemanager.cpp
    assets/resources.qrc
)

target_include_directories(amelia_qt6
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

>>>>>>> 486a6af (- display version bumped to `7.1b`)
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
