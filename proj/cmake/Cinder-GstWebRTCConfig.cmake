if( NOT TARGET Cinder-GstWebRTC )
	get_filename_component( CINDER_GST_WEBRTC_SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../src" ABSOLUTE )
	if( NOT CINDER_PATH )
		get_filename_component( CINDER_PATH "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE )
	endif()

	set( CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_LIST_DIR}/modules )
	
	find_package( GStreamer REQUIRED )
	find_package( LibSoup REQUIRED )
	find_package( JsonGlib REQUIRED )

	add_library( Cinder-GstWebRTC ${CINDER_GST_WEBRTC_SOURCE_PATH}/CinderGstWebRTC.cpp 
		${CINDER_GST_WEBRTC_SOURCE_PATH}/AsyncSurfaceReader.cpp
	)

	target_include_directories( Cinder-GstWebRTC PUBLIC ${CINDER_GST_WEBRTC_SOURCE_PATH}
		${GSTREAMER_INCLUDE_DIRS}
		${GSTREAMER_BASE_INCLUDE_DIRS}
		${GSTREAMER_APP_INCLUDE_DIRS}
		${GSTREAMER_VIDEO_INCLUDE_DIRS}
		${GSTREAMER_GL_INCLUDE_DIRS}
		${GSTREAMER_WEBRTC_INCLUDE_DIRS}
		${GSTREAMER_SDP_INCLUDE_DIRS}
		${LIBSOUP_INCLUDE_DIRS}
		${JsonGlib_PROCESS_INCLUDES}
	)
	target_include_directories( Cinder-GstWebRTC SYSTEM BEFORE PUBLIC ${CINDER_PATH}/include )

	if( NOT TARGET cinder )
		    include( "${CINDER_PATH}/proj/cmake/configure.cmake" )
		    find_package( cinder REQUIRED PATHS
		        "${CINDER_PATH}/${CINDER_LIB_DIRECTORY}"
		        "$ENV{CINDER_PATH}/${CINDER_LIB_DIRECTORY}" )
	endif()
	target_link_libraries( Cinder-GstWebRTC PRIVATE cinder ${GSTREAMER_WEBRTC_LIBRARIES} ${GSTREAMER_SDP_LIBRARIES} ${LIBSOUP_LIBRARIES} ${JsonGlib_PROCESS_LIBS} )
	
endif()
