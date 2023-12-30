## tgen: an all-purpose traffic generation tool
project(tgen C)
cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

## configure the default build type
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif()

## cmake module path is used for both main and tests
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})

include(FindPkgConfig)

## dependencies
find_package(RT REQUIRED)
find_package(M REQUIRED)

pkg_check_modules(IGRAPH REQUIRED igraph)
pkg_check_modules(GLIB REQUIRED glib-2.0)

## Parse out igraph version. Needed to work around breaking API changes in igraph.
string(REPLACE "." ";" IGRAPH_VERSION_LIST ${IGRAPH_VERSION})
list(GET IGRAPH_VERSION_LIST 0 IGRAPH_VERSION_MAJOR)
list(GET IGRAPH_VERSION_LIST 1 IGRAPH_VERSION_MINOR)
list(GET IGRAPH_VERSION_LIST 2 IGRAPH_VERSION_PATCH)
add_definitions(-DIGRAPH_VERSION_MAJOR=${IGRAPH_VERSION_MAJOR})
add_definitions(-DIGRAPH_VERSION_MINOR=${IGRAPH_VERSION_MINOR})
add_definitions(-DIGRAPH_VERSION_PATCH=${IGRAPH_VERSION_PATCH})

## recurse our project tree
add_subdirectory(${CMAKE_SOURCE_DIR}/src/)
add_subdirectory(${CMAKE_SOURCE_DIR}/test/)
