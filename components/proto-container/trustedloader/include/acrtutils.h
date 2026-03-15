
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

uintptr_t tsldr_acrtutil_check_mapping(seL4_Word vaddr, void *metadata_base);
uint8_t tsldr_acrtutil_check_channel(seL4_Word channel, uint8_t *cstate, void *metadata_base);
uint8_t tsldr_acrtutil_check_irq(seL4_Word irq, void *metadata_base);


void tsldr_acrtutil_restore_channels(void *data, void *metadata_base);
void tsldr_acrtutil_restore_irqs(void *data, void *metadata_base);
void tsldr_acrtutil_restore_mappings(void *data);


void tsldr_acrtutil_revoke_channels(void *data, void *metadata_base);
void tsldr_acrtutil_revoke_irqs(void *data, void *metadata_base);
void tsldr_acrtutil_revoke_mappings(void *data);



void tsldr_acrtutil_add_rights_to_whitelist(void *data, void *input, void *metadata_base);
void tsldr_acrtutil_populate_all_rights(void *context_data, void *src_data, seL4_Word num);


void tsldr_acrtutil_encode_rights(void *base, const uint64_t *channel_ids, size_t n_channels, const uint64_t *irq_ids, size_t n_irqs, const uint64_t *memory_vaddrs, size_t n_vaddrs);
seL4_Word tsldr_acrtutil_check_access_rights_table(void *base);
