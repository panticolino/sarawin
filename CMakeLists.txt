cmake_minimum_required(VERSION 3.16)
project(SaraLibre
    VERSION 0.1.0
    DESCRIPTION "Software de Automatización RAdial Libre"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Qt6 automoc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ── Opciones ──────────────────────────────────────────────
option(SARA_BUILD_TESTS "Build unit tests" ON)

# ── Dependencias del sistema ──────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)
pkg_check_modules(GST REQUIRED
    gstreamer-1.0
    gstreamer-app-1.0
    gstreamer-audio-1.0
    gstreamer-pbutils-1.0
)
pkg_check_modules(TAGLIB REQUIRED taglib)

# ── Third-party (header-only) ────────────────────────────
# spdlog
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
    set(SPDLOG_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/spdlog/include")
else()
    find_package(spdlog QUIET)
    if(spdlog_FOUND)
        set(SPDLOG_INCLUDE "")
    else()
        message(FATAL_ERROR
            "spdlog not found. Run:\n"
            "  git clone --depth 1 https://github.com/gabime/spdlog.git third_party/spdlog\n"
            "or install: apt install libspdlog-dev")
    endif()
endif()

# toml++ 
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include")
    set(TOML_INCLUDE "${CMAKE_SOURCE_DIR}/third_party/tomlplusplus/include")
else()
    find_path(TOML_INCLUDE_SEARCH "toml++/toml.hpp"
        PATHS /usr/include /usr/local/include)
    if(TOML_INCLUDE_SEARCH)
        set(TOML_INCLUDE "${TOML_INCLUDE_SEARCH}")
    else()
        message(FATAL_ERROR
            "toml++ not found. Run:\n"
            "  git clone --depth 1 https://github.com/marzer/tomlplusplus.git third_party/tomlplusplus\n"
            "or install: apt install libtomlplusplus-dev")
    endif()
endif()

# ── Fuentes ───────────────────────────────────────────────
set(SARA_CORE_SOURCES
    src/core/types.h
    src/core/schedule_engine.h
    src/core/schedule_engine.cpp
    src/core/track_selector.h
    src/core/track_selector.cpp
    src/core/time_announcer.h
    src/core/time_announcer.cpp
    src/core/pisador_manager.h
    src/core/pisador_manager.cpp
    src/core/event_dispatcher.h
    src/core/event_dispatcher.cpp
)

set(SARA_AUDIO_SOURCES
    src/audio/audio_pipeline.h
    src/audio/audio_pipeline.cpp
    src/audio/audio_engine.h
    src/audio/audio_engine.cpp
    src/audio/crossfader.h
    src/audio/crossfader.cpp
    src/audio/recording_manager.h
    src/audio/recording_manager.cpp
)

set(SARA_DATA_SOURCES
    src/data/database.h
    src/data/database.cpp
    src/data/config_manager.h
    src/data/config_manager.cpp
    src/data/schedule_repository.h
    src/data/schedule_repository.cpp
    src/data/event_repository.h
    src/data/event_repository.cpp
    src/data/instant_play_repo.h
    src/data/instant_play_repo.cpp
    src/data/audit_manager.h
    src/data/audit_manager.cpp
    src/data/state_manager.h
    src/data/state_manager.cpp
    src/data/backup_manager.h
    src/data/backup_manager.cpp
    src/data/user_manager.h
    src/data/user_manager.cpp
    src/data/stream_preset_repo.h
    src/data/stream_preset_repo.cpp
)

set(SARA_UTIL_SOURCES
    src/util/logger.h
    src/util/logger.cpp
    src/util/codec_checker.h
    src/util/codec_checker.cpp
    src/util/file_scanner.h
    src/util/file_scanner.cpp
    src/util/metadata_reader.h
    src/util/metadata_reader.cpp
    src/util/qt_connect_helpers.h
)

set(SARA_UI_SOURCES
    src/ui/main_window.h
    src/ui/main_window.cpp
    src/ui/header_widget.h
    src/ui/header_widget.cpp
    src/ui/schedule_column.h
    src/ui/schedule_column.cpp
    src/ui/events_column.h
    src/ui/events_column.cpp
    src/ui/live_assist_column.h
    src/ui/live_assist_column.cpp
    src/ui/settings_dialog.h
    src/ui/settings_dialog.cpp
    src/ui/schedule_editor.h
    src/ui/schedule_editor.cpp
    src/ui/schedule_manager.h
    src/ui/schedule_manager.cpp
    src/ui/weekly_grid.h
    src/ui/weekly_grid.cpp
    src/ui/list_manager.h
    src/ui/list_manager.cpp
    src/ui/event_editor.h
    src/ui/event_editor.cpp
    src/ui/metatag_editor.h
    src/ui/metatag_editor.cpp
    src/ui/login_dialog.h
    src/ui/login_dialog.cpp
    src/ui/eq_compressor_dialog.h
    src/ui/eq_compressor_dialog.cpp
    src/ui/inverse_assign.h
    src/ui/inverse_assign.cpp
    src/ui/event_summary.h
    src/ui/event_summary.cpp
    src/ui/audit_dashboard.h
    src/ui/audit_dashboard.cpp
    src/ui/vumeter_widget.h
    src/ui/vumeter_widget.cpp
)

# Recurso Qt con SQL schema
set(SARA_RESOURCES
    resources/saralibre.qrc
)

# ── Biblioteca core (compartida entre app y tests) ────────
add_library(sara_core STATIC
    ${SARA_CORE_SOURCES}
    ${SARA_AUDIO_SOURCES}
    ${SARA_DATA_SOURCES}
    ${SARA_UTIL_SOURCES}
    ${SARA_RESOURCES}
)

target_include_directories(sara_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${GST_INCLUDE_DIRS}
    ${TAGLIB_INCLUDE_DIRS}
)

# Agregar includes de third-party solo si existen localmente
if(SPDLOG_INCLUDE)
    target_include_directories(sara_core PUBLIC ${SPDLOG_INCLUDE})
endif()
if(TOML_INCLUDE)
    target_include_directories(sara_core PUBLIC ${TOML_INCLUDE})
endif()

target_link_libraries(sara_core PUBLIC
    Qt6::Core
    Qt6::Sql
    ${GST_LIBRARIES}
    ${TAGLIB_LIBRARIES}
)

# Si spdlog fue encontrado como paquete del sistema
if(spdlog_FOUND)
    target_link_libraries(sara_core PUBLIC spdlog::spdlog)
endif()

target_compile_options(sara_core PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Ejecutable principal ──────────────────────────────────
add_executable(saralibre
    src/main.cpp
    ${SARA_UI_SOURCES}
)

target_link_libraries(saralibre PRIVATE
    sara_core
    Qt6::Widgets
    ${TAGLIB_LIBRARIES}
)

target_include_directories(saralibre PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${TAGLIB_INCLUDE_DIRS}
)

target_compile_options(saralibre PRIVATE
    -Wall -Wextra -Wpedantic
)

# ── Tests ─────────────────────────────────────────────────
if(SARA_BUILD_TESTS)
    enable_testing()

    # Test de base de datos
    add_executable(test_database tests/test_database.cpp)
    target_link_libraries(test_database PRIVATE sara_core)
    add_test(NAME DatabaseTest COMMAND test_database)

    # Test de config manager
    add_executable(test_config tests/test_config.cpp)
    target_link_libraries(test_config PRIVATE sara_core)
    add_test(NAME ConfigTest COMMAND test_config)

    # Test de audio pipeline
    add_executable(test_audio tests/test_audio.cpp)
    target_link_libraries(test_audio PRIVATE sara_core)
    add_test(NAME AudioTest COMMAND test_audio)

    # Test de file scanner
    add_executable(test_file_scanner tests/test_file_scanner.cpp)
    target_link_libraries(test_file_scanner PRIVATE sara_core)
    add_test(NAME FileScannerTest COMMAND test_file_scanner)
endif()

# ── Instalación ───────────────────────────────────────────
install(TARGETS saralibre DESTINATION bin)
install(FILES dist/saralibre.desktop DESTINATION share/applications)

# ── Info ──────────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════╗")
message(STATUS "║  SARA Libre v${PROJECT_VERSION}                     ║")
message(STATUS "║  Build type: ${CMAKE_BUILD_TYPE}                    ║")
message(STATUS "║  Qt6:        ${Qt6_VERSION}                     ║")
message(STATUS "║  GStreamer:   ${GST_VERSION}                    ║")
message(STATUS "║  Tests:      ${SARA_BUILD_TESTS}                       ║")
message(STATUS "╚══════════════════════════════════════════╝")
message(STATUS "")
