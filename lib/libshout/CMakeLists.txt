cmake_minimum_required(VERSION 3.0)
project(shout_mixxx C)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DHAVE_CONFIG_H -Wall -ffast-math -pthread -g -O2")
 
find_package(OpenSSL)

include_directories(
  .
  src
  include
  src/common
)

add_library(shout_mixxx STATIC
  src/common/avl/avl.c
  src/common/net/sock.c
  src/common/net/resolver.c
  src/common/timing/timing.c
  src/common/httpp/httpp.c
  src/common/httpp/encoding.c
  src/common/thread/thread.c
  src/shout.c
  src/util.c
  src/queue.c
  src/proto_http.c
  src/proto_xaudiocast.c
  src/proto_icy.c
  src/proto_roaraudio.c
  src/format_ogg.c
  src/format_webm.c
  src/format_mp3.c
  src/codec_vorbis.c
  src/codec_opus.c
  src/codec_theora.c
  src/codec_speex.c
  src/tls.c
)

target_link_libraries(shout_mixxx ogg vorbis theora speex OpenSSL::SSL OpenSSL::Crypto)


