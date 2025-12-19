# 如果上层定义了 EXAMPLE_NAME，则作为模块加入现有示例工程
if(DEFINED EXAMPLE_NAME)
    set(APP_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

    set(APP_MODULE_SRCS)
    file(GLOB_RECURSE APP_MODULE_SRCS "${APP_MODULE_PATH}/*.c")

    set(APP_MODULE_INC
        ${APP_MODULE_PATH}
    )

    # 将当前目录的源码与头文件添加到已有的 ${EXAMPLE_LIB}
    target_sources(${EXAMPLE_LIB}
        PRIVATE
            ${APP_MODULE_SRCS}
    )
    target_include_directories(${EXAMPLE_LIB}
        PRIVATE
            ${APP_MODULE_INC}
    )

else()
    # 默认独立模式：构建本目录的 ui 静态库
    file(GLOB IMG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/images/*.c")
    file(GLOB GIF_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/gif/*.c")

    set(SOURCES
        screens/ui_home.c
        screens/ui_setting.c
        ui.c
        components/ui_comp_hook.c
        ui_helpers.c
        ui_events.c
        ui_themes.c
        ui_theme_manager.c
        ui_audio_waveform.c
        ${IMG_SRCS}
        ${GIF_SRCS}
        fonts/ui_font_mont_82.c
    )

    add_library(ui ${SOURCES})

    target_include_directories(ui PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}           # 当前目录，用于 ui.h 等本地头文件
        ${CMAKE_SOURCE_DIR}                   # 项目根目录，用于 lvgl/lvgl.h
    )

    target_link_libraries(ui PUBLIC lvgl)
endif()
# # set(APP_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})

# # set(APP_MODULE_SRCS)
# # file(GLOB_RECURSE APP_MODULE_SRCS "${APP_MODULE_PATH}/*.c")

# # set(APP_MODULE_INC
# #     ${APP_MODULE_PATH}
# # )

# # ########################################
# # # Target Configure
# # ########################################
# # target_sources(${EXAMPLE_LIB}
# #     PRIVATE
# #         ${APP_MODULE_SRCS}
# #     )
# # target_include_directories(${EXAMPLE_LIB}
# #     PRIVATE
# #         ${APP_MODULE_INC}
# #     )

# # 自动收集图片和 GIF 目录下的所有 .c 资源文件
# file(GLOB IMG_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/images/*.c")
# file(GLOB GIF_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/gif/*.c")

# SET(SOURCES
#     screens/ui_home.c
#     screens/ui_setting.c
#     ui.c
#     components/ui_comp_hook.c
#     ui_helpers.c
#     ui_events.c
#     ${IMG_SRCS}
#     ${GIF_SRCS}
#     fonts/ui_font_mont_82.c
# )

# add_library(ui ${SOURCES})

# # 设置包含路径，让 ui 库能找到 lvgl 头文件
# target_include_directories(ui PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}           # 当前目录，用于 ui.h 等本地头文件
#     ${CMAKE_SOURCE_DIR}                   # 项目根目录，用于 lvgl/lvgl.h
# )

# # 链接 lvgl 库
# target_link_libraries(ui PUBLIC lvgl)
