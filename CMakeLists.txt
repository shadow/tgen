## tgen: an all-purpose traffic generation plug-in for Shadow
project(tgen C)
cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

## configure the default build type
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif()

## dependencies
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})
find_package(RT REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

include_directories(AFTER src/ ${RT_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${IGRAPH_INCLUDES})

## build as position-independent so it can run in Shadow
add_compile_options(-std=gnu11 -fPIC -fno-omit-frame-pointer -ggdb)
add_definitions(-D_GNU_SOURCE)

## build type specific options
if(CMAKE_BUILD_TYPE MATCHES "Release")
	add_compile_options(-O3)
	add_definitions(-DNDEBUG)
else()
	add_compile_options(-O0)
	add_definitions(-DDEBUG)
endif()

set(tgen_sources
    src/tgen-config.c
    src/tgen-driver.c
    src/tgen-flow.c
    src/tgen-graph.c
    src/tgen-io.c
    src/tgen-log.c
    src/tgen-main.c
    src/tgen-markovmodel.c
    src/tgen-optionparser.c
    src/tgen-peer.c
    src/tgen-pool.c
    src/tgen-server.c
    src/tgen-timer.c
    src/tgen-transfer.c
    src/tgen-transport.c
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

## print a status update
message(STATUS "CMAKE_BUILD_TYPE = ${CMAKE_BUILD_TYPE}")
get_target_property(FLAGS tgen COMPILE_OPTIONS)
message(STATUS "COMPILE_OPTIONS = ${FLAGS}")
