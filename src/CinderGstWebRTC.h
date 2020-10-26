#pragma once

#include <thread>
#include "cinder/app/App.h"
#include "cinder/Signals.h"
#include "AsyncSurfaceReader.h"
//> GST
#include <gst/gst.h>
#include <gst/sdp/sdp.h>
#define GST_USE_UNSTABLE_API
#include <gst/webrtc/webrtc.h>
//> Signalling
#include <libsoup/soup.h>

/*
 * Based on https://github.com/centricular/gstwebrtc-demos/blob/master/sendrecv/gst/webrtc-sendrecv.c
 * Cinder's output is acting as an appsrc that is linked to the video 
 * pipeline that is created through PipelineData::videoPipelineDescr
 * The video pipeline description describes the en/decoding path
 * for sending/receiving through GstWebRTC and it allows us to use HW acceleration
 * ( when available ) thanks to GStreamer's pipeline architecture.
 */
class CinderGstWebRTC {
public:
	struct PipelineData {
		std::string videoPipelineDescr;
		int width{ -1 };
		int height{ -1 };
		std::string remotePeerId{ -1 };
		uint localPeerId{ 0 };
		std::string serverURL;
		std::string stunServer;
	};
	CinderGstWebRTC( const PipelineData pipelineData, const ci::app::WindowRef& window = nullptr );
	~CinderGstWebRTC();
	void startCapture();
	void endCapture();
	void streamCapture();
	ci::gl::TextureRef getStreamTexture();
	const bool dataChannelReady() const;
	void sendStringMsg( const std::string msg );
	ci::signals::Signal<void()>& getDataChannelOpenedSignal() { return mDataChannelOpenSignal; };
	ci::signals::Signal<void( std::string )>& getDataChannelMsgSignal() { return mDataChannelMsgSignal; };
private:
		enum ConnectionState {
			CONNECTION_STATE_UNKNOWN = 0,
			CONNECTION_STATE_ERROR = 1, /* generic error */
			SERVER_CONNECTING = 1000,
			SERVER_CONNECTION_ERROR,
			SERVER_CONNECTED, /* Ready to register */
			SERVER_REGISTERING = 2000,
			SERVER_REGISTRATION_ERROR,
			SERVER_REGISTERED, /* Ready to call a peer */
			SERVER_CLOSED, /* server connection closed by us or the server */
			PEER_CONNECTING = 3000,
			PEER_CONNECTION_ERROR,
			PEER_CONNECTED,
			PEER_CALL_NEGOTIATING = 4000,
			PEER_CALL_STARTED,
			PEER_CALL_STOPPING,
			PEER_CALL_STOPPED,
			PEER_CALL_ERROR,
		};
	bool initializeGStreamer();
	void startGMainLoopThread();
	void startGMainLoop( GMainLoop* loop );
	void connectToServerAsync();
	void setPipelineEncoderName( std::string& pd );
	static void onServerConnected( SoupSession* session, GAsyncResult* result, gpointer userData );
	static void registerWithServer( gpointer userData );
	static void onServerClosed( SoupWebsocketConnection* wsconn, gpointer userData );
	static void onServerMsg( SoupWebsocketConnection* wsConn, SoupWebsocketDataType type, GBytes* message, gpointer userData );
	static bool setupCall( gpointer userData );
	static bool startPipeline( gpointer userData );
	static void onNegotionNeeded( GstElement* element, gpointer userData );
	static void onIceCandidate( GstElement* webrtc, guint mlineindex, gchar* candidate, gpointer userData );
	static void onIceGatheringState( GstElement* webrtc, GParamSpec* pspec, gpointer userData );
	static void onIceConnectionState( GstElement* webrtc, GParamSpec* pspec, gpointer userData );
#if defined( ENABLE_INCOMING_VIDEO_STREAM ) // stub - not implemented yet..
	static void onIncomingStream( GstElement* webrtc, GstPad* pad, GstElement* pipeline );
	static void onIncomingDecodebinStream( GstElement* decodebin, GstPad* pad, GstElement* pipeline );
	static void handleMediaStreams( GstPad* pad, GstElement* pipeline, const char* convertName, const char* sinkName );
#endif
	static void onOfferCreated( GstPromise* promise, gpointer userData );
	static void sendSdpOffer( GstWebRTCSessionDescription* offer, gpointer userData );
	static void connectDataChannelSignals( GObject* dataChannel, gpointer userData );
	static void onDataChannelError( GObject* dc, gpointer userData );
	static void onDataChannelOpen( GObject* dc, gpointer userData );
	static void onDataChannelClose( GObject* dc, gpointer userData );
	static void onDataChannelMsg( GObject* dc, gchar* str, gpointer userData );
	static void onDataChannel( GstElement* webrtc, GObject* dc, gpointer userData );
	static std::string getConnectionStateString( const ConnectionState connectionState );
private:
	ci::SurfaceRef mCaptureSurface;
	std::unique_ptr<AsyncSurfaceReader> mAsyncSurfaceReader;

	ci::app::WindowRef mWindow;
	int mButtonMask{ 0 };
	int mMouseButtonInitiator{ 0 };

	std::thread mGMainLoopThread;
	GMainLoop* mGMainLoop;
	PipelineData mPipelineData;
	GstClockTime mTimestamp{ 0 };
	GstElement* mAppsrc{ nullptr };
	GstElement* mWebRTC{ nullptr };
	GObject* mDataChannel{ nullptr };
	SoupWebsocketConnection* mWSConn{ nullptr };
	GstElement* mPipeline{ nullptr };
	ci::signals::Signal<void()> mDataChannelOpenSignal;
	ci::signals::Signal<void( std::string )> mDataChannelMsgSignal;
	ConnectionState state{ CONNECTION_STATE_UNKNOWN };
	std::pair<std::string, std::string> mEncoder;
};
