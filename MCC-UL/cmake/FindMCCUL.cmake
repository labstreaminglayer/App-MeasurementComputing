# - try to find the MeasurementComputing Universal Library
#
#
# Non-cache variables you might use in your CMakeLists.txt:
#  MCCUL_FOUND
#  MCCUL_INCLUDE_DIRS
#  MCCUL_LIBRARIES
#  MCCUL_BINARIES
#
# Requires these CMake modules:
#  FindPackageHandleStandardArgs (known included with CMake >=2.6.2)
#
# Authored by:
# 2018 Chadwick Boulay <chadwick.boulay@gmail.com>

IF (MCCUL_INCLUDE_DIR AND MCCUL_LIBRARIES)

    # in cache already
    set(MCCUL_FOUND TRUE)
	
ELSE (MCCUL_INCLUDE_DIR AND MCCUL_LIBRARIES)
	IF(NOT MCCUL_ROOT)
		set(MCCUL_ROOT "C:/Users/Public/Documents/Measurement Computing/DAQ/C")
	ENDIF()
	
	find_path(MCCUL_INCLUDE_DIRS
        NAMES
            cbw.h
        PATHS 
            ${MCCUL_ROOT}
    )
	
	IF (CMAKE_SIZEOF_VOID_P EQUAL 8)
		FIND_LIBRARY(MCCUL_LIBRARIES
            NAMES cbw64
            PATHS ${MCCUL_ROOT})
	ELSE()
		FIND_LIBRARY(MCCUL_LIBRARIES
            NAMES cbw32
            PATHS ${MCCUL_ROOT})
	ENDIF()

	SET(MCCUL_FOUND TRUE)

	include(FindPackageHandleStandardArgs)
	find_package_handle_standard_args(MCCUL
		DEFAULT_MSG
		MCCUL_FOUND
		MCCUL_INCLUDE_DIRS
		MCCUL_LIBRARIES)

	mark_as_advanced(
		MCCUL_FOUND
		MCCUL_INCLUDE_DIRS
		MCCUL_LIBRARIES)
ENDIF (MCCUL_INCLUDE_DIR AND MCCUL_LIBRARIES)