#include "CinderGstWebRTC.h"
#include <gst/app/app.h>
#include "cinder/app/App.h"
#include "cinder/Log.h"

using namespace ci;
using namespace ci::app;

#define RTP_CAPS_OPUS "application/x-rtp,media=audio,encoding-name=OPUS,payload="

static gchar* get_string_from_json_object( JsonObject * object )
{
	JsonNode *root;
	JsonGenerator *generator;
	gchar *text;

	/* Make it the root node */
	root = json_node_init_object (json_node_alloc (), object);
	generator = json_generator_new ();
	json_generator_set_root (generator, root);
	text = json_generator_to_data (generator, NULL);

	/* Release everything */
	g_object_unref (generator);
	json_node_free (root);
	return text;
} 

CinderGstWebRTC::CinderGstWebRTC( const PipelineData pipelineData )
: mPipelineData( pipelineData )
{
	mAsyncSurfaceReader = std::make_unique<AsyncSurfaceReader>( mPipelineData.width, mPipelineData.height );
	initializeGStreamer();
	startGMainLoopThread();
	connectToServerAsync();
}

CinderGstWebRTC::~CinderGstWebRTC()
{
	if( mGstData.wsconn ) {
		if( soup_websocket_connection_get_state( mGstData.wsconn ) == SOUP_WEBSOCKET_STATE_OPEN ) {
			soup_websocket_connection_close( mGstData.wsconn, 1000, "" );
		}
		else g_object_unref( mGstData.wsconn );
	}
	if( g_main_loop_is_running( mGMainLoop ) ) {
		g_main_loop_quit( mGMainLoop );
	}
	g_main_loop_unref( mGMainLoop );

	if( mGMainLoopThread.joinable() ){
		mGMainLoopThread.join();
	}
}

void CinderGstWebRTC::registerWithServer( gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	gchar* hello;
	hello = g_strdup_printf("HELLO %i", gstData->pipelineData->localPeerId );
	if( soup_websocket_connection_get_state( gstData->wsconn ) !=
		SOUP_WEBSOCKET_STATE_OPEN ) {
		CI_LOG_E( "Failed to register hello with server since the ws connection seems not to be open!" );
		return;
	}
	CI_LOG_I( "Registering id: "<< std::to_string( gstData->pipelineData->remotePeerId ) << " with server..." );
	gstData->state = GstData::SERVER_REGISTERING;
	soup_websocket_connection_send_text( gstData->wsconn, hello );
}

void CinderGstWebRTC::onServerClosed( SoupWebsocketConnection* wsconn, gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	gstData->state = GstData::SERVER_CLOSED;
}

bool CinderGstWebRTC::setupCall( gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	gchar* msg{ nullptr };
	if( soup_websocket_connection_get_state( gstData->wsconn ) !=
		SOUP_WEBSOCKET_STATE_OPEN ) {
		return false;
	}
	CI_LOG_I( "Setting up signalling server call with peer: " << std::to_string( gstData->pipelineData->remotePeerId ) );
	gstData->state = GstData::PEER_CONNECTING;
	msg = g_strdup_printf( "SESSION %s %s",
		std::to_string( gstData->pipelineData->localPeerId ).c_str(),
	 std::to_string( gstData->pipelineData->remotePeerId ).c_str() );
	soup_websocket_connection_send_text( gstData->wsconn, msg );
	g_free( msg );
	return true;
}

void CinderGstWebRTC::onOfferCreated( GstPromise* promise, gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	GstWebRTCSessionDescription* offer{ nullptr };
	const GstStructure* reply;
	g_assert_cmphex( gstData->state, ==, GstData::PEER_CALL_NEGOTIATING );
	g_assert_cmphex( gst_promise_wait( promise ), ==, GST_PROMISE_RESULT_REPLIED );
	reply = gst_promise_get_reply( promise );
	gst_structure_get( reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr );
	gst_promise_unref( promise );

	promise = gst_promise_new();
	g_signal_emit_by_name( gstData->webrtc, "set-local-description", offer, promise );
	gst_promise_interrupt( promise );
	gst_promise_unref( promise );
	
	sendSdpOffer( offer, gstData );
	gst_webrtc_session_description_free( offer );
	
}

