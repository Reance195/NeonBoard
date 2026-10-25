# R4-05：纯逻辑单元测试（Qt Test + CTest）。
#
# 设计取舍：
#   1. 测试目标把「被测源码」单独编入，不链接 NeonBoard 主程序 —— 主程序是
#      WIN32 可执行文件并打包了 QML 模块，直接作为测试依赖既跑不起来也过重。
#   2. 只收录「不依赖数据库 / 窗口 / QML 引擎」的纯逻辑模块，保证测试可离线、
#      可并行、无时序抖动；涉及 Database 的门面（TaskModel/TaskStats/TaskTree）
#      后续可按同样方式建独立的 sqlite 临时库测试。
#   3. 每个测试都是独立可执行文件，CTest 单独计时，失败定位到具体用例。

find_package(Qt6 6.8 REQUIRED COMPONENTS Test)

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

# R4-02：文件关联 / 应用标识。只验证纯逻辑与只读查询，
# 写入注册表的动作不进测试（否则跑测试就改了开发机设置）。
neonboard_add_test(tst_windowsintegration
    ${CMAKE_SOURCE_DIR}/src/WindowsIntegration.cpp
)
if (WIN32)
    # SetCurrentProcessExplicitAppUserModelID / SHChangeNotify
    target_link_libraries(tst_windowsintegration PRIVATE Shell32)
endif()
