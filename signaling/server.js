//
// Based on https://github.com/centricular/gstwebrtc-demos/blob/master/signalling/simple-server.py
//
const express = require( 'express' );
const app = express();
const { createServer } = require( 'http' );
const WebSocket = require( 'ws' );
const server = createServer( app );
const websocket = new WebSocket.Server( { server } );
const mdnsResolver = require( 'mdns-resolver' );

app.use( express.static( __dirname ) );
app.get( '/', ( req, res ) => {
	res.sendFile( __dirname + '/index.html' );
});

let peers = {};
let sessions = {};
let removeSession = function( key ) {
	let otherKey = sessions[key];
	if( sessions.hasOwnProperty( key ) )
		delete sessions[key];
	if( sessions.hasOwnProperty( key ) )
		delete sessions[key];
}
websocket.on( 'connection', ( ws, req ) => {
	ws.isAlive = true;
	ws.on( 'pong', heartbeat );
	console.log( 'Got new websocket connection: ' +req.connection.remoteAddress );
	ws.on( 'message', ( message ) => {
		if( message.startsWith( "HELLO" ) ) {
			let splitHelloHandshake = message.split( " " );	
			if( splitHelloHandshake.length !== 2 ) {
				console.error( "Hello handshake format seems wrong. Should be 'HELLO __peerId__' case sensitive" );
				return;
			}
			let peerId = splitHelloHandshake[1];
			console.log( "Hello from peer: " +peerId );
			peers[peerId] = { "ws": ws, "address": req.connection.remoteAddress };
			ws.send( 'HELLO' );
		}
		else if( message.startsWith( 'SESSION' ) ) {
			let splitSessionHandshake = message.split( " " );
			if( splitSessionHandshake.length !== 3 ) {
				console.error( "Session handshake format seems wrong. Should be 'SESSION __remotePeerId__' case sensitive" );
				return;
			}

			let caller = splitSessionHandshake[1];
			let callee = splitSessionHandshake[2];
			sessions[caller] = callee;
			sessions[callee] = caller;

			if( ( caller in peers ) && ( callee in peers ) ) {
				let sessionId = caller+callee;
				let sessionStr = "SESSION_CREATED " + sessionId;
				console.log( sessionStr );
				peers[caller].ws.send( sessionStr );
				peers[callee].ws.send( sessionStr );
			}
		}
		else if( message.startsWith( 'DISCONNECT' ) ) {
			let splitDisconnectStr = message.split( " " );
			if( splitDisconnectStr.length !== 2 ) {
				console.error( "Disconnect format seems wrong. Should be 'DISCONNECT __remotePeerId__' case sensitive" );
				return;
			}

			let disconnectedPeerId = splitDisconnectStr[1];
			console.log( "Attempt to disconnect peer: " +disconnectedPeerId );
			if( peers[disconnectedPeerId] ) {
				delete peers[disconnectedPeerId];
				removeSession( disconnectedPeerId );
				console.log( "Disconnected peer: " +disconnectedPeerId );
			}
			else {
				console.error( "Disconnect failed. Did not find peer: " +disconnectedPeerId+ " in our list of connected peers." );
			}
		}
		else {
			//> We are in session mode route message to connected peer
			let json = JSON.parse( message );
			if( json.localPeerId != null ) {
				let localPeerId = json.localPeerId;
				let remotePeerId = sessions[localPeerId];
				//console.log( "Sending message from: " +localPeerId+ " to " +remotePeerId );
				/*
				 * Hack to avoid mDNS IP resolve issues during local development.
				 * see https://groups.google.com/forum/#!topic/discuss-webrtc/4Yggl6ZzqZk
				 * and https://bugs.chromium.org/p/chromium/issues/detail?id=930339
				 * for a bit of background on this madness.
				 */
				if( json.ice != null 
					&& json.ice.candidate != null
					&& json.ice.candidate.includes( ".local" ) ) {
						console.log( "Found mDNS ice candidate: " + json.ice.candidate );
						console.log( "Will try to resolve local IP address before forwarding to other peer" );
						let iceCandidateStr = json.ice.candidate;
						let splitIceCandidateStr = iceCandidateStr.split( " " ).filter( str => str.includes( ".local" ) );
						if( splitIceCandidateStr.length == 1 ) {
							mdnsResolver.resolve4( splitIceCandidateStr[0] ).then( ( ip ) => { 
								iceCandidateStr = iceCandidateStr.replace( splitIceCandidateStr[0], ip );
								json.ice.candidate = iceCandidateStr ;
								peers[remotePeerId].ws.send( JSON.stringify( json ) ); 
								console.log( "Resolved local IP sending ice candidate to remote peer: " +remotePeerId+ " at address: " +ip );
							});
						}
				}
				else {
					peers[remotePeerId].ws.send( message );
				}
			}
		}
	});	
});

var noop = function(){};
var heartbeat = function() {
	this.isAlive = true;
}

const interval = setInterval( () => {
	if( websocket.clients.size === 0 ) {
		if( peers.length > 0 ) {
			peers.splice( 0, peers.length );
		}
	}
	websocket.clients.forEach( ( ws ) => {
		if( ws.isAlive === false ) {
			return ws.terminate();
		}
		ws.isAlive = false;
		ws.ping( noop );
	});
}, 2000 );

const port = 8443;
server.listen( port, () => {
	console.log( 'listening for requests on port: ' +port );
});