void CinderGstWebRTC::sendSdpOffer( GstWebRTCSessionDescription* offer, gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	gchar* text{ nullptr };
	JsonObject* msg{ nullptr };
	JsonObject* sdp{ nullptr };

	if( gstData->state < GstData::PEER_CALL_NEGOTIATING ) {
		CI_LOG_E ("Can't send offer, not in call");
		return;
	}

	text = gst_sdp_message_as_text( offer->sdp );
	CI_LOG_I("Sending offer: "<< text);

	sdp = json_object_new();
	json_object_set_string_member( sdp, "type", "offer" );
	json_object_set_string_member( sdp, "sdp", text );
	g_free( text );

	msg = json_object_new ();
	json_object_set_int_member( msg, "localPeerId", gstData->pipelineData->localPeerId );
	json_object_set_object_member( msg, "sdp", sdp );
	text = get_string_from_json_object( msg );
	json_object_unref( msg );

	soup_websocket_connection_send_text( gstData->wsconn, text );
	g_free( text );
}

std::string CinderGstWebRTC::getConnectionStateString( const GstData::ConnectionState connectionState )
{
	switch( connectionState ) {
		case GstData::CONNECTION_STATE_UNKNOWN:
		{
			return "CONNECTION_STATE_UNKNOWN";
		}
		case GstData::CONNECTION_STATE_ERROR:
		{
			return "CONNECTION_STATE_ERROR";
		}
		case GstData::SERVER_CONNECTING:
		{
			return "SERVER_CONNECTING";
		}
		case GstData::SERVER_CONNECTION_ERROR:
		{
			return "SERVER_CONNECTION_ERROR";
		}
		case GstData::SERVER_CONNECTED:
		{
			return "SERVER_CONNECTED";
		}
		case GstData::SERVER_REGISTERING:
		{
			return "SERVER_REGISTERING";
		}
		case GstData::SERVER_REGISTRATION_ERROR:
		{
			return "SERVER_REGISTRATION_ERROR";
		}
		case GstData::SERVER_REGISTERED:
		{
			return "SERVER_REGISTERED";
		}
		case GstData::SERVER_CLOSED:
		{
			return "SERVER_CLOSED";
		}
		case GstData::PEER_CONNECTING:
		{
			return "PEER_CONNECTING";
		}
		case GstData::PEER_CONNECTION_ERROR:
		{
			return "PEER_CONNECTION_ERROR";
		}
		case GstData::PEER_CONNECTED:
		{
			return "PEER_CONNECTED";
		}
		case GstData::PEER_CALL_NEGOTIATING:
		{
			return "PEER_CALL_NEGOTIATING";
		}
		case GstData::PEER_CALL_STARTED:
		{
			return "PEER_CALL_STARTED";
		}
		case GstData::PEER_CALL_STOPPING:
		{
			return "PEER_CALL_STOPPING";
		}
		case GstData::PEER_CALL_STOPPED:
		{
			return "PEER_CALL_STOPPED";
		}
		case GstData::PEER_CALL_ERROR:
		{
			return "PEER_CONNECTION_ERROR";
		}
		default:
		{
			return "CONNECTION_STATE_UNKNOWN";
		}
	}
}

void CinderGstWebRTC::onNegotionNeeded( GstElement* element, gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	GstPromise* promise{ nullptr };
	gstData->state = GstData::PEER_CALL_NEGOTIATING;
	promise = gst_promise_new_with_change_func( onOfferCreated, gstData, nullptr );
	g_signal_emit_by_name( gstData->webrtc, "create-offer", nullptr, promise );
}

void CinderGstWebRTC::onIceCandidate( GstElement* webrtc, guint mlineindex, gchar* candidate, gpointer userData ) 
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	gchar* text{ nullptr };
	JsonObject* ice{ nullptr };
	JsonObject* msg{ nullptr };
	if( gstData->state < GstData::PEER_CALL_NEGOTIATING ) {
		CI_LOG_E( "Can't send ICE, not in call.." );
		return;
	}
	ice = json_object_new();
	json_object_set_string_member( ice, "candidate", candidate );
	json_object_set_int_member( ice, "sdpMLineIndex", mlineindex );
	msg = json_object_new();
	json_object_set_int_member( msg, "localPeerId", gstData->pipelineData->localPeerId );
	json_object_set_object_member( msg, "ice", ice );
	text = get_string_from_json_object( msg );
	json_object_unref( msg );

	soup_websocket_connection_send_text( gstData->wsconn, text );
	g_free( text );
}

