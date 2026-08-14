# SPDX-FileCopyrightText: 2026 KMail Vim Navigation contributors
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.20)

project(
    KMailVimNavigation
    VERSION 1.1.0
    LANGUAGES CXX
)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM 6.22 REQUIRED NO_MODULE)
list(PREPEND CMAKE_MODULE_PATH "${ECM_MODULE_PATH}")
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6Config REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6XmlGui REQUIRED)
find_package(KPim6Akonadi REQUIRED)
find_package(KPim6AkonadiMime REQUIRED)
find_package(KPim6MessageList REQUIRED)
find_package(KPim6MessageViewer REQUIRED)
find_package(KPim6PimCommon REQUIRED)
find_package(KPim6PimCommonAkonadi REQUIRED)

add_library(kmail_vimnavigationplugin MODULE)
target_sources(
    kmail_vimnavigationplugin
    PRIVATE
        src/vimnavigationplugin.cpp
        src/vimnavigationplugin.h
        src/vimnavigationplugininterface.cpp
        src/vimnavigationplugininterface.h
        src/messagelistmodelutils.cpp
        src/messagelistmodelutils.h
        src/vimmessagemanager.cpp
        src/vimmessagemanager.h
        src/vimshortcutmapper.cpp
        src/vimshortcutmapper.h
)
target_link_libraries(
    kmail_vimnavigationplugin
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::XmlGui
        KPim6::AkonadiCore
        KPim6::AkonadiMime
        KPim6::MessageList
        KPim6::MessageViewer
        KPim6::PimCommon
        KPim6::PimCommonAkonadi
)
set_target_properties(
    kmail_vimnavigationplugin
    PROPERTIES
        PREFIX ""
        OUTPUT_NAME "kmail_vimnavigationplugin"
)

set(
    KMAIL_PLUGIN_INSTALL_DIR
    "${CMAKE_INSTALL_LIBDIR}/qt6/plugins/pim6/kmail/mainview"
    CACHE PATH
    "KMail main-view plugin directory, relative to CMAKE_INSTALL_PREFIX"
)
install(
    TARGETS kmail_vimnavigationplugin
    LIBRARY DESTINATION "${KMAIL_PLUGIN_INSTALL_DIR}"
)

if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(vimshortcutmappertest)
    target_sources(
        vimshortcutmappertest
        PRIVATE
            tests/vimshortcutmappertest.cpp
            src/vimshortcutmapper.cpp
            src/vimshortcutmapper.h
    )
    target_include_directories(vimshortcutmappertest PRIVATE src)
    target_link_libraries(
        vimshortcutmappertest
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Test
            KF6::XmlGui
    )

    add_test(NAME vimshortcutmappertest COMMAND vimshortcutmappertest)
    set_tests_properties(
        vimshortcutmappertest
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(messagelistmodelutilstest)
    target_sources(
        messagelistmodelutilstest
        PRIVATE
            tests/messagelistmodelutilstest.cpp
            src/messagelistmodelutils.cpp
            src/messagelistmodelutils.h
    )
    target_include_directories(messagelistmodelutilstest PRIVATE src)
    target_link_libraries(
        messagelistmodelutilstest
        PRIVATE
            Qt6::Core
            Qt6::Widgets
            Qt6::Test
            KPim6::AkonadiCore
    )
    add_test(NAME messagelistmodelutilstest COMMAND messagelistmodelutilstest)
    set_tests_properties(
        messagelistmodelutilstest
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(pluginloadtest tests/pluginloadtest.cpp)
    target_link_libraries(
        pluginloadtest
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Widgets
            Qt6::Test
            KF6::CoreAddons
            KF6::XmlGui
            KPim6::PimCommon
            KPim6::PimCommonAkonadi
    )
    add_dependencies(pluginloadtest kmail_vimnavigationplugin)
    add_test(NAME pluginloadtest COMMAND pluginloadtest)
    set_tests_properties(
        pluginloadtest
        PROPERTIES
            ENVIRONMENT
                "QT_QPA_PLATFORM=offscreen;KMAIL_VIM_PLUGIN_PATH=$<TARGET_FILE:kmail_vimnavigationplugin>;AKONADI_INSTANCE=kmail_vim_navigation_plugin_test;XDG_CONFIG_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-config"
    )
endif()
