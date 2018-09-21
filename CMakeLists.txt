cmake_minimum_required(VERSION 3.5)

project(MCCOutlet
	LANGUAGES CXX
	VERSION 0.1)

# set up LSL if not done already
if(NOT TARGET LSL::lsl)
	# when building out of tree LSL_ROOT needs to be specified on the cmd line
	file(TO_CMAKE_PATH "${LSL_INSTALL_ROOT}" LSL_INSTALL_ROOT)
	list(APPEND LSL_INSTALL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../LSL/liblsl/build/install")
	list(APPEND LSL_INSTALL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../build/install")
	find_package(LSL HINTS ${LSL_INSTALL_ROOT}/share/LSL/ ${LSL_INSTALL_ROOT}/LSL/share/LSL QUIET)
	if(NOT LSL_FOUND)
		message(FATAL_ERROR "Precompiled LSL was not found. See https://github.com/labstreaminglayer/labstreaminglayer/blob/master/doc/BUILD.md#lsl_install_root for more information.")
	endif()
	list(APPEND CMAKE_MODULE_PATH ${LSL_DIR})
	message(STATUS "Looking for LSLCMake in ${LSL_DIR}")
	include(LSLCMake)
endif()

# mccdaq
add_subdirectory(MCCDaq-libusb-driver)

# Target executable
add_executable(${PROJECT_NAME} MACOSX_BUNDLE WIN32
    MCCOutlet.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE
    mccdaq
)

target_link_libraries(${PROJECT_NAME}
    mccdaq
    LSL::lsl
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 14)
# target_compile_features(${PROJECT_NAME} PRIVATE cxx_auto_type cxx_lambda_init_captures)

installLSLApp(${PROJECT_NAME})
installLSLAuxFiles(${PROJECT_NAME}
	${PROJECT_NAME}.cfg
)
LSLGenerateCPackConfig()
