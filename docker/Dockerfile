FROM nvidia/cudagl:10.1-devel-ubuntu18.04 AS base
ENV NVIDIA_DRIVER_CAPABILITIES ${NVIDIA_DRIVER_CAPABILITIES},compute,graphics,video,utility

LABEL maintaner="a3naon@gmail.com"
LABEL version="0.1"
LABEL description="Ubuntu 18.04 image for Cinder-GstWebRTC"

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update

RUN apt-get install -y cmake \
	build-essential \
	python3 \
	ninja-build \
	python3-pip \
	git \
	clang \
	libxcursor-dev \
	libxrandr-dev \
	libxinerama-dev \
	libxi-dev \
	libgl1-mesa-dev \
	libglu1-mesa-dev \
	zlib1g-dev \
	libfontconfig1-dev \
	libmpg123-dev \
	libsndfile1 \
	libsndfile1-dev \
	libpulse-dev \
	libasound2-dev \
	libcurl4-gnutls-dev \
	libglib2.0-dev \
	libsoup2.4-dev libjson-glib-dev

RUN pip3 install --user meson
ENV PATH="/root/.local/bin:${PATH}"

RUN apt-get install -y bison \
flex \
libssl-dev \
libsrtp2-dev

RUN git clone https://gitlab.freedesktop.org/gstreamer/gst-build.git
RUN cd gst-build \
&& meson build --buildtype=debug

RUN apt-get install -y libx11-dev libxcb1-dev libx11-xcb-dev

RUN cd gst-build/build \
&& meson configure \
-Dpython=disabled \
-Dqt5=disabled \
-Dexamples=disabled \
-Dgtk_doc=disabled \
-Dtests=disabled \
-Ddevtools=disabled \
-Dtls=disabled \
-Dges=disabled \
-Dgst-examples=disabled \
-Dgst-plugins-base:gl=enabled \
-Dgst-plugins-base:gl_api=opengl

RUN cd gst-build \
&& ninja -C build

RUN cd gst-build \
&& meson install -C build

WORKDIR /home/Software/

RUN git clone https://github.com/Cinder/Cinder.git CiDocker
RUN sed -i '1i#include <unistd.h>' CiDocker/src/cinder/app/linux/AppImplLinuxHeadless.cpp

RUN cd CiDocker \
&& mkdir build \
&& cd build \
&& cmake ../ -DCINDER_HEADLESS_GL=egl -DOpenGL_GL_PREFERENCE=GLVND \
&& make -j8

RUN cd CiDocker/blocks \
&& git clone https://github.com/PetrosKataras/Cinder-GstWebRTC.git

RUN cd CiDocker/blocks/Cinder-GstWebRTC \
&& cd samples/BasicStreamApp/proj/cmake \
&& mkdir build \
&& cd build \
&& cmake .. \
&& make

RUN apt-get install -y curl
RUN curl -sL https://deb.nodesource.com/setup_12.x | bash -
RUN apt-get install -y nodejs

RUN cd CiDocker/blocks/Cinder-GstWebRTC/web \
&& npm install 

EXPOSE 8080

COPY entrypoint.sh /
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
