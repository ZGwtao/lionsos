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


#pragma once

/**
 * Architecture specific PMU events.
 * The following should be declared:
 *
 * - `pmu_enable`. This should enable the PMU and set up the cycle
 *    counters, and will ideally reset.
 * - `pmu_read_cycles`. This should read the current cycle counter and any
 *   barriers as required by the architecture.
 * - `CYCLES_MIN`, `CYCLES_MAX`
 * - `cycles_t` type big enough for the cycle counter.
 *
 * Please ensure that functions are marked as `static inline` and check that
 * they are inlined.
 */
#ifdef CONFIG_ARCH_ARM_V8A

/**
 * Architectural reference:
 *  -  'Arm CoreSight Architecture Performance Monitoring Unit Architecture'
 *     ARM IHI 0091 B.a
 *  -  'Arm Architecture Reference Manual'
 *     ARM ARM DDI 0487 L.b
 *  - Cortex-A55 PMU Use-Cases Application Note (with sample code)
 *     Document ID: 107865 (release 1)
 *
 */

#define CYCLES_MAX UINT64_MAX
#define CYCLES_MIN 0

typedef uint64_t cycles_t;

static inline void isb_sy(void) { asm volatile("isb sy" ::: "memory"); }

static inline cycles_t pmccntr_el0(void) {
  cycles_t v;
  /* D24.5.2 in DDI 0487L.b, PMCCNTR_EL0. All 64 bits is CCNT. */
  asm volatile("mrs %0, pmccntr_el0" : "=r"(v) :: "memory");
  /* TODO: From the ARM sample code, I think there's no need for an ISB here.
           But I can't justify this w.r.t the specification...
   */
  return v;
}

/* 3.11 of Use-Cases app note: step 4 */
static inline void pmu_enable(void) {
  uint64_t v;
  asm volatile("mrs %0, pmcr_el0" : "=r"(v));
  v |= (1ull << 0);
  v &= ~(1ull << 3);
  asm volatile("msr pmcr_el0, %0" : : "r"(v));

  asm volatile("mrs %0, pmcntenset_el0" : "=r"(v));
  v |= (1ull << 31);
  asm volatile("msr pmcntenset_el0, %0" : : "r"(v));

#ifdef CONFIG_ARM_HYPERVISOR_SUPPORT
  /* NSH - count cycles in EL2 */
  v = (1ull << 27);
#else
  v = 0;
#endif
  asm volatile("msr pmccfiltr_el0, %0" : : "r"(v));

  /* Zero the cycle counter */
  asm volatile("msr pmccntr_el0, xzr" : :);

  isb_sy();
}

static inline cycles_t pmu_read_cycles(void) { return pmccntr_el0(); }

#else
#error "unsupported architecture"
#endif


void init(void)
{
    pmu_enable();
    assert(serial_config_check_magic(&serial_config));
    if (serial_config.rx.queue.vaddr != NULL) {
        serial_queue_init(&serial_rx_queue_handle, serial_config.rx.queue.vaddr, serial_config.rx.data.size, serial_config.rx.data.vaddr);
    }
    serial_queue_init(&serial_tx_queue_handle, serial_config.tx.queue.vaddr, serial_config.tx.data.size, serial_config.tx.data.vaddr);
    serial_putchar_init(serial_config.tx.id, &serial_tx_queue_handle);

    sddf_printf("Hello from client_timeout!\n");
    int cnt = 0;
    cycles_t last = pmu_read_cycles();
    for (;;) {
        cycles_t curr = pmu_read_cycles();
        if (curr - last < 0xb0000000) {
            continue;
        }
        last = curr;
        sddf_printf("Current cnt from cl-timeout: %d\n", cnt++);
    }
}

void notified(microkit_channel ch)
{
    ;
}
