/*
*
* Based on https://github.com/GoogleCloudPlatform/selkies-vdi/blob/master/images/gst-web/src/app.js released under the Apache 2.0 license from GoogleLLC (c) 2019.
*
*/

/*
* Cinder WebRTC frontend app
* PK, 2020
*
* Modified/adapted to work with the Cinder-GstWebRTC stack.
* https://github.com/PetrosKataras/Cinder-GstWebRTC
* 
*/

var ScaleLoader = VueSpinner.ScaleLoader;
var app = new Vue({
	el: '#app',
	components: {
		ScaleLoader
	},
	data() {
		return {
			appName: window.location.pathname.split( "/" )[1] || "cinder-gstwebrtc",
			videoBitRate: ( parseInt( window.localStorage.getItem( "videoBitRate" ) ) || 2000 ),
			videoBitRateOptions: [
				{ text: '500 kb/s', value: 500 },
				{ text: '1 mbps', value: 1000 },
				{ text: '2 mbps', value: 2000 },
				{ text: '3 mbps', value: 3000 },
				{ text: '4 mbps', value: 4000 },
				{ text: '8 mbps', value: 8000 },
				{ text: '20 mbps', value: 20000 },
				{ text: '100 mbps', value: 100000 },
				{ text: '150 mbps', value: 150000 },
				{ text: '200 mbps', value: 200000 },
			],
			videoFramerate: ( parseInt( window.localStorage.getItem( "videoFramerate" ) ) || 30 ),
			videoFramerateOptions: [
				{ text: '1 fps', value: 1 },
				{ text: '15 fps', value: 15 },
				{ text: '30 fps', value: 30 },
				{ text: '60 fps', value: 60 },
				{ text: '100 fps', value: 100 },
			],
			showStart: true,
			logEntries: [],
			debugEntries: [],
			showDrawer: false,
			debug: true,
			windowResolution: "",
			loadingText: "",
			connectionStatType: "unknown",
			connectionLatency: 0,
			connectionVideoLatency: 0,
			connectionAudioLatency: 0,
			connectionAudioCodecName: "unknown",
			connectionAudioBitrate: 0,
			connectionPacketsReceived: 0,
			connectionPacketsLost: 0,
			connectionCodec: "unknown",
			connectionVideoDecoder: "unknown",
			connectionResolution: "",
			connectionFrameRate: 0,
			connectionVideoBitrate: 0,
			connectionAvailableBandwidth: 0,
			status: 'connecting'
		}
	},
	methods: {
		enterFullscreen() {
			webrtc.element.parentElement.requestFullscreen();
		},	
		startStream() {
			signalling.sendStartStream();
			this.showStart = false;
		}
	},
	watch: {
		videoBitRate( newValue ) {
			webrtc.sendDataChannelMessage( 'vb,' +newValue );
			window.localStorage.setItem( "videoBitRate", newValue.toString() );
		},
		videoFramerate( newValue ) {
			console.log( "video frame rate changed to " + newValue );
			webrtc.sendDataChannelMessage( '_arg_fps,' + newValue );
			window.localStorage.setItem( "videoFramerate", newValue.toString() );
		},
		debug(newValue) {
			window.localStorage.setItem( "debug", newValue.toString() );
			// Reload the page to force read of stored value on first load.
			setTimeout( () => {
				document.location.reload();
			}, 700 );
		},
		showDrawer( newValue ) {
			// Detach inputs when menu is shown.
			if( newValue === true ) {
				webrtc.input.detach();
			} 
			else {
				webrtc.input.attach();
			}
		}
	}
});

var videoElement = document.getElementById( "videoStream" );
var signalling = new CinderSignalling( new URL( "wss://" + window.location.host ), uuidv4() );
var webrtc = new CinderWebRTCDemo( signalling, videoElement );

// Function to add timestamp to logs.
var applyTimestamp = ( msg ) => {
	var now = new Date();
	var ts = now.getHours() + ":" + now.getMinutes() + ":" + now.getSeconds();
	return "[" + ts + "]" + " " + msg;
}

// Send signalling status and error messages to logs.
signalling.onstatus = ( message ) => {
	app.loadingText = message;
	app.logEntries.push( applyTimestamp( "[signalling] " + message ) );
};
signalling.onerror = ( message ) => { app.logEntries.push( applyTimestamp("[signalling] [ERROR] " + message ) ) };

signalling.onpeerquit = () => {
	app.status = 'failed';
}

// Send webrtc status and error messages to logs.
webrtc.onstatus = ( message ) => { app.logEntries.push( applyTimestamp("[webrtc] " + message ) ) };
webrtc.onerror = ( message ) => { app.logEntries.push( applyTimestamp("[webrtc] [ERROR] " + message ) ) };

