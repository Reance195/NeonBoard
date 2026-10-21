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
    src/UiSettings.h src/UiSettings.cpp
    src/AppPaths.h src/AppPaths.cpp
    src/RoadmapSync.h src/RoadmapSync.cpp
    src/PluginContext.h src/PluginContext.cpp
    src/PluginEventBus.h src/PluginEventBus.cpp
    src/PluginRuntime.h src/PluginRuntime.cpp
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

# QML 文件统一别名到模块根：否则文件位于模块子目录时，引擎的隐式导入解析为「所在目录」，
# 导致同模块的文件型类型（组件）不可见，单例却能解析。
set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS "Main.qml")
set_source_files_properties(qml/NeonBackground.qml PROPERTIES QT_RESOURCE_ALIAS "NeonBackground.qml")
set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_RESOURCE_ALIAS "Theme.qml")
set_source_files_properties(qml/theme/Motion.qml PROPERTIES QT_RESOURCE_ALIAS "Motion.qml")
set_source_files_properties(qml/components/WinButton.qml PROPERTIES QT_RESOURCE_ALIAS "WinButton.qml")
set_source_files_properties(qml/components/StatChip.qml PROPERTIES QT_RESOURCE_ALIAS "StatChip.qml")
set_source_files_properties(qml/components/TaskCard.qml PROPERTIES QT_RESOURCE_ALIAS "TaskCard.qml")
set_source_files_properties(qml/components/BoardColumn.qml PROPERTIES QT_RESOURCE_ALIAS "BoardColumn.qml")
set_source_files_properties(qml/components/ThemePicker.qml PROPERTIES QT_RESOURCE_ALIAS "ThemePicker.qml")
set_source_files_properties(qml/components/SettingsPanel.qml PROPERTIES QT_RESOURCE_ALIAS "SettingsPanel.qml")
set_source_files_properties(qml/components/PluginPanel.qml PROPERTIES QT_RESOURCE_ALIAS "PluginPanel.qml")
set_source_files_properties(qml/components/AppearancePanel.qml PROPERTIES QT_RESOURCE_ALIAS "AppearancePanel.qml")
set_source_files_properties(qml/components/StageBar.qml PROPERTIES QT_RESOURCE_ALIAS "StageBar.qml")

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
        qml/components/SettingsPanel.qml
        qml/components/PluginPanel.qml
        qml/components/AppearancePanel.qml
        qml/components/StageBar.qml
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
