cmake_minimum_required(VERSION 3.21)
project(amelia_qt6 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# QtDBus (used only for freedesktop desktop-notification integration) is a
# Linux/BSD desktop concept; mainstream Windows and macOS Qt distributions
# don't ship it, and NotificationCenter falls back to QSystemTrayIcon
# everywhere else, so only require it where it actually exists.
set(AMELIA_HAS_DBUS FALSE)
if(UNIX AND NOT APPLE)
    set(AMELIA_HAS_DBUS TRUE)
endif()

set(AMELIA_QT_COMPONENTS Core Widgets Network Concurrent Pdf)
if(AMELIA_HAS_DBUS)
    list(APPEND AMELIA_QT_COMPONENTS DBus)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${AMELIA_QT_COMPONENTS})

# Tesseract/Leptonica: pkg-config is the standard discovery path on
# Linux/macOS (and also works under a pkgconf-enabled toolchain such as
# vcpkg or MSYS2 on Windows); fall back to each library's own CMake config
# package when pkg-config itself isn't available.
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(Tesseract IMPORTED_TARGET tesseract)
    pkg_check_modules(Leptonica IMPORTED_TARGET lept)
endif()

if(TARGET PkgConfig::Tesseract)
    set(AMELIA_TESSERACT_TARGET PkgConfig::Tesseract)
else()
    find_package(Tesseract CONFIG REQUIRED)
    set(AMELIA_TESSERACT_TARGET Tesseract::libtesseract)
endif()

if(TARGET PkgConfig::Leptonica)
    set(AMELIA_LEPTONICA_TARGET PkgConfig::Leptonica)
else()
    find_package(Leptonica CONFIG REQUIRED)
    set(AMELIA_LEPTONICA_TARGET leptonica)
endif()

qt_standard_project_setup()

qt_add_executable(amelia_qt6
    src/app/main.cpp
    src/core/appconfig.h
    src/core/appconfig.cpp
    src/core/appversion.h
    src/core/transcriptformatter.h
    src/core/transcriptformatter.cpp
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

target_link_libraries(amelia_qt6
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Concurrent
        Qt6::Pdf
        ${AMELIA_TESSERACT_TARGET}
        ${AMELIA_LEPTONICA_TARGET}
)

if(AMELIA_HAS_DBUS)
    target_link_libraries(amelia_qt6 PRIVATE Qt6::DBus)
endif()

install(TARGETS amelia_qt6
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# .desktop launchers and the hicolor icon theme are freedesktop/XDG desktop
# conventions with no Windows or macOS equivalent (those use .app bundles /
# MSI shortcuts instead), so only install them where they mean something.
if(UNIX AND NOT APPLE)
    install(FILES packaging/amelia_qt6.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )

    install(FILES assets/amelia_logo.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME amelia_qt6.svg
    )
endif()

install(FILES config/config.example.json
    DESTINATION ${CMAKE_INSTALL_DATADIR}/amelia_qt6/config
)
