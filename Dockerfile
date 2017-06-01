# TODO
# - Create docker images with needed deps installed
# - Create a new docker image after every efl build with efl installed to be used by E, terminology..
# - Create a matrix build with docker images for Ubuntu, Fedora, Arch, openSuse...
# - Create docker build for all extra options
# - Create docker build for wayland options

FROM ubuntu:16.04

# Install
RUN apt-get update -y
RUN apt-get install -y build-essential autoconf automake autopoint doxygen check luajit libharfbuzz-dev libpng12-dev libudev-dev libwebp-dev libssl-dev libluajit-5.1-dev libfribidi-dev libcogl-gles2-dev libgif-dev libtiff5-dev libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libdbus-1-dev libmount-dev libblkid-dev libpulse-dev libxrandr-dev libxtst-dev libxcursor-dev libxcomposite-dev libxinerama-dev libxkbfile-dev libbullet-dev libvlc-dev libsndfile1-dev libraw-dev libspectre-dev libpoppler-cpp-dev libpam0g-dev liblz4-dev faenza-icon-theme gettext git imagemagick libasound2-dev libbluetooth-dev libfontconfig1-dev libfreetype6-dev libibus-1.0-dev libiconv-hook-dev libjpeg-dev libjpeg-turbo8-dev libpoppler-dev libpoppler-private-dev libproxy-dev librsvg2-dev libscim-dev libsystemd-dev libtool libudisks2-dev libunibreak-dev libxcb-keysyms1-dev libxine2-dev libxss-dev linux-tools-common libcurl4-openssl-dev

# Build
RUN mkdir -p /src/
WORKDIR /src/
COPY . /src/
RUN ./autogen.sh --disable-cxx-bindings
RUN make -j 10
RUN make -j 10 examples
#RUN make -j 10 check
RUN make -j 10 benchmark

#  - if [[ "$TRAVIS_OS_NAME" == "linux" ]]; then eval $(dbus-launch --sh-syntax --exit-with-session); fi
#  - if [[ "$TRAVIS_OS_NAME" == "linux" ]]; then make -j 10 dist && mkdir tmp && cp efl*.gz tmp/ && cd tmp/ && tar xf efl* && cd efl* && ./configure $COPTS && make -j 10 && make -j 10 ex    amples && make -j 10 benchmark && make -j 10 check && cd ../.. ;fi
#  - if [[ "$TRAVIS_OS_NAME" == "linux" ]]; then make -j 10 distcheck; fi
