# MechAssault (original Xbox), statically recompiled

cmake_minimum_required(VERSION 3.25)
project(ma LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the SDK: sdk/ here (a checkout, or a junction to your own), two folders up inside the SDK's templates, else cloned
# into sdk/ at the commit patches/sdk.patch was made against, with the patch applied
set(SDK_REPOSITORY https://github.com/TGP482/XboxXBERecomp.git)
set(SDK_COMMIT 91146ff)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdk/cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/XbeRecomp.cmake)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/../..)
else()
    find_package(Git REQUIRED)
    set(SDK ${CMAKE_CURRENT_SOURCE_DIR}/sdk)
    message(STATUS "Cloning the SDK ${SDK_REPOSITORY} at ${SDK_COMMIT}")
    # an LF checkout: the line endings the patch was made against
    foreach(STEP
            "clone;--quiet;-c;core.autocrlf=false;-c;core.eol=lf;${SDK_REPOSITORY};${SDK}"
            "-C;${SDK};checkout;--quiet;${SDK_COMMIT}"
            "-C;${SDK};apply;${CMAKE_CURRENT_SOURCE_DIR}/patches/sdk.patch")
        execute_process(COMMAND ${GIT_EXECUTABLE} ${STEP} RESULT_VARIABLE RESULT)
        if(NOT RESULT EQUAL 0)
            file(REMOVE_RECURSE ${SDK})
            message(FATAL_ERROR "Setting up the SDK failed at: git ${STEP}\nCheck the network and that git is installed, then run this again.")
        endif()
    endforeach()
endif()
include(${SDK}/cmake/XbeRecomp.cmake)

file(GLOB XBE_HOST_SOURCES CONFIGURE_DEPENDS src/*/*.cpp)
list(FILTER XBE_HOST_SOURCES EXCLUDE REGEX "/src/game/")
# the controller setup screen reads SDL directly (the SDK gives SDL's headers to its input code only)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/common/pad_mapping.cpp
    PROPERTIES INCLUDE_DIRECTORIES "${sdl3_SOURCE_DIR}/include")

# partitions as xbe_symbols wrote them into config/game.toml
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS config/game.toml)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config/game.toml)
    file(STRINGS config/game.toml GAME_PARTITIONS REGEX "^partitions *=")
    string(REGEX MATCH "[0-9]+" GAME_PARTITIONS "${GAME_PARTITIONS}")
endif()
# no config yet: build.bat writes it with xbe_symbols, the change reconfigures
if(NOT GAME_PARTITIONS)
    set(GAME_PARTITIONS 8)
endif()

# target name = GAME_EXE in build.bat, xbe = GAME_XBE
add_xbe_image(MechAssault default.xbe game.toml ${GAME_PARTITIONS} game)
