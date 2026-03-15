
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

#define PD_CAP_BITS    (64) 

/* for monitor to access the cnode of container */
#define CHILD_CSPACE_BASE       (458)
/* for monitor to access the background CNode of its child */
#define CHILD_BACKUP_BASE       (474)

#define CNODE_SELF_CAP          (506)
#define CNODE_BACKUP_CAP        (CNODE_SELF_CAP + 1)

#define VSPACE_SELF_CAP         (CNODE_BACKUP_CAP + 1)
#define VSPACE_BACKUP_CAP       (9)

#define NTFN_BASE_CAP     (10)
#define PPC_BASE_CAP      (NTFN_BASE_CAP + 64)
#define IRQ_BASE_CAP      (PPC_BASE_CAP + 64)
// FIXME
// Looks like with current mapping method, we need only one slot for mapping
// and too much slots will cause problems with overlapping cslots
#define MAPPING_BASE_CAP  (458)

#define BACKUP_NTFN_BASE_CAP    (10)
#define BACKUP_IRQ_BASE_CAP     (BACKUP_NTFN_BASE_CAP + 64)
#define BACKUP_PPC_BASE_CAP     (BACKUP_IRQ_BASE_CAP + 64)
#define BACKUP_MAPPING_BASE_CAP (BACKUP_PPC_BASE_CAP + 64)



void tsldr_caputil_delete_cap(seL4_Word cap_idx);
void tsldr_caputil_delete_cap_from_cnode(seL4_Word cap_idx, seL4_Word cnode_idx);
void tsldr_caputil_load_cap_from_backup_cnode(seL4_Word dest_idx, seL4_Word src_idx);
void tsldr_caputil_store_cap_to_backup_cnode(seL4_Word dest_idx, seL4_Word src_idx);
void tsldr_caputil_copy_cap_from_backup_cnode(seL4_Word dest_idx, seL4_Word src_idx, seL4_CapRights_t rights);
void tsldr_caputil_copy_cap_between_cnode(seL4_Word cap_dest, seL4_Word cnode_dest, seL4_Word cap_src, seL4_Word cnode_src);


void tsldr_caputil_pd_deprivilege(void);
void tsldr_caputil_pd_privilege(seL4_Word pd_idx);
void tsldr_caputil_pd_grant_vspace_access(void);
void tsldr_caputil_pd_revoke_vspace_access(void);
void tsldr_caputil_pd_page_map(seL4_Word page_idx, uintptr_t vaddr, seL4_CapRights_t rights, seL4_Word attrs);
void tsldr_caputil_pd_page_unmap(seL4_Word page_idx);
void tsldr_caputil_pd_grant_page_access(seL4_Word page_idx, uintptr_t vaddr, seL4_CapRights_t rights, seL4_Word attrs);
void tsldr_caputil_pd_revoke_page_access(seL4_Word page_idx);



void tsldr_caputil_revoke_irq_cap(seL4_Word irq_idx);
void tsldr_caputil_revoke_ppc_cap(seL4_Word ppc_idx);
void tsldr_caputil_revoke_notification_cap(seL4_Word ntfn_idx);

void tsldr_caputil_restore_irq_cap(seL4_Word irq_idx);
void tsldr_caputil_restore_ppc_cap(seL4_Word ppc_idx);
void tsldr_caputil_restore_notification_cap(seL4_Word ntfn_idx);

