cmake_minimum_required(VERSION 3.16)
project(spin_noc VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── SystemC ────────────────────────────────────────────────────────────────────
# Pode ser sobrescrito via: cmake .. -DSYSTEMC_HOME=/seu/caminho
if(NOT DEFINED SYSTEMC_HOME)
    set(SYSTEMC_HOME "/usr/local/systemc")
endif()

find_library(SYSTEMC_LIB
    NAMES systemc
    PATHS "${SYSTEMC_HOME}/lib" "${SYSTEMC_HOME}/lib-linux64"
    REQUIRED
)
find_path(SYSTEMC_INCLUDE
    NAMES systemc.h
    PATHS "${SYSTEMC_HOME}/include"
    REQUIRED
)

message(STATUS "SystemC library : ${SYSTEMC_LIB}")
message(STATUS "SystemC include : ${SYSTEMC_INCLUDE}")

# ── Fontes ────────────────────────────────────────────────────────────────────
file(GLOB_RECURSE NOC_SOURCES
    "src/router/*.cpp"
    "src/network/*.cpp"
    "src/packet/*.cpp"
    "src/testbench/*.cpp"
)

add_executable(spin_noc_sim ${NOC_SOURCES})

target_include_directories(spin_noc_sim PRIVATE
    ${SYSTEMC_INCLUDE}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(spin_noc_sim PRIVATE ${SYSTEMC_LIB})

# ── Diretório de saída para artefatos de simulação ───────────────────────────
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/sim)

# ── Flags de warning úteis ───────────────────────────────────────────────────
target_compile_options(spin_noc_sim PRIVATE -Wall -Wextra)
