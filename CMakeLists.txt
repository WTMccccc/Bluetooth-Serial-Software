cmake_minimum_required(VERSION 3.16)
project(ble_uart C)

# Windows 10/11 + MSVC（需要 Windows SDK 10）
if (NOT MSVC)
    message(WARNING "本项目按 MSVC 编写，其它编译器需要自行调整 WinRT 调用方式")
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
enable_language(RC)

add_executable(ble_uart WIN32
    src/main.c
    src/ble.c
    src/util.c
    src/crashlog.c
    src/logview.c
    src/plot.c
    src/app.rc
)

target_compile_definitions(ble_uart PRIVATE UNICODE _UNICODE)
target_compile_options(ble_uart PRIVATE /utf-8 /W4 /wd4201)

target_link_libraries(ble_uart PRIVATE
    user32 gdi32 comctl32 comdlg32 shell32
    bthprops runtimeobject ole32 uuid dbghelp
)

# 可选的命令行测试程序（扫描/连接/收发）
add_executable(ble_cli
    tests/ble_cli.c
    src/ble.c
    src/util.c
)
target_compile_definitions(ble_cli PRIVATE UNICODE _UNICODE)
target_compile_options(ble_cli PRIVATE /utf-8 /W4 /wd4201)
target_link_libraries(ble_cli PRIVATE user32 bthprops runtimeobject ole32)
