# NeonBoard 构建工具链版本单点（DX-145）
#
# 改 Qt / MSVC 工具集 / 生成器 / sccache 版本只改这里：
#   · CI（.github/workflows/*.yml）经 scripts/check_toolchain_versions.py
#     --emit-github-output 读成 step outputs，install-qt-action 用 outputs 填值；
#   · PowerShell 脚本读本文件的 set() 声明，不再各自写死版本号；
#   · scripts/check_toolchain_versions.py --check 拦住全仓散落的重复硬编码。
#
# 注意：产品版本号在顶层 CMakeLists.txt 的 project(VERSION)，不在本文件（Z5）。

set(NB_QT_VERSION "6.8.3")
set(NB_QT_ARCH "win64_msvc2022_64")
set(NB_QT_MODULES "qtshadertools")
set(NB_QT_DIR_SUFFIX "msvc2022_64")
set(NB_MSVC_TOOLSET "v143")
set(NB_CMAKE_GENERATOR_CI "Visual Studio 17 2022")
set(NB_CMAKE_GENERATOR_LOCAL "Ninja")
set(NB_SCCACHE_VERSION "0.8.2")
