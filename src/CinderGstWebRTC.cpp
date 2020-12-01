#include "CinderGstWebRTC.h"
#include <gst/app/app.h>
#include <gio/gio.h>

#include "cinder/Utilities.h"
#define CI_MIN_LOG_LEVEL 2
#include "cinder/Log.h"
#include "nlohmann/json.hpp"
#include <regex>

using namespace ci;
using namespace ci::app;
using json = nlohmann::json;

CinderGstWebRTC::CinderGstWebRTC( const PipelineData pipelineData, const app::WindowRef& window )
: mPipelineData( pipelineData )
, mWindow( window )
{
	mAsyncSurfaceReader = std::make_unique<AsyncSurfaceReader>( mPipelineData.width, mPipelineData.height );
	setPipelineEncoderName( mPipelineData.videoPipelineDescr );
	initializeGStreamer();
	startGMainLoopThread();
	connectToServerAsync();
}

CinderGstWebRTC::~CinderGstWebRTC()
{
	if( mWSConn ) {
		if( soup_websocket_connection_get_state( mWSConn ) == SOUP_WEBSOCKET_STATE_OPEN ) {
			soup_websocket_connection_close( mWSConn, 1000, "" );
		}
		else g_object_unref( mWSConn );
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
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	gchar* hello;
	hello = g_strdup_printf("HELLO %u", parent->mPipelineData.localPeerId );
	if( soup_websocket_connection_get_state( parent->mWSConn ) !=
		SOUP_WEBSOCKET_STATE_OPEN ) {
		CI_LOG_E( "Failed to register with server - ws connection seems to be closed!" );
		return;
	}
	CI_LOG_I( "Registering id: "<< parent->mPipelineData.remotePeerId << " with server..." );
	parent->state = SERVER_REGISTERING;
	soup_websocket_connection_send_text( parent->mWSConn, hello );
	g_free( hello );
}

void CinderGstWebRTC::onServerClosed( SoupWebsocketConnection* wsconn, gpointer userData )
{
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	parent->state = SERVER_CLOSED;
}

bool CinderGstWebRTC::setupCall( gpointer userData )
{
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	gchar* msg{ nullptr };
	if( soup_websocket_connection_get_state( parent->mWSConn ) !=
		SOUP_WEBSOCKET_STATE_OPEN ) {
		return false;
	}
	CI_LOG_I( "Setting up signalling server call with peer: " << parent->mPipelineData.remotePeerId );
	parent->state = PEER_CONNECTING;
	msg = g_strdup_printf( "SESSION %s %s",
		std::to_string( parent->mPipelineData.localPeerId ).c_str(),
 	 	 parent->mPipelineData.remotePeerId.c_str() );
	soup_websocket_connection_send_text( parent->mWSConn, msg );
	g_free( msg );
	return true;
}

void CinderGstWebRTC::onOfferCreated( GstPromise* promise, gpointer userData )
{
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	GstWebRTCSessionDescription* offer{ nullptr };
	const GstStructure* reply;
	g_assert_cmphex( parent->state, ==, PEER_CALL_NEGOTIATING );
	g_assert_cmphex( gst_promise_wait( promise ), ==, GST_PROMISE_RESULT_REPLIED );
	reply = gst_promise_get_reply( promise );
	gst_structure_get( reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &offer, nullptr );
	gst_promise_unref( promise );

	promise = gst_promise_new();
	g_signal_emit_by_name( parent->mWebRTC, "set-local-description", offer, promise );
	gst_promise_interrupt( promise );
	gst_promise_unref( promise );
	
	sendSdpOffer( offer, parent );
	gst_webrtc_session_description_free( offer );
	
}

void CinderGstWebRTC::sendSdpOffer( GstWebRTCSessionDescription* offer, gpointer userData )
{
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	gchar* text{ nullptr };
	if( parent->state < PEER_CALL_NEGOTIATING ) {
		CI_LOG_E ("Can't send offer, not in call!");
		return;
	}

	text = gst_sdp_message_as_text( offer->sdp );
	CI_LOG_I("Sending offer: "<< text);

	json msg;
	msg["sdp"]["type"] = "offer";
	msg["sdp"]["sdp"] = text;
	msg["localPeerId"] = parent->mPipelineData.localPeerId;

	auto msgStr = msg.dump();
	soup_websocket_connection_send_text( parent->mWSConn, msgStr.c_str() );

	g_free( text );
}

std::string CinderGstWebRTC::getConnectionStateString( const ConnectionState connectionState )
{
	switch( connectionState ) {
		case CONNECTION_STATE_UNKNOWN:
		{
			return "CONNECTION_STATE_UNKNOWN";
		}
		case CONNECTION_STATE_ERROR:
		{
			return "CONNECTION_STATE_ERROR";
		}
		case SERVER_CONNECTING:
		{
			return "SERVER_CONNECTING";
		}
		case SERVER_CONNECTION_ERROR:
		{
			return "SERVER_CONNECTION_ERROR";
		}
		case SERVER_CONNECTED:
		{
			return "SERVER_CONNECTED";
		}
		case SERVER_REGISTERING:
		{
			return "SERVER_REGISTERING";
		}
		case SERVER_REGISTRATION_ERROR:
		{
			return "SERVER_REGISTRATION_ERROR";
		}
		case SERVER_REGISTERED:
		{
			return "SERVER_REGISTERED";
		}
		case SERVER_CLOSED:
		{
			return "SERVER_CLOSED";
		}
		case PEER_CONNECTING:
		{
			return "PEER_CONNECTING";
		}
		case PEER_CONNECTION_ERROR:
		{
			return "PEER_CONNECTION_ERROR";
		}
		case PEER_CONNECTED:
		{
			return "PEER_CONNECTED";
		}
		case PEER_CALL_NEGOTIATING:
		{
			return "PEER_CALL_NEGOTIATING";
		}
		case PEER_CALL_STARTED:
		{
			return "PEER_CALL_STARTED";
		}
		case PEER_CALL_STOPPING:
		{
			return "PEER_CALL_STOPPING";
		}
		case PEER_CALL_STOPPED:
		{
			return "PEER_CALL_STOPPED";
		}
		case PEER_CALL_ERROR:
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
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	GstPromise* promise{ nullptr };
	parent->state = PEER_CALL_NEGOTIATING;
	promise = gst_promise_new_with_change_func( onOfferCreated, parent, nullptr );
	g_signal_emit_by_name( parent->mWebRTC, "create-offer", nullptr, promise );
}

void CinderGstWebRTC::onIceCandidate( GstElement* webrtc, guint mlineindex, gchar* candidate, gpointer userData ) 
{
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	if( parent->state < PEER_CALL_NEGOTIATING ) {
		CI_LOG_E( "Can't send ICE, not in call!" );
		return;
	}
	json msg;
	msg["ice"]["candidate"] = candidate;
	msg["ice"]["sdpMLineIndex"] = mlineindex;
	msg["localPeerId"] = parent->mPipelineData.localPeerId;
	auto msgStr = msg.dump();
	soup_websocket_connection_send_text( parent->mWSConn, msgStr.c_str() );
}

void CinderGstWebRTC::onIceConnectionState( GstElement* webrtc, GParamSpec* pspec, gpointer userData )
{
	GstWebRTCICEConnectionState iceConnectionState;
	g_object_get( webrtc, "ice-connection-state", &iceConnectionState, nullptr );
	CI_LOG_I( "ICE CONNECTION STATE: " << iceConnectionState );
}

void CinderGstWebRTC::onIceGatheringState( GstElement* webrtc, GParamSpec* pspec, gpointer userData ) 
{
	GstWebRTCICEGatheringState iceGatherState;
	g_object_get( webrtc, "ice-gathering-state", &iceGatherState, nullptr );
	auto newState = "UNKNOWN";
	switch( iceGatherState ) {
		case GST_WEBRTC_ICE_GATHERING_STATE_NEW:
		{
			newState = "NEW";
			break;
		}
		case GST_WEBRTC_ICE_GATHERING_STATE_GATHERING:
		{
			newState = "GATHERING";
			break;
		}
		case GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE:
		{
			newState = "COMPLETE";
			break;
		}
	}
	CI_LOG_I( "ICE gathering state changed to: " << newState );
}

#if defined( ENABLE_INCOMING_VIDEO_STREAM )
void CinderGstWebRTC::handleMediaStreams( GstPad* pad, GstElement* pipeline, const char* convertName, const char* sinkName )
{
//XXX Incoming stream to be handled by an optional appsink eventually
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
#endif

void CinderGstWebRTC::setPipelineEncoderName( std::string& pd )
{
	using namespace std::string_literals;
	std::regex encRegex( R"(\w*enc\b)" );
	std::smatch encMatch;
	auto pipelineEntries = split( pd, "!" );
	// Check if the user has passed a named encoder or not.
	// If not, set one. The name of the encoder can later be used
	// to set encoder properties ( e.g bitrate ) at runtime.
	std::string encoderName = "encoder";
	std::string encoderStr;
	for( size_t i = 0; i < pipelineEntries.size(); ++i ) {
		if( std::regex_search( pipelineEntries[i], encMatch, encRegex ) ) {
			auto encoderEntry = pipelineEntries[i];
			encoderStr = encMatch[0].str();
			if( encoderEntry.find( "name" ) != std::string::npos ) {
				std::smatch encNameMatch;
				std::regex encNameRegex( R"(name=\s*(\S+))");
				if( std::regex_search( encoderEntry, encNameMatch, encNameRegex ) ) {
					if( encNameMatch.size() == 2 ) {
						encoderName = encNameMatch[1].str();
						CI_LOG_V( "Encoder name: " << encoderName );
					}
				}
			}
			else {
				CI_LOG_V( "No encoder name set by user, setting a default one: " << encoderName );
				auto encoderStrPos = pd.find( encoderStr );	
				if( encoderStrPos != std::string::npos ) {
					pd.insert( encoderStrPos + encoderStr.size() + 1, "name="+encoderName+" " );
				}
			}
		}
	}
	if( encoderStr.empty() ) {
		CI_LOG_E( "No encoder present on the pipeline description!" );
		return;
	}
	mEncoder = std::make_pair( encoderStr, encoderName );
}

bool CinderGstWebRTC::startPipeline( gpointer userData )
{
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	GstStateChangeReturn ret;
	GError* error{ nullptr };
	auto videoPipe = "webrtcbin bundle-policy=max-bundle name=cinder_webrtc " +parent->mPipelineData.stunServer+
      " appsrc name=cinder_appsrc !" +parent->mPipelineData.videoPipelineDescr+ "! cinder_webrtc. ";
	parent->mPipeline = gst_parse_launch( videoPipe.c_str(), &error );
	parent->mWebRTC = gst_bin_get_by_name( GST_BIN( parent->mPipeline ), "cinder_webrtc" );
	GArray* transceivers;
	GstWebRTCRTPTransceiver* transceiver;
	g_signal_emit_by_name( parent->mWebRTC, "get-transceivers", &transceivers );
	transceiver = g_array_index( transceivers, GstWebRTCRTPTransceiver*, 0 );
	transceiver->direction = GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;
	parent->mAppsrc = gst_bin_get_by_name( GST_BIN( parent->mPipeline ), "cinder_appsrc" );
	g_object_set( G_OBJECT( parent->mAppsrc ), "caps",
		gst_caps_new_simple( "video/x-raw",
					"format", G_TYPE_STRING, "RGBA", 
					"width", G_TYPE_INT, parent->mPipelineData.width,
					"height", G_TYPE_INT, parent->mPipelineData.height,
					"framerate", GST_TYPE_FRACTION, 0, 1,
					nullptr ), 
	"is-live", true,
	"max-bytes", 0,
	"format", GST_FORMAT_TIME,
	"stream-type", 0, nullptr );

	if( ! parent->mWebRTC ) {
		CI_LOG_E( "WebRTC bin is empty!" );
		g_clear_object( &parent->mPipeline );
		return false;
	}
	g_signal_connect( parent->mWebRTC, "on-negotiation-needed", G_CALLBACK( onNegotionNeeded ), parent );
	g_signal_connect( parent->mWebRTC, "on-ice-candidate", G_CALLBACK( onIceCandidate ), parent );
	g_signal_connect( parent->mWebRTC, "notify::ice-gathering-state", G_CALLBACK( onIceGatheringState ), parent );
	g_signal_connect( parent->mWebRTC, "notify::ice-connection-state", G_CALLBACK( onIceConnectionState ), parent );

	gst_element_set_state( parent->mPipeline, GST_STATE_READY );
	g_signal_emit_by_name( parent->mWebRTC, "create-data-channel", "channel", nullptr, &parent->mDataChannel );
	if( parent->mDataChannel ) {
		connectDataChannelSignals( parent->mDataChannel, parent );
	}
	else CI_LOG_E( "Failed to create send data channel...." );
#if defined( ENABLE_INCOMING_VIDEO_STREAM )
	g_signal_connect( parent->mWebRTC, "pad-added", G_CALLBACK( onIncomingStream ), parent->mPipeline );
#endif
	gst_object_unref( parent->mWebRTC );
	CI_LOG_I( "Starting pipeline..........." );
	ret = gst_element_set_state( GST_ELEMENT( parent->mPipeline ), GST_STATE_PLAYING );
	if( ret == GST_STATE_CHANGE_FAILURE ) {
		CI_LOG_E( "Failed to set the pipeline to PLAYING state!" );
		g_clear_object( &parent->mPipeline );
		parent->mWebRTC = nullptr;
	}
	return true;
}

void CinderGstWebRTC::connectDataChannelSignals( GObject* dataChannel, gpointer userData )
{
	g_signal_connect( dataChannel, "on-error",
		G_CALLBACK( onDataChannelError ), userData );
	g_signal_connect( dataChannel, "on-open",
		G_CALLBACK( onDataChannelOpen ), userData );
	g_signal_connect( dataChannel, "on-close",
		G_CALLBACK( onDataChannelClose ), userData );
	g_signal_connect( dataChannel, "on-message-string",
		G_CALLBACK( onDataChannelMsg ), userData );
}

void CinderGstWebRTC::onDataChannelError( GObject* dc, gpointer userData )
{
	CI_LOG_E( "Data channel errored" );
}

void CinderGstWebRTC::onDataChannelOpen( GObject* dc, gpointer userData )
{
	CI_LOG_V( "Data channel opened" );
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	parent->mDataChannelOpenSignal.emit();
}

void CinderGstWebRTC::onDataChannelClose( GObject* dc, gpointer userData )
{
	CI_LOG_V( "Data channel closed" );
}

void CinderGstWebRTC::onDataChannelMsg( GObject* dc, gchar* msg, gpointer userData )
{
	CI_LOG_V( "Received data channel msg:" << msg );
	auto msgStr = std::string( msg );
	auto msgTokens = split( msgStr, "," );
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	if( msgTokens.size() > 0 ) {
		auto msgType = msgTokens[0];
		if( msgType == "m" ) {
			CI_LOG_V( "Received mouse input: " << msgTokens[1] << " << " << msgTokens[2] << " << " << msgTokens[3] );
			auto x = std::stod( msgTokens[1] );
			auto y = std::stod( msgTokens[2] );
			auto buttonMask = std::stod( msgTokens[3] );
			auto window = parent->mWindow;
			if( window ) {
				if( parent->mButtonMask != buttonMask ) {
					auto maxButtons = 5;
					for( int i = 0; i < maxButtons; i++ ) {
						if( ( (int)buttonMask ^ parent->mButtonMask ) & ( 1 << i ) ) {
							parent->mMouseButtonInitiator = MouseEvent::LEFT_DOWN;
							if( i == 1 ) {
								parent->mMouseButtonInitiator = MouseEvent::MIDDLE_DOWN;
							}
							else if( i == 2 ) {
								parent->mMouseButtonInitiator = MouseEvent::RIGHT_DOWN;
							}
							if( (int)buttonMask & ( 1 << i ) ) {
								auto initiator = &parent->mMouseButtonInitiator;
								ci::app::AppBase::get()->dispatchAsync( [ &parent, x, y] {
									MouseEvent event( parent->mWindow, parent->mMouseButtonInitiator, x, y, 0, 0.0f, 0 );
									parent->mWindow->emitMouseDown( &event );
								});
							}	
							else {
								ci::app::AppBase::get()->dispatchAsync( [&parent, x, y] {
									MouseEvent event( parent->mWindow, parent->mMouseButtonInitiator, x, y, 0, 0.0f, 0 );
									parent->mWindow->emitMouseUp( &event );
									parent->mMouseButtonInitiator = 0;
								});
							}
						}
					}
					parent->mButtonMask = buttonMask;
				}	
				if( parent->mMouseButtonInitiator != 0 ) {
					ci::app::AppBase::get()->dispatchAsync( [&parent, x, y] {
						MouseEvent event( parent->mWindow, parent->mMouseButtonInitiator, x, y, parent->mMouseButtonInitiator, 0.0f, 0 );
						parent->mWindow->emitMouseDrag( &event );
					});
				}
				else {
					ci::app::AppBase::get()->dispatchAsync( [&parent, x, y] {
						MouseEvent event( parent->mWindow, parent->mMouseButtonInitiator, x, y, parent->mMouseButtonInitiator, 0.0f, 0 );
						parent->mWindow->emitMouseMove( &event );
					});
				}
			}
		}
		else if( msgType == "vb" ) {
			auto encoderElement = gst_bin_get_by_name( GST_BIN( parent->mPipeline ), parent->mEncoder.second.c_str() );
			if( ! encoderElement ) {
				CI_LOG_E( "No encoder " << parent->mEncoder.first 
					<<" element found in the pipeline with name: " << parent->mEncoder.second.c_str() << "\n"
					<<" Cannot set bitrate!" );
				return;
			}
			if( parent->mEncoder.first.rfind( "nvh264", 0 ) == 0 ) {
				CI_LOG_V( "nvh264enc encoder used." );
				g_object_set( encoderElement, "bitrate", stod( msgTokens[1] ),nullptr );
			}
			else if( parent->mEncoder.first.rfind( "x264", 0 ) == 0 ) {
				CI_LOG_V( "x264 encoder used." );
				g_object_set( encoderElement, "bitrate", stod( msgTokens[1] ),nullptr );
			} 
			else if( parent->mEncoder.first.rfind( "vp8", 0 ) == 0 ) {
				CI_LOG_V( "vp8 encoder used." );
				g_object_set( encoderElement, "target-bitrate", stod( msgTokens[1] )*1000, nullptr );
			} 
		}
		else { // Unknown to CinderGstWebRTC, forward to user
			parent->mDataChannelMsgSignal.emit( msgStr );
		}
	}
}

void CinderGstWebRTC::onServerMsg( SoupWebsocketConnection* wsConn, SoupWebsocketDataType type, GBytes* message, gpointer userData )
{
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
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
		if( parent->state != SERVER_REGISTERING ) {
			CI_LOG_E( "Received HELLO while not registering!" );
			parent->state = CONNECTION_STATE_ERROR;
		}
		else {
			parent->state = SERVER_REGISTERED;
			CI_LOG_I( "Registered successfully with the server." );
			if( ! setupCall( parent ) ) {
				CI_LOG_E( "Failed to setup call with remote peer!" );
				parent->state = PEER_CALL_ERROR;
			}
		}
	}
	else if( g_str_has_prefix( text, "SESSION_CREATED" )  ) {
		if( parent->state != PEER_CONNECTING ) {
			CI_LOG_E( "Received SESSION_OK when not calling!" );
			parent->state = PEER_CONNECTION_ERROR;
		}
		else {
			parent->state = PEER_CONNECTED;
			if( ! startPipeline( parent ) ) {
				CI_LOG_E( "Failed to start pipeline!" );
				parent->state = PEER_CALL_ERROR;
			}
		}
	}
	else if( g_str_has_prefix( text, "QUIT" ) ) {
		CI_LOG_W( "Received QUIT from server. Exiting!" );
		app::AppBase::get()->quit();
	}
	else if( g_str_has_prefix( text, "ERROR" ) ) {
		switch( parent->state ) {
			case SERVER_CONNECTING:
			{
				parent->state = SERVER_CONNECTION_ERROR;
				break;
			}
			case SERVER_REGISTERING:
			{
				parent->state = SERVER_REGISTRATION_ERROR;
				break;
			}
			case PEER_CONNECTING:
			{
				parent->state = PEER_CONNECTION_ERROR;
			}
			case PEER_CONNECTED:
			case PEER_CALL_NEGOTIATING:
			{
				parent->state = PEER_CALL_ERROR;
				break;
				
			}
			default:
				parent->state = CONNECTION_STATE_ERROR;
		}

		CI_LOG_E( "Got connection error: " << getConnectionStateString( parent->state ) );
	}
	else {
		auto msgJson = json::parse( text );
		if( msgJson.is_discarded() || msgJson.empty() ) {
			CI_LOG_E( "Unknown message: " << text << " Ignoring..." );
		}
		if( msgJson.find( "sdp" ) != msgJson.end() ) {
			g_assert_cmphex( parent->state, ==, PEER_CALL_NEGOTIATING );
			auto sdpJson = msgJson["sdp"];
			if( sdpJson.find( "type" ) == sdpJson.end() ) {
				CI_LOG_E( "Received SDP without type!" );
			}
			else {
				auto typeStr = sdpJson["type"].get<std::string>();
				const auto sdpStr = sdpJson["sdp"].get<std::string>();
				int ret;
				GstSDPMessage* sdp{ nullptr };
				GstWebRTCSessionDescription* answer{ nullptr };
				ret = gst_sdp_message_new( &sdp );
				g_assert_cmphex( ret, ==, GST_SDP_OK );
				ret = gst_sdp_message_parse_buffer( (guint8 *)sdpStr.data(), sdpStr.length(), sdp );
				g_assert_cmphex( ret, ==, GST_SDP_OK );
				answer = gst_webrtc_session_description_new( GST_WEBRTC_SDP_TYPE_ANSWER, sdp );
				{
					GstPromise* promise = gst_promise_new();
					g_signal_emit_by_name( parent->mWebRTC, "set-remote-description", answer, promise );
					gst_promise_interrupt( promise );
					gst_promise_unref( promise );
				}
				parent->state = PEER_CALL_STARTED;
			}
		} 
		else if( msgJson.find( "ice" ) != msgJson.end() ) {
			auto candidate = msgJson["ice"]["candidate"].get<std::string>();
			auto sdpmlineindex = msgJson["ice"]["sdpMLineIndex"].get<int>();
			g_signal_emit_by_name( parent->mWebRTC, "add-ice-candidate", sdpmlineindex, candidate.c_str() );
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
	CinderGstWebRTC* parent = reinterpret_cast<CinderGstWebRTC*>( userData );
	parent->mWSConn = soup_session_websocket_connect_finish( session, result, &error );
	if( error ) {
		CI_LOG_E( "Failed to connect to websocket! " << error->message );
		g_error_free( error );
		return;
	}	
	if( ! parent->mWSConn ) {
		CI_LOG_E( "Websocket is empty!" );
		return;
	}
	parent->state = SERVER_CONNECTED;
	CI_LOG_I( "Connected to signalling server, will now register" );
	g_signal_connect( parent->mWSConn, "closed", G_CALLBACK( onServerClosed ), parent );
	g_signal_connect( parent->mWSConn, "message", G_CALLBACK( onServerMsg ), parent );
	registerWithServer( parent );
}

void CinderGstWebRTC::connectToServerAsync()
{
	SoupLogger* logger{ nullptr };
	SoupMessage* message{ nullptr };
	SoupSession* session{ nullptr };
	const char* httpsAliases[] = { "wss", "https", nullptr };
	//> Create the ws session	
	GError* error{ nullptr };
	//GTlsDatabase* database = g_tls_file_database_new (, &error);
	session = soup_session_new_with_options( SOUP_SESSION_SSL_STRICT, false/*true for using certificate*/, SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, false,
	SOUP_SESSION_HTTPS_ALIASES, httpsAliases, nullptr );
	//> Create the session logger
	logger = soup_logger_new( SOUP_LOGGER_LOG_BODY, -1 );
	soup_session_add_feature( session, SOUP_SESSION_FEATURE( logger ) );
	g_object_unref( logger );
	//> Construct the initial server message
	message = soup_message_new( SOUP_METHOD_GET, mPipelineData.serverURL.c_str() );
	CI_LOG_I( "Connecting to server...." );
	soup_session_websocket_connect_async( session, message, nullptr, nullptr, nullptr, (GAsyncReadyCallback) onServerConnected, this );
	state = SERVER_CONNECTING;
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
	if( ! mAsyncSurfaceReader || ! mAppsrc )
		return;
	// Read pixels
	mCaptureSurface = mAsyncSurfaceReader->readPixels();
	// Create gst buffer and fill it with our pixels
	GstBuffer* buffer = gst_buffer_new_and_alloc( mAsyncSurfaceReader->getWidth() * mAsyncSurfaceReader->getHeight() * mCaptureSurface->getPixelBytes() );
	const auto bytesCopied = gst_buffer_fill( buffer, 0, mCaptureSurface->getData(), mAsyncSurfaceReader->getWidth() * mAsyncSurfaceReader->getHeight() * mCaptureSurface->getPixelBytes() );
	GST_BUFFER_PTS( buffer ) = mTimestamp;
	GST_BUFFER_DTS( buffer ) = mTimestamp;
	GST_BUFFER_DURATION( buffer ) = gst_util_uint64_scale_int( 1, GST_SECOND, 60 );
	// Forwared the pixels along the appsrc pipeline
	const auto result = gst_app_src_push_buffer( GST_APP_SRC_CAST( mAppsrc ), buffer );
	mTimestamp += GST_BUFFER_DURATION( buffer );
	//CI_LOG_V( "Push result " << result << " for copied bytes " << bytesCopied );
}

ci::gl::TextureRef CinderGstWebRTC::getStreamTexture()
{
	if( ! mAsyncSurfaceReader )
		return nullptr;

	return mAsyncSurfaceReader->getTexture();
}

void CinderGstWebRTC::sendStringMsg( const std::string msg )
{
	if( dataChannelReady() ) {
		g_signal_emit_by_name( mDataChannel, "send-string", msg.c_str() );
	}
	else CI_LOG_W( "Attempting to send string msg to peer before channel has been created/opened. Skipping this msg!" );
}

const bool CinderGstWebRTC::dataChannelReady() const
{
	if( ! mDataChannel ) {
		return false;
	}
	GstWebRTCDataChannelState dataChannelState;
	g_object_get( mDataChannel, "ready-state", &dataChannelState, nullptr );
	if( dataChannelState != GST_WEBRTC_DATA_CHANNEL_STATE_OPEN ) {
		return false;
	}
	return true;
}