void CinderGstWebRTC::handleMediaStreams( GstPad* pad, GstElement* pipeline, const char* convertName, const char* sinkName )
{

//XXX Incoming stream to be handled by an optional appsink eventyally
#if 0
 	GstPad *qpad;
	GstElement *q, *conv, *resample, *sink;
	GstPadLinkReturn ret;

	CI_LOG_I ("Trying to handle stream with " << convertName << " ! " <<  sinkName );

	q = gst_element_factory_make( "queue", nullptr );
	g_assert_nonnull( q );
	conv = gst_element_factory_make( convertName, nullptr );
	g_assert_nonnull( conv );
	sink = gst_element_factory_make( sinkName, nullptr );
	g_assert_nonnull( sink );

	if( g_strcmp0( convertName, "audioconvert" ) == 0 ) {
		/* Might also need to resample, so add it just in case.
 	 	 * Will be a no-op if it's not required. */
		resample = gst_element_factory_make( "audioresample", nullptr );
		g_assert_nonnull( resample );
		gst_bin_add_many( GST_BIN( pipeline ), q, conv, resample, sink, nullptr );
		gst_element_sync_state_with_parent( q );
		gst_element_sync_state_with_parent( conv );
		gst_element_sync_state_with_parent( resample );
		gst_element_sync_state_with_parent( sink );
		gst_element_link_many( q, conv, resample, sink, nullptr );
	} else {
		gst_bin_add_many( GST_BIN ( pipeline ), q, conv, sink, nullptr );
		gst_element_sync_state_with_parent( q );
		gst_element_sync_state_with_parent( conv );
		gst_element_sync_state_with_parent( sink );
		gst_element_link_many( q, conv, sink, nullptr );
	}

	qpad = gst_element_get_static_pad( q, "sink" );

	ret = gst_pad_link( pad, qpad );
	g_assert_cmphex( ret, ==, GST_PAD_LINK_OK );
#endif
}

void CinderGstWebRTC::onIncomingDecodebinStream( GstElement* decodebin, GstPad* pad, GstElement* pipeline )
{
	GstCaps* caps{ nullptr };
	const gchar* name{ nullptr };
	if( ! gst_pad_has_current_caps( pad ) ) {
		CI_LOG_E( "Pad " << GST_PAD_NAME( pad ) << " has no caps, can't do anything, ignoring...." );
		return;
	}
	caps = gst_pad_get_current_caps( pad );
	name = gst_structure_get_name( gst_caps_get_structure( caps, 0 ) );
	if( g_str_has_prefix( name, "video" ) ) {
		handleMediaStreams( pad, pipeline, "videoconvert", "autovideosink" );
	}
	else if( g_str_has_prefix( name, "audio" ) ) {
		handleMediaStreams( pad, pipeline, "audioconvert", "autoaudiosink" );
	}
	else {
		CI_LOG_E( "Unknown pad " << GST_PAD_NAME( pad ) );
	}
	
}

void CinderGstWebRTC::onIncomingStream( GstElement* webrtc, GstPad* pad, GstElement* pipeline )
{
	GstElement* decodebin{ nullptr };
	GstPad* sinkpad{ nullptr };
	
	if( GST_PAD_DIRECTION( pad ) != GST_PAD_SRC ) {
		return;
	}
	decodebin = gst_element_factory_make( "decodebin", nullptr );
	g_signal_connect( decodebin, "pad-added",
		G_CALLBACK( onIncomingDecodebinStream ), pipeline );
	gst_bin_add( GST_BIN( pipeline ), decodebin );
	gst_element_sync_state_with_parent( decodebin );
	
	sinkpad = gst_element_get_static_pad( decodebin, "sink" );
	gst_pad_link( pad, sinkpad );
	gst_object_unref( sinkpad );
}

