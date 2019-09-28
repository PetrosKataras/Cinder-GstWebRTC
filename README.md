## Cinder-GstWebRTC
An experimental Cinder block that allows pixel streaming from a Cinder application to a remote peer through [GStreamer and WebRTC](https://opensource.com/article/19/1/gstreamer).

### The What
The idea is very much similar to [Unity Render Streaming](https://github.com/Unity-Technologies/UnityRenderStreaming) and [Unreal Pixel Streaming](https://www.unrealengine.com/en-US/blog/pixel-streaming-delivering-high-quality-ue4-content-to-any-device-anywhere) but targeted to Cinder.

In the case of this block the pixel stream of a Cinder application can be delivered to remote peers through [webrtcbin](https://gstreamer.freedesktop.org/data/doc/gstreamer/head/gst-plugins-bad/html/gst-plugins-bad-plugins-webrtcbin.html) which is a relatively new GStreamer plugin that implements WebRTC. The GStreamer pipeline model allows us to take advantage of hardware accelerated (de)encoding when hardware support is available in order to achieve real-time pixel streaming at high resolutions depending on the network and device capabilities.

This block is currently tested in a (W)LAN setup that involves one NVIDIA Jetson Nano acting as the render server that serves the pixel stream from a Cinder application  and a Chrome/Firefox client running on 2012 Macbook Pro acting as the receiving end of the p2p session.

It is based on the [demos](https://github.com/centricular/gstwebrtc-demos) that members of GStreamer team have released to showcase the new functionality.

### The How
In order to build the sample app you need to install a GStreamer version that is >= 1.14

If you are running this on any modern Linux distro you should already have most required packages installed as part of your Cinder installation. In any case you can run the following for installing on `apt` based systems:

`sudo apt-get install -y gstreamer1.0-tools gstreamer1.0-nice gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly gstreamer1.0-plugins-good libgstreamer1.0-dev libglib2.0-dev libgstreamer-plugins-bad1.0-dev libsoup2.4-dev libjson-glib-dev`

On macOS and Windows( untested ) if you opt-in for a complete installation of GStreamer binary and development packages you should have all the required dependencies installed as part of that process.

The block includes a very basic signaling nodejs server, based on the python version included with the demos, that can be used for testing locally. The relevant files are located under the **signaling** directory and you can install the dependencies by running `npm install`. To start the server run `node server` in the same directory.

After the server has started open `http://127.0.0.1:8443` on Firefox. The client is ready to accept the incoming stream now and we should be ready to start the rendering server. Before doing that we must update the `remotePeerId` entry in the `config.json` file that is located inside the `assets` sample folder with the peer id that is displayed on the webpage so that a direct connection can be established between the two peers.
