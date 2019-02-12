## tgen: an all-purpose traffic generation plug-in for Shadow
project(tgen C)
cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

## dependencies
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
find_package(RT REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

include_directories(AFTER ../ ${RT_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${IGRAPH_INCLUDES})

## build as position-independent so it can run in Shadow
add_definitions(-D_GNU_SOURCE)
add_compile_options(-O2 -ggdb -fno-omit-frame-pointer -fPIC -std=gnu11)

## CFLAGS status update
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

set(tgen_sources
    src/shd-tgen-action.c
    src/shd-tgen-config.c
    src/shd-tgen-driver.c
    src/shd-tgen-generator.c
    src/shd-tgen-graph.c
    src/shd-tgen-io.c
    src/shd-tgen-main.c
    src/shd-tgen-markovmodel.c
    src/shd-tgen-peer.c
    src/shd-tgen-pool.c
    src/shd-tgen-server.c
    src/shd-tgen-timer.c
    src/shd-tgen-transfer.c
    src/shd-tgen-transport.c
)

## build the tgen executable
add_executable(tgen ${tgen_sources})

## this ensures it is linked as a position-independent executable so that
## the system calls can be intercepted (so that it works in Shadow)
## this is OK because none of tgen's internal symbols need to be interposed!
set_target_properties(tgen PROPERTIES 
        INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
        INSTALL_RPATH_USE_LINK_PATH TRUE 
        LINK_FLAGS "-pie -rdynamic -Wl,--no-as-needed")

## link in our dependencies and install
target_link_libraries(tgen ${RT_LIBRARIES} ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
install(TARGETS tgen DESTINATION bin)