bool CinderGstWebRTC::startPipeline( gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	bool success{ true };
	GstStateChangeReturn ret;
	GError* error{ nullptr };
	auto videoPipe = "webrtcbin bundle-policy=2 name=sendrecv " +gstData->pipelineData->stunServer+
      "appsrc name=cinder_appsrc !" +gstData->pipelineData->videoPipelineDescr+ "! sendrecv. ";

	gstData->pipeline = gst_parse_launch( videoPipe.c_str()
	  /*"audiotestsrc is-live=true wave=red-noise ! audioconvert ! audioresample ! queue ! opusenc ! rtpopuspay ! "
      "queue ! " RTP_CAPS_OPUS "97 ! sendrecv. "*/,
      &error );
	gstData->webrtc = gst_bin_get_by_name( GST_BIN( gstData->pipeline ), "sendrecv" );
	gstData->appsrc = gst_bin_get_by_name( GST_BIN( gstData->pipeline ), "cinder_appsrc" );
	g_object_set( G_OBJECT( gstData->appsrc ), "caps",
		gst_caps_new_simple( "video/x-raw",
					"format", G_TYPE_STRING, "RGBA", 
					"width", G_TYPE_INT, gstData->pipelineData->width,
					"height", G_TYPE_INT, gstData->pipelineData->height,
					"framerate", GST_TYPE_FRACTION, 0, 1,
					nullptr ), 
	"is-live", true,
	"max-bytes", 0,
	"format", GST_FORMAT_TIME,
	"stream-type", 0, nullptr );
	if( ! gstData->webrtc ) {
		CI_LOG_I( "WebRTC bin seems to be null. Something is off..." );
		success = false;
	}
	g_signal_connect( gstData->webrtc, "on-negotiation-needed", G_CALLBACK( onNegotionNeeded ), gstData );
	g_signal_connect( gstData->webrtc, "on-ice-candidate", G_CALLBACK( onIceCandidate ), gstData );

	gst_element_set_state( gstData->pipeline, GST_STATE_READY );
	g_signal_connect( gstData->webrtc, "pad-added", G_CALLBACK( onIncomingStream ), gstData->pipeline );
	gst_object_unref( gstData->webrtc );
	CI_LOG_I( "Starting pipeline..........." );
	ret = gst_element_set_state( GST_ELEMENT( gstData->pipeline ), GST_STATE_PLAYING );
	if( ret == GST_STATE_CHANGE_FAILURE ) {
		CI_LOG_E( "Failed to set the pipeline to PLAYING state!" );
		success = false;
	}
	if( ! success ) {
		g_clear_object( &gstData->pipeline );
		gstData->webrtc = nullptr;
	}
	return success;
}

