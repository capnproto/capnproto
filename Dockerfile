FROM debian:jessie
RUN \
  apt-get update && apt-get install -y \
    autoconf automake libtool g++ git make \
    && rm -rf /var/lib/apt/lists/*
COPY . /tmp/capnproto
RUN cd /tmp/capnproto/c++ && \
      autoreconf -i && \
      ./configure && \
      make -j6 check && \
      make install
WORKDIR /tmp/capnproto
