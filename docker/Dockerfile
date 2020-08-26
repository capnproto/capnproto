###
FROM ubuntu:19.10

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

WORKDIR /tmp/capnproto.linux
RUN git clone https://github.com/capnproto/capnproto.git
WORKDIR /tmp/capnproto.linux/capnproto/c++
RUN autoreconf -i
RUN ./configure
RUN make -j6 check
RUN make install

### capnproto linux example

WORKDIR /tmp/capnproto.linux/capnproto
RUN mkdir build-samples
WORKDIR /tmp/capnproto.linux/capnproto/build-samples
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
RUN ./emsdk install latest && ./emsdk activate latest && . ./emsdk_env.sh && echo 'source "/tmp/emsdk/emsdk_env.sh"' >> $HOME/.bash_profile

### capnproto emscripten

WORKDIR /tmp/capnproto.emscripten
RUN git clone https://github.com/capnproto/capnproto.git
WORKDIR /tmp/capnproto.emscripten/capnproto/c++
RUN autoreconf -i
RUN ./configure
RUN /bin/bash -c "source ~/.bash_profile && make -j6 check"

### capnproto emscripten example

WORKDIR /tmp/capnproto.emscripten/capnproto
RUN mkdir build-samples
WORKDIR /tmp/capnproto.emscripten/capnproto/build-samples
RUN cmake ../c++/samples
RUN /bin/bash -c "source ~/.bash_profile && cmake --build ."

