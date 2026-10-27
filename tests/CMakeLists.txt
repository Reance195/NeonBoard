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
    # 单元测试必须快：超时即视为死锁/死循环，而不是无限挂住 CI。
    #
    # 另加一条：把 Qt 的 bin 前置进测试进程的 PATH。
    # 不设的话，在没把 Qt bin 加进 PATH 的终端里直接跑 ctest，测试进程会在加载
    # Qt6Core.dll 处失败，Windows 弹一个「系统错误」硬错误框、进程挂住不退出，
    # ctest 只能干等到 TIMEOUT —— 表象是「测试卡死」，实际是根本没启动起来，
    # 而且每跑一个测试就攒一个弹窗（本次攒了七个）。
    set_tests_properties(${target} PROPERTIES
        TIMEOUT 120
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
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

# R5-01：软件更新的纯逻辑（版本比较 / 清单解析 / 相对地址 / 自更新脚本 / 包根定位）。
# 下载与安装要联网并替换进程自身，不进自动化测试。
neonboard_add_test(tst_updatemanager
    ${CMAKE_SOURCE_DIR}/src/UpdateManager.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
)
# QNetworkAccessManager 与 QDesktopServices
target_link_libraries(tst_updatemanager PRIVATE Qt6::Network Qt6::Gui)

# M0：任务执行定义侧表（task_exec）——默认值 / 往返 / 校验 / 清空 / 删除同步。
# 侧表读写要真库，用 sqlite 临时库跑（与 tst_taskcolumns 同一方式）。
neonboard_add_test(tst_taskexec
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskInputParser.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTree.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskExec.cpp
)
target_link_libraries(tst_taskexec PRIVATE Qt6::Sql)

# R6-02：运行记录侧表与同卡锁 —— 取锁 / 忙拒绝 / 释放后可再取 / 孤儿回收 / schema 版本不变。
neonboard_add_test(tst_taskruns
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskRunStore.cpp
)
target_link_libraries(tst_taskruns PRIVATE Qt6::Sql)

# R6-03：agent profile —— 内置三条 / argv 拼装 / stdin 不塞 argv / 校验 / meta 持久化 /
# 总开关与 cwd 白名单。profile 存 meta 表，用 sqlite 临时库跑。
neonboard_add_test(tst_agentprofile
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/AgentProfile.cpp
)
target_link_libraries(tst_agentprofile PRIVATE Qt6::Sql)

# R6-04：执行器 TaskRunner —— 拉起进程 / 流式日志 / 超时·终止 / 同卡锁 / 生命周期。
# 依赖 TaskRunStore（锁）与 AgentProfile（argv / 总开关）与 TaskExec（执行定义）。
neonboard_add_test(tst_taskrunner
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskExec.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskInputParser.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTree.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskRunStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AgentProfile.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskRunner.cpp
)
target_link_libraries(tst_taskrunner PRIVATE Qt6::Sql)

# R6-05：验收器与越界检查 —— command 白名单 / shell 语法拒绝 / assert file.exists /
# fail-fast / verdict 聚合；ScopeMatcher 用临时 git 仓库 fixture，不碰真实工作区。
neonboard_add_test(tst_acceptance
    ${CMAKE_SOURCE_DIR}/src/AcceptanceRunner.cpp
)

neonboard_add_test(tst_scopematcher
    ${CMAKE_SOURCE_DIR}/src/ScopeMatcher.cpp
)

# R6-06：执行结果回写 —— 摘要（状态/耗时/退出码/末尾日志）/ 重试 attempt 递增 /
# latestRun 活跃优先 / 取消不改看板列。要真库（task_run + task），sqlite 临时库跑。
neonboard_add_test(tst_runsummary
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskInputParser.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTree.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskRunStore.cpp
)
target_link_libraries(tst_runsummary PRIVATE Qt6::Sql)

# R6-07：Triage 只读分析 —— JSON 抠取 / glob 合法性 / 草案校验清洗 / prompt 模板 /
# 草案存 meta / start 前置拒绝（无只读参数硬拒）。真实 agent 进程不进单测。
neonboard_add_test(tst_triage
    ${CMAKE_SOURCE_DIR}/src/Database.cpp
    ${CMAKE_SOURCE_DIR}/src/AppPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskInputParser.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskFilterModel.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskTree.cpp
    ${CMAKE_SOURCE_DIR}/src/TaskExec.cpp
    ${CMAKE_SOURCE_DIR}/src/AgentProfile.cpp
    ${CMAKE_SOURCE_DIR}/src/ScopeMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/TriageRunner.cpp
)
target_link_libraries(tst_triage PRIVATE Qt6::Sql)

# 项目全景：路线图 §9 / 台账 / docs/project-map.json 的解析 + 证据实证。
# 这组断言是「地图跟着仓库走」的护栏：表格结构、脚本路径、派生规则任一被改坏，
# 这里先红，避免面板展示一张与仓库脱节的地图。
# 派生在 QThreadPool 后台线程跑（QtCore 自带），无需额外组件
neonboard_add_test(tst_projectmap
    ${CMAKE_SOURCE_DIR}/src/ProjectMap.cpp
)

