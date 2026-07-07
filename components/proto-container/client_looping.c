#include <microkit.h>

#include <sddf/timer/client.h>
#include <sddf/timer/config.h>
#include <sddf/serial/queue.h>
#include <sddf/serial/config.h>
#include <lions/fs/config.h>

#include <sddf/util/printf.h>
#include <pc_config.h>
#include <protocon.h>
#include <libtrustedlo.h>

#include "pd_io_queue.h"

#define MONITOR_PPC_CHANNEL          15
#define MONITOR_NOTIFICATION_CHANNEL 16

#define PD_IO_CAPACITY                 512u
#define PD_IO_BUFFER_SIZE              2048u

#define CLIENT_RX_FREE_ADDR          0x04800000u
#define CLIENT_TX_FREE_ADDR          0x04803000u
#define CLIENT_RX_ACTIVE_ADDR        0x04806000u
#define CLIENT_TX_ACTIVE_ADDR        0x04809000u
#define CLIENT_RX_DATA_ADDR          0x0480C000u
#define CLIENT_TX_DATA_ADDR          0x0490C000u
#define CLIENT_DATA_SIZE             (PD_IO_CAPACITY * PD_IO_BUFFER_SIZE)

__attribute__((__section__(".serial_client_config")))
serial_client_config_t serial_config;
__attribute__((__section__(".timer_client_config")))
timer_client_config_t timer_config;
__attribute__((__section__(".fs_client_config")))
fs_client_config_t fs_config;

static void app_early_init(void)
{
    microkit_dbg_puts("application constructor\n");
    trampoline_args_t *args = (trampoline_args_t *)0x0A02000;
    client_args_t *client_args =
        (client_args_t *)((unsigned char *)args + sizeof(trampoline_args_t));

    microkit_pps = client_args->bitmap_ppcs;
    microkit_irqs = client_args->bitmap_irqs;
    microkit_ioports = client_args->bitmap_ioports;
    microkit_notifications = client_args->bitmap_notifications;
    tsldr_miscutil_memcpy(microkit_name,
                          client_args->dynamic_pd_name,
                          sizeof(client_args->dynamic_pd_name));
}

__attribute__((constructor))
static void register_app_early_init(void)
{
    app_early_init();
}

serial_queue_handle_t serial_rx_queue_handle;
serial_queue_handle_t serial_tx_queue_handle;

__attribute__((__section__(".pc_svc_desc")))
const protocon_svc_desc_t ciface = {
    .t1_num = 1,
    .t3_num = 1,
    .type1 = TIMER_IFACE,
    .type3 = SERIAL_IFACE,
    .t1_iface = { (uintptr_t)&timer_config, 0, 0, 0, 0, 0, 0, 0 },
    .t3_iface = { (uintptr_t)&serial_config, 0, 0, 0, 0, 0, 0, 0 },
};

typedef struct {
    uint64_t _unused;
    bool avail;
} neighbours_t;

neighbours_t neighbours[PC_CHILD_PER_MONITOR_MAX_NUM];
sddf_channel timer_channel;
static pd_io_link_t monitor_link;

static void init_monitor_link(void)
{
    /*
     * Client RX is monitor -> client.
     * Client TX is client -> monitor.
     *
     * The monitor owns shared queue reset/fill. The client only constructs
     * local handles over the already-initialised shared memory.
     */
    pd_io_direction_init(
        &monitor_link.rx,
        (pd_io_queue_t *)CLIENT_RX_FREE_ADDR,
        (pd_io_queue_t *)CLIENT_RX_ACTIVE_ADDR,
        (void *)CLIENT_RX_DATA_ADDR,
        CLIENT_DATA_SIZE,
        PD_IO_CAPACITY,
        PD_IO_BUFFER_SIZE
    );

    pd_io_direction_init(
        &monitor_link.tx,
        (pd_io_queue_t *)CLIENT_TX_FREE_ADDR,
        (pd_io_queue_t *)CLIENT_TX_ACTIVE_ADDR,
        (void *)CLIENT_TX_DATA_ADDR,
        CLIENT_DATA_SIZE,
        PD_IO_CAPACITY,
        PD_IO_BUFFER_SIZE
    );
}

static void send_ping(void)
{
    static const char ping[] = "ping from client";

    int err = pd_io_direction_send_raw(&monitor_link.tx,
                                       ping,
                                       (uint32_t)sizeof(ping));
    if (err != PD_IO_QUEUE_OK) {
        sddf_printf("CLIENT|ERROR: send ping failed: %d\n", err);
        return;
    }

    microkit_notify(MONITOR_NOTIFICATION_CHANNEL);
    sddf_printf("CLIENT|INFO: ping sent\n");
}

static void drain_monitor_messages(void)
{
    char payload[PD_IO_BUFFER_SIZE];
    uint32_t payload_len;

    for (;;) {
        int err = pd_io_direction_receive_raw(&monitor_link.rx,
                                              payload,
                                              sizeof(payload),
                                              &payload_len);
        if (err == PD_IO_QUEUE_EMPTY) {
            break;
        }
        if (err != PD_IO_QUEUE_OK) {
            sddf_printf("CLIENT|ERROR: receive failed: %d\n", err);
            break;
        }

        if (payload_len == 0) {
            payload[0] = '\0';
        } else {
            payload[payload_len - 1] = '\0';
        }
        sddf_printf("CLIENT|INFO: received %u bytes: %s\n",
                    payload_len,
                    payload);
    }
}

void init(void)
{
    assert(serial_config_check_magic(&serial_config));
    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle,
                          serial_config.rx.queue.vaddr,
                          serial_config.rx.data.size,
                          serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle,
                      serial_config.tx.queue.vaddr,
                      serial_config.tx.data.size,
                      serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    assert(timer_config_check_magic(&timer_config));

    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        neighbours[i].avail = false;
    }

    timer_channel = timer_config.driver_id;

    microkit_mr_set(0, 10);
    microkit_msginfo info =
        microkit_ppcall(MONITOR_PPC_CHANNEL,
                        microkit_msginfo_new(0, 1));
    seL4_Error error = microkit_msginfo_get_label(info);
    if (error != seL4_NoError) {
        microkit_internal_crash(error);
    }

    seL4_Word bitmap = seL4_GetMR(0);
    seL4_Word self_id = seL4_GetMR(1);

    for (int i = 0; i < PC_CHILD_PER_MONITOR_MAX_NUM; ++i) {
        if ((bitmap & (1ULL << i)) && i != self_id) {
            neighbours[i].avail = true;
        }
    }

    init_monitor_link();
    send_ping();
}

void notified(microkit_channel ch)
{
    if (ch == MONITOR_NOTIFICATION_CHANNEL) {
        drain_monitor_messages();
        sddf_timer_set_timeout(timer_channel, NS_IN_S);
        return;
    }

    if (ch == timer_channel) {
        uint64_t time = sddf_timer_time_now(timer_channel);
        sddf_printf("CLIENT|INFO: timer: %lu ns\n", time);
        send_ping();
    }
}
