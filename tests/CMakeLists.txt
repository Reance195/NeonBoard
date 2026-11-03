# R4-05：纯逻辑单元测试（Qt Test + CTest）。
#
# 设计取舍：
#   1. 测试目标把「被测源码」单独编入，不链接 NeonBoard 主程序 —— 主程序是
#      WIN32 可执行文件并打包了 QML 模块，直接作为测试依赖既跑不起来也过重。
#   2. 只收录「不依赖数据库 / 窗口 / QML 引擎」的纯逻辑模块，保证测试可离线、
#      可并行、无时序抖动；涉及 Database 的门面（TaskModel/TaskStats/TaskTree）
#      后续可按同样方式建独立的 sqlite 临时库测试。
#   3. 每个测试都是独立可执行文件，CTest 单独计时，失败定位到具体用例。

# Gui：R14-01 的 tst_pluginruntime 要整体编入 PluginLoader.cpp，它用 QDesktopServices
# 打开插件目录。测试本身是 QTEST_GUILESS_MAIN，不创建 QGuiApplication，只是链接期需要。
find_package(Qt6 6.8 REQUIRED COMPONENTS Test Sql Gui)

# ── MOD-02：源码目录映射（唯一事实源）──────────────────────────────────────────
#
# 本文件有 199 处引用 src/ 下的被测源文件。若每处都写死源码根，那么 src/ 一旦按
# 模块分层（MOD-06），199 处全部失效、全部测试链接失败。所以路径只在这里定义
# 一次，调用点一律写 ${NB_DIR_<模块>}/Xxx.cpp —— 分层那天只改下面 8 行，调用点零改动。
#
# 归属依据 docs/module-split-plan.md §3.1 / §4.2，含 MOD-01 修正的三个例外：
# TaskRunner→agent、DataPort→board、HookEventQueue→plugin（本文件只引用 TaskRunner）。
set(NB_DIR_CORE "${CMAKE_SOURCE_DIR}/src/core")
set(NB_DIR_TASK "${CMAKE_SOURCE_DIR}/src/task")
set(NB_DIR_PLUGIN "${CMAKE_SOURCE_DIR}/src/plugin")
set(NB_DIR_AGENT "${CMAKE_SOURCE_DIR}/src/agent")
set(NB_DIR_BOARD "${CMAKE_SOURCE_DIR}/src/board")
set(NB_DIR_SHELL "${CMAKE_SOURCE_DIR}/src/shell")
set(NB_DIR_UPDATE "${CMAKE_SOURCE_DIR}/src/update")
set(NB_DIR_THEME "${CMAKE_SOURCE_DIR}/src/theme")

# 每个模块独占一个目录，头文件跨模块 include，故 include 路径要覆盖全部 8 个。
# MOD-06 之后 8 个变量各指一个子目录，去重后是 8 条 include。
set(NB_INCLUDE_DIRS
    ${NB_DIR_CORE} ${NB_DIR_TASK} ${NB_DIR_PLUGIN} ${NB_DIR_AGENT}
    ${NB_DIR_BOARD} ${NB_DIR_SHELL} ${NB_DIR_UPDATE} ${NB_DIR_THEME})
list(REMOVE_DUPLICATES NB_INCLUDE_DIRS)

function(neonboard_add_test target)
    qt_add_executable(${target} ${target}.cpp ${ARGN})
    target_link_libraries(${target} PRIVATE Qt6::Test)
    target_include_directories(${target} PRIVATE ${NB_INCLUDE_DIRS})
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
    ${NB_DIR_TASK}/TaskInputParser.cpp
)

neonboard_add_test(tst_pluginpackage
    ${NB_DIR_PLUGIN}/PluginPackage.cpp
)

