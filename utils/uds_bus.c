#include "uds_bus.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

struct call_type_to_fd_s {
  enum uds_call_type_e call_type;
  int fd;
  call call_func;
};

// extern void *clip_call(int src_fd, uint32_t data_size, uint8_t *msg_data);
static struct call_type_to_fd_s call_type_to_fd[UDS_CALL_TYPE_MAX] = {
    // 添加其它算法调用，需要在这新增
    // 映射 关系
    // {.call_type = UDS_CALL_TYPE_CLIP, .fd = -1, .call_func = clip_call},
};
static void del_call_type_fd(int fd) {
  for (int i = 0; i < UDS_CALL_TYPE_MAX; i++) {
    if (call_type_to_fd[i].fd == fd) {
      call_type_to_fd[i].fd = -1;
      LOG("Cleared call_type %d mapping for socket %d",
          call_type_to_fd[i].call_type, fd);
    }
  }
}

int uds_register_handler(uint8_t call_type, call callback) {
  if (call_type >= UDS_CALL_TYPE_MAX) {
    LOG("Invalid call_type %d", call_type);
    return -1;
  }

  call_type_to_fd[call_type].call_func = callback;
  call_type_to_fd[call_type].call_type = call_type;
  call_type_to_fd[call_type].fd = -1;  // Initialize fd as not registered
  LOG("Registered call_type %d mapping", call_type_to_fd[call_type].call_type);
  return 0;
}

static int add_call_type_fd(int fd, uint8_t call_type) {
  if (call_type >= UDS_CALL_TYPE_MAX) {
    LOG("Invalid call_type %d", call_type);
    return -1;
  }
  call_type_to_fd[call_type].fd = fd;
  return 0;
}

static int find_call_type_fd(int fd) {
  for (int i = 0; i < UDS_CALL_TYPE_MAX; i++) {
    if (call_type_to_fd[i].fd == fd) {
      return call_type_to_fd[i].call_type;
    }
  }
  return -1;
}

// Find the registered fd for a given call_type
static int find_fd_by_call_type(uint8_t call_type) {
  if (call_type >= UDS_CALL_TYPE_MAX) {
    return -1;
  }
  return call_type_to_fd[call_type].fd;
}
static call find_call_func_by_call_type(uint8_t call_type) {
  if (call_type >= UDS_CALL_TYPE_MAX || call_type < 0) {
    return NULL;
  }
  return call_type_to_fd[call_type].call_func;
}

static int set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    LOG("Failed to get socket %d flags", fd);
    return -1;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    LOG("Failed to set socket %d to nonblocking", fd);
    return -1;
  }
  return 0;
}

static struct bus_message_s *bus_message_get(int fd) {
#define BUFF_SIZE 1024
  uint8_t buffer[BUFF_SIZE] = {0};
  int read_size = 0;
  struct bus_message_s *msg = NULL;
  struct bus_message_s *ret = NULL;
  uint32_t total_received = 0;

  msg = malloc(sizeof(struct bus_message_s));
  if (msg == NULL) {
    LOG("Failed to malloc bus_message_s\n");
    goto end;
  }

  read_size = recv(fd, &msg->magic, sizeof(uint8_t), 0);
  if (read_size <= 0) {
    LOG("Failed to read magic from socket %d, error: %s", fd, strerror(errno));
    goto end;
  }

  LOG("Read magic %d from socket %d", msg->magic, fd);

  read_size = recv(fd, &msg->header, sizeof(union bus_header_s), 0);
  if (read_size != sizeof(union bus_header_s)) {
    LOG("Failed to read header from socket %d, expected %zu, got %d", fd,
        sizeof(union bus_header_s), read_size);
    goto end;
  }

  if (msg->magic == UDS_MESSAGE_MAGIC_INTERNAL) {
    msg->header.internal.dest_fd = ntohl(msg->header.internal.dest_fd);
    msg->header.internal.src_fd = ntohl(msg->header.internal.src_fd);
  }

  read_size = recv(fd, &msg->data_size, sizeof(msg->data_size), 0);
  if (read_size != sizeof(msg->data_size)) {
    LOG("Failed to read data_size from socket %d", fd);
    goto end;
  }
  msg->data_size = ntohl(msg->data_size);
  LOG("Read %d bytes from socket %d, msg_Type %d ", msg->data_size, fd,
      msg->header.external.msg_type);

  if (msg->data_size > 0) {
    msg->msg_data = malloc(msg->data_size);
    if (msg->msg_data == NULL) {
      LOG("Failed to malloc msg_data of size %d\n", msg->data_size);
      goto end;
    }

    while (total_received < msg->data_size) {
      int remaining = msg->data_size - total_received;
      read_size =
          recv(fd, (uint8_t *)msg->msg_data + total_received, remaining, 0);
      if (read_size <= 0) {
        LOG("Failed to read message data from socket %d, error: %s", fd,
            strerror(errno));
        free(msg->msg_data);
        msg->msg_data = NULL;
        goto end;
      }
      total_received += read_size;
    }
  } else {
    msg->msg_data = NULL;
  }

  if (msg->data_size != total_received) {
    LOG("Failed to read message data from socket %d, expected %d, got %d", fd,
        msg->data_size, total_received);
    goto end;
  }

  ret = msg;

end:
  if (ret == NULL) {
    if (msg != NULL) {
      bus_message_free(msg);
      msg = NULL;
    }
  }
  return msg;
}

static int dispatch_message_to_external(int fd, struct bus_message_s *msg) {
  int ret = -1;
  uint8_t call_type = find_call_type_fd(fd);
  struct bus_message_s *resp = NULL;

  if (call_type >= 0 && call_type < UDS_CALL_TYPE_MAX) {
    resp = bus_message_new_external_response(call_type, msg->msg_data,
                                             msg->data_size);
    if (resp != NULL) {
      send_bus_message(msg->header.internal.dest_fd, resp);
      ret = 0;
    }
  } else {
    LOG("No call type found for fd %d", fd);
  }
  LOG("dispatch_message_to_external %d ===> %d %d", fd,
      msg->header.internal.dest_fd, ret);

  if (resp != NULL) {
    bus_message_free(resp);
  }
  return ret;
}

static int dispatch_message_to_internal(int fd, struct bus_message_s *msg) {
  int ret = -1;
  call fun = find_call_func_by_call_type(msg->header.external.call_type);
  int dst_fd = find_fd_by_call_type(msg->header.external.call_type);
  int src_fd = fd;
  struct bus_message_s *req = NULL;

  if (fun == NULL) {
    LOG("No function found for call_type %d", msg->header.external.call_type);
    ret = -1;
    goto end;
  }

  if (dst_fd == -1) {  // not registered
    fun(src_fd, msg->data_size, msg->msg_data);
    ret = 0;
  } else {
    req =
        bus_message_new_internal(src_fd, dst_fd, msg->msg_data, msg->data_size);
    if (req != NULL) {
      send_bus_message(dst_fd, req);  // Send to destination fd, not source
      ret = 0;
    }
  }
  LOG("dispatch_message_to_internal %d ===> %d %d", fd, dst_fd, ret);
end:
  if (req) {
    bus_message_free(req);
  }
  return ret;
}

static int send_message(int fd, uint8_t *data, uint32_t data_size) {
  int ret = -1;
  int total_sent = 0;

  while (total_sent < data_size) {
    int remaining = data_size - total_sent;
    ret = send(fd, data + total_sent, remaining, 0);
    if (ret < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;  // Try again
      } else {
        LOG("Send failed to socket %d: %s", fd, strerror(errno));
        return -1;
      }
    }
    total_sent += ret;
  }
  LOG("Sent %d bytes to socket %d", total_sent, fd);
  return total_sent;
}

static int uds_epoll_server_create() {
  int epollfd = 0;
  epollfd =
      epoll_create1(0);  // Use epoll_create1 instead of deprecated epoll_create
  if (epollfd < 0) {
    LOG("Epoll create error %s", strerror(errno));
    epollfd = -1;
  }
  return epollfd;
}

