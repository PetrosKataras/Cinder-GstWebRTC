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
}
