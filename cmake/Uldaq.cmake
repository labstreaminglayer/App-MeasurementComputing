# =============================================================================
# Uldaq.cmake - Measurement Computing uldaq library via ExternalProject
# =============================================================================
# Builds uldaq from its Autotools source tree and creates an imported
# uldaq::uldaq static library target.
#
# Prerequisites (host):
#   - autoconf, automake, libtool (for autoreconf)
#   - libusb-1.0
#   - macOS: IOKit + CoreFoundation frameworks
#
# Provides:
#   uldaq::uldaq   - imported static library target
# =============================================================================

include(ExternalProject)
include(ProcessorCount)
ProcessorCount(NPROC)
if(NOT NPROC OR NPROC EQUAL 0)
    set(NPROC 1)
endif()

set(ULDAQ_INSTALL_DIR "${CMAKE_BINARY_DIR}/uldaq-install")
set(ULDAQ_GIT_TAG "v1.2.1" CACHE STRING "uldaq version to fetch from GitHub")

# Platform-specific configure environment (e.g., Homebrew paths on macOS)
set(_uldaq_env "")
if(APPLE)
    execute_process(
        COMMAND brew --prefix
        OUTPUT_VARIABLE HOMEBREW_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(HOMEBREW_PREFIX)
        list(APPEND _uldaq_env
            "LDFLAGS=-L${HOMEBREW_PREFIX}/lib"
            "CPPFLAGS=-I${HOMEBREW_PREFIX}/include"
        )
    endif()
endif()

ExternalProject_Add(uldaq_external
    GIT_REPOSITORY https://github.com/mccdaq/uldaq.git
    GIT_TAG ${ULDAQ_GIT_TAG}
    GIT_SHALLOW ON

    CONFIGURE_COMMAND sh -c "cd <SOURCE_DIR> && autoreconf --install --force"
        COMMAND ${CMAKE_COMMAND} -E env ${_uldaq_env}
            <SOURCE_DIR>/configure
            --prefix=${ULDAQ_INSTALL_DIR}
            --disable-examples
            --enable-static
            --disable-shared

    BUILD_COMMAND make -j${NPROC}
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE OFF
    BUILD_BYPRODUCTS "${ULDAQ_INSTALL_DIR}/lib/libuldaq.a"
)

# Create imported target for uldaq
file(MAKE_DIRECTORY "${ULDAQ_INSTALL_DIR}/include")

add_library(uldaq::uldaq STATIC IMPORTED GLOBAL)
set_target_properties(uldaq::uldaq PROPERTIES
    IMPORTED_LOCATION "${ULDAQ_INSTALL_DIR}/lib/libuldaq.a"
    INTERFACE_INCLUDE_DIRECTORIES "${ULDAQ_INSTALL_DIR}/include"
)
add_dependencies(uldaq::uldaq uldaq_external)

# uldaq's transitive dependencies
find_library(LIBUSB_LIBRARY NAMES usb-1.0
    HINTS ${HOMEBREW_PREFIX}/lib
)
if(NOT LIBUSB_LIBRARY)
    message(FATAL_ERROR "libusb-1.0 not found. Install it (e.g., brew install libusb)")
endif()

set(_uldaq_link_libs ${LIBUSB_LIBRARY})
if(APPLE)
    list(APPEND _uldaq_link_libs "-framework IOKit" "-framework CoreFoundation")
endif()
set_property(TARGET uldaq::uldaq APPEND PROPERTY
    INTERFACE_LINK_LIBRARIES ${_uldaq_link_libs}
)
