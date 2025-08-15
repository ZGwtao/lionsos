
#include <microkit.h>
#include <stdbool.h>
#include <stdint.h>
#include <sddf/resources/common.h>

/* multiplexer as a client to fs server */
typedef struct fs_mul_client_config {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    uint16_t queue_len;
    uint8_t id;
} fs_mul_client_config_t;

/* mutiplxer as a server to app clients */
typedef struct fs_mul_server_config {
    region_resource_t command_queue;
    region_resource_t completion_queue;
    region_resource_t pathname_share;
    uint16_t queue_len;
    uint8_t id;
} fs_mul_server_config_t;