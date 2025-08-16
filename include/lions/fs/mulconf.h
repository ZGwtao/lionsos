
#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>

/* mutiplxer as a server to app clients */
typedef struct fs_mul_server_config {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    uint16_t queue_len;
    uint8_t id;
} fs_mul_server_config_t;

/* multiplexer as a client to fs server */
typedef struct fs_mul_client_config {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    region_resource_t pathname_share;
    uint16_t queue_len;
    uint8_t id;
} fs_mul_client_config_t;

typedef struct fs_server_client_config {
    region_resource_t pathname_share;
    region_resource_t share;
    uint16_t queue_len;
} fs_server_client_config_t;

typedef struct fs_mul_client_channs {
    fs_mul_client_config_t multiplexer;
    fs_server_client_config_t server;
} fs_mul_client_channs_t;
