cmake_minimum_required(VERSION 3.21)
project(NeonBoard VERSION 0.1.5 LANGUAGES C CXX)

# 版本号唯一来源就是上面这行 project(VERSION)。发版只需改这里：
#   · 运行时版本（QCoreApplication::applicationVersion）→ 编译期常量 NEONBOARD_VERSION_STRING
#   · 安装包版本（installer/neonboard.iss 的 MyAppVersion）→ 由 scripts/release.ps1
#     与 release.yml 解析本文件后以 /DMyAppVersion=<版本> 传入
# 五处手工同步（CMake / main.cpp / HostMain.cpp / SplashWindow.cpp / .iss）曾各自漂移，
# 漏改 main.cpp 会让更新判定永远认为「已是最新」，因此收敛到单点。
add_compile_definitions(NEONBOARD_VERSION_STRING="${PROJECT_VERSION}")

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
    src/core/Database.h src/core/Database.cpp
    src/core/Metrics.h src/core/Metrics.cpp
    src/shell/WindowController.h src/shell/WindowController.cpp
    src/shell/Hotkey.h src/shell/Hotkey.cpp
    src/shell/TrayController.h src/shell/TrayController.cpp
    src/plugin/PluginLoader.h src/plugin/PluginLoader.cpp
    src/plugin/PluginHost.h src/plugin/PluginHost.cpp
    src/task/TaskModel.h src/task/TaskModel.cpp
    src/task/TaskUndo.h src/task/TaskUndo.cpp
    src/board/CardDetail.h src/board/CardDetail.cpp
    src/task/TaskInputParser.h
    src/task/TaskInputParser.cpp
    src/board/DataPort.h
    src/board/DataPort.cpp
    src/task/TaskFilterModel.h src/task/TaskFilterModel.cpp
    src/board/BoardColumns.h src/board/BoardColumns.cpp
    src/core/SingleInstance.h src/core/SingleInstance.cpp
    src/core/AppActions.h src/core/AppActions.cpp
    src/shell/SplashWindow.h src/shell/SplashWindow.cpp
    src/shell/SplashScene.h src/shell/SplashScene.cpp
    src/shell/SplashLayeredWindow.h src/shell/SplashLayeredWindow.cpp
    src/core/UiSettings.h src/core/UiSettings.cpp
    src/core/AppPaths.h src/core/AppPaths.cpp
    src/board/RoadmapSync.h src/board/RoadmapSync.cpp
    src/plugin/PluginContext.h src/plugin/PluginContext.cpp
    src/plugin/PluginEventBus.h src/plugin/PluginEventBus.cpp
    src/plugin/PluginRuntime.h src/plugin/PluginRuntime.cpp
    src/plugin/HookEventQueue.h src/plugin/HookEventQueue.cpp
    src/plugin/PluginMarket.h src/plugin/PluginMarket.cpp
    src/plugin/PluginPackage.h src/plugin/PluginPackage.cpp
    src/plugin/PluginLibrary.h src/plugin/PluginLibrary.cpp
    src/task/TaskTree.h src/task/TaskTree.cpp
    src/task/TaskExtra.h src/task/TaskExtra.cpp
    src/task/TaskAudit.h src/task/TaskAudit.cpp
    src/theme/ThemePack.h src/theme/ThemePack.cpp
    src/theme/ThemeMarket.h src/theme/ThemeMarket.cpp
    src/theme/MenuSkin.h src/theme/MenuSkin.cpp
    src/board/ProjectStore.h src/board/ProjectStore.cpp
    src/task/TaskExec.h src/task/TaskExec.cpp
    src/shell/ReminderScheduler.h src/shell/ReminderScheduler.cpp
    src/board/PreviewHub.h src/board/PreviewHub.cpp
    src/task/TaskStats.h src/task/TaskStats.cpp
    src/board/BoardSync.h src/board/BoardSync.cpp
    src/shell/WindowsIntegration.h src/shell/WindowsIntegration.cpp
    src/update/UpdateManager.h src/update/UpdateManager.cpp
    src/task/TaskRunStore.h src/task/TaskRunStore.cpp
    src/agent/AgentProfile.h src/agent/AgentProfile.cpp
    src/agent/TaskRunner.h src/agent/TaskRunner.cpp
    src/board/ProjectMap.h src/board/ProjectMap.cpp
    src/agent/AcceptanceRunner.h src/agent/AcceptanceRunner.cpp
    src/core/ScopeMatcher.h src/core/ScopeMatcher.cpp
    src/agent/TriageRunner.h src/agent/TriageRunner.cpp
    src/agent/LedgerImport.h src/agent/LedgerImport.cpp
    src/agent/Scheduler.h src/agent/Scheduler.cpp
    src/agent/WorkflowRunner.h src/agent/WorkflowRunner.cpp
    src/agent/WorkflowPresets.h src/agent/WorkflowPresets.cpp
    src/shell/DragFeel.h src/shell/DragFeel.cpp
    src/shell/KeyMove.h src/shell/KeyMove.cpp
    src/update/DeltaUpdate.h src/update/DeltaUpdate.cpp
    src/update/BsdiffWrapper.h src/update/BsdiffWrapper.cpp
    thirdparty/bsdiff/bsdiff.c
    thirdparty/bsdiff/bspatch.c
    thirdparty/bzip2/bzlib.c
    thirdparty/bzip2/blocksort.c
    thirdparty/bzip2/huffman.c
    thirdparty/bzip2/crctable.c
    thirdparty/bzip2/randtable.c
    thirdparty/bzip2/compress.c
    thirdparty/bzip2/decompress.c
    resources/neonboard.rc
)

# MOD-06：8 个模块子目录。源码里的 #include "Database.h" 一律不改写法，
# 靠这一组 include 目录解析 —— 搬运与改 include 分离，rename 占比才上得去。
target_include_directories(NeonBoard PRIVATE
    src
    src/core src/task src/theme src/update src/agent src/board src/shell src/plugin)

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
set_source_files_properties(qml/components/TopStatBar.qml PROPERTIES QT_RESOURCE_ALIAS "TopStatBar.qml")
set_source_files_properties(qml/components/TopToolButton.qml PROPERTIES QT_RESOURCE_ALIAS "TopToolButton.qml")
set_source_files_properties(qml/components/TaskCard.qml PROPERTIES QT_RESOURCE_ALIAS "TaskCard.qml")
set_source_files_properties(qml/components/BoardColumn.qml PROPERTIES QT_RESOURCE_ALIAS "BoardColumn.qml")
set_source_files_properties(qml/components/ColumnManager.qml PROPERTIES QT_RESOURCE_ALIAS "ColumnManager.qml")
set_source_files_properties(qml/components/ThemePicker.qml PROPERTIES QT_RESOURCE_ALIAS "ThemePicker.qml")
set_source_files_properties(qml/components/SettingsPanel.qml PROPERTIES QT_RESOURCE_ALIAS "SettingsPanel.qml")
set_source_files_properties(qml/components/PluginPanel.qml PROPERTIES QT_RESOURCE_ALIAS "PluginPanel.qml")
set_source_files_properties(qml/components/AppearancePanel.qml PROPERTIES QT_RESOURCE_ALIAS "AppearancePanel.qml")
set_source_files_properties(qml/components/StageBar.qml PROPERTIES QT_RESOURCE_ALIAS "StageBar.qml")
set_source_files_properties(qml/components/ProjectSwitcher.qml PROPERTIES QT_RESOURCE_ALIAS "ProjectSwitcher.qml")
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
set_source_files_properties(qml/components/OnboardingOverlay.qml PROPERTIES QT_RESOURCE_ALIAS "OnboardingOverlay.qml")
set_source_files_properties(qml/components/ProjectMapPanel.qml PROPERTIES QT_RESOURCE_ALIAS "ProjectMapPanel.qml")
set_source_files_properties(qml/components/CardDetailPanel.qml PROPERTIES QT_RESOURCE_ALIAS "CardDetailPanel.qml")
set_source_files_properties(qml/components/ThemeMarketPanel.qml PROPERTIES QT_RESOURCE_ALIAS "ThemeMarketPanel.qml")

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
        qml/components/TopStatBar.qml
        qml/components/TopToolButton.qml
        qml/components/TaskCard.qml
        qml/components/BoardColumn.qml
        qml/components/ColumnManager.qml
        qml/components/ThemePicker.qml
        qml/components/SettingsPanel.qml
        qml/components/PluginPanel.qml
    qml/components/AppearancePanel.qml
    qml/components/StageBar.qml
    qml/components/ProjectSwitcher.qml
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
        qml/components/OnboardingOverlay.qml
        qml/components/ProjectMapPanel.qml
        qml/components/CardDetailPanel.qml
        qml/components/ThemeMarketPanel.qml
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
    src/host/TrayMenu.h src/host/TrayMenu.cpp
    src/theme/MenuSkin.h src/theme/MenuSkin.cpp
    src/core/AppPaths.h src/core/AppPaths.cpp
    src/core/SingleInstance.h src/core/SingleInstance.cpp
    src/core/AppActions.h src/core/AppActions.cpp
    resources/neonboard.rc    # 安装包的快捷方式指向 Host，缺它 exe 里没有图标资源
)

target_link_libraries(NeonBoardHost PRIVATE
    Qt6::Core
    Qt6::Network
    user32
    shell32
    # R17-01：托盘右键菜单改成自绘的分层窗口 —— GDI+ 画带 alpha 的圆角 + 辉光，
    # 再用 UpdateLayeredWindow 贴到屏幕上。gdiplus.dll 是 Windows 自带的，不用随包分发。
    gdiplus
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
target_include_directories(NeonBoardHost PRIVATE src src/core src/theme)

# 增量升级发布侧工具（R5-06）：neonboard-delta-tool build --from <旧> --to <新> --out <目录>
# 由 scripts/build-delta.ps1 调用，生成 delta.json + files/ + patches/（bsdiff patch）。
# 与主程序共用 DeltaUpdate / BsdiffWrapper 源码；main() 在 DeltaUpdate.cpp 里
# 以 NEONBOARD_DELTA_TOOL_MAIN 宏隔离，不进主程序。
add_executable(neonboard-delta-tool
    src/update/DeltaUpdate.cpp
    src/update/BsdiffWrapper.cpp
    thirdparty/bsdiff/bsdiff.c
    thirdparty/bsdiff/bspatch.c
    thirdparty/bzip2/bzlib.c
    thirdparty/bzip2/blocksort.c
    thirdparty/bzip2/huffman.c
    thirdparty/bzip2/crctable.c
    thirdparty/bzip2/randtable.c
    thirdparty/bzip2/compress.c
    thirdparty/bzip2/decompress.c
)
target_compile_definitions(neonboard-delta-tool PRIVATE NEONBOARD_DELTA_TOOL_MAIN)
target_link_libraries(neonboard-delta-tool PRIVATE Qt6::Core)
target_include_directories(neonboard-delta-tool PRIVATE src src/update ${CMAKE_SOURCE_DIR})
if (MSVC)
    target_compile_options(neonboard-delta-tool PRIVATE /utf-8)
endif()

# 自动化测试（R4-05）：include(CTest) 提供 BUILD_TESTING 开关与 test 目标，
# 默认开启，因此 CI 与本地 `cmake --build` 都会编译测试目标 —— 编译失败即等于
# 质量门禁失败，不存在「测试代码烂掉但没人发现」的窗口。
# 只想快速出包时可加 -DBUILD_TESTING=OFF 跳过。
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
