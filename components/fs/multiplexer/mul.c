
#include <assert.h>
#include <microkit.h>
#include <libmicrokitco.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include <lions/fs/mulconf.h>
#include <lions/fs/multiplexer.h>

__attribute__((__section__(".fs_mul_server_config"))) fs_mul_server_config_t server_config;
__attribute__((__section__(".fs1_mul_client1_config"))) fs_mul_client_config_t fs_c1_config;
__attribute__((__section__(".fs1_mul_client2_config"))) fs_mul_client_config_t fs_c2_config;

co_control_t co_controller_mem;
microkit_cothread_sem_t sem[MULTIPLEXER_WORKER_TRHEAD_NUM + 1];

uint64_t worker_thread_stack_one = 0xA0000000;
uint64_t worker_thread_stack_two = 0xB0000000;

typedef struct {
    fs_queue_t *command_queue;
    fs_queue_t *completion_queue;
    char *pathname_share;
    uint8_t channel;
} cstub_t;
/* client stubs for channels */
cstub_t cstubs[MULTIPLEXER_CLIENT_NUM];

mul_queue_t *fs_server_command_queue;
mul_queue_t *fs_server_completion_queue;

void init(void)
{
    fs_server_command_queue = server_config.command_queue.vaddr;
    fs_server_completion_queue = server_config.completion_queue.vaddr;

    cstubs[0].command_queue = fs_c1_config.command_queue.vaddr;
    cstubs[0].completion_queue = fs_c1_config.completion_queue.vaddr;
    cstubs[0].pathname_share = fs_c1_config.pathname_share.vaddr;
    cstubs[0].channel = fs_c1_config.id;

    cstubs[1].command_queue = fs_c2_config.command_queue.vaddr;
    cstubs[1].completion_queue = fs_c2_config.completion_queue.vaddr;
    cstubs[1].pathname_share = fs_c2_config.pathname_share.vaddr;
    cstubs[1].channel = fs_c2_config.id;

    stack_ptrs_arg_array_t costacks = {
        worker_thread_stack_one,
        worker_thread_stack_two
    };

    microkit_cothread_init(&co_controller_mem, MULTIPLEXER_WORKER_TRHEAD_STACKSIZE, costacks);
    for (uint32_t i = 0; i < (MULTIPLEXER_WORKER_TRHEAD_NUM + 1); i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
}

struct fs_request;

static void handle_server_response(void)
{
    uint64_t server_completion_queue_size;

    /* each client response forwarding state */
    uint32_t fs_response_forwarded[MULTIPLEXER_CLIENT_NUM] = { 0x0 };

    /*
     * How many requests have you sent to the server,
     * how many responses will you receive from the server,
     * the only thing to ensure is reserving enough slot for completition 
     * when forwarding the requests
     */
    server_completion_queue_size = mul_queue_length_producer(fs_server_completion_queue);
    /* forward to responses to the fs client */
    for (uint64_t i = 0; i < server_completion_queue_size; ++i) {
        mul_msg_t server_resp = *mul_queue_idx_filled(fs_server_completion_queue, i);

        /* fetch client id from multiplexer message */
        uint32_t cid = server_resp.cmpl.client;

        /* forward to request to client's completion queue */
        msg_mul2fs_cmpl(&server_resp, fs_queue_idx_empty(cstubs[cid].completion_queue, fs_response_forwarded[cid]));
        fs_response_forwarded[cid]++;
    }
    /* for fs server, all requests are identical */
    if (server_completion_queue_size) {
        mul_queue_publish_consumption(fs_server_completion_queue, server_completion_queue_size);
    }
    for (int32_t i = 0; i < MULTIPLEXER_CLIENT_NUM; ++i) {
        if (fs_response_forwarded[i]) {
            fs_queue_publish_production(cstubs[i].completion_queue, fs_response_forwarded[i]);
            microkit_notify(cstubs[i].channel);
        }
    }
}

static uint32_t get_id_from_ch(microkit_channel ch)
{
    for (uint32_t i = 0; i < MULTIPLEXER_CLIENT_NUM; ++i) {
        if (cstubs[i].channel == ch) {
            return i;
        }
    }
    __builtin_unreachable();
}

void notified(microkit_channel ch)
{
    if (ch == server_config.id) {
        handle_server_response();
        return;
    }

    /* fetch the notification signaller */
    uint32_t cid = get_id_from_ch(ch);

    uint64_t command_queue_size;
    uint64_t completion_queue_size;

    uint32_t fs_request_dequeued = 0;
    uint32_t fs_request_forwarded = 0;
    
    command_queue_size = fs_queue_length_consumer(cstubs[cid].command_queue);
    completion_queue_size = fs_queue_length_producer(cstubs[cid].completion_queue);

    // FIXME
    //  how to deal with the unmanaged file system requests?
    //  (should we throw the requests away?)
    while (command_queue_size && completion_queue_size < FS_QUEUE_CAPACITY) {
        /* get a request to forward to the server */
        fs_msg_t client_req = *fs_queue_idx_filled(cstubs[cid].command_queue, fs_request_dequeued);

        fs_request_dequeued++;
        command_queue_size--;

        if (client_req.cmd.type >= FS_NUM_COMMANDS) {
            continue;
        }

        /* forward a request to the server */
        msg_fs2mul_cmd(&client_req, mul_queue_idx_empty(fs_server_command_queue, fs_request_forwarded), cid);
        /* increase available index for forwarding requests */
        fs_request_forwarded++;

        completion_queue_size++;
    }
    if (fs_request_dequeued) {
        fs_queue_publish_consumption(cstubs[cid].command_queue, fs_request_dequeued);
    }
    if (fs_request_forwarded) {
        /* produced new command and forward them to the server */
        mul_queue_publish_production(fs_server_command_queue, fs_request_forwarded);
        microkit_notify(server_config.id);
    }
}