
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

extern uintptr_t tsldr_metadata;

uintptr_t tsldr_acrtutil_check_mapping(seL4_Word vaddr);
uint8_t tsldr_acrtutil_check_channel(seL4_Word channel, uint8_t *cstate);
uint8_t tsldr_acrtutil_check_irq(seL4_Word irq);


void tsldr_acrtutil_restore_channels(void *data);
void tsldr_acrtutil_restore_irqs(void *data);
void tsldr_acrtutil_restore_mappings(void *data);


void tsldr_acrtutil_revoke_channels(void *data);
void tsldr_acrtutil_revoke_irqs(void *data);
void tsldr_acrtutil_revoke_mappings(void *data);


