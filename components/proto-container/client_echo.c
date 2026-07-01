#include <microkit.h>

#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/fs/config.h>

#include <sddf/util/printf.h>
#include <pc_config.h>
#include <protocon.h>
#include <libtrustedlo.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;


serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

// interface per client payload
__attribute__((__section__(".pc_svc_desc"))) const protocon_svc_desc_t ciface = {
    /* numbers of each interface type */
    .t3_num = 1,
    /* type identifiers */
    .type3 = SERIAL_IFACE,
    /* pointer array of each interface type */
    .t3_iface = { (uintptr_t)&serial_config, 0, 0, 0, 0, 0, 0, 0 }
};

typedef struct {
    uint64_t _unused;
    bool avail;
} neighbours_t;

neighbours_t neighbours[PC_CHILD_PER_MONITOR_MAX_NUM];


void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        neighbours[i].avail = false;
    }

    sddf_printf("Hello from client_echo!\n");
    // sddf_printf("Ready to invoke monitor call 'terminate' to exit\n");
    // sddf_printf("(Terminated) Ready to be intercepted/restored by monitor\n");

    // // exit from client...
    // microkit_mr_set(0, 0x100);
    microkit_mr_set(0, 10);

    microkit_msginfo info = microkit_ppcall(15, microkit_msginfo_new(0, 1));
    seL4_Error error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }

    seL4_Word bitmap = seL4_GetMR(0);
    seL4_Word self_id = seL4_GetMR(1);
    // sddf_printf("Monitor returned bitmap: 0x%llx\n", (unsigned long long)bitmap);

    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if (bitmap & (1ULL << i) && i != self_id) {
            neighbours[i].avail = true;
            sddf_printf("Dynamic PD with ID %d is available\n", i);
        }
    }
}

void notified(microkit_channel ch)
{
    ;
}