void CinderGstWebRTC::onServerMsg( SoupWebsocketConnection* wsConn, SoupWebsocketDataType type, GBytes* message, gpointer userData )
{
	GstData* gstData = reinterpret_cast<GstData*>( userData );
	gchar* text{ nullptr };
	switch( type ) {
		case SOUP_WEBSOCKET_DATA_BINARY: {
			CI_LOG_E( "Received unknown binary message, ignoring.." );	
			return;
		}
		case SOUP_WEBSOCKET_DATA_TEXT: {
			gsize size;
			const gchar* data = reinterpret_cast<const gchar*>( g_bytes_get_data( message, &size ) );
			text = g_strndup( data, size );
			break;
		}
	}
	if( g_strcmp0( text, "HELLO" ) == 0 ) {
		if( gstData->state != GstData::SERVER_REGISTERING ) {
			CI_LOG_E( "Received HELLO while not registering!" );
			gstData->state = GstData::CONNECTION_STATE_ERROR;
		}
		else {
			gstData->state = GstData::SERVER_REGISTERED;
			CI_LOG_I( "Registered successfully with the server." );
			if( ! setupCall( gstData ) ) {
				CI_LOG_E( "Failed to setup call with remote peer!" );
				gstData->state = GstData::PEER_CALL_ERROR;
			}
		}
	}
	else if( g_str_has_prefix( text, "SESSION_CREATED" )  ) {
		if( gstData->state != GstData::PEER_CONNECTING ) {
			CI_LOG_E( "Received SESSION_OK when not calling." );
			gstData->state = GstData::PEER_CONNECTION_ERROR;
		}
		else {
			gstData->state = GstData::PEER_CONNECTED;
			if( ! startPipeline( gstData ) ) {
				CI_LOG_E( "Failed to start pipeline!" );
				gstData->state = GstData::PEER_CALL_ERROR;
			}
		}
	}
	else if( g_str_has_prefix( text, "ERROR" ) ) {
		switch( gstData->state ) {
			case GstData::SERVER_CONNECTING:
			{
				gstData->state = GstData::SERVER_CONNECTION_ERROR;
				break;
			}
			case GstData::SERVER_REGISTERING:
			{
				gstData->state = GstData::SERVER_REGISTRATION_ERROR;
				break;
			}
			case GstData::PEER_CONNECTING:
			{
				gstData->state = GstData::PEER_CONNECTION_ERROR;
			}
			case GstData::PEER_CONNECTED:
			case GstData::PEER_CALL_NEGOTIATING:
			{
				gstData->state = GstData::PEER_CALL_ERROR;
				break;
				
			}
			default:
				gstData->state = GstData::CONNECTION_STATE_ERROR;
		}

		CI_LOG_E( "Got connection error: " << getConnectionStateString( gstData->state ) );
	}
	else {
		JsonNode* root{ nullptr };
		JsonObject* object{ nullptr };
		JsonObject* child{ nullptr };
		JsonParser* parser = json_parser_new();
		if( ! json_parser_load_from_data( parser, text, -1, nullptr ) ) {
			CI_LOG_E( "Unknown message: " << text << " Ignoring..." );
			g_object_unref( parser );
		}
		root = json_parser_get_root( parser );
		if( ! JSON_NODE_HOLDS_OBJECT( root ) ) {
			CI_LOG_E( "Unknown message: " << text << " Ignoring..." );
			g_object_unref( parser );
		}

		object =json_node_get_object( root );
		if( json_object_has_member( object, "sdp" ) ) {
			int ret;
			GstSDPMessage* sdp{ nullptr };
			const gchar* text{ nullptr };
			const gchar* sdpType{ nullptr };
			GstWebRTCSessionDescription* answer{ nullptr };
			g_assert_cmphex( gstData->state, ==, GstData::PEER_CALL_NEGOTIATING );
			child = json_object_get_object_member( object, "sdp" );
			if( ! json_object_has_member( child, "type" ) ) {
				CI_LOG_E( "Received SDP without type!" );
			}
			else {
				sdpType = json_object_get_string_member( child, "type" );
				//g_assert_cmphex( sdpType, ==, "answer" );
				text = json_object_get_string_member( child, "sdp" );
				CI_LOG_I( "Received answer: " << text );

				ret = gst_sdp_message_new( &sdp );
				g_assert_cmphex( ret, ==, GST_SDP_OK );
				ret = gst_sdp_message_parse_buffer( (guint8 *)text, strlen( text ), sdp );
				g_assert_cmphex( ret, ==, GST_SDP_OK );
				answer = gst_webrtc_session_description_new( GST_WEBRTC_SDP_TYPE_ANSWER, sdp );
				//g_assert_nonull( answer );
				{
					GstPromise* promise = gst_promise_new();
					g_signal_emit_by_name( gstData->webrtc, "set-remote-description", answer, promise );
					gst_promise_interrupt( promise );
					gst_promise_unref( promise );
				}
				gstData->state = GstData::PEER_CALL_STARTED;
			}
			
		} 
		else if( json_object_has_member( object, "ice" ) ) {
			const char* candidate{ nullptr };
			gint sdpmlineindex;
			child = json_object_get_object_member( object, "ice" );
			candidate = json_object_get_string_member( child, "candidate" );
			sdpmlineindex = json_object_get_int_member( child, "sdpMLineIndex" );

			g_signal_emit_by_name( gstData->webrtc, "add-ice-candidate", sdpmlineindex, candidate );
		}
		else {
			CI_LOG_W( "Ignoring unknown JSON message: " << text );
		}
	}
	g_free( text );
}

void CinderGstWebRTC::onServerConnected( SoupSession* session, GAsyncResult* result, gpointer userData )
{
	GError* error{ nullptr };
	GstData* gstData = static_cast<GstData*>( userData );
	gstData->wsconn = soup_session_websocket_connect_finish( session, result, &error );
	if( error ) {
		CI_LOG_E( "Failed to connect to websocket: " << error->message );
		g_error_free( error );
		return;
	}	
	if( ! gstData->wsconn ) {
		CI_LOG_E( "Websocket didn't throw an error but it is null. Something is off....." );
		return;
	}
	gstData->state = GstData::SERVER_CONNECTED;
	CI_LOG_I( "Connected to signalling server..." );
	g_signal_connect( gstData->wsconn, "closed", G_CALLBACK( onServerClosed ), gstData );
	g_signal_connect( gstData->wsconn, "message", G_CALLBACK( onServerMsg ), gstData );
	registerWithServer( gstData );
}

