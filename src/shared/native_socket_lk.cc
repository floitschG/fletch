// Copyright (c) 2014, the Fletch project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE.md file.

#if defined(FLETCH_TARGET_OS_LK)

#include "src/shared/native_socket.h"

// #include <arpa/inet.h>
#include <errno.h>
// #include <fcntl.h>
#include <stdlib.h>
#include <lwip/tcp.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
// #include <unistd.h>

#include "src/shared/assert.h"

namespace fletch {

struct Socket::SocketData {
  int fd;
};

Socket::Socket() : data_(new SocketData()) {
  data_->fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (data_->fd < 0) FATAL("Failed socket creation.");
  // TODO(LK): missing fcntl.
  // int status = fcntl(data_->fd, FD_CLOEXEC);
  // if (status == -1) FATAL("Failed making socket close on exec.");
  int optval = 1;
  int status = lwip_setsockopt(data_->fd, SOL_SOCKET, SO_REUSEADDR,
                           &optval, sizeof(optval));
  if (status == -1) FATAL("Failed setting socket options.");
}

Socket::Socket(int fd) : data_(new SocketData()) {
  data_->fd = fd;
  ASSERT(data_->fd >= 0);
  // TODO(LK): missing fcntl.
  // int status = fcntl(data_->fd, FD_CLOEXEC);
  // if (status == -1) FATAL("Failed making socket close on exec.");
}

Socket::~Socket() {
  TEMP_FAILURE_RETRY(lwip_close(data_->fd));
  delete data_;
}

static struct sockaddr LookupAddress(const char* host, int port) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  // lwip does not support flags.
  // hints.ai_flags = AI_ADDRCONFIG;
  hints.ai_protocol = IPPROTO_TCP;
  struct addrinfo* info = NULL;
  int status = lwip_getaddrinfo(host, 0, &hints, &info);
  ASSERT(status == 0);
  for (struct addrinfo* c = info; c != NULL; c = c->ai_next) {
    if (c->ai_family == AF_INET) {
      reinterpret_cast<struct sockaddr_in*>(c->ai_addr)->sin_port =
          lwip_htons(port);
      struct sockaddr addr = *reinterpret_cast<struct sockaddr*>(c->ai_addr);
      lwip_freeaddrinfo(info);
      return addr;
    }
  }
  UNREACHABLE();
  return sockaddr();
}

bool Socket::Connect(const char* host, int port) {
  struct sockaddr addr = LookupAddress(host, port);
  int status = lwip_connect(data_->fd, &addr, sizeof(struct sockaddr_in));
  return status == 0;
}

void Socket::Bind(const char* host, int port) {
  struct sockaddr addr = LookupAddress(host, port);
  int status = lwip_bind(data_->fd, &addr, sizeof(struct sockaddr_in));
  if (status == -1) FATAL("Failed Socket::Bind.");
}

int Socket::Listen() {
  int status = lwip_listen(data_->fd, TCP_DEFAULT_LISTEN_BACKLOG);
  ASSERT(status == 0);
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  status = lwip_getsockname(data_->fd,
                            reinterpret_cast<struct sockaddr*>(&addr),
                            &len);
  if (status == -1) FATAL("Failed Socket::Listen.");
  return lwip_ntohs(addr.sin_port);
}

Socket* Socket::Accept() {
  struct sockaddr clientaddr;
  socklen_t addrlen = sizeof(clientaddr);
  while (true) {
    int socket =
        TEMP_FAILURE_RETRY(lwip_accept(data_->fd, &clientaddr, &addrlen));
    if (socket >= 0) {
      Socket* child = new Socket();
      child->data_->fd = socket;
      return child;
    } else {
      int error = errno;
      if (ShouldRetryAccept(error)) continue;
      UNREACHABLE();
    }
  }
  UNIMPLEMENTED();
  return NULL;
}

void Socket::Write(uint8* data, int length) {
  int offset = 0;
  while (offset < length) {
    int bytes = TEMP_FAILURE_RETRY(
        lwip_write(data_->fd, data + offset, length - offset));
    if (bytes < 0) {
      fprintf(stderr, "Failed to write to socket: %i\n", data_->fd);
      UNREACHABLE();
    }
    offset += bytes;
  }
}

uint8* Socket::Read(int length) {
  uint8* data = static_cast<uint8*>(malloc(length));
  int offset = 0;
  while (offset < length) {
    int bytes = TEMP_FAILURE_RETRY(
        lwip_read(data_->fd, data + offset, length - offset));
    if (bytes <= 0) {
      fprintf(stderr, "Failed to read from socket\n");
      free(data);
      return NULL;
    }
    offset += bytes;
  }
  return data;
}

int Socket::FileDescriptor() {
  return data_->fd;
}

void Socket::SetTCPNoDelay(bool value) {
  int option = value ? 1 : 0;
  int status = lwip_setsockopt(data_->fd, IPPROTO_TCP, TCP_NODELAY,
                               &option, sizeof(option));
  if (status == -1) {
    fprintf(stderr, "Failed setting TCP_NODELAY socket options.");
  }
}

}  // namespace fletch

#endif  // defined(FLETCH_TARGET_OS_LK)