# R14-01：官方来源插件的授权补齐策略。
# 只测 PluginLoader 的静态策略函数（来源判定 + 补齐计算），因此仍要整体编入
# PluginLoader.cpp —— 它带 Q_OBJECT，且构造需要 Database，故一并带上依赖。
# 关键用例是「grants 已存在」的场景：全新库走首次 bootstrap 会全授予，
# 拿它验收等于没测（R9 四张卡就是这么漏过去的）。
neonboard_add_test(tst_pluginruntime
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_PLUGIN}/PluginLoader.cpp
)
target_link_libraries(tst_pluginruntime PRIVATE Qt6::Sql Qt6::Gui)

# R4-02：动作层（命令行 → 动作 → QML 调用，以及二次启动的 IPC 帧）
neonboard_add_test(tst_appactions
    ${NB_DIR_CORE}/AppActions.cpp
)

# R4-02：文件关联 / 应用标识 / 跳转列表任务项。只验证纯逻辑与只读查询，
# 写入注册表或系统跳转列表的动作不进测试（否则跑测试就改了开发机设置）。
neonboard_add_test(tst_windowsintegration
    ${NB_DIR_SHELL}/WindowsIntegration.cpp
    ${NB_DIR_CORE}/AppActions.cpp
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
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
)
target_link_libraries(tst_databasemigration PRIVATE Qt6::Sql)

# R4-03 批次二：列感知的模型层（新增任务落默认列 / 按列搬运 / 按列过滤不串味）
neonboard_add_test(tst_taskcolumns
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
)
target_link_libraries(tst_taskcolumns PRIVATE Qt6::Sql)

# R10-03：标签语义修正（阶段与分类的派生与标签顺序无关）。
# 只调 TaskModel 的静态派生函数，但 TaskModel.cpp 整体编入，故仍带上 Database / 解析器。
neonboard_add_test(tst_taskmodel
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
)
target_link_libraries(tst_taskmodel PRIVATE Qt6::Sql)

# R10-01：项目维度侧表（project / task_project）的门面行为。
# 要真库（建表 / 事务 / REPLACE 语义都要 sqlite 自己判），照 tst_taskexec 的方式开临时库。
neonboard_add_test(tst_projectstore
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_BOARD}/ProjectStore.cpp
)
target_link_libraries(tst_projectstore PRIVATE Qt6::Sql)

# R10-02：两级看板（全局 ⇄ 项目范围）的视图语义。
# 要真库 + 真列模型：验收问的是「切到项目后列里还剩哪些卡」，
# 那是 BoardColumns → TaskFilterModel → task_project 这条链的结果。
neonboard_add_test(tst_projectscope
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_BOARD}/ProjectStore.cpp
    ${NB_DIR_BOARD}/BoardColumns.cpp
)
target_link_libraries(tst_projectscope PRIVATE Qt6::Sql)

# R4-03 批次三：看板列清单（列由库驱动）+ 列内精确插位 + 状态/列一致性。
# 覆盖「列顺序与角色 / 搜索落到每列 / 按列录入 / 列内插位 / 按状态移动同步列 /
# reload 不重建模型 / 撤销连列一起还原」。
neonboard_add_test(tst_boardcolumns
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_BOARD}/BoardColumns.cpp
    ${NB_DIR_BOARD}/ProjectStore.cpp
    ${NB_DIR_TASK}/TaskUndo.cpp
    ${NB_DIR_TASK}/TaskExtra.cpp
)
target_link_libraries(tst_boardcolumns PRIVATE Qt6::Sql)

# R8-03：卡片详情抽屉的后端契约 —— 补丁的脏检查 / 「没改动不写库」/ 只写变化字段 /
# 侧表提醒与重复 / 空标题整单拒绝 / 到期与提醒按界面粒度比较。
# 用 sqlite 临时库跑真实读写：写没写库要看 TaskModel 副本与 updated_at，mock 会把它藏起来。
neonboard_add_test(tst_carddetail
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskUndo.cpp
    ${NB_DIR_TASK}/TaskExtra.cpp
    ${NB_DIR_BOARD}/CardDetail.cpp
)
target_link_libraries(tst_carddetail PRIVATE Qt6::Sql)