void CinderGstWebRTC::connectToServerAsync()
{
	SoupLogger* logger{ nullptr };
	SoupMessage* message{ nullptr };
	SoupSession* session{ nullptr };
	const char* httpsAliases[] = { "wss", nullptr };
	//> Create the ws session	
	session = soup_session_new_with_options( SOUP_SESSION_SSL_STRICT, false/*true for using certificate*/, SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, true,
	SOUP_SESSION_HTTPS_ALIASES, httpsAliases, nullptr );
	//> Create the session logger
	logger = soup_logger_new( SOUP_LOGGER_LOG_BODY, -1 );
	soup_session_add_feature( session, SOUP_SESSION_FEATURE( logger ) );
	g_object_unref( logger );
	//> Construct the initial server message
	message = soup_message_new( SOUP_METHOD_GET, mPipelineData.serverURL.c_str() );
	CI_LOG_I( "Connecting to server...." );
	soup_session_websocket_connect_async( session, message, nullptr, nullptr, nullptr, (GAsyncReadyCallback) onServerConnected, &mGstData );
	mGstData.state = GstData::SERVER_CONNECTING;
	mGstData.pipelineData = &mPipelineData;
}

void CinderGstWebRTC::startGMainLoopThread()
{
	mGMainLoop = g_main_loop_new( nullptr, false );
	mGMainLoopThread = std::thread( &CinderGstWebRTC::startGMainLoop, this, mGMainLoop );
}

void CinderGstWebRTC::startGMainLoop( GMainLoop* loop )
{
	g_main_loop_run( loop );
}

bool CinderGstWebRTC::initializeGStreamer()
{
	if( ! gst_is_initialized() ) {
		guint major;
		guint minor;
		guint micro;
		guint nano;

		gst_version( &major, &minor, &micro, &nano );

		GError* err;
		/// If we havent already initialized GStreamer do this.
		if( ! gst_init_check( nullptr, nullptr, &err ) ) {
			if( err->message ) {
				CI_LOG_E( "FAILED to initialize GStreamer : " << err->message );
			}
			else {
				CI_LOG_E( "FAILED to initialize GStreamer due to unknown error." );
			}
			return false;
		}
		else {
			CI_LOG_I( "Initialized GStreamer version : " << major << "." << minor << "." << micro << "." << nano );
			return true;
		}
	}
	return true;
}

void CinderGstWebRTC::startCapture()
{
	if( ! mAsyncSurfaceReader )
		return;
	mAsyncSurfaceReader->bind();
}

void CinderGstWebRTC::endCapture()
{
	if( ! mAsyncSurfaceReader )
		return;
	mAsyncSurfaceReader->unbind();
}

void CinderGstWebRTC::streamCapture()
{
	if( ! mAsyncSurfaceReader || ! mGstData.appsrc )
		return;
	static GstClockTime timestamp = 0;
	// Read pixels
	mCaptureSurface = mAsyncSurfaceReader->readPixels();
	// Create gst buffer and fill it with our pixels
	GstBuffer* buffer = gst_buffer_new_and_alloc( mAsyncSurfaceReader->getWidth() * mAsyncSurfaceReader->getHeight() * mCaptureSurface->getPixelBytes() );
	const auto bytesCopied = gst_buffer_fill( buffer, 0, mCaptureSurface->getData(), mAsyncSurfaceReader->getWidth() * mAsyncSurfaceReader->getHeight() * mCaptureSurface->getPixelBytes() );
	GST_BUFFER_PTS( buffer ) = timestamp;
	GST_BUFFER_DURATION( buffer ) = gst_util_uint64_scale_int( 1, GST_SECOND, 60 );
	// Forwared the pixels along the appsrc pipeline
	const auto result = gst_app_src_push_buffer( GST_APP_SRC_CAST( mGstData.appsrc ), buffer );
	timestamp += GST_BUFFER_DURATION( buffer );
	//CI_LOG_V( "Push result " << result << " for copied bytes " << bytesCopied );
}

