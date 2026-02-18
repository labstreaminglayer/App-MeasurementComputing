# Core library - Qt-independent, shared between CLI and GUI
add_library(mccoutlet_core STATIC
    src/Device.cpp
    src/LSLOutlet.cpp
    src/Config.cpp
    src/StreamThread.cpp
)

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