int uds_add_epoll(struct uds_server_s *info, int clientfd, uint32_t events) {
  struct epoll_event event = {0};
  int ret = -1;

  if (info->epoll_fd < 0) {
    LOG("Epoll not create");
    goto end;
  }

  if (set_nonblocking(clientfd) < 0) {
    LOG("Failed to set socket %d to nonblocking", clientfd);
    goto end;
  }

  event.events = events;
  event.data.fd = clientfd;

  if (epoll_ctl(info->epoll_fd, EPOLL_CTL_ADD, clientfd, &event) != 0) {
    LOG("Failed to add socket %d to epoll: %s", clientfd, strerror(errno));
    goto end;
  }
  ret = 0;
  LOG("Added noblock socket %d to epoll", clientfd);
end:
  return ret;
}

void uds_del_epoll(struct uds_server_s *info, struct epoll_event *event) {
  del_call_type_fd(event->data.fd);
  epoll_ctl(info->epoll_fd, EPOLL_CTL_DEL, event->data.fd, NULL);
  close(event->data.fd);
}

int uds_server_create(struct uds_server_s *info, const char *uds_path,
                      int port) {
  int uds_sockfd = -1;
  int tcp_server_sockfd = -1;
  int epollfd = -1;
  int ret = -1;
  struct sockaddr_un udsaddr = {0};
  struct sockaddr_in tcpaddr = {0};

  if (info == NULL || uds_path == NULL) {
    LOG("Invalid parameters");
    goto end;
  }

  // Initialize struct
  info->epoll_fd = -1;
  info->tcp_server_sockfd = -1;
  info->uds_server_sockfd = -1;

  {
    uds_sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (uds_sockfd < 0) {
      LOG("UDS socket create error %s", strerror(errno));
      goto end;
    }

    if (set_nonblocking(uds_sockfd) != 0) {
      LOG("UDS socket set nonblocking error %s", strerror(errno));
      goto err;
    }
    unlink(uds_path);
    udsaddr.sun_family = AF_UNIX;
    strncpy(udsaddr.sun_path, uds_path, sizeof(udsaddr.sun_path) - 1);
    if (bind(uds_sockfd, (struct sockaddr *)&udsaddr, sizeof(udsaddr)) != 0) {
      LOG("UDS socket bind error %s", strerror(errno));
      goto err;
    }

    if (listen(uds_sockfd, LISTEN_BACKLOG) != 0) {
      LOG("UDS socket listen error %s", strerror(errno));
      unlink(uds_path);
      goto err;
    }
  }
  {
    tcp_server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_server_sockfd < 0) {
      LOG("Failed to create tcp server: %s", strerror(errno));
      goto err;
    }

    if (set_nonblocking(tcp_server_sockfd) != 0) {
      LOG("Failed to set tcp server socket nonblocking");
      goto err;
    }

    // Enable socket reuse
    int opt = 1;
    setsockopt(tcp_server_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    tcpaddr.sin_family = AF_INET;
    tcpaddr.sin_addr.s_addr =
        INADDR_ANY;  // Use INADDR_ANY instead of htonl(INADDR_ANY)
    tcpaddr.sin_port = htons(port);

    if (bind(tcp_server_sockfd, (struct sockaddr *)&tcpaddr, sizeof(tcpaddr)) !=
        0) {
      LOG("Failed to bind tcp server socket: %s", strerror(errno));
      goto err;
    }

    if (listen(tcp_server_sockfd, LISTEN_BACKLOG) != 0) {
      LOG("Failed to listen tcp server socket: %s", strerror(errno));
      goto err;
    }
  }
  epollfd = uds_epoll_server_create();
  if (epollfd < 0) {
    LOG("Failed to UDS epoll create");
    goto err;
  }

  info->epoll_fd = epollfd;
  info->tcp_server_sockfd = tcp_server_sockfd;
  info->uds_server_sockfd = uds_sockfd;
  if (uds_add_epoll(info, uds_sockfd, EPOLLIN | EPOLLET | EPOLLRDHUP) != 0) {
    LOG("Failed to add server socket %d to epoll event", uds_sockfd);
    goto err;
  }

  if (uds_add_epoll(info, tcp_server_sockfd, EPOLLIN | EPOLLET | EPOLLRDHUP) !=
      0) {
    LOG("Failed to add tcp server socket %d to epoll event", tcp_server_sockfd);
    goto err;
  }

  LOG("UDS server fd %d, tcp server fd %d created", uds_sockfd,
      tcp_server_sockfd);
  ret = 0;
  goto end;
err:
  if (uds_sockfd != -1) {
    close(uds_sockfd);
  }
  if (epollfd != -1) {
    close(epollfd);
  }
  if (tcp_server_sockfd != -1) {
    close(tcp_server_sockfd);
  }
  if (uds_path) {
    unlink(uds_path);
  }
end:
  return ret;
}

