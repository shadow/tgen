## tgen: an all-purpose traffic generation tool
project(tgen C)
cmake_minimum_required(VERSION 2.8.12 FATAL_ERROR)

## configure the default build type
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif()

## cmake module path is used for both main and tests
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/" ${CMAKE_MODULE_PATH})

## dependencies
find_package(RT REQUIRED)
find_package(M REQUIRED)
find_package(IGRAPH REQUIRED)
find_package(GLIB REQUIRED)

## recurse our project tree
add_subdirectory(${CMAKE_SOURCE_DIR}/src/)
add_subdirectory(${CMAKE_SOURCE_DIR}/test/)
