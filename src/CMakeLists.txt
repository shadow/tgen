## build the tgen binary
include_directories(AFTER src/ ${RT_INCLUDES} ${M_INCLUDES} ${GLIB_INCLUDES} ${IGRAPH_INCLUDES})

## build as position-independent so it can run in Shadow
add_compile_options(-std=gnu11 -fPIC -fno-omit-frame-pointer -ggdb)
add_definitions(-D_GNU_SOURCE)

## build type specific options
if(CMAKE_BUILD_TYPE MATCHES "Release")
	add_compile_options(-O3)
	add_definitions(-DNDEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES "Shadow")
	## Use -O2 until shadow/shadow#671 is fixed
	## https://github.com/shadow/shadow/issues/671
	add_compile_options(-O2)
	add_definitions(-DNDEBUG)
else()
	add_compile_options(-O0)
	add_definitions(-DDEBUG)
endif()

set(tgen_sources
    tgen-config.c
    tgen-driver.c
    tgen-generator.c
    tgen-graph.c
    tgen-io.c
    tgen-log.c
    tgen-main.c
    tgen-markovmodel.c
    tgen-optionparser.c
    tgen-peer.c
    tgen-pool.c
    tgen-server.c
    tgen-stream.c
    tgen-timer.c
    tgen-transport.c
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
