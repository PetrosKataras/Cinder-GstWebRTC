/*
*
* Cinder WebRTC demo specifics.
* PK, 2020
*
*
* WebRTCDemo released under the Apache 2.0 license from GoogleLLC (c) 2019.
*
* http://www.apache.org/licenses/LICENSE-2.0
*/

class CinderWebRTCDemo extends WebRTCDemo {
	constructor( signalling, element ) {
		super( signalling, element );
	}
	_onPeerDataChannelMessage( event ) {
		// Attempt to parse message as JSON
		var msg;
		try {
			msg = JSON.parse(event.data);
		} 
		catch (e) {
			if (e instanceof SyntaxError) {
				this._setError("error parsing data channel message as JSON: " + event.data);
			} else {
				this._setError("failed to parse data channel message: " + event.data);
			}
			return;
		}

		this._setDebug("data channel message: " + event.data);

		if( msg.type === 'hello' ) {
			this._setStatus( msg.data );
		}
		else if( msg.type === 'pipeline' ) {
			this._setStatus(msg.data.status);
		} 
		else if (msg.type === 'system') {
			if (msg.action !== null) {
				this._setDebug("received system msg, action: " + msg.data.action);
				var action = msg.data.action;
				if (this.onsystemaction !== null) {
					this.onsystemaction(action);
				}
			}
		}
		else {
			this._setError("Unhandled message recevied: " + msg.type);
		}
	}

	_onSDP(sdp) {
		if (sdp.type != "offer") {
			this._setError("received SDP was not type offer.");
			return
		}
		const sdpTransform = require( 'sdp-transform' );
		const offerSDP = sdpTransform.parse( sdp.sdp )
		console.log("Received remote SDP", offerSDP);
		this.peerConnection.setRemoteDescription(sdp).then(() => {
			this._setDebug("received SDP offer, creating answer");
			this.peerConnection.createAnswer()
				.then((local_sdp) => {
					//> The following hack is necessary on some Android devices
					//> when using Chrome and H264. There is seems to be a bug
					//> in the way that the H264 decoder is initialized in some instances
					//> and the generation of the answer is incomplete.
					//> If that is the case, force the format and relevant parameters on our own.
					//> Potentially related: https://bugs.chromium.org/p/webrtc/issues/detail?id=11620
					let answerSDP = sdpTransform.parse( local_sdp.sdp )
					answerSDP.media.forEach( answerMedia => {
						if( answerMedia.type === "video" ) {
							if( answerMedia.rtp.length === 0 ) {
								offerSDP.media.forEach( offerMedia => {
									if( offerMedia.type === "video" ) {	
										answerMedia.fmtp = offerMedia.fmtp;
										answerMedia.fmtp[0].config = 'level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e015';
										answerMedia.payloads = offerMedia.payloads;
										answerMedia.rtcpFb = offerMedia.rtcpFb;
										answerMedia.rtp = offerMedia.rtp;
										local_sdp.sdp = sdpTransform.write( answerSDP );
									}
								});
							}
						}
					});
					this.peerConnection.setLocalDescription(local_sdp).then(() => {
						this._setDebug("Sending SDP answer");
						this.signalling.sendSDP(this.peerConnection.localDescription);
					}).catch( ( e ) => {
						console.log( e );
					});
				}).catch(() => {
					this._setError("Error creating local SDP");
				});
		})
	}

}
