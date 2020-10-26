#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/Json.h"
#define CI_MIN_LOG_LEVEL 0
#include "cinder/Log.h"
#include "cinder/Rand.h"
#include "CinderGstWebRTC.h"
#include "nlohmann/json.hpp"
#include "cxxopts.hpp"

//#define GRAB_CAMERA
#if defined( GRAB_CAMERA )
	#include "cinder/linux/GstPlayer.h"
	using namespace gst::video;
#else
	#include "cinder/GeomIO.h"
	#include "cinder/ImageIo.h"
	#include "cinder/CameraUi.h"
#endif
using namespace ci;
using namespace ci::app;
using namespace std;

//#define ENCODE_H264
#define STUN_SERVER "stun-server=stun://stun.l.google.com:19302"
#define RTP_CAPS_VP8 "application/x-rtp,media=video,encoding-name=VP8,payload="
#define RTP_CAPS_H264 "application/x-rtp,media=video,encoding-name=H264,payload="

#if defined( ENABLE_FULL_HD )
	const vec2 kStreamResolution = vec2( 1920, 1080 );
#else
	const vec2 kStreamResolution = vec2( 720, 720);
#endif
class Argv {
public:
	Argv(std::vector<const char*> args)
	: m_argv(new const char*[args.size()])
	, m_argc(static_cast<int>(args.size()))
	{
	int i = 0;
	auto iter = args.begin();
	while (iter != args.end()) {
			auto len = strlen(*iter) + 1;
			auto ptr = std::unique_ptr<char[]>(new char[len]);

			strcpy(ptr.get(), *iter);
			m_args.push_back(std::move(ptr));
			m_argv.get()[i] = m_args.back().get();

			++iter;
			++i;
		}
	}

	const char** argv() const {
		return m_argv.get();
	}

	int argc() const {
		return m_argc;
	}

private:
	std::vector<std::unique_ptr<char[]>> m_args{};
	std::unique_ptr<const char*[]> m_argv;
	int m_argc;
};

class BasicStreamApp : public App {
public:
	static void prepareSettings( Settings *settings ) { 
		settings->setMultiTouchEnabled( false ); 
		settings->setWindowSize( kStreamResolution.x, kStreamResolution.y );
		settings->disableFrameRate();
	}
	void setup() final;
	void update() final;
	void draw() final;
	void keyDown( KeyEvent event ) final { };
	void mouseDown( MouseEvent event ) final 
	{ 
		CI_LOG_V( "Mouse down: " << event.getPos() );
#if ! defined( GRAB_CAMERA )
		mCamUi.mouseDown( event );
#endif
	};

	void mouseUp( MouseEvent event ) final 
	{ 
		CI_LOG_V( "Mouse up: " << event.getPos() );
#if ! defined( GRAB_CAMERA )
		mCamUi.mouseUp( event );
#endif
	};
	void mouseDrag( MouseEvent event ) override { 
		CI_LOG_I( "Mouse drag: " << event.getPos() );
#if ! defined( GRAB_CAMERA )
		mCamUi.mouseDrag( event );
#endif
	};
private:	
	std::unique_ptr<CinderGstWebRTC> mGstWebRTC;
#if defined( GRAB_CAMERA )
	std::unique_ptr<GstPlayer> mGstPlayer;
#else
	gl::VboMeshRef	mVboMesh;
	gl::TextureRef	mTexture;
	
	CameraPersp		mCamera;
	CameraUi		mCamUi;
#endif
};

