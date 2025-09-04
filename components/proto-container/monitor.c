
#include <microkit.h>
#include <stdarg.h>
#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <sddf/util/printf.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;


serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    assert(timer_config_check_magic(&timer_config));

    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);

    microkit_dbg_puts("Hello from monitor\n");
    sddf_printf("Test serial driver\n");
#if 0
/* to use printf, we need stdout as an FD (1) */
// no plan for a VFS...
// can use seL4_libs instead
// -- sel4muslsys
// ...
    printf(">>>\n");
#endif
}

void notified(microkit_channel ch)
{
    
    ;
}