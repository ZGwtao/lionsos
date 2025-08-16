
#include <assert.h>
#include <microkit.h>
#include <libmicrokitco.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include <lions/fs/mul.h>

__attribute__((__section__(".fs_mul_server_config"))) fs_mul_server_config_t server_config;
__attribute__((__section__(".fs1_mul_client_config"))) fs_mul_client_config_t fs1_config;


co_control_t co_controller_mem;
microkit_cothread_sem_t sem[MULTIPLEXER_WORKER_TRHEAD_NUM + 1];

uint64_t worker_thread_stack_one = 0xA0000000;
//uint64_t worker_thread_stack_two = 0xB0000000;

fs_queue_t *fs_client1_command_queue;
fs_queue_t *fs_client1_completion_queue;
char *fs_client1_pathname_share;

fs_queue_t *fs_server_command_queue;
fs_queue_t *fs_server_completion_queue;

void init(void)
{
    fs_server_command_queue = server_config.command_queue.vaddr;
    fs_server_completion_queue = server_config.completion_queue.vaddr;

    fs_client1_command_queue = fs1_config.command_queue.vaddr;
    fs_client1_completion_queue = fs1_config.completion_queue.vaddr;
    fs_client1_pathname_share = fs1_config.pathname_share.vaddr;

    stack_ptrs_arg_array_t costacks = {
        worker_thread_stack_one
        //worker_thread_stack_one,
        //worker_thread_stack_two
    };

    microkit_cothread_init(&co_controller_mem, MULTIPLEXER_WORKER_TRHEAD_STACKSIZE, costacks);
    for (uint32_t i = 0; i < (MULTIPLEXER_WORKER_TRHEAD_NUM + 1); i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
}

struct fs_request;

void notified(microkit_channel ch)
{
    uint64_t command_queue_size;
    uint64_t completion_queue_size;

    uint64_t server_completion_queue_size;

    uint32_t fs_request_dequeued = 0;

    uint32_t fs_request_forwarded = 0;
    uint32_t fs_response_forwarded = 0;

    if (ch == fs1_config.id) {
    
        command_queue_size = fs_queue_length_consumer(fs_client1_command_queue);
        completion_queue_size = fs_queue_length_producer(fs_client1_completion_queue);

        // FIXME
        //  how to deal with the unmanaged file system requests?
        //  (should we throw the requests away?)
        while (command_queue_size && completion_queue_size < FS_QUEUE_CAPACITY) {
            /* get a request to forward to the server */
            fs_msg_t client_req = *fs_queue_idx_filled(fs_client1_command_queue, fs_request_dequeued);

            fs_request_dequeued++;
            command_queue_size--;

            if (client_req.cmd.type >= FS_NUM_COMMANDS) {
                continue;
            }

            /* forward a request to the server */
            *fs_queue_idx_empty(fs_server_command_queue, fs_request_forwarded) = client_req;
            /* increase available index for forwarding requests */
            fs_request_forwarded++;

            completion_queue_size++;
        }
        if (fs_request_dequeued) {
            fs_queue_publish_consumption(fs_client1_command_queue, fs_request_dequeued);
        }
        if (fs_request_forwarded) {
            /* produced new command and forward them to the server */
            fs_queue_publish_production(fs_server_command_queue, fs_request_forwarded);
            microkit_notify(server_config.id);
        }

        return;
    }

    if (ch == server_config.id) {
        /*
         * How many requests have you sent to the server,
         * how many responses will you receive from the server,
         * the only thing to ensure is reserving enough slot for completition 
         * when forwarding the requests
         */
        server_completion_queue_size = fs_queue_length_producer(fs_server_completion_queue);
        /* forward to responses to the fs client */
        for (uint64_t i = 0; i < server_completion_queue_size; ++i) {
            fs_msg_t server_resp = *fs_queue_idx_filled(fs_server_completion_queue, i);
            /* forward to request to client's completion queue */
            *fs_queue_idx_empty(fs_client1_completion_queue, fs_response_forwarded) = server_resp;
            fs_response_forwarded++;
        }
        if (fs_response_forwarded) {
            fs_queue_publish_consumption(fs_server_completion_queue, fs_response_forwarded);
        }
        if (server_completion_queue_size) {
            fs_queue_publish_production(fs_client1_completion_queue, server_completion_queue_size);
            microkit_notify(fs1_config.id);
        }

        return;
    }

    assert(0);
}