# R4-07：首启动引导的持久化与「该不该弹」判定。
# QSettings 的 UserScope 被指到临时目录，不碰开发机真实设置。
neonboard_add_test(tst_uisettings
    ${NB_DIR_CORE}/UiSettings.cpp
)

# R5-01：软件更新的纯逻辑（版本比较 / 清单解析 / 相对地址 / 自更新脚本 / 包根定位）。
# 下载与安装要联网并替换进程自身，不进自动化测试。
neonboard_add_test(tst_updatemanager
    ${NB_DIR_UPDATE}/UpdateManager.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_UPDATE}/DeltaUpdate.cpp
    ${NB_DIR_UPDATE}/BsdiffWrapper.cpp
    ${CMAKE_SOURCE_DIR}/thirdparty/bsdiff/bsdiff.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bsdiff/bspatch.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bzip2/bzlib.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bzip2/blocksort.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bzip2/huffman.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bzip2/crctable.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bzip2/randtable.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bzip2/compress.c
    ${CMAKE_SOURCE_DIR}/thirdparty/bzip2/decompress.c
)
# QNetworkAccessManager 与 QDesktopServices
target_link_libraries(tst_updatemanager PRIVATE Qt6::Network Qt6::Gui)
# bsdiff / bzip2 头在 thirdparty/ 下，BsdiffWrapper.cpp 按仓库根相对路径包含
target_include_directories(tst_updatemanager PRIVATE ${CMAKE_SOURCE_DIR})

# M0：任务执行定义侧表（task_exec）——默认值 / 往返 / 校验 / 清空 / 删除同步。
# 侧表读写要真库，用 sqlite 临时库跑（与 tst_taskcolumns 同一方式）。
neonboard_add_test(tst_taskexec
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
)
target_link_libraries(tst_taskexec PRIVATE Qt6::Sql)

# R6-02：运行记录侧表与同卡锁 —— 取锁 / 忙拒绝 / 释放后可再取 / 孤儿回收 / schema 版本不变。
neonboard_add_test(tst_taskruns
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
)
target_link_libraries(tst_taskruns PRIVATE Qt6::Sql)

# R6-03：agent profile —— 内置三条 / argv 拼装 / stdin 不塞 argv / 校验 / meta 持久化 /
# 总开关与 cwd 白名单。profile 存 meta 表，用 sqlite 临时库跑。
neonboard_add_test(tst_agentprofile
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
)
target_link_libraries(tst_agentprofile PRIVATE Qt6::Sql)

# R6-04：执行器 TaskRunner —— 拉起进程 / 流式日志 / 超时·终止 / 同卡锁 / 生命周期。
# 依赖 TaskRunStore（锁）与 AgentProfile（argv / 总开关）与 TaskExec（执行定义）。
neonboard_add_test(tst_taskrunner
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
    ${NB_DIR_AGENT}/TaskRunner.cpp
)
target_link_libraries(tst_taskrunner PRIVATE Qt6::Sql)

# R6-05：验收器与越界检查 —— command 白名单 / shell 语法拒绝 / assert file.exists /
# fail-fast / verdict 聚合；ScopeMatcher 用临时 git 仓库 fixture，不碰真实工作区。
neonboard_add_test(tst_acceptance
    ${NB_DIR_AGENT}/AcceptanceRunner.cpp
)

neonboard_add_test(tst_scopematcher
    ${NB_DIR_CORE}/ScopeMatcher.cpp
)

# R6-06：执行结果回写 —— 摘要（状态/耗时/退出码/末尾日志）/ 重试 attempt 递增 /
# latestRun 活跃优先 / 取消不改看板列。要真库（task_run + task），sqlite 临时库跑。
neonboard_add_test(tst_runsummary
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
)
target_link_libraries(tst_runsummary PRIVATE Qt6::Sql)

# R6-07：Triage 只读分析 —— JSON 抠取 / glob 合法性 / 草案校验清洗 / prompt 模板 /
# 草案存 meta / start 前置拒绝（无只读参数硬拒）。真实 agent 进程不进单测。
neonboard_add_test(tst_triage
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
    ${NB_DIR_CORE}/ScopeMatcher.cpp
    ${NB_DIR_AGENT}/TriageRunner.cpp
)
target_link_libraries(tst_triage PRIVATE Qt6::Sql)

# R6-08：Loop 失败摘要回灌与有限重试 —— maxAttempts 钳制 / 重试 attempt 递增 /
# 每轮日志独立 / 取消不重试 / 耗尽提示人工介入 / 摘要截断。载体 cmd.exe，离线可跑。
neonboard_add_test(tst_runloop
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
    ${NB_DIR_AGENT}/TaskRunner.cpp
)
target_link_libraries(tst_runloop PRIVATE Qt6::Sql)

# R7-01：执行队列与调度器 —— FIFO 顺序 / 同卡防重 / 并发上限与背压 / 暂停·调序·跳过 /
# 派发失败结算成 blocked / 孤儿回收不误杀排队条目 / schema 版本不变。
# 不真拉起 agent 进程（用不存在的 agent 让 TaskRunner::start 立刻失败）。
neonboard_add_test(tst_scheduler
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
    ${NB_DIR_AGENT}/TaskRunner.cpp
    ${NB_DIR_AGENT}/Scheduler.cpp
)
target_link_libraries(tst_scheduler PRIVATE Qt6::Sql)

# R7-03：角色链 workflow 引擎 —— 预设步骤 / 步骤校验 / 卡片覆盖存 meta /
# 交接物路径与 prompt 注入 / 摘要保留尾部 / 两步链端到端 / 失败即停。
# 载体 cmd.exe（ComSpec 指到 profile 的 exe），离线可跑。
neonboard_add_test(tst_workflow
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
    ${NB_DIR_AGENT}/TaskRunner.cpp
    ${NB_DIR_CORE}/ScopeMatcher.cpp
    ${NB_DIR_AGENT}/AcceptanceRunner.cpp
    ${NB_DIR_AGENT}/WorkflowRunner.cpp
    ${NB_DIR_AGENT}/WorkflowPresets.cpp
)
target_link_libraries(tst_workflow PRIVATE Qt6::Sql)

# R7-04：预设 workflow 与 orchestrator 收口 —— 内置六条（与 presetSteps 一致）/
# 规模选型 / 跳过规则（qa 永不裁）/ 预设与映射表可编辑（存 meta，不升 user_version）/
# 收口 summary / WorkflowRunner 接上预设后的自动选型与步骤展开。载体 cmd.exe，离线可跑。
neonboard_add_test(tst_workflowpresets
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
    ${NB_DIR_AGENT}/TaskRunner.cpp
    ${NB_DIR_CORE}/ScopeMatcher.cpp
    ${NB_DIR_AGENT}/AcceptanceRunner.cpp
    ${NB_DIR_AGENT}/WorkflowRunner.cpp
    ${NB_DIR_AGENT}/WorkflowPresets.cpp
)
target_link_libraries(tst_workflowpresets PRIVATE Qt6::Sql)

# R7-05：夜间批量模式 —— 多选卡按序入队 / 预计总时长（兜底值·按并发折算·单卡历史均值）/
# enqueueNightBatch 返回估算 / 单卡失败（缺 agent）不阻断其余 / schema 版本不变。
# 好卡走 cmd.exe，坏卡指向不存在的 agent。
neonboard_add_test(tst_nightbatch
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskRunStore.cpp
    ${NB_DIR_AGENT}/AgentProfile.cpp
    ${NB_DIR_AGENT}/TaskRunner.cpp
    ${NB_DIR_AGENT}/Scheduler.cpp
)
target_link_libraries(tst_nightbatch PRIVATE Qt6::Sql)