if( app.debug ) {
	signalling.ondebug = ( message ) => { app.debugEntries.push( "[signalling] " + message ); };
	webrtc.ondebug = ( message ) => { app.debugEntries.push( applyTimestamp("[webrtc] " + message ) ) };
}

// Bind vue status to connection state.
webrtc.onconnectionstatechange = ( state ) => {
	console.log( state )
	if( app.status === "failed" )
		return;

	if( state === "disconnected" ) {
		console.log( "Disconnected, handling as failed." );
		app.status = "failed";
		return;
	}
	app.status = state;

	if( state === "connected" ) {
		// Start watching stats.
		var bytesReceivedStart = 0;
		var audiobytesReceivedStart = 0;
		var statsStart = new Date().getTime() / 1000;
		var statsLoop = () => {
			webrtc.getConnectionStats().then( ( stats ) => {
				stats.audiobytesReceived = 0;
				app.connectionLatency = app.connectionVideoLatency;
				app.connectionStatType = stats.videoLocalCandidateType;
				app.connectionVideoLatency = parseInt( stats.videoCurrentDelayMs );
				app.connectionPacketsReceived = parseInt( stats.videopacketsReceived );
				app.connectionPacketsLost = parseInt( stats.videopacketsLost );
				app.connectionCodec = stats.videoCodecName;
				app.connectionVideoDecoder = stats.videocodecImplementationName;
				app.connectionResolution = stats.videoFrameWidthReceived + "x" + stats.videoFrameHeightReceived;
				app.connectionFrameRate = stats.videoFrameRateOutput;
				app.connectionAvailableBandwidth = ( parseInt( stats.videoAvailableReceiveBandwidth ) / 1e+6 ).toFixed(2) + " mbps";

				// Compute current video bitrate in mbps
				var now = new Date().getTime() / 1000;
				app.connectionVideoBitrate = ( ( ( parseInt( stats.videobytesReceived ) - bytesReceivedStart ) / ( now - statsStart ) ) * 8 / 1e+6) .toFixed(2);
				bytesReceivedStart = parseInt( stats.videobytesReceived );

				statsStart = now;

				// Stats refresh loop.
				setTimeout( statsLoop, 1000 );
			});
		};
		statsLoop();
	}
 };

webrtc.ondatachannelopen = () => {
	webrtc.input.ongamepadconnected = ( gamepad_id ) => {
		app.gamepadState = "connected";
		app.gamepadName = gamepad_id;
	}

	webrtc.input.ongamepaddisconnected = () => {
		app.gamepadState = "disconnected";
		app.gamepadName = "none";
	}

	webrtc.input.attach();
}

webrtc.ondatachannelclose = () => {
	webrtc.input.detach();
}

webrtc.input.onmenuhotkey = () => {
	app.showDrawer = !app.showDrawer;
}

webrtc.input.onfullscreenhotkey = () => {
	app.enterFullscreen();
}

webrtc.input.onresizeend = () => {
	app.windowResolution = webrtc.input.getWindowResolution();
	console.log( `Window size changed: ${app.windowResolution[0]}x${app.windowResolution[1]}` );
}

webrtc.onplayvideorequired = () => {
	app.showStart = true;
}

// Actions to take whenever window changes focus
window.addEventListener('focus', () => {
});

window.addEventListener('blur', () => {
});

webrtc.onsystemaction = ( action ) => {
	webrtc._setStatus( "Executing system action: " + action );
	if( action === 'reload' ) {
		setTimeout( () => {
			document.location.reload();
		}, 700);
	}
	else if( action.startsWith( 'framerate' ) ) {
		app.videoFramerate = parseInt( action.split(",")[1] );
	}
	 else if( action.startsWith( 'audio' ) ) {
		app.audioEnabled = ( action.split(",")[1].toLowerCase() === 'true' );
	}
	 else {
		webrtc._setStatus( 'Unhandled system action: ' + action );
	}
}

// get initial local resolution
app.windowResolution = webrtc.input.getWindowResolution();
webrtc.connect();

/*
// Fetch RTC configuration containing STUN/TURN servers.
fetch( "/turn/" )
	.then( function ( response ) {
		return response.json();
	})
	.then( ( config ) => {
		// for debugging, force use of relay server.
		webrtc.forceTurn = app.turnSwitch;

		// get initial local resolution
		app.windowResolution = webrtc.input.getWindowResolution();

		app.debugEntries.push( applyTimestamp( "[app] using TURN servers: " + config.iceServers[1].urls.join(", ") ) );
		webrtc.rtcPeerConfig = config;
		webrtc.connect();
	});
*/
