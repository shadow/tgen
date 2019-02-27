project(test C)
cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

## dependencies
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/../cmake/" ${CMAKE_MODULE_PATH})
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

include_directories(AFTER ../src/ ${M_INCLUDES} ${GLIB_INCLUDES} ${IGRAPH_INCLUDES})

## build as position-independent so it can run in Shadow
add_definitions(-D_GNU_SOURCE)
add_compile_options(-O2 -ggdb -fno-omit-frame-pointer -fPIC -std=gnu11)

## CFLAGS status update
message(STATUS "CMAKE_C_FLAGS = ${CMAKE_C_FLAGS}")

set(tgen_sources
	test-markovmodel.c
    ../src/tgen-log.c
    ../src/tgen-markovmodel.c
)

## build the tgen executable
add_executable(test-mmodel ${tgen_sources})

## this ensures it is linked as a position-independent executable so that
## the system calls can be intercepted (so that it works in Shadow)
## this is OK because none of tgen's internal symbols need to be interposed!
set_target_properties(test-mmodel PROPERTIES 
        INSTALL_RPATH ${CMAKE_INSTALL_PREFIX}/lib 
        INSTALL_RPATH_USE_LINK_PATH TRUE 
        LINK_FLAGS "-pie -rdynamic -Wl,--no-as-needed")

## link in our dependencies and install
target_link_libraries(test-mmodel ${M_LIBRARIES} ${IGRAPH_LIBRARIES} ${GLIB_LIBRARIES})
