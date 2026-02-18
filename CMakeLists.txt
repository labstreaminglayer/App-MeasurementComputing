# =============================================================================
# MCCOutlet - Measurement Computing DAQ to Lab Streaming Layer
# =============================================================================
# Streams analog input data from MCC DAQ devices into LSL.
# Based on the LSL AppTemplate_cpp_qt template.
#
# Dependencies fetched automatically:
#   - liblsl (via FetchContent)
#   - uldaq (via ExternalProject, built from Autotools source)
# =============================================================================

cmake_minimum_required(VERSION 3.28)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(MCCOutlet
    VERSION 2.0.0
    DESCRIPTION "Measurement Computing DAQ to LSL"
    HOMEPAGE_URL "https://github.com/labstreaminglayer/App-MeasurementComputing"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =============================================================================
# Build Options
# =============================================================================
option(MCCOUTLET_BUILD_GUI "Build the GUI application (requires Qt6)" ON)
option(MCCOUTLET_BUILD_CLI "Build the CLI application" ON)

# =============================================================================
# liblsl Dependency
# =============================================================================
set(LSL_INSTALL_ROOT "" CACHE PATH "Path to installed liblsl (optional)")
set(LSL_FETCH_REF "v1.17.4" CACHE STRING "liblsl version to fetch from GitHub")

if(LSL_INSTALL_ROOT)
    find_package(LSL REQUIRED
        HINTS "${LSL_INSTALL_ROOT}"
        PATH_SUFFIXES share/LSL lib/cmake/LSL Frameworks/lsl.framework/Resources/CMake
    )
    message(STATUS "Using installed liblsl: ${LSL_DIR}")
    include("${LSL_DIR}/LSLCMake.cmake")
else()
    message(STATUS "Fetching liblsl ${LSL_FETCH_REF} from GitHub...")
    include(FetchContent)
    set(LSL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(liblsl
        GIT_REPOSITORY https://github.com/sccn/liblsl.git
        GIT_TAG ${LSL_FETCH_REF}
        GIT_SHALLOW ON
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(liblsl)
    if(NOT TARGET LSL::lsl)
        add_library(LSL::lsl ALIAS lsl)
    endif()
    include("${liblsl_SOURCE_DIR}/cmake/LSLCMake.cmake")
endif()

# =============================================================================
# uldaq Dependency (Measurement Computing driver library)
# =============================================================================
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Uldaq.cmake)

# =============================================================================
# Qt6 (for GUI build only)
# =============================================================================
if(MCCOUTLET_BUILD_GUI)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    set(CMAKE_AUTOUIC ON)

    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

    if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
        get_target_property(_wrap_gl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
        if(_wrap_gl_libs)
            list(FILTER _wrap_gl_libs EXCLUDE REGEX ".*AGL.*")
            set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES INTERFACE_LINK_LIBRARIES "${_wrap_gl_libs}")
        endif()
    endif()
endif()

# =============================================================================
# Common Dependencies
# =============================================================================
find_package(Threads REQUIRED)

# =============================================================================
# RPATH Configuration
# =============================================================================
LSL_configure_rpath()

# =============================================================================
# Targets
# =============================================================================

add_subdirectory(src/core)

if(MCCOUTLET_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(MCCOUTLET_BUILD_GUI)
    add_subdirectory(src/gui)
endif()

# =============================================================================
# Installation
# =============================================================================
include(GNUInstallDirs)

if(WIN32)
    set(INSTALL_BINDIR ".")
    set(INSTALL_LIBDIR ".")
    set(INSTALL_DATADIR ".")
elseif(APPLE)
    set(INSTALL_BINDIR ".")
    set(INSTALL_LIBDIR ".")
    set(INSTALL_DATADIR ".")
else()
    set(INSTALL_BINDIR "${CMAKE_INSTALL_BINDIR}")
    set(INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
    set(INSTALL_DATADIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")
endif()

if(MCCOUTLET_BUILD_CLI)
    install(TARGETS ${PROJECT_NAME}CLI
        RUNTIME DESTINATION "${INSTALL_BINDIR}"
    )
endif()

if(MCCOUTLET_BUILD_GUI)
    install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION "${INSTALL_BINDIR}"
        BUNDLE DESTINATION "${INSTALL_BINDIR}"
    )
endif()

set(_config_dest "${INSTALL_DATADIR}")
if(APPLE AND MCCOUTLET_BUILD_GUI)
    set(_config_dest "${INSTALL_BINDIR}/${PROJECT_NAME}.app/Contents/MacOS")
endif()
install(FILES ${PROJECT_NAME}.cfg DESTINATION "${_config_dest}")

# =============================================================================
# Bundle liblsl
# =============================================================================
if(APPLE)
    if(MCCOUTLET_BUILD_GUI)
        LSL_install_liblsl(
            FRAMEWORK_DESTINATION "${INSTALL_BINDIR}/${PROJECT_NAME}.app/Contents/Frameworks"
        )
    endif()
    if(MCCOUTLET_BUILD_CLI)
        LSL_install_liblsl(FRAMEWORK_DESTINATION "Frameworks")
    endif()
else()
    LSL_install_liblsl(DESTINATION "${INSTALL_LIBDIR}")
endif()

# =============================================================================
# Qt Deployment
# =============================================================================
if(MCCOUTLET_BUILD_GUI)
    LSL_deploy_qt(TARGET "${PROJECT_NAME}" DESTINATION "${INSTALL_BINDIR}")
endif()

# =============================================================================
# MinGW Runtime Deployment
# =============================================================================
LSL_install_mingw_runtime(DESTINATION "${INSTALL_BINDIR}")

# =============================================================================
# macOS: Code Sign
# =============================================================================
if(APPLE)
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "org.labstreaminglayer.${PROJECT_NAME}")
    set(MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

    if(MCCOUTLET_BUILD_GUI)
        LSL_codesign(
            TARGET "${PROJECT_NAME}"
            DESTINATION "${INSTALL_BINDIR}"
            ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/app.entitlements"
            BUNDLE
        )
    endif()

    if(MCCOUTLET_BUILD_CLI)
        LSL_codesign(
            TARGET "${PROJECT_NAME}CLI"
            DESTINATION "${INSTALL_BINDIR}"
            ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/app.entitlements"
            FRAMEWORK "Frameworks/lsl.framework"
        )
    endif()
endif()

# =============================================================================
# CPack
# =============================================================================
LSL_get_target_arch()
LSL_get_os_name()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Labstreaminglayer")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${LSL_OS}_${LSL_ARCH}")
set(CPACK_STRIP_FILES ON)

if(WIN32)
    set(CPACK_GENERATOR ZIP)
elseif(APPLE)
    set(CPACK_GENERATOR TGZ)
else()
    set(CPACK_GENERATOR DEB TGZ)
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "LabStreamingLayer Developers")
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
    if(MCCOUTLET_BUILD_GUI)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6")
    endif()
endif()

include(CPack)
