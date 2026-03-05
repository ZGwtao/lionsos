
#include <elf_utils.h>
#include <acg.h>

/* use ED25519 algorithm for encryption now */
#define PUBLIC_KEY_BYTES        32

#define NUM_ENTRIES_SIZE        sizeof(size_t)

/* number of access rights (for seL4 capabilities only) */
#define MAX_ACCESS_RIGHTS       MICROKIT_MAX_CHANNELS * 3

#define ACCESS_RIGHT_ENTRY_SIZE 9

// Access types
typedef enum {
    ACCESS_TYPE_CHANNEL = 0x01,
    ACCESS_TYPE_IRQ     = 0x02,
    ACCESS_TYPE_MEMORY  = 0x03
} AccessType;

// Structure to hold each access right entry
typedef struct {
    AccessType type;
    seL4_Word data; // For CHANNEL and IRQ: ID; For MEMORY: VADDR
} AccessRightEntry;

// Structure to hold all access rights
typedef struct {
    //seL4_Word system_hash;
    uint32_t num_entries;
    AccessRightEntry entries[MAX_ACCESS_RIGHTS];
} AccessRights;

// Structure for memory mapping
typedef struct {
    seL4_Word vaddr;
    seL4_Word page;
    seL4_Word number_of_pages;
    seL4_Word page_size;
    seL4_Word rights;
    seL4_Word attrs;
} MemoryMapping;


typedef struct {
    size_t        child_id;
    seL4_Word     system_hash;
    unsigned char public_key[PUBLIC_KEY_BYTES];
    uint8_t       channels[MICROKIT_MAX_CHANNELS];
    uint8_t       cstate[MICROKIT_MAX_CHANNELS];
    seL4_Word     irqs[MICROKIT_MAX_CHANNELS];
    MemoryMapping mappings[MICROKIT_MAX_CHANNELS];
    bool          init;
} tsldr_md_t;


/* each template PD has one */
typedef struct {
    uint8_t avails;
    /* maximum is 16 per monitor */
    tsldr_md_t md_array[16];
} tsldr_md_array_t;

#define PD_CAP_BITS    64 

/* for monitor to access the cnode of container */
#define PD_TEMPLATE_CHILD_CSPACE_BASE   (458)
/* for monitor to access the vspace of container */
#define PD_TEMPLATE_CHILD_VSPACE_BASE   (490)
/* for monitor to access the background CNode of its child */
#define PD_TEMPLATE_CHILD_BNODE_BASE    (474)
/* for monitor to access it's own cspace */
#define PD_TEMPLATE_CNODE_ROOT          (506)

#define CNODE_SELF_CAP          506
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

#define BACKGROUND_VSPACE_CAP       9
#define BACKGROUND_NTFN_BASE_CAP    10
#define BACKGROUND_IRQ_BASE_CAP     (BACKGROUND_NTFN_BASE_CAP + 64)
#define BACKGROUND_PPC_BASE_CAP     (BACKGROUND_IRQ_BASE_CAP + 64)
#define BACKGROUND_MAPPING_BASE_CAP (BACKGROUND_PPC_BASE_CAP + 64)

#define BACKGROUND_TSLDR_CONTEXT_CAP    (500)

typedef int (*crypto_verify_fn)(const unsigned char *signature,
                                const unsigned char *data,
                                size_t data_size,
                                const unsigned char *public_key);


/* Trusted loader metadata / state */
typedef struct {
    /* Access right table */
    AccessRights access_rights;

    size_t child_id;
    /*
     * Rights bitmaps / filters:
     *   1. Channels
     *   2. IRQs
     *   3. Mappings
     */
    bool          allowed_channels[MICROKIT_MAX_CHANNELS];
    bool          allowed_irqs[MICROKIT_MAX_CHANNELS];
    MemoryMapping *allowed_mappings[MICROKIT_MAX_CHANNELS];

    /* Mapping bookkeeping */
    int num_allowed_mappings;   /* 32-bit, but promotes with padding to 64-bit boundary */

    /* Capability management / state flags */
    struct {
        bool removed_caps; /* unused for now ... */
        bool flag_bootstrap;
        bool flag_restore_caps;
        bool init;
        /* compiler will pad this group to 8 bytes */
    } flags;
} trusted_loader_t;


enum {
    TYPE_CHANNEL = 0x01,
    TYPE_IRQ     = 0x02,
    TYPE_MEMORY  = 0x03,
};

void encode_access_rights_to(void *base,
                             const uint64_t *channel_ids, size_t n_channels,
                             const uint64_t *irq_ids,     size_t n_irqs,
                             const uint64_t *memory_vaddrs,size_t n_vaddrs);


MemoryMapping *tsldr_find_mapping_by_vaddr(trusted_loader_t *loader, seL4_Word vaddr, bool sldr, void *data);


seL4_Error tsldr_parse_rights(Elf64_Ehdr *ehdr, char *ref_section[], seL4_Word *size);

/**
 * @brief Populates access rights and verifies signature of the data.
 *
 * @param loader Pointer to where the AccessRights structure to be populated and stored.
 * @param signed_message Pointer to the signed message (signature || data).
 * @param len Length of the signed message in bytes.
 * @return true if the signature is valid, false otherwise.
 */
seL4_Error tsldr_populate_rights(trusted_loader_t *loader, const unsigned char *signed_message, size_t len);

/**
 * @brief Applies access rights to build allowed lists
 *
 * @param loader Pointer to the loader which contains recorded access rights table
 */
seL4_Error tsldr_populate_allowed(trusted_loader_t *loader);


void tsldr_init_metadata(tsldr_md_array_t *array, size_t id);

/**
 * @brief Initialise a trusted loader
 *
 * @param loader Pointer to the trusted loader to initialise
 * @param id The id of child PD (for a template PD)
 */
void tsldr_init(trusted_loader_t *loader, size_t id);


void tsldr_restore_caps(trusted_loader_t *loader, bool self_loading);


void tsldr_remove_caps(trusted_loader_t *loader, bool self_loading);


// FIXME: this function refresh the regions where the client elf should live
seL4_Error tsldr_loading_epilogue(uintptr_t client_exec, uintptr_t client_stack);



seL4_Error tsldr_loading_prologue(trusted_loader_t *loader);

/* grant access to the child's cspaces from the monitor's view */
seL4_Error tsldr_grant_cspace_access(size_t child_id);

