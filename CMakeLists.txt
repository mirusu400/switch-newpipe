cmake_minimum_required(VERSION 3.15)

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)
set(LUNASVG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lunasvg)
set(CMAKE_DISABLE_FIND_PACKAGE_DBus TRUE CACHE BOOL "Disable DBus for Switch builds" FORCE)
set(LIBROMFS_PREBUILT_GENERATOR
    ${BOREALIS_DIR}/libromfs-generator
    CACHE FILEPATH "Host libromfs generator"
    FORCE)

include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(switch_newpipe
    VERSION 0.0.1
    LANGUAGES C CXX
)

set(PROJECT_AUTHOR "switch-newpipe")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

option(USE_LIBROMFS "Use libromfs for resource embedding" ON)
if (USE_LIBROMFS)
    add_libromfs(${PROJECT_NAME} ${PROJECT_RESOURCES})
endif()

file(GLOB_RECURSE MAIN_SRC
    src/activity/*.cpp
    src/common/*.cpp
    src/tab/*.cpp
    src/view/*.cpp
)
list(APPEND MAIN_SRC src/main.cpp)

if (PLATFORM_SWITCH)
    file(GLOB_RECURSE SWITCH_SRC src/switch/*.cpp)
    list(APPEND MAIN_SRC ${SWITCH_SRC})
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
endif()

add_subdirectory(${BOREALIS_LIBRARY})
target_compile_definitions(borealis PUBLIC BRLS_I18N_PREFIX="strings/")

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_compile_definitions(PLUTOVG_DISABLE_FONT_FACE_CACHE_LOAD)
add_subdirectory(${LUNASVG_DIR} EXCLUDE_FROM_ALL)

set(QJS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI OFF CACHE BOOL "" FORCE)
if (PLATFORM_SWITCH)
    set(CMAKE_DL_LIBS "" CACHE STRING "" FORCE)
    set(CMAKE_THREAD_LIBS_INIT "" CACHE STRING "" FORCE)
    set(M_LIBRARIES "" CACHE STRING "" FORCE)
    set(M_LIBRARIES-NOTFOUND TRUE CACHE BOOL "" FORCE)
endif()
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/quickjs EXCLUDE_FROM_ALL)

program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    vendor/third_party
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    APP_NAME="${PROJECT_NAME}"
    APP_VERSION="${PROJECT_VERSION}"
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -ffunction-sections
    -fdata-sections
)

set(APP_PLATFORM_LIB "")
if (PLATFORM_SWITCH)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)
    get_target_property(MPV_LINK_LIBS PkgConfig::MPV INTERFACE_LINK_LIBRARIES)
    if (MPV_LINK_LIBS)
        set_property(TARGET PkgConfig::MPV PROPERTY INTERFACE_LINK_LIBRARIES
            "${MPV_LINK_LIBS};mbedtls;mbedx509;mbedcrypto")
    endif()
    list(APPEND APP_PLATFORM_LIB
        PkgConfig::MPV
        PkgConfig::SDL2
        PkgConfig::EGL
        PkgConfig::CURL
        GLESv2
    )
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    lunasvg::lunasvg
    qjs
    z
    ${APP_PLATFORM_LIB}
)

if (PLATFORM_SWITCH)
    add_custom_target(${PROJECT_NAME}.nro
        DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "Switch-NewPipe" "${PROJECT_AUTHOR}" "${PROJECT_VERSION}" ${PROJECT_NAME}.nacp
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
            --icon=${PROJECT_ICON}
            --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
        ALL
    )
endif()