void BasicStreamApp::setup()
{
	ci::Rand::randomize();
	cxxopts::Options cliOptions( "CinderWebRTCSample", "Expose Cinder apps to the web." );
	cliOptions.add_options()
		( "remotePeerId", "Remote peer id", cxxopts::value<string>() )
		( "serverUrl", "Signalling server url", cxxopts::value<string>() );
	auto cliArgs = getCommandLineArgs();
	std::vector<const char*> argsCString;
	argsCString.reserve( cliArgs.size() );
	for( size_t i = 0; i < cliArgs.size(); ++i ) {
		argsCString.push_back( cliArgs[i].c_str() );
	}
	Argv argv( argsCString );
	auto** args = argv.argv();
	auto argc = argv.argc();
	auto argsParseResult = cliOptions.parse( argc, const_cast<char**&>( args ) );
	if( ! argsParseResult.count( "remotePeerId" ) 
		|| ! argsParseResult.count( "serverUrl" ) ) {
			CI_LOG_E( "Required arg options not specified. Aborting!" );
			quit();
	}
	ci::gl::enableVerticalSync( true );
	CinderGstWebRTC::PipelineData webrtcData;
#if defined( ENCODE_H264 )
	#if defined( JETSON )
	webrtcData.videoPipelineDescr = "nvvidconv ! queue ! omxh264enc control-rate=1  preset-level=3  SliceIntraRefreshEnable=true iframeinterval=30 EnableTwopassCBR=true EnableStringentBitrate=true bitrate=20000000 ! h264parse ! rtph264pay config-interval=1 name=payloader aggregate-mode=zero-latency ! queue !" RTP_CAPS_H264 "97";
	#else
	webrtcData.videoPipelineDescr = "videoconvert ! video/x-raw, format=I420 ! queue ! x264enc  threads=2 speed-preset=1 tune=zerolatency key-int-max=30 ! queue !  h264parse ! rtph264pay config-interval=1 name=payloader aggregate-mode=zero-latency ! queue !" RTP_CAPS_H264 "123";
	#endif
#else
	#if defined( JETSON )
	webrtcData.videoPipelineDescr = "nvvidconv ! queue ! omxvp8enc bitrate=15000000 !  rtpvp8pay ! queue ! " RTP_CAPS_VP8 "97";
	#else
	webrtcData.videoPipelineDescr = "videoconvert ! video/x-raw, format=I420 ! queue ! vp8enc keyframe-max-dist=30 threads=2 cpu-used=4 deadline=1 auto-alt-ref=true target-bitrate=2000000 ! rtpvp8pay ! queue ! " RTP_CAPS_VP8 "123";
	#endif
#endif
	webrtcData.remotePeerId = argsParseResult["remotePeerId"].as<std::string>();
	webrtcData.localPeerId = ci::Rand::randUint();
	webrtcData.width = kStreamResolution.x;
	webrtcData.height = kStreamResolution.y;
	webrtcData.serverURL = argsParseResult["serverUrl"].as<std::string>();
	webrtcData.stunServer = STUN_SERVER;
	mGstWebRTC = std::make_unique<CinderGstWebRTC>( webrtcData, getWindow() );
	mGstWebRTC->getDataChannelOpenedSignal().connect(
		[&] {
			nlohmann::json helloMsg;
			helloMsg["type"] = "hello";
			helloMsg["data"] = "Hello Web world from Cinder!";
			mGstWebRTC->sendStringMsg( helloMsg.dump() );	
		}
	);
	mGstWebRTC->getDataChannelMsgSignal().connect(
		[&]( std::string msg ) {
			CI_LOG_I( "Received message from web peer: " << msg );
		}
	);
#if defined( GRAB_CAMERA )
	mGstPlayer = std::make_unique<GstPlayer>();
	mGstPlayer->initialize();
    GstCustomPipelineData data;
	#if defined( JETSON )
    data.pipeline = "nvarguscamerasrc ! video/x-raw(memory:NVMM),width=1920, height=1080, framerate=30/1, format=NV12 ! nvvidconv flip-method=0 ! appsink name=videosink";
	#else
    data.pipeline = "autovideosrc device=/dev/video0 ! videoconvert ! video/x-raw, framerate=25/1, format=RGBA ! appsink name=videosink";
	#endif
    mGstPlayer->setCustomPipeline( data);
    mGstPlayer->play();
#else

	// create some geometry using a geom::Plane
	auto plane = geom::Plane().size( vec2( 20, 20 ) ).subdivisions( ivec2( 200, 50 ) );

	// Specify two planar buffers - positions are dynamic because they will be modified
	// in the update() loop. Tex Coords are static since we don't need to update them.
	vector<gl::VboMesh::Layout> bufferLayout = {
		gl::VboMesh::Layout().usage( GL_DYNAMIC_DRAW ).attrib( geom::Attrib::POSITION, 3 ),
		gl::VboMesh::Layout().usage( GL_STATIC_DRAW ).attrib( geom::Attrib::TEX_COORD_0, 2 )
	};

	mVboMesh = gl::VboMesh::create( plane, bufferLayout );

	mTexture = gl::Texture::create( loadImage( loadAsset( "cinder_logo.png" ) ) );
	
	mCamUi = CameraUi( &mCamera, getWindow() );
#endif
}

void BasicStreamApp::update()
{
#if ! defined( GRAB_CAMERA )
	float offset = getElapsedSeconds() * 4.0f;

	// Dynmaically generate our new positions based on a sin(x) + cos(z) wave
	// We set 'orphanExisting' to false so that we can also read from the position buffer, though keep
	// in mind that this isn't the most efficient way to do cpu-side updates. Consider using VboMesh::bufferAttrib() as well.
	auto mappedPosAttrib = mVboMesh->mapAttrib3f( geom::Attrib::POSITION, false );
	for( int i = 0; i < mVboMesh->getNumVertices(); i++ ) {
		vec3 &pos = *mappedPosAttrib;
		mappedPosAttrib->y = sinf( pos.x * 1.1467f + offset ) * 0.323f + cosf( pos.z * 0.7325f + offset ) * 0.431f;
		++mappedPosAttrib;
	}
	mappedPosAttrib.unmap();
#endif
}

void BasicStreamApp::draw()
{
	mGstWebRTC->startCapture();
	gl::clear( Color( 0.15f, 0.15f, 0.15f ) );
#if defined( GRAB_CAMERA )
		auto cameraTexture = mGstPlayer->getVideoTexture();
		if( cameraTexture ) {
			Rectf centeredRect = Rectf( cameraTexture->getBounds() ).getCenteredFit( getWindowBounds(), true );
			gl::draw( cameraTexture, centeredRect );
		}
#else
		gl::ScopedMatrices camMat;
		gl::setMatrices( mCamera );
		gl::ScopedGlslProg glslScope( gl::getStockShader( gl::ShaderDef().texture() ) );
		gl::ScopedTextureBind texScope( mTexture );

		gl::draw( mVboMesh );
#endif
	mGstWebRTC->endCapture();
	mGstWebRTC->streamCapture();
	gl::clear( Color( 0.15f, 0.15f, 0.15f ) );
	auto streamTexture = mGstWebRTC->getStreamTexture();
	if( streamTexture ) {
		Rectf centeredRect = Rectf( streamTexture->getBounds() ).getCenteredFit( getWindowBounds(), true );
		ci::gl::ScopedColor color( ci::Colorf( 1.0f, 1.0f, 1.0f ) );
		ci::gl::draw( streamTexture, centeredRect );
	}
}

CINDER_APP( BasicStreamApp, RendererGl, BasicStreamApp::prepareSettings );
