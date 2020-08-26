###
FROM ubuntu:19.10 as filament

RUN apt-get update

RUN apt-get install -y cmake ninja-build python3-distutils autoconf automake libtool
RUN apt-get install -y clang-9 libglu1-mesa-dev libc++-9-dev libc++abi-9-dev
RUN apt-get install -y libxi-dev
RUN apt-get install -y emacs-nox git curl wget unzip
RUN apt-get -y install pkg-config

RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-9 100
RUN update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-9 100
RUN update-alternatives --install /usr/bin/cc cc /usr/bin/clang 100
RUN update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++ 100

### capnproto linux

WORKDIR /tmp
RUN git clone https://github.com/capnproto/capnproto.git
WORKDIR /tmp/capnproto/c++
RUN autoreconf -i
RUN ./configure
RUN make -j6 check
RUN make install

### capnproto linux example

WORKDIR /tmp/capnproto
RUN mkdir build-samples
WORKDIR /tmp/capnproto/build-samples
RUN cmake ../c++/samples
RUN cmake --build .

### build/install emsdk

WORKDIR /tmp
RUN git clone https://github.com/emscripten-core/emsdk.git
WORKDIR /tmp/emsdk
RUN /bin/bash -c "source ./emsdk_env.sh"

ENV PATH="/tmp/emsdk:/tmp/emsdk/upstream/emscripten:/tmp/emsdk/node/12.9.1_64bit/bin:${PATH}"

ENV EMSDK /tmp/emsdk
ENV EM_CONFIG /root/.emscripten
ENV EMSDK_NODE /tmp/emsdk/node/12.9.1_64bit/bin/node
RUN ./emsdk install latest
RUN ./emsdk activate latest

### capnproto emscripten

WORKDIR /tmp/filament/third_party/capnproto/c++
RUN autoreconf -i
RUN ./configure
RUN make -j6 check

### capnproto emscripten example

WORKDIR /tmp/filament/third_party/capnproto
RUN mkdir build-samples
WORKDIR /tmp/filament/third_party/capnproto/build-samples
RUN cmake ../c++/samples
RUN cmake --build .
