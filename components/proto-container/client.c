#include <microkit.h>

#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/fs/config.h>

__attribute__((__section__(".serial_client_config"))) serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config"))) timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config"))) fs_client_config_t fs_config;


void init(void)
{
    microkit_dbg_puts("\n");
    microkit_dbg_puts("\n");
    microkit_dbg_puts("Hello from client!\n");
    microkit_dbg_puts("\n");
    microkit_dbg_puts("\n");

    //*((seL4_Word *)0xC000000) = 0x10;

    //microkit_notify(2);
}

void notified(microkit_channel ch)
{
    ;
}
