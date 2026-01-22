#ifndef __UDS_BUS_H__
#define __UDS_BUS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define LISTEN_BACKLOG 256
#define MAX_EVENTS 1024
#define MAX_IMAGE_COUNT 128
#define PATH_LEN 256

#define UDS_PATH "/data/local/tmp/sjtuai.sock"
#define TCP_PORT 8080

#define LOG(fmt, ...) \
  printf("[%s] %d : " fmt "\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);

enum uds_message_type_e {
  UDS_MESSAGE_TYPE_REGISTER = 0,
  UDS_MESSAGE_TYPE_CALL_REQ = 1,
  UDS_MESSAGE_TYPE_CALL_RESP = 2,
  UDS_MESSAGE_TYPE_MAX,
};

// 添加其它算法调用，需要在这新增
enum uds_call_type_e {
  UDS_CALL_TYPE_CLIP = 0,
  UDS_CALL_TYPE_MAX,
};

enum uds_message_magic_e {
  UDS_MESSAGE_MAGIC_EXTERNAL = 0x01,
  UDS_MESSAGE_MAGIC_INTERNAL = 0x02,
};

/**
 * @brief 消息框架：mgic + header + data
 *
 */
/**
 * @brief 外部调用消息头
 *
 */
struct external_header_s {
  uint8_t msg_type;
  uint8_t call_type;
} __attribute__((packed));

/**
 * @brief 内部调用消息头
 *
 */
struct internal_header_s {
  uint32_t src_fd;
  uint32_t dest_fd;
} __attribute__((packed));

union bus_header_s {
  struct external_header_s external;
  struct internal_header_s internal;
} __attribute__((packed));

struct bus_message_s {
  uint8_t magic;
  union bus_header_s header;
  uint32_t data_size;
  uint8_t *msg_data;
} __attribute__((packed));

struct uds_server_s {
  int uds_server_sockfd;
  int tcp_server_sockfd;
  int epoll_fd;
};

#define PACKET_LEN(msg)                                               \
  (sizeof(union bus_header_s) + msg->data_size + sizeof(msg->magic) + \
   sizeof(msg->data_size))

int uds_server_create(struct uds_server_s *info, const char *uds_path,
                      int port);
int uds_client_create(const char *uds_path);
int tcp_client_create(const char *ip, int port);
int uds_add_epoll(struct uds_server_s *info, int clientfd, uint32_t events);
void uds_close(struct uds_server_s *info);
int uds_event_loop(struct uds_server_s *server);

void bus_message_free(struct bus_message_s *msg);

struct bus_message_s *bus_message_new_external_request(uint8_t call_type,
                                                       uint8_t *data,
                                                       uint32_t data_size);
struct bus_message_s *bus_message_new_external_response(uint8_t call_type,
                                                        uint8_t *data,
                                                        uint32_t data_size);

struct bus_message_s *bus_message_new_internal(int src_fd, int dst_fd,
                                               uint8_t *data,
                                               uint32_t data_size);
struct bus_message_s *bus_message_new_from_register(uint8_t call_type);

/**
 * @brief 接收消息
 *
 * @param fd
 * @param timeout ms, 0表示无超时, -1表示阻塞, >0表示超时
 * @return struct bus_message_s*

 */
struct bus_message_s *recv_bus_message(int fd, int timeout);
int send_bus_message(int fd, struct bus_message_s *msg);

typedef void *(*call)(int, uint32_t, uint8_t *);
int uds_register_handler(uint8_t call_type, call callback);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif