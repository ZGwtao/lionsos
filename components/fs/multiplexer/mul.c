
#include <microkit.h>
#include <libmicrokitco.h>
#include <lions/fs/protocol.h>
#include <lions/fs/config.h>
#include <lions/fs/mul.h>

__attribute__((__section__(".fs_mul_server_config"))) fs_mul_server_config_t server_config;


co_control_t co_controller_mem;
microkit_cothread_sem_t sem[MULTIPLEXER_WORKER_TRHEAD_NUM + 1];

uint64_t worker_thread_stack_one = 0xA0000000;
uint64_t worker_thread_stack_two = 0xB0000000;


fs_queue_t *fs_server_command_queue;
fs_queue_t *fs_server_completion_queue;

void init(void)
{
    fs_server_command_queue = server_config.command_queue.vaddr;
    fs_server_completion_queue = server_config.completion_queue.vaddr;

    stack_ptrs_arg_array_t costacks = {
        worker_thread_stack_one,
        worker_thread_stack_two
    };

    microkit_cothread_init(&co_controller_mem, MULTIPLEXER_WORKER_TRHEAD_STACKSIZE, costacks);
    for (uint32_t i = 0; i < (MULTIPLEXER_WORKER_TRHEAD_NUM + 1); i++) {
        microkit_cothread_semaphore_init(&sem[i]);
    }
}


void notified(microkit_channel ch)
{
    while (1);    
}