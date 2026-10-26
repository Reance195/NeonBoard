# R4-05：纯逻辑单元测试（Qt Test + CTest）。
#
# 设计取舍：
#   1. 测试目标把「被测源码」单独编入，不链接 NeonBoard 主程序 —— 主程序是
#      WIN32 可执行文件并打包了 QML 模块，直接作为测试依赖既跑不起来也过重。
#   2. 只收录「不依赖数据库 / 窗口 / QML 引擎」的纯逻辑模块，保证测试可离线、
#      可并行、无时序抖动；涉及 Database 的门面（TaskModel/TaskStats/TaskTree）
#      后续可按同样方式建独立的 sqlite 临时库测试。
#   3. 每个测试都是独立可执行文件，CTest 单独计时，失败定位到具体用例。

find_package(Qt6 6.8 REQUIRED COMPONENTS Test Sql)

function(neonboard_add_test target)
    qt_add_executable(${target} ${target}.cpp ${ARGN})
    target_link_libraries(${target} PRIVATE Qt6::Test)
    target_include_directories(${target} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if (MSVC)
        # 源码含中文断言数据：不加 /utf-8 会被按本地代码页解析成乱码
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
    add_test(NAME ${target} COMMAND ${target})
    # 单元测试必须快：超时即视为死锁/死循环，而不是无限挂住 CI
    set_tests_properties(${target} PROPERTIES TIMEOUT 120)
endfunction()

neonboard_add_test(tst_taskinputparser
    ${CMAKE_SOURCE_DIR}/src/TaskInputParser.cpp
)

neonboard_add_test(tst_pluginpackage
    ${CMAKE_SOURCE_DIR}/src/PluginPackage.cpp
)

# R4-02：动作层（命令行 → 动作 → QML 调用，以及二次启动的 IPC 帧）
neonboard_add_test(tst_appactions
    ${CMAKE_SOURCE_DIR}/src/AppActions.cpp
)

# R4-02：文件关联 / 应用标识 / 跳转列表任务项。只验证纯逻辑与只读查询，
# 写入注册表或系统跳转列表的动作不进测试（否则跑测试就改了开发机设置）。
neonboard_add_test(tst_windowsintegration
    ${CMAKE_SOURCE_DIR}/src/WindowsIntegration.cpp
    ${CMAKE_SOURCE_DIR}/src/AppActions.cpp
)
if (WIN32)
    # SetCurrentProcessExplicitAppUserModelID / SHChangeNotify
    target_link_libraries(tst_windowsintegration PRIVATE Shell32)
    # 跳转列表的 COM 入口（CoInitializeEx / CoCreateInstance / CoTaskMemAlloc）
    target_link_libraries(tst_windowsintegration PRIVATE Ole32)
endif()

# R4-03：schema 迁移（v1 老库 → v2 看板/列）。用 sqlite 临时库跑，
# 覆盖「新库建到 v2 / 老库搬迁不丢字段 / 重开幂等 / 拒绝过高版本」四条。
neonboard_add_test(tst_databasemigration
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
)
target_link_libraries(tst_databasemigration PRIVATE Qt6::Sql)

# R4-03 批次二：列感知的模型层（新增任务落默认列 / 按列搬运 / 按列过滤不串味）
neonboard_add_test(tst_taskcolumns
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskInputParser.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTree.cpp
)
target_link_libraries(tst_taskcolumns PRIVATE Qt6::Sql)

# R4-03 批次三：看板列清单（列由库驱动）+ 列内精确插位 + 状态/列一致性。
# 覆盖「列顺序与角色 / 搜索落到每列 / 按列录入 / 列内插位 / 按状态移动同步列 /
# reload 不重建模型 / 撤销连列一起还原」。
neonboard_add_test(tst_boardcolumns
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskInputParser.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTree.cpp
    ${CMAKE_SOURCE_DIR}/src/BoardColumns.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskUndo.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskExtra.cpp
)
target_link_libraries(tst_boardcolumns PRIVATE Qt6::Sql)

# R4-07：首启动引导的持久化与「该不该弹」判定。
# QSettings 的 UserScope 被指到临时目录，不碰开发机真实设置。
neonboard_add_test(tst_uisettings
    ${CMAKE_SOURCE_DIR}/src/UiSettings.cpp
)