int uds_client_create(const char *uds_path) {
  int sockfd = -1;
  struct sockaddr_un addr = {0};

  if (uds_path == NULL) {
    LOG("UDS path is NULL");
    goto end;
  }

  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd < 0) {
    LOG("UDS client socket create error %s", strerror(errno));
    goto end;
  }

  if (set_nonblocking(sockfd) != 0) {
    LOG("UDS client socket set nonblocking error %s", strerror(errno));
    goto err;
  }

  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, uds_path, sizeof(addr.sun_path) - 1);
  if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    if (errno != EINPROGRESS) {
      LOG("UDS client connect error %s", strerror(errno));
      goto err;
    }
  }

  LOG("UDS client connected to %s with fd %d", uds_path, sockfd);

  usleep(10000);

  goto end;
err:
  if (sockfd != -1) {
    close(sockfd);
    sockfd = -1;
  }
end:
  return sockfd;
}

int uds_event_loop(struct uds_server_s *server) {
  struct epoll_event events[MAX_EVENTS] = {0};

  if (server == NULL) {
    LOG("Server info is NULL");
    return -1;
  }

  while (1) {
    int nfds = epoll_wait(server->epoll_fd, events, MAX_EVENTS, -1);
    if (nfds < 0) {
      LOG("Epoll wait error %s", strerror(errno));
      break;
    }

    for (int i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == server->uds_server_sockfd && events[i].events & EPOLLIN) {
        int clientfd = accept(server->uds_server_sockfd, NULL, NULL);
        if (clientfd < 0) {
          LOG("Accept error %s", strerror(errno));
          continue;
        }
        LOG("New client connected, socket fd %d", clientfd);
        if (uds_add_epoll(server, clientfd, EPOLLIN | EPOLLET | EPOLLRDHUP) !=
            0) {
          LOG("Failed to add UDS client socket %d to epoll event", clientfd);
          close(clientfd);
        }

      } else if (fd == server->tcp_server_sockfd &&
                 events[i].events & EPOLLIN) {
        int clientfd = accept(server->tcp_server_sockfd, NULL, NULL);
        if (clientfd < 0) {
          LOG("Accept error %s", strerror(errno));
          continue;
        }
        LOG("New TCP client connected, socket fd %d", clientfd);
        if (uds_add_epoll(server, clientfd, EPOLLIN | EPOLLET | EPOLLRDHUP) !=
            0) {
          LOG("Failed to add TCP client socket %d to epoll event", clientfd);
          close(clientfd);
        }
      } else {
        LOG("Receive message from %d", fd);

        if (events[i].events & EPOLLRDHUP) {
          uds_del_epoll(server, &events[i]);
          LOG("Client %d disconnected", fd);
          continue;
        } else if (events[i].events & EPOLLIN) {
          struct bus_message_s *msg = bus_message_get(fd);
          if (msg == NULL) {
            LOG("Failed to receive message from socket %d", fd);
            continue;
          }
          LOG("Receive message from %d, size %d , data %s ", fd,
              PACKET_LEN(msg), msg->msg_data);

          if (msg->magic == UDS_MESSAGE_MAGIC_INTERNAL) {
            dispatch_message_to_external(fd, msg);
          } else if (msg->magic == UDS_MESSAGE_MAGIC_EXTERNAL) {
            if (msg->header.external.msg_type == UDS_MESSAGE_TYPE_REGISTER) {
              add_call_type_fd(fd, msg->header.external.call_type);
            } else if (msg->header.external.msg_type ==
                       UDS_MESSAGE_TYPE_CALL_REQ) {
              dispatch_message_to_internal(fd, msg);
            } else {
              LOG("message type %d, not supported",
                  msg->header.external.msg_type);
            }
          }
          bus_message_free(msg);
        }
      }
    }
  }
  return 0;
}

void bus_message_free(struct bus_message_s *msg) {
  if (msg == NULL) {
    return;
  }
  if (msg->msg_data != NULL) {
    free(msg->msg_data);
  }
  free(msg);
}

void uds_close(struct uds_server_s *info) {
  if (info == NULL) {
    return;
  }
  if (info->uds_server_sockfd != -1) {
    close(info->uds_server_sockfd);
    info->uds_server_sockfd = -1;
  }
  if (info->tcp_server_sockfd != -1) {
    close(info->tcp_server_sockfd);
    info->tcp_server_sockfd = -1;
  }
  if (info->epoll_fd != -1) {
    close(info->epoll_fd);
    info->epoll_fd = -1;
  }
}

struct bus_message_s *bus_message_new_external_request(uint8_t call_type,
                                                       uint8_t *data,
                                                       uint32_t data_size) {
  struct bus_message_s *msg = NULL;

  if (call_type >= UDS_CALL_TYPE_MAX) {
    LOG("Invalid call_type %d", call_type);
    return NULL;
  }

  msg = malloc(sizeof(struct bus_message_s));
  if (msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_EXTERNAL;
  msg->header.external.call_type = call_type;
  msg->data_size = data_size;
  msg->header.external.msg_type = UDS_MESSAGE_TYPE_CALL_REQ;

  if (data_size > 0 && data != NULL) {
    msg->msg_data = malloc(data_size);
    if (msg->msg_data == NULL) {
      LOG("malloc failed\n");
      goto end;
    }
    memcpy(msg->msg_data, data, data_size);
  } else {
    msg->msg_data = NULL;
  }
end:
  return msg;
}

struct bus_message_s *bus_message_new_external_response(uint8_t call_type,
                                                        uint8_t *data,
                                                        uint32_t data_size) {
  struct bus_message_s *msg = NULL;

  if (call_type >= UDS_CALL_TYPE_MAX) {
    LOG("Invalid call_type %d", call_type);
    return NULL;
  }

  msg = malloc(sizeof(struct bus_message_s));
  if (msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_EXTERNAL;
  msg->header.external.call_type = call_type;
  msg->data_size = data_size;
  msg->header.external.msg_type = UDS_MESSAGE_TYPE_CALL_RESP;

  if (data_size > 0 && data != NULL) {
    msg->msg_data = malloc(data_size);
    if (msg->msg_data == NULL) {
      LOG("malloc failed\n");
      goto end;
    }
    memcpy(msg->msg_data, data, data_size);
  } else {
    msg->msg_data = NULL;
  }
end:
  return msg;
}

struct bus_message_s *bus_message_new_internal(int src_fd, int dst_fd,
                                               uint8_t *data,
                                               uint32_t data_size) {
  struct bus_message_s *msg = NULL;

  msg = malloc(sizeof(struct bus_message_s));
  if (msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_INTERNAL;
  msg->header.internal.src_fd = htonl(src_fd);
  msg->header.internal.dest_fd = htonl(dst_fd);
  msg->data_size = data_size;

  if (data_size > 0 && data != NULL) {
    msg->msg_data = malloc(data_size);
    if (msg->msg_data == NULL) {
      LOG("malloc failed\n");
      goto end;
    }
    memcpy(msg->msg_data, data, data_size);
  } else {
    msg->msg_data = NULL;
  }
end:
  return msg;
}

static uint8_t *format_bus_message(struct bus_message_s *msg,
                                   uint32_t *data_size) {
  uint8_t *data = NULL;
  uint32_t datalen = 0;

  if (msg == NULL) {
    LOG("Message is NULL");
    return NULL;
  }

  data = malloc(PACKET_LEN(msg));
  if (data != NULL) {
    size_t offset = 0;
    memcpy(data + offset, &msg->magic, sizeof(uint8_t));
    offset += sizeof(uint8_t);

    union bus_header_s header_copy = msg->header;
    if (msg->magic == UDS_MESSAGE_MAGIC_INTERNAL) {
      header_copy.internal.src_fd = htonl(header_copy.internal.src_fd);
      header_copy.internal.dest_fd = htonl(header_copy.internal.dest_fd);
    }
    memcpy(data + offset, &header_copy, sizeof(union bus_header_s));
    offset += sizeof(union bus_header_s);

    datalen = htonl(msg->data_size);
    memcpy(data + offset, &datalen, sizeof(uint32_t));
    offset += sizeof(uint32_t);

    if (msg->data_size != 0 && msg->msg_data != NULL) {
      memcpy(data + offset, msg->msg_data, msg->data_size);
    }
  }
  if (data_size != NULL) {
    *data_size = PACKET_LEN(msg);
  }
  return data;
}

struct bus_message_s *bus_message_new_from_register(uint8_t call_type) {
  struct bus_message_s *msg = NULL;

  if (call_type >= UDS_CALL_TYPE_MAX) {
    LOG("Invalid call_type %d", call_type);
    return NULL;
  }

  msg = malloc(sizeof(struct bus_message_s));
  if (msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_EXTERNAL;
  msg->header.external.call_type = call_type;
  msg->header.external.msg_type = UDS_MESSAGE_TYPE_REGISTER;
  msg->data_size = 0;
  msg->msg_data = NULL;
end:
  return msg;
}

struct bus_message_s *recv_bus_message(int fd, int timeout) {
  struct bus_message_s *msg = NULL;
  struct pollfd pfd = {.fd = fd, .events = POLLIN};

  if (poll(&pfd, 1, timeout) <= 0) {
    LOG("socket %d is not ready or timeout", fd);
    goto end;
  }

  if (pfd.revents & POLLIN) {
    msg = bus_message_get(fd);
  }

end:
  return msg;
}
int send_bus_message(int fd, struct bus_message_s *msg) {
  uint8_t *buffer = NULL;
  uint32_t data_size = 0;
  int ret = -1;

  if (msg == NULL) {
    LOG("msg is null");
    goto end;
  }

  buffer = format_bus_message(msg, &data_size);
  if (buffer == NULL) {
    LOG("format_bus_message failed");
    goto end;
  }
  ret = send_message(fd, buffer, data_size);

end:
  if (buffer != NULL) {
    free(buffer);
  }
  return ret;
}

int tcp_client_create(const char *server_ip, int server_port) {
  int clientfd = -1;
  struct sockaddr_in server_addr = {0};
  int ret = -1;

  if (server_ip == NULL) {
    LOG("Server IP is NULL");
    goto err;
  }

  clientfd = socket(AF_INET, SOCK_STREAM, 0);
  if (clientfd < 0) {
    LOG("Failed to create tcp client socket: %s", strerror(errno));
    goto err;
  }

  if (set_nonblocking(clientfd) != 0) {
    LOG("Failed to set tcp client socket nonblocking");
    goto err;
  }

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port);
  if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
    LOG("Invalid server IP: %s, err: %s", server_ip, strerror(errno));
    goto err;
  }

  if ((connect(clientfd, (struct sockaddr *)&server_addr,
               sizeof(server_addr))) != 0 &&
      errno != EINPROGRESS) {
    LOG("Failed to connect to server %s:%d: %s", server_ip, server_port,
        strerror(errno));
    goto err;
  }

  LOG("TCP client connected to %s:%d with fd %d", server_ip, server_port,
      clientfd);
  ret = clientfd;
err:
  if (ret == -1) {
    if (clientfd != -1) {
      close(clientfd);
      clientfd = -1;
    }
  }
  return ret;
}
