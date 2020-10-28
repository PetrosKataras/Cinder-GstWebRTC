/*
* Based on examples found at https://gitlab.freedesktop.org/gstreamer/gst-examples/-/tree/master/webrtc
* PK, 2020
*/

const express = require( 'express' );
const app = express();
const { createServer } = require( 'http' );
const WebSocket = require( 'ws' );
const server = createServer( app );
const websocket = new WebSocket.Server( { server } );
const { spawn } = require( 'child_process' );
const parseArgs = require( 'minimist' )( process.argv.slice( 2 ) );
if( ! parseArgs.binaryPath ) {
	console.error( "'--binaryPath' option required! Set it to the Cinder WebRTC executable path that you want to launch." );
	process.exit( 1 );
}

app.use( express.static( __dirname ) );
app.get( '/', ( req, res ) => {
	res.sendFile( __dirname + '/index.html' );
});

let peers = {};
let removePeer = function( key ) {
}

let sessions = {};
let removeSession = function( key ) {
	if( sessions.hasOwnProperty( key ) ) {
		delete sessions[key];

		let otherKey = sessions[key]; 
		if( sessions.hasOwnProperty( otherKey ) ) {
			delete sessions[otherKey];
		}
	}
}

websocket.on( 'connection', ( ws, req ) => {
	ws.isAlive = true;
	ws.on( 'pong', heartbeat );

	console.log( 'Got new websocket connection: ' +req.connection.remoteAddress );

	ws.on( 'message', ( message ) => {
		console.log( message )
		if( message.startsWith( "HELLO" ) ) {
			let splitHelloHandshake = message.split( " " );	
			let peerId = -1;
			if( splitHelloHandshake.length === 2 ) {
				peerId = splitHelloHandshake[1];
				console.log( "Hello from rendering peer: " +peerId );
			}
			else if( splitHelloHandshake.length === 3 ) {
				peerId = splitHelloHandshake[2];
			}
			if( peerId === -1 ) {
				console.error( "Hello handshake format seems wrong. Should be 'HELLO __peerId__' case sensitive" );
				return;
			}
			peers[peerId] = { "ws": ws, "address": req.connection.remoteAddress };
			//console.log( peers )
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
			console.log( caller , " + " , callee )
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
		else if( message.startsWith( 'START_RENDERING' ) ) {
			let splitStartRenderingHandshake = message.split( " " );
			let peerId = splitStartRenderingHandshake[1];
			//> Start rendering process
			const cmd = parseArgs.binaryPath + " " + "--remotePeerId=" +peerId+ " " +"--serverUrl=http://" +listener.address().address+":"+listener.address().port;
			const cmdSpawn = spawn( cmd, {
				detached: true,
				shell: true
			});
			cmdSpawn.on( 'exit', ( code, signal ) => {
				console.log( 'Renderer exited with ' + `code ${code} and signal ${signal}` );
			});
			cmdSpawn.stdout.on( 'data', ( data ) => {
				console.log( `Renderer cout: \n${data}` );
			});
			cmdSpawn.stderr.on( 'data', ( data ) => {
				console.log( `Renderer error: \n${data}` );
			});
		}
		else {
			//> We are in session mode, route message to connected peer
			let json = JSON.parse( message );
			let localPeerId = json.localPeerId;
			let remotePeerId = sessions[localPeerId];
			if( json.localPeerId != null ) {
				peers[remotePeerId].ws.send( JSON.stringify( json ) ); 
			}
		}
	});	
});

websocket.on( 'close', () => {
	clearInterval( interval );
	console.log( "CLOSE" );
});

var noop = function(){};
var heartbeat = function() {
	this.isAlive = true;
}

const interval = setInterval( () => {
	//> Check that all sides are still connected.
	//> If not remove hanging peers and sessions.
	let peersLength = Object.keys( peers ).length;
	let wsConnectedClientsLength = websocket.clients.size;
	let sessionsLength = Object.keys( sessions ).length;
	//console.log( "Peers size: ", peersLength, " ws connections size: ", wsConnectedClientsLength, " sessions size: ", sessionsLength );
	if( peersLength !==  wsConnectedClientsLength ) {
		let disconnectedPeers = [];
		for( let peer in peers ) {
			let found = false;
			for( let ws of websocket.clients ) {
				if( peers[peer].ws === ws ) {
					found = true;
					break;
				}
			}
			if( ! found ) {
				disconnectedPeers.push( peer );
			}
		}
		disconnectedPeers.forEach( disconnectedPeer => {
			let connectedPeerId = sessions[disconnectedPeer];
			if( connectedPeerId ) {
				let connectedPeer = peers[connectedPeerId];	
				if( connectedPeer )
					connectedPeer.ws.send( "QUIT" );
			}
			delete peers[disconnectedPeer];
			removeSession( disconnectedPeer );
		})
		disconnectedPeers = [];
	}
	//> Ping clients
	websocket.clients.forEach( ( ws ) => {
		if( ws.isAlive === false ) {
			console.log( "Terminating ws conn: " ,ws );
			return ws.terminate();
		}
		ws.isAlive = false;
		ws.ping( noop );
	});
}, 2000 );

const port = 8080;
const listener = server.listen( port, '0.0.0.0', () => {
	console.log( 'listening on: ', listener.address() );
});
