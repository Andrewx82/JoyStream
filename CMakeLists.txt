cmake_minimum_required(VERSION 3.13)
project(joystream VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch" OR NINTENDO_SWITCH)
    # devkitPro Nintendo Switch toolchain
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DEPS REQUIRED
        mpv
        libcurl
        sdl2
        SDL2_image
        SDL2_ttf
    )

    include_directories(${DEPS_INCLUDE_DIRS} ${NX_STANDARD_INCLUDE_DIRECTORIES})
    link_directories(${DEPS_LIBRARY_DIRS})

    add_executable(joystream
        src/main.cpp
        src/http_client.cpp
        src/stremio_client.cpp
        src/addon_manager.cpp
        src/stream_filter.cpp
        src/player.cpp
        src/ui_engine.cpp
    )

    target_compile_options(joystream PRIVATE ${DEPS_CFLAGS_OTHER})
    target_link_libraries(joystream PRIVATE
        ${DEPS_LIBRARIES}
        ${NX_STANDARD_LIBRARIES}
        -lEGL -lglapi -ldrm_nouveau
    )

    # NACP and NRO Generation
    nx_generate_nacp(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/joystream.nacp"
        NAME "JoyStream"
        AUTHOR "Andrewx82"
        VERSION "0.1"
    )

    nx_create_nro(joystream
        TARGET joystream
        NACP "${CMAKE_CURRENT_BINARY_DIR}/joystream.nacp"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.jpg"
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/joystream.nro"
    )
else()
    # Host build (Desktop / Tests)
    find_package(CURL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(DEPS REQUIRED mpv sdl2 SDL2_image SDL2_ttf)

    add_executable(test_stremio_core
        tests/test_stremio_core.cpp
        src/http_client.cpp
        src/stremio_client.cpp
        src/addon_manager.cpp
        src/stream_filter.cpp
    )
    target_link_libraries(test_stremio_core PRIVATE CURL::libcurl pthread)

    add_executable(joystream-desktop
        src/main.cpp
        src/http_client.cpp
        src/stremio_client.cpp
        src/addon_manager.cpp
        src/stream_filter.cpp
        src/player.cpp
        src/ui_engine.cpp
    )
    target_include_directories(joystream-desktop PRIVATE ${DEPS_INCLUDE_DIRS})
    target_link_libraries(joystream-desktop PRIVATE ${DEPS_LIBRARIES} CURL::libcurl pthread)
endif()
