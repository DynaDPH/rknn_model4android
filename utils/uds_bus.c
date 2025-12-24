#include "uds_bus.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

typedef void *(*call)(int, uint32_t, void *);
struct call_type_to_fd_s {
  enum uds_call_type_e call_type;
  int fd;
  call call_func;
};

static void *clip_call(int src_fd, uint32_t data_size,
                       void *msg_data) {
  LOG("Received clip call from socket %d", src_fd);
  return NULL;
}

static struct call_type_to_fd_s call_type_to_fd[UDS_CALL_TYPE_MAX] = { //添加其它算法调用，需要在这新增
    // 映射 关系
    {.call_type = UDS_CALL_TYPE_CLIP, .fd = -1, .call_func = clip_call},
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

static int add_call_type_fd(int fd, uint8_t call_type) {
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

static int find_fd_call_type(int fd) {
  for (int i = 0; i < UDS_CALL_TYPE_MAX; i++) {
    if (call_type_to_fd[i].fd == fd) {
      return call_type_to_fd[i].call_type;
    }
  }
  return -1;
}
static call find_call_func_by_call_type(uint8_t call_type) {
  if(call_type >= UDS_CALL_TYPE_MAX || call_type < 0) {
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
  }
  return 0;
}

struct bus_message_s *bus_message_get(int fd) {
  #define BUFF_SIZE 1024
  uint8_t buffer[BUFF_SIZE] = {0};
  // uint8_t read_magic = 0;
  int read_size = 0;
  struct bus_message_s *msg = NULL;

  msg =  malloc(sizeof(struct bus_message_s));
  if (msg == NULL) {
    LOG("Failed to malloc bus_message_s\n");
    goto end;
  }

  read_size = recv(fd, &msg->magic, sizeof(uint8_t), 0);
  if (read_size <= 0) {
    LOG("Failed to read magic from socket %d", fd);
    goto err;
  }

  LOG("Read magic %d from socket %d", msg->magic, fd);
  
  read_size = recv(fd, &msg->header, sizeof(union bus_header_s), 0);
  if(read_size != sizeof(union bus_header_s)) {
    LOG("Failed to read header from socket %d", fd);
    goto err;
  }

  if(msg->magic == UDS_MESSAGE_MAGIC_INTERNAL) {
    msg->header.internal.dest_fd = ntohl(msg->header.internal.dest_fd);
    msg->header.internal.src_fd = ntohl(msg->header.internal.src_fd);
  }

  recv(fd, &msg->data_size, sizeof(msg->data_size), 0);
  msg->data_size = ntohl(msg->data_size);
  LOG("Read %d bytes from socket %d, msg_Type %d ", msg->data_size, fd, msg->header.external.msg_type);

  msg->msg_data = malloc(msg->data_size);
  if (msg->msg_data == NULL) {
    LOG("Failed to malloc msg_data\n");
    goto err;
  }
 
  recv(fd, msg->msg_data, msg->data_size, 0);
  goto end;
err:
  if(msg != NULL) {
    bus_message_free(msg);
    msg = NULL;
  }
end:
  return msg;
}

static int dispatch_message_to_external(int fd, struct bus_message_s *msg) { 
  int ret = -1;
  uint8_t *buff = NULL;
  uint8_t call_type = find_call_type_fd(fd);
  struct bus_message_s *resp = NULL;
  uint32_t size = 0;

  if(call_type < UDS_CALL_TYPE_MAX && call_type >= 0) {
    resp = bus_message_new_external_response(call_type, msg->msg_data, msg->data_size);
    if(resp != NULL) {
      buff= format_bus_message(resp, &size);
      send_message(msg->header.internal.dest_fd, buff, size);
      ret = 0;
    }
  }
  LOG("dispatch_message_to_external %d ===> %d %d", fd, msg->header.internal.dest_fd, ret);
end:
  if(resp != NULL) {
    bus_message_free(resp);
  }
  if(buff != NULL) {
    free(buff);
  }
  return ret;
}

static int dispatch_message_to_internal(int fd, struct bus_message_s *msg) {
    int ret = -1;
    uint8_t *buff = NULL;
    call fun = find_call_func_by_call_type(msg->header.external.call_type);
    int dst_fd = find_fd_call_type(msg->header.external.call_type);
    int src_fd = fd;
    struct bus_message_s *req = NULL;
    uint32_t size = 0;

    if(dst_fd == -1) { // not registered
      fun(src_fd, msg->data_size, msg->msg_data);
      ret = 0;
    } else {
      req = bus_message_new_internal(src_fd, dst_fd, msg->msg_data, msg->data_size);
      if(req != NULL) {
        buff = format_bus_message(req, &size);
        send_message(dst_fd, buff, size);
        ret = 0;
      }
    }
end:
    if(buff != NULL) {
      free(buff);
    }
    if(req) {
      bus_message_free(req);
    }
    return ret;
}

int send_message(int fd, uint8_t *data, uint32_t data_size) {

  int ret = -1;
  ret = send(fd, data, data_size, 0);
  if (ret < 0) {
  }
  LOG("Sent %d bytes to socket %d", ret, fd);
  return ret;
}

static int uds_epoll_server_create() {
  int epollfd = 0;
  epollfd = epoll_create(1);
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

  event.events = events;
  event.data.fd = clientfd;

  if (epoll_ctl(info->epoll_fd, EPOLL_CTL_ADD, clientfd, &event) != 0) {
    LOG("Failed to add socket %d to epoll", clientfd);
    goto end;
  }
  ret = 0;
  LOG("Added socket %d to epoll", clientfd);
end:
  return ret;
}

void uds_del_epoll(struct uds_server_s *info, struct epoll_event *event) {
  del_call_type_fd(event->data.fd);
  epoll_ctl(info->epoll_fd, EPOLL_CTL_DEL, event->data.fd, NULL);
  close(event->data.fd);
}

int uds_server_create(struct uds_server_s *info, const char *uds_path) {
  int sockfd = -1;
  int epollfd = -1;
  int ret = -1;
  struct sockaddr_un addr = {0};

  if (uds_path == NULL) {
    LOG("UDS path is NULL");
    goto end;
  }

  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd < 0) {
    LOG("UDS socket create error %s", strerror(errno));
    goto end;
  }

  if (set_nonblocking(sockfd) != 0) {
    LOG("UDS socket set nonblocking error %s", strerror(errno));
    goto err;
  }
  /**
   * TCP Server Socket not implemented yet
   */
  unlink(uds_path);
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, uds_path, sizeof(addr.sun_path));
  if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    LOG("UDS socket binnd errr %s", strerror(errno));
    goto err;
  }

  if (listen(sockfd, LISTEN_BACKLOG) != 0) {
    unlink(uds_path);
    goto err;
  }

  epollfd = uds_epoll_server_create();
  if (epollfd < 0) {
    LOG("Failed to UDS epoll create");
    goto err;
  }

  info->epoll_fd = epollfd;
  info->uds_server_sockfd = sockfd;
  if (uds_add_epoll(info, sockfd,
                    EPOLLIN | EPOLLET | EPOLLRDHUP) != 0) {
    LOG("Failed to add server socket %d to epoll event", sockfd);
  }
  LOG("UDS server created with fd %d", sockfd);
  ret = 0;
  goto end;
err:
  if (sockfd != -1) {
    close(sockfd);
  }
  if (epollfd != -1) {
    close(epollfd);
  }
  unlink(uds_path);
end:
  return ret;
}

int uds_client_create(const char *uds_path) {
  int sockfd = -1;
  struct sockaddr_un addr = {0};

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
  strncpy(addr.sun_path, uds_path, sizeof(addr.sun_path));
  if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    LOG("UDS client connect error %s", strerror(errno));
    goto err;
  }
  LOG("UDS client connected to %s with fd %d", uds_path, sockfd);
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

  while (1) {
    int nfds = epoll_wait(server->epoll_fd, events, MAX_EVENTS, -1);
    if (nfds < 0) {
      LOG("Epoll wait error %s", strerror(errno));
      break;
    }

    for (size_t i = 0; i < nfds; i++) {
      int fd = events[i].data.fd;
      if (fd == server->uds_server_sockfd) {
        int clientfd = accept(server->uds_server_sockfd, NULL, NULL);
        if (clientfd < 0) {
          LOG("Accept error %s", strerror(errno));
          continue;
        }
        LOG("New client connected, socket fd %d", clientfd);
        if (uds_add_epoll(server, clientfd,
                          EPOLLIN | EPOLLET | EPOLLRDHUP) != 0) {
          LOG("Failed to add UDS client socket %d to epoll event", clientfd);
          close(clientfd);
        }
      } else if (fd == server->tcp_server_sockfd) {
        int clientfd = accept(server->tcp_server_sockfd, NULL, NULL);
        if (clientfd < 0) {
          LOG("Accept error %s", strerror(errno));
          continue;
        }
        LOG("New client connected, socket fd %d", clientfd);
        if (uds_add_epoll(server, clientfd,
                          EPOLLIN | EPOLLET | EPOLLRDHUP) != 0) {
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
          if(msg->magic == UDS_MESSAGE_MAGIC_INTERNAL) {
            dispatch_message_to_external(fd, msg);
          } else if(msg->magic == UDS_MESSAGE_MAGIC_EXTERNAL) {
             if(msg->header.external.msg_type == UDS_MESSAGE_TYPE_REGISTER) {
               add_call_type_fd(fd, msg->header.external.call_type);
             } else if(msg->header.external.msg_type == UDS_MESSAGE_TYPE_CALL_REQ) {
               dispatch_message_to_internal(fd, msg);
             } else {
               LOG("message type %d, not supported", msg->header.external.msg_type);
             }
          }
          bus_message_free(msg);
        }
      }
    }
  }
}

