# Core library - Qt-independent, shared between CLI and GUI
add_library(mccoutlet_core STATIC
    src/Device.cpp
    src/LSLOutlet.cpp
    src/Config.cpp
    src/StreamThread.cpp
)

# Power management: keep the host awake during acquisition. macOS needs the
# Objective-C++ implementation (NSProcessInfo activity, built with ARC) linked
# against Foundation; other platforms use the no-op stub.
if(APPLE)
    target_sources(mccoutlet_core PRIVATE src/PowerAssertion.mm)
    set_source_files_properties(src/PowerAssertion.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(mccoutlet_core PRIVATE "-framework Foundation")
else()
    target_sources(mccoutlet_core PRIVATE src/PowerAssertion.cpp)
endif()

target_include_directories(mccoutlet_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(mccoutlet_core
    PUBLIC
        LSL::lsl
        Threads::Threads
        uldaq::uldaq
)

add_library(MCCOutlet::core ALIAS mccoutlet_core)
