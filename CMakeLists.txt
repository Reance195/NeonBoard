cmake_minimum_required(VERSION 3.21)
project(NeonBoard VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Sql Network Widgets ShaderTools
)

qt_standard_project_setup()

qt_add_executable(NeonBoard
    src/main.cpp
    src/Database.h src/Database.cpp
    src/Metrics.h src/Metrics.cpp
    src/WindowController.h src/WindowController.cpp
    src/Hotkey.h src/Hotkey.cpp
    src/TrayController.h src/TrayController.cpp
    src/PluginLoader.h src/PluginLoader.cpp
    src/PluginHost.h src/PluginHost.cpp
    src/TaskModel.h src/TaskModel.cpp
    src/TaskFilterModel.h src/TaskFilterModel.cpp
    src/SingleInstance.h src/SingleInstance.cpp
    src/SplashWindow.h src/SplashWindow.cpp
    resources/neonboard.rc
)

target_link_libraries(NeonBoard PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Sql
    Qt6::Network
    Qt6::Widgets
    Qt6::ShaderTools
    Psapi
    Dwmapi
)

target_compile_definitions(NeonBoard PRIVATE
    NEONBOARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if (MSVC)
    target_compile_options(NeonBoard PRIVATE /utf-8)
endif()

qt_policy(SET QTP0001 NEW)

set_source_files_properties(qml/theme/Theme.qml qml/theme/Motion.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(NeonBoard
    URI NeonBoard
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/NeonBackground.qml
        qml/theme/Theme.qml
        qml/theme/Motion.qml
        qml/components/WinButton.qml
        qml/components/StatChip.qml
        qml/components/TaskCard.qml
        qml/components/BoardColumn.qml
        qml/components/ThemePicker.qml
)

qt_add_resources(NeonBoard "icons"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/resources/icons"
    FILES
        resources/icons/neonboard.svg
)

qt_add_shaders(NeonBoard "neon_shaders"
    PREFIX "/shaders"
    BASE "${CMAKE_SOURCE_DIR}/shaders"
    FILES
        shaders/neon.frag
)

set_target_properties(NeonBoard PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE FALSE
)