void bus_message_free(struct bus_message_s *msg) {
  if (msg == NULL) {
    return;
  }
  if(msg->msg_data != NULL) {
    free(msg->msg_data);
  }
  free(msg);
}

void uds_close(struct uds_server_s *info) {
  if (info->uds_server_sockfd != -1) {
    close(info->uds_server_sockfd);
  }
  if (info->tcp_server_sockfd != -1) {
    close(info->tcp_server_sockfd);
  }
  if (info->epoll_fd != -1) {
    close(info->epoll_fd);
  }
}

struct bus_message_s *bus_message_new_external_request(uint8_t call_type,
                                                   uint8_t *data,
                                                   uint32_t data_size) {
  struct bus_message_s *msg = NULL;

  msg = malloc(sizeof(struct bus_message_s));
  if(msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_EXTERNAL;
  msg->header.external.call_type = call_type;
  msg->data_size = data_size;
  msg->header.external.msg_type = UDS_MESSAGE_TYPE_CALL_REQ;

  if(data_size > 0) {
    msg->msg_data = malloc(data_size);
    if(msg->msg_data == NULL) {
      LOG("malloc failed\n");
      goto end;
    }
    memcpy(msg->msg_data, data, data_size);
  }
end:
  return msg;
}

struct bus_message_s *bus_message_new_external_response(uint8_t call_type, uint8_t *data, uint32_t data_size) {
    struct bus_message_s *msg = NULL;

  msg = malloc(sizeof(struct bus_message_s));
  if(msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_EXTERNAL;
  msg->header.external.call_type = call_type;
  // msg->header.external.data_size = data_size;
  msg->data_size = data_size;
  msg->header.external.msg_type = UDS_MESSAGE_TYPE_CALL_RESP;

  if(data_size > 0) {
    msg->msg_data = malloc(data_size);
    if(msg->msg_data == NULL) {
      LOG("malloc failed\n");
      goto end;
    }
    memcpy(msg->msg_data, data, data_size);
  }
end:
  return msg;
}

struct bus_message_s *bus_message_new_internal(int src_fd, int dst_fd, uint8_t *data, uint32_t data_size) {
    struct bus_message_s *msg = NULL;

  msg = malloc(sizeof(struct bus_message_s));
  if(msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_INTERNAL;
  msg->header.internal.src_fd = src_fd;
  msg->header.internal.dest_fd = dst_fd;
  msg->data_size = data_size;

  if(data_size > 0) {
    msg->msg_data = malloc(data_size);
    if(msg->msg_data == NULL) {
      LOG("malloc failed\n");
      goto end;
    }
    memcpy(msg->msg_data, data, data_size);
  }
end:
  return msg;
}
uint8_t *format_bus_message(struct bus_message_s *msg, uint32_t *data_size) {
  uint8_t *data = NULL;
  uint32_t datalen = 0;

  data = malloc(PACKET_LEN(msg));
    if(data != NULL) {
      memcpy(data, &msg->magic, sizeof(uint8_t));
      if(msg->magic == UDS_MESSAGE_MAGIC_INTERNAL) {
        msg->header.internal.src_fd = htonl(msg->header.internal.src_fd);
        msg->header.internal.dest_fd = htonl(msg->header.internal.dest_fd);
      }
      memcpy(data + sizeof(msg->magic), &msg->header, sizeof(union bus_header_s));
      datalen = htonl(msg->data_size);
      memcpy(data + sizeof(msg->magic) + sizeof(union bus_header_s), &datalen, sizeof(msg->data_size));
      if(msg->data_size != 0) {
        memcpy(data + sizeof(msg->magic) + sizeof(union bus_header_s) + sizeof(msg->data_size), msg->msg_data, msg->data_size);
      }
    }
  *data_size = PACKET_LEN(msg);
  return data;
}

struct bus_message_s *bus_message_new_from_register(uint8_t call_type) {
  struct bus_message_s *msg = NULL;
  msg = malloc(sizeof(struct bus_message_s));
  if(msg == NULL) {
    LOG("malloc failed\n");
    goto end;
  }

  msg->magic = UDS_MESSAGE_MAGIC_EXTERNAL;
  msg->header.external.call_type = call_type;
  msg->header.external.msg_type = UDS_MESSAGE_TYPE_REGISTER;
  msg->data_size = 0;
end:
  return msg;
}