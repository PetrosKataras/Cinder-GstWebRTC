cmake_minimum_required( VERSION 3.0 FATAL_ERROR )

project( BasicStreamApp )

get_filename_component( CINDER_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.." ABSOLUTE )
get_filename_component( APP_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../" ABSOLUTE )

include( "${CINDER_PATH}/proj/cmake/modules/cinderMakeApp.cmake" )

ci_make_app(
	SOURCES     ${APP_PATH}/src/BasicStreamApp.cpp
	INCLUDES	${APP_PATH}/src
	CINDER_PATH ${CINDER_PATH}
	BLOCKS		Cinder-GstWebRTC
)

if( GRAB_CAMERA )
	target_compile_definitions( BasicStreamApp PUBLIC "-DGRAB_CAMERA" )
endif()
