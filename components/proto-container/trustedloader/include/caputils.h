
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>

#define PD_CAP_BITS    (64) 

/* for monitor to access the cnode of container */
#define PD_TEMPLATE_CHILD_CSPACE_BASE   (458)
/* for monitor to access the vspace of container */
#define PD_TEMPLATE_CHILD_VSPACE_BASE   (490)
/* for monitor to access the background CNode of its child */
#define PD_TEMPLATE_CHILD_BNODE_BASE    (474)
/* for monitor to access it's own cspace */
#define PD_TEMPLATE_CNODE_ROOT          (506)

#define CNODE_SELF_CAP          (506)
#define CNODE_BACKGROUND_CAP    (CNODE_SELF_CAP + 1)
#define CNODE_VSPACE_CAP        (CNODE_BACKGROUND_CAP + 1)

#define CNODE_NTFN_BASE_CAP     (10)
#define CNODE_PPC_BASE_CAP      (CNODE_NTFN_BASE_CAP + 64)
#define CNODE_IRQ_BASE_CAP      (CNODE_PPC_BASE_CAP + 64)
// FIXME
// Looks like with current mapping method, we need only one slot for mapping
// and too much slots will cause problems with overlapping cslots
#define CNODE_BASE_MAPPING_CAP  (458)

#define CNODE_TSLDR_CONTEXT_CAP (500)
/* put it in somewhere in the middle of no where... */

#define BACKGROUND_VSPACE_CAP       (9)
#define BACKGROUND_NTFN_BASE_CAP    (10)
#define BACKGROUND_IRQ_BASE_CAP     (BACKGROUND_NTFN_BASE_CAP + 64)
#define BACKGROUND_PPC_BASE_CAP     (BACKGROUND_IRQ_BASE_CAP + 64)
#define BACKGROUND_MAPPING_BASE_CAP (BACKGROUND_PPC_BASE_CAP + 64)

#define BACKGROUND_TSLDR_CONTEXT_CAP    (500)




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

