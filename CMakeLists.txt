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

# 编译缓存（R4-08）：本机装有 sccache 时，将其作为 C/C++ 编译器启动器包裹 cl.exe，
# 复用未变动的翻译单元编译结果，显著加速增量 / 重复构建（与 Ninja 生成器配合最佳）。
# 未安装 sccache 时 find_program 返回空，此处自动跳过，不影响任何现有构建路径。
find_program(NEONBOARD_SCCACHE NAMES sccache)
if (NEONBOARD_SCCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${NEONBOARD_SCCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${NEONBOARD_SCCACHE}")
    message(STATUS "NeonBoard: 启用 sccache 编译缓存 (${NEONBOARD_SCCACHE})")
endif()

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
    src/TaskUndo.h src/TaskUndo.cpp
    src/TaskInputParser.h
    src/TaskInputParser.cpp
    src/DataPort.h
    src/DataPort.cpp
    src/TaskFilterModel.h src/TaskFilterModel.cpp
    src/SingleInstance.h src/SingleInstance.cpp
    src/AppActions.h src/AppActions.cpp
    src/SplashWindow.h src/SplashWindow.cpp
    src/UiSettings.h src/UiSettings.cpp
    src/AppPaths.h src/AppPaths.cpp
    src/RoadmapSync.h src/RoadmapSync.cpp
    src/PluginContext.h src/PluginContext.cpp
    src/PluginEventBus.h src/PluginEventBus.cpp
    src/PluginRuntime.h src/PluginRuntime.cpp
    src/HookEventQueue.h src/HookEventQueue.cpp
    src/PluginMarket.h src/PluginMarket.cpp
    src/PluginPackage.h src/PluginPackage.cpp
    src/PluginLibrary.h src/PluginLibrary.cpp
    src/TaskTree.h src/TaskTree.cpp
    src/TaskExtra.h src/TaskExtra.cpp
    src/ReminderScheduler.h src/ReminderScheduler.cpp
    src/PreviewHub.h src/PreviewHub.cpp
    src/TaskStats.h src/TaskStats.cpp
    src/BoardSync.h src/BoardSync.cpp
    src/WindowsIntegration.h src/WindowsIntegration.cpp
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
    # R4-02：SetCurrentProcessExplicitAppUserModelID / SHChangeNotify
    Shell32
    # R4-02：跳转列表的 COM（CoInitializeEx / CoCreateInstance / CoTaskMemAlloc）
    Ole32
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
set_source_files_properties(qml/components/PluginLibraryPanel.qml PROPERTIES QT_RESOURCE_ALIAS "PluginLibraryPanel.qml")
set_source_files_properties(qml/components/CommandPalette.qml PROPERTIES QT_RESOURCE_ALIAS "CommandPalette.qml")
set_source_files_properties(qml/components/PreviewPanel.qml PROPERTIES QT_RESOURCE_ALIAS "PreviewPanel.qml")
set_source_files_properties(qml/components/PluginSlot.qml PROPERTIES QT_RESOURCE_ALIAS "PluginSlot.qml")
set_source_files_properties(qml/components/PluginPermissionDialog.qml PROPERTIES QT_RESOURCE_ALIAS "PluginPermissionDialog.qml")
set_source_files_properties(qml/components/ActivityHeatmap.qml PROPERTIES QT_RESOURCE_ALIAS "ActivityHeatmap.qml")
set_source_files_properties(qml/components/FocusRing.qml PROPERTIES QT_RESOURCE_ALIAS "FocusRing.qml")
set_source_files_properties(qml/components/TaskComposer.qml PROPERTIES QT_RESOURCE_ALIAS "TaskComposer.qml")
set_source_files_properties(qml/components/FocusBadge.qml PROPERTIES QT_RESOURCE_ALIAS "FocusBadge.qml")
set_source_files_properties(qml/components/WindowButtons.qml PROPERTIES QT_RESOURCE_ALIAS "WindowButtons.qml")

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
    qml/components/CommandPalette.qml
        qml/components/PluginLibraryPanel.qml
        qml/components/PreviewPanel.qml
        qml/components/PluginSlot.qml
        qml/components/PluginPermissionDialog.qml
        qml/components/ActivityHeatmap.qml
        qml/components/FocusRing.qml
        qml/components/TaskComposer.qml
        qml/components/FocusBadge.qml
        qml/components/WindowButtons.qml
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

qt_add_executable(NeonBoardHost
    src/host/HostMain.cpp
    src/AppPaths.h src/AppPaths.cpp
    src/SingleInstance.h src/SingleInstance.cpp
    src/AppActions.h src/AppActions.cpp
)

target_link_libraries(NeonBoardHost PRIVATE
    Qt6::Core
    Qt6::Network
    user32
    shell32
)

target_compile_definitions(NeonBoardHost PRIVATE
    NEONBOARD_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

if (MSVC)
    target_compile_options(NeonBoardHost PRIVATE /utf-8)
endif()

set_target_properties(NeonBoardHost PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE FALSE
)
target_include_directories(NeonBoardHost PRIVATE src)

# 自动化测试（R4-05）：include(CTest) 提供 BUILD_TESTING 开关与 test 目标，
# 默认开启，因此 CI 与本地 `cmake --build` 都会编译测试目标 —— 编译失败即等于
# 质量门禁失败，不存在「测试代码烂掉但没人发现」的窗口。
# 只想快速出包时可加 -DBUILD_TESTING=OFF 跳过。
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
