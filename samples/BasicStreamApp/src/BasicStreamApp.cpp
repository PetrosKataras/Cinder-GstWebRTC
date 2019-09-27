#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/Json.h"
#include "CinderGstWebRTC.h"

#if defined( GRAB_CAMERA )
#include "cinder/linux/GstPlayer.h"
using namespace gst::video;
#endif
using namespace ci;
using namespace ci::app;
using namespace std;

//#define ENCODE_H264
#define STUN_SERVER " stun-server=stun://stun.l.google.com:19302 "
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="
#define RTP_CAPS_H264 "application/x-rtp,media=video,encoding-name=H264,payload="

const vec2 kStreamResolution = vec2( 600, 500 );

class BasicStreamApp : public App {
	public:
		static void prepareSettings( Settings *settings ) { 
			settings->setMultiTouchEnabled( false ); 
			settings->setWindowSize( kStreamResolution.x, kStreamResolution.y );
		}
		void setup() final;
		void update() final;
		void draw() final;
	private:	
		std::unique_ptr<CinderGstWebRTC> mGstWebRTC;
#if defined( GRAB_CAMERA )
		std::unique_ptr<GstPlayer> mGstPlayer;
#endif
};

void BasicStreamApp::setup()
{
	ci::JsonTree configJson = ci::JsonTree(ci::app::loadAsset("config.json"));
	CinderGstWebRTC::PipelineData webrtcData;
#if defined( ENCODE_H264 )
	#if defined( JETSON )
	webrtcData.videoPipelineDescr = "nvvidconv ! queue ! omxh264enc control-rate=1  preset-level=3  SliceIntraRefreshEnable=true iframeinterval=30 EnableTwopassCBR=true EnableStringentBitrate=true bitrate=20000000 ! h264parse ! rtph264pay ! queue !" RTP_CAPS_H264 "97";
	#else
	webrtcData.videoPipelineDescr = "videoconvert ! video/x-raw, format=RGBA ! queue ! x264enc ! h264parse ! rtph264pay ! queue !" RTP_CAPS_H264 "97";
	#endif
#else
	#if defined( JETSON )
	webrtcData.videoPipelineDescr = "nvvidconv ! queue ! omxvp8enc bitrate=15000000 !  rtpvp8pay ! queue ! " RTP_CAPS_VP8 "97";
	#else
	webrtcData.videoPipelineDescr = "videoconvert ! video/x-raw, framerate=25/1, format=RGBA ! queue ! vp8enc bitrate=12000000 ! h264parse ! rtph264pay ! queue !" RTP_CAPS_H264 "97";
	#endif
#endif
	webrtcData.remotePeerId = configJson.getValueForKey<int>( "remotePeerId" );
	webrtcData.localPeerId = configJson.getValueForKey<int>( "localPeerId" );
	webrtcData.width = kStreamResolution.x;
	webrtcData.height = kStreamResolution.y;
	webrtcData.serverURL = configJson.getValueForKey( "server_url" );
	webrtcData.stunServer = STUN_SERVER;
	mGstWebRTC = std::make_unique<CinderGstWebRTC>( webrtcData );
#if defined( GRAB_CAMERA )
	mGstPlayer = std::make_unique<GstPlayer>();
	mGstPlayer->initialize();
    GstCustomPipelineData data;
	#if defined( JETSON )
    data.pipeline = "nvarguscamerasrc ! video/x-raw(memory:NVMM),width=1920, height=1080, framerate=30/1, format=NV12 ! nvvidconv flip-method=0 ! appsink name=videosink";
	#else
    data.pipeline = "v4lsrc ! videoconvert ! video/x-raw, framerate=25/1, format=RGBA ! appsink name=videosink";
	#endif
    mGstPlayer->setCustomPipeline( data);
    mGstPlayer->play();
#endif
}

void BasicStreamApp::update()
{
	mGstWebRTC->startCapture();
		gl::clear( Color( 1, 0, 0 ) );
#if defined( GRAB_CAMERA )
		auto cameraTexture = mGstPlayer->getVideoTexture();
		if( cameraTexture ) {
			Rectf centeredRect = Rectf( cameraTexture->getBounds() ).getCenteredFit( getWindowBounds(), true );
			gl::draw( cameraTexture, centeredRect );
		}
#endif
	mGstWebRTC->endCapture();
	mGstWebRTC->streamCapture();
}

void BasicStreamApp::draw()
{
}

CINDER_APP( BasicStreamApp, RendererGl, BasicStreamApp::prepareSettings );
