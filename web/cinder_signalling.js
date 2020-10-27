/*
*
* Cinder specific signalling handler details.
* PK, 2020
*
*
* WebRTCDemoSignalling released under the Apache 2.0 license from GoogleLLC (c) 2019.
*
* http://www.apache.org/licenses/LICENSE-2.0
*/

class CinderSignalling extends WebRTCDemoSignalling {
	constructor( server, peer_id ) {
		super( server, peer_id );
	}
	_onServerOpen() {
		this._state = 'connected';
		this._ws_conn.send( 'HELLO WEB '+ this._peer_id );
        this._setStatus( "Registering with server, peer ID: " + this._peer_id );
	}	
	sendICE( ice ) {
        this._setDebug( "sending ice candidate: " + JSON.stringify( ice ) + " PEER ID " + this._peer_id );
        this._ws_conn.send( JSON.stringify( { 'localPeerId': this._peer_id, 'ice': ice } ) ) ;
	}	

	sendSDP( sdp ) {
        this._setDebug( "sending local sdp: " + JSON.stringify( sdp ) );
        this._ws_conn.send( JSON.stringify( { 'localPeerId': this._peer_id, 'sdp': sdp } ) );
	}

	sendStartStream() {
        this._setDebug( "Requesting stream start." );
        this._ws_conn.send( 'START_RENDERING ' +this._peer_id );
	}
}
