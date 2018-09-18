cmake_minimum_required(VERSION 2.8)

PROJECT(MCCOutlet)

# General settings
LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
cmake_policy(SET CMP0042 NEW) # ENABLE CMP0042: MACOSX_RPATH is enabled by default.
IF(UNIX)
    # c++11 for nullptr and others
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
ENDIF()

add_subdirectory(MCCDaq-libusb-driver)

# Third party packages
SET(REQUIRED_LIBS)
# LSL
set(LSL_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../labstreaminglayer/LSL/liblsl)
find_package(LSL REQUIRED)
LIST(APPEND REQUIRED_LIBS ${LSL_LIBRARIES})

# Target executable
add_executable(${PROJECT_NAME}
    ${CMAKE_CURRENT_SOURCE_DIR}/MCCOutlet.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    mccdaq
    ${LSL_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME}
    mccdaq
    ${REQUIRED_LIBS}
)

IF(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${LSL_BINARY_RELEASE}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${LSL_BINARY_DEBUG}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>)
ENDIF()