# R6-09：台账卡单向导入（ADR-0001）—— 只补空值 / 冲突只报告 / 未建卡只报告 /
# 幂等 / note「目标：」段作 goal。台账 fixture 在临时目录，不碰真实台账。
neonboard_add_test(tst_ledgerimport
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskExec.cpp
    ${NB_DIR_AGENT}/LedgerImport.cpp
)
target_link_libraries(tst_ledgerimport PRIVATE Qt6::Sql)

# R11-03：统计门面的逾期计数 —— 未完成 + 设了到期时间 + 已过期才计数。
# 涉及 task 表，要真库（sqlite 临时库跑），与 tst_taskcolumns 同一方式。
neonboard_add_test(tst_taskstats
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskStats.cpp
)
target_link_libraries(tst_taskstats PRIVATE Qt6::Sql)

# R8-05：拖拽落点几何 —— 空列 / 上半区下半区 / 滚动时最后一张可见卡下面还有卡 /
# 磁吸半径 / 落位动效预算。纯算术，不需要数据库与 QML 引擎。
neonboard_add_test(tst_dragfeel
    ${NB_DIR_SHELL}/DragFeel.cpp
)

# R8-06：键盘跨列位移 —— 目标列越界不移动（也不压空撤销记录）/ 落位保持相对位置 /
# 目标列更短落列尾 / 两步到末列 / Ctrl+Z 全还原 / 「取消」一个字节都不写。
neonboard_add_test(tst_keymove
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskUndo.cpp
    ${NB_DIR_TASK}/TaskExtra.cpp
    ${NB_DIR_SHELL}/KeyMove.cpp
)
target_link_libraries(tst_keymove PRIVATE Qt6::Sql Qt6::Gui)

# R8-04：卡片事件审计 —— 老库自动补 task_event（且不升 user_version）/
# 一个动作一条事件（完成优先于移动）/ 删除后事件留档 / 倒序与 limit / 单卡封顶。
neonboard_add_test(tst_taskevent
    ${NB_DIR_CORE}/Database.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
    ${NB_DIR_TASK}/TaskModel.cpp
    ${NB_DIR_TASK}/TaskInputParser.cpp
    ${NB_DIR_TASK}/TaskFilterModel.cpp
    ${NB_DIR_TASK}/TaskTree.cpp
    ${NB_DIR_TASK}/TaskAudit.cpp
)
target_link_libraries(tst_taskevent PRIVATE Qt6::Sql)

# R8-01：外部主题包 —— 合法包解析 / #RGB 与 #RRGGBB / 坏 JSON、缺令牌、非法色值、
# 非法 id 统统拒绝 / 目录里坏包跳过其余照常 / 同 key 后者覆盖 / 重新扫描能增删。
neonboard_add_test(tst_themepack
    ${NB_DIR_THEME}/ThemePack.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
)

# R8-02：主题包市场 —— 离线目录非空 / 拉不到索引退离线 / 装完标已安装 /
# 文件名规整成 <id>.json / 坏包不落盘 / 中途中断不留半成品 / 卸载只删那一个。
# 网络部分只打 127.0.0.1:1（必定连不上），不依赖外网。
neonboard_add_test(tst_thememarket
    ${NB_DIR_THEME}/ThemeMarket.cpp
    ${NB_DIR_THEME}/ThemePack.cpp
    ${NB_DIR_CORE}/AppPaths.cpp
)
target_link_libraries(tst_thememarket PRIVATE Qt6::Network)

# 项目全景：路线图 §9 / 台账 / docs/project-map.json 的解析 + 证据实证。
# 这组断言是「地图跟着仓库走」的护栏：表格结构、脚本路径、派生规则任一被改坏，
# 这里先红，避免面板展示一张与仓库脱节的地图。
# 派生在 QThreadPool 后台线程跑（QtCore 自带），无需额外组件
neonboard_add_test(tst_projectmap
    ${NB_DIR_BOARD}/ProjectMap.cpp
)

