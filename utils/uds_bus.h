#ifndef __UDS_BUS_H__
#define __UDS_BUS_H__

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/epoll.h>

#define LISTEN_BACKLOG 256
#define MAX_EVENTS 1024
#define MAX_IMAGE_COUNT 20
#define PATH_LEN 256

#define UDS_PATH "/tmp/sjtuai.sock"

#define LOG(fmt, ...) \
    printf("[%s] %d : "fmt"\n", __FUNCTION__, __LINE__, ##__VA_ARGS__);

enum uds_message_type_e {
    UDS_MESSAGE_TYPE_REGISTER = 0,
    UDS_MESSAGE_TYPE_CALL_REQ = 1,
    UDS_MESSAGE_TYPE_CALL_RESP = 2,
    UDS_MESSAGE_TYPE_MAX,
};

//添加其它算法调用，需要在这新增
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
struct external_header_s {
    uint8_t msg_type;
    uint8_t call_type;
} __attribute__((packed));

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

#define PACKET_LEN(msg) \
    (sizeof(union bus_header_s) + msg->data_size + sizeof(msg->magic) + sizeof(msg->data_size))


int uds_server_create(struct uds_server_s *info, const char *uds_path);
int uds_client_create(const char *uds_path);
int uds_add_epoll(struct uds_server_s *info, int clientfd, uint32_t events);
void uds_close(struct uds_server_s *info);
int uds_event_loop(struct uds_server_s *server);

struct bus_message_s *bus_message_get(int fd);
void bus_message_free(struct bus_message_s *msg);

struct bus_message_s *bus_message_new_external_request(uint8_t call_type, uint8_t *data, uint32_t data_size);
struct bus_message_s *bus_message_new_external_response(uint8_t call_type, uint8_t *data, uint32_t data_size);

struct bus_message_s *bus_message_new_internal(int src_fd, int dst_fd, uint8_t *data, uint32_t data_size);
struct bus_message_s *bus_message_new_from_register(uint8_t call_type);

uint8_t *format_bus_message(struct bus_message_s *msg, uint32_t *size);

int send_message(int fd, uint8_t *data, uint32_t data_size);

#endif