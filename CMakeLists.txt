cmake_minimum_required(VERSION 3.21)
project(NeonBoard VERSION 0.1.7 LANGUAGES C CXX)

# ---- DX-52：把「实际编译链路」上的 /showIncludes 前缀告诉 Ninja 依赖解析 ----
# CMake 的编译器探测**绕开启动器**（CMAKE_DETERMINE_MSVC_SHOWINCLUDES_PREFIX 直接调 cl.exe，
# 不走 CMAKE_<LANG>_COMPILER_LAUNCHER），记下的是「干净 cl.exe」的前缀；而真正编译经过
# sccache 时，本地化的前缀字节被它改写。两边对不上 → Ninja 解析不出依赖条目（#deps 0）
# → 改公共头文件不重编 → 陈旧 obj 被链进新代码 → 类布局不一致 → 堆损坏。
#
# 构建脚本用同一条链路（同一个启动器）实测一次前缀，把原始字节落成文件，这里读回来覆盖自探测值。
# 前缀按**字节**经文件传递，不走命令行 —— 本地化前缀在中文机器上可能是 GBK，不是合法 UTF-8，
# 走命令行会被替换成 U+FFFD，字节一失真就又对不上了。
#
# 注意：这两个是**普通变量**（不是缓存变量），CMakeCXXCompiler.cmake 里刚设过，
# 必须在 project() 之后覆盖才生效。
if (NEONBOARD_SHOWINCLUDES_PREFIX_FILE AND EXISTS "${NEONBOARD_SHOWINCLUDES_PREFIX_FILE}")
    file(READ "${NEONBOARD_SHOWINCLUDES_PREFIX_FILE}" NEONBOARD_SHOWINCLUDES_PREFIX)
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${NEONBOARD_SHOWINCLUDES_PREFIX}")
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${NEONBOARD_SHOWINCLUDES_PREFIX}")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${NEONBOARD_SHOWINCLUDES_PREFIX}")
    # 前缀变了，编译命令行也得跟着变：否则旧 obj 不会被重编，「陈旧 obj」的隐患原样留着。
    # 哈希进命令行 → Ninja 判定命令变了 → 该重编的一定重编。
    if (NEONBOARD_SHOWINCLUDES_PREFIX_HASH)
        add_compile_definitions(NEONBOARD_DEPS_PREFIX_TAG=${NEONBOARD_SHOWINCLUDES_PREFIX_HASH})
    endif()
    message(STATUS "NeonBoard: /showIncludes 前缀由构建脚本实测指定（哈希 ${NEONBOARD_SHOWINCLUDES_PREFIX_HASH}）")
endif()

# ---- DX-52：编译缓存按「源码树」分桶 ----
# sccache 的缓存按内容寻址、跨工作区共享；命中时它回放的是**当初那个目录**的 /showIncludes 输出，
# 里面的头文件路径属于别的 worktree。Ninja 把那些路径当依赖记下来 → 改本地头文件不重编（陈旧 obj），
# 那个 worktree 被回收后构建还会直接报错。
# 强制包含一份带「源码根路径」的头：预处理结果随之按源码树分桶，缓存 key 也随之分桶，
# 回放的路径就永远属于当前这棵树。文件由 scripts/build.ps1 生成在构建目录里，不进仓库。
if (NEONBOARD_BUILD_IDENTITY_HEADER AND EXISTS "${NEONBOARD_BUILD_IDENTITY_HEADER}")
    # /FI 后面不能有空格：MSVC 只把「/FI 之后那个 token 的剩余部分」当文件名，
    # 而 CMake 会给含空格的选项整体加引号（Ninja 不经过 shell，引号不会被剥掉）。
    # 构建脚本已保证传进来的是不含空格的路径（必要时用 8.3 短路径）。
    add_compile_options("/FI${NEONBOARD_BUILD_IDENTITY_HEADER}")
endif()

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

# ---- DX-53：/utf-8 收敛到顶层一处 ----
# MSVC 默认按**本地代码页**（中文机器上是 CP936）解析源码里的中文字面量，症状是
# 「断言莫名失败」/中文常量乱码；/utf-8 把源字符集与执行字符集统一成 UTF-8。
#
# 这件事以前散在四处（NeonBoard / NeonBoardHost / neonboard-delta-tool 三个
# target_compile_options，加上 tests/CMakeLists.txt 的 neonboard_add_test 一处），
# 每加一个目标都得记得再写一遍 —— 漏写就静默回退到本地代码页。靠人记的约定迟早会漂：
# 现状之所以没出事，是因为 Qt6::Platform 的 INTERFACE_COMPILE_OPTIONS 顺带带了 -utf-8
# （Qt6Targets.cmake:64），凡是链 Qt6::Core 的目标都白捡了这个开关。那是**搭便车**，
# 不是我方的约定 —— 哪天多一个不链 Qt 的目标（纯 C 工具、第三方库），覆盖就静默消失。
#
# 现在收敛成目录级 add_compile_options：一处声明，本目录以及此后 add_subdirectory
# 引入的子目录里所有目标自动继承，新增目标不用再写第二遍。
#
# 位置有讲究：必须落在所有 add_subdirectory() 之前（本文件的 tests/ 在末尾），
# 否则子目录拿不到目录属性 —— scripts/check_utf8_flag.py 强制校验这一条。
if (MSVC)
    add_compile_options(/utf-8)
endif()

# 编译缓存（R4-08）：本机装有 sccache 时，将其作为 C/C++ 编译器启动器包裹 cl.exe，
# 复用未变动的翻译单元编译结果，显著加速增量 / 重复构建（与 Ninja 生成器配合最佳）。
# 未安装 sccache 时 find_program 返回空，此处自动跳过，不影响任何现有构建路径。
#
# DX-52：launcher 由构建脚本**显式指定**（-DNEONBOARD_SCCACHE_LAUNCHER），不再让这里自己找 ——
# 因为「构建脚本实测 /showIncludes 前缀」用的必须与真正编译**同一条链路**，两边 launcher 不同，
# 测到的前缀就对不上（Ninja 又解析不出依赖条目）。没指定时才退回 find_program 自找。
if (NOT DEFINED NEONBOARD_SCCACHE_LAUNCHER)
    find_program(NEONBOARD_SCCACHE NAMES sccache)
    set(NEONBOARD_SCCACHE_LAUNCHER "${NEONBOARD_SCCACHE}")
endif()
if (NEONBOARD_SCCACHE_LAUNCHER)
    set(CMAKE_C_COMPILER_LAUNCHER "${NEONBOARD_SCCACHE_LAUNCHER}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${NEONBOARD_SCCACHE_LAUNCHER}")
    message(STATUS "NeonBoard: 启用 sccache 编译缓存 (${NEONBOARD_SCCACHE_LAUNCHER})")
else()
    # 显式清空：缓存里可能留着上一轮的 launcher，不清就会出现「关了还在用」（-NoCache 就是这么失灵的）。
    set(CMAKE_C_COMPILER_LAUNCHER "")
    set(CMAKE_CXX_COMPILER_LAUNCHER "")
    message(STATUS "NeonBoard: 未启用 sccache 编译缓存")
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
    src/core/SafePaths.h src/core/SafePaths.cpp
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
    src/board/RepoScaffolder.h src/board/RepoScaffolder.cpp
    resources/neonboard.rc
    src/app/AppMain.cpp
    src/app/AppContext.cpp
    src/app/CliCommands.cpp
    src/app/SelfTestRunner.cpp
    src/app/AppObjects.cpp
    src/app/AppWiring.cpp
    src/app/CliHandlers.cpp
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
set_source_files_properties(qml/components/Overdue.js PROPERTIES QT_RESOURCE_ALIAS "Overdue.js")
set_source_files_properties(qml/components/BoardColumn.qml PROPERTIES QT_RESOURCE_ALIAS "BoardColumn.qml")
set_source_files_properties(qml/components/EmptyState.qml PROPERTIES QT_RESOURCE_ALIAS "EmptyState.qml")
set_source_files_properties(qml/components/AddColumnButton.qml PROPERTIES QT_RESOURCE_ALIAS "AddColumnButton.qml")
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
set_source_files_properties(qml/components/DialogShell.qml PROPERTIES QT_RESOURCE_ALIAS "DialogShell.qml")
set_source_files_properties(qml/components/DrawerShell.qml PROPERTIES QT_RESOURCE_ALIAS "DrawerShell.qml")
set_source_files_properties(qml/components/ExecQueueDialog.qml PROPERTIES QT_RESOURCE_ALIAS "ExecQueueDialog.qml")
set_source_files_properties(qml/components/ExecConfirmDialog.qml PROPERTIES QT_RESOURCE_ALIAS "ExecConfirmDialog.qml")
set_source_files_properties(qml/components/ExecPanelDialog.qml PROPERTIES QT_RESOURCE_ALIAS "ExecPanelDialog.qml")
set_source_files_properties(qml/components/NightBatchDialog.qml PROPERTIES QT_RESOURCE_ALIAS "NightBatchDialog.qml")
set_source_files_properties(qml/components/BoardChrome.qml PROPERTIES QT_RESOURCE_ALIAS "BoardChrome.qml")
set_source_files_properties(qml/components/NeonBackgroundLayer.qml PROPERTIES QT_RESOURCE_ALIAS "NeonBackgroundLayer.qml")
set_source_files_properties(qml/components/TaskEditorDialog.qml PROPERTIES QT_RESOURCE_ALIAS "TaskEditorDialog.qml")
set_source_files_properties(qml/components/DataButton.qml PROPERTIES QT_RESOURCE_ALIAS "DataButton.qml")
set_source_files_properties(qml/components/NeonComboBox.qml PROPERTIES QT_RESOURCE_ALIAS "NeonComboBox.qml")
set_source_files_properties(qml/components/NeonField.qml PROPERTIES QT_RESOURCE_ALIAS "NeonField.qml")

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
        qml/components/Overdue.js
        qml/components/BoardColumn.qml
        qml/components/EmptyState.qml
        qml/components/AddColumnButton.qml
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
        qml/components/DialogShell.qml
        qml/components/DrawerShell.qml
    qml/components/ExecQueueDialog.qml
    qml/components/ExecConfirmDialog.qml
    qml/components/ExecPanelDialog.qml
    qml/components/NightBatchDialog.qml
    qml/components/BoardChrome.qml
    qml/components/NeonBackgroundLayer.qml
    qml/components/TaskEditorDialog.qml
    qml/components/DataButton.qml
    qml/components/NeonComboBox.qml
    qml/components/NeonField.qml
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
    src/core/SafePaths.cpp
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
target_include_directories(neonboard-delta-tool PRIVATE src src/update src/core ${CMAKE_SOURCE_DIR})

# 自动化测试（R4-05）：include(CTest) 提供 BUILD_TESTING 开关与 test 目标，
# 默认开启，因此 CI 与本地 `cmake --build` 都会编译测试目标 —— 编译失败即等于
# 质量门禁失败，不存在「测试代码烂掉但没人发现」的窗口。
# 只想快速出包时可加 -DBUILD_TESTING=OFF 跳过。
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
