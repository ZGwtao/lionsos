#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>
#include <elf_utils.h>

/* use ED25519 algorithm for encryption now */
#define PUBLIC_KEY_BYTES        32

#define NUM_ENTRIES_SIZE        sizeof(uint32_t)

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
    seL4_Word system_hash;
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


#define TSLDR_MD_SIZE 0x1000
typedef struct {
    seL4_Word system_hash;
    unsigned char public_key[PUBLIC_KEY_BYTES];
    seL4_Word channels[MICROKIT_MAX_CHANNELS];
    seL4_Word irqs[MICROKIT_MAX_CHANNELS];
    MemoryMapping mappings[MICROKIT_MAX_CHANNELS];
    /* for recording ... */
    bool init;
    uint8_t padding[TSLDR_MD_SIZE
                    - ( sizeof(seL4_Word) 
                      + PUBLIC_KEY_BYTES 
                      + sizeof(seL4_Word) * MICROKIT_MAX_CHANNELS 
                      + sizeof(seL4_Word) * MICROKIT_MAX_CHANNELS 
                      + sizeof(MemoryMapping) * MICROKIT_MAX_CHANNELS
                      + sizeof(bool) )];
} tsldr_md_t;
_Static_assert(sizeof(tsldr_md_t) == TSLDR_MD_SIZE,
               "tsldr_md_t must be exactly one page");


#define PD_CAP_BITS     10

/* access to child TCB from monitor */
#define PD_TEMPLATE_CHILD_TCB   1
/* for monitor to access the cnode of container */
#define PD_TEMPLATE_CHILD_CNODE     8
/* for monitor to access the vspace of container */
#define PD_TEMPLATE_CHILD_VSPACE    9
/* for monitor to access it's own cspace */
#define PD_TEMPLATE_CNODE_ROOT  586
/* for monitor to access the background CNode of its child */
#define PD_TEMPLATE_CBG_CNODE   587

#define CNODE_BACKGROUND_CAP    588
#define CNODE_SELF_CAP          589
#define CNODE_NTFN_BASE_CAP     (10)
#define CNODE_PPC_BASE_CAP      (CNODE_NTFN_BASE_CAP + 64)
#define CNODE_IRQ_BASE_CAP      (CNODE_PPC_BASE_CAP + 64)

#define CNODE_TSLDR_CONTEXT_CAP (500)
/* put it in somewhere in the middle of no where... */
#define CNODE_VSPACE_CAP        (750)
#define CNODE_BASE_MAPPING_CAP  (CNODE_VSPACE_CAP + 1)

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

    /*
     * Rights bitmaps / filters:
     *   1. Channels
     *   2. IRQs
     *   3. Mappings
     */
    bool          allowed_channels[MICROKIT_MAX_CHANNELS];
    bool          allowed_irqs[MICROKIT_MAX_CHANNELS];
    MemoryMapping allowed_mappings[MICROKIT_MAX_CHANNELS];

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

    /* Crypto / verification */
    seL4_Word     system_hash;               /* 8 bytes */
    size_t        hash_len;                  /* 8 bytes */
    size_t        signature_len;             /* 8 bytes */
    unsigned char public_key[PUBLIC_KEY_BYTES];

    /* Hook for signature verification function */
    crypto_verify_fn verify_func;
} trusted_loader_t;



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


void tsldr_init_metadata(tsldr_md_t *tsldr_metadata_patched);

/**
 * @brief Initialise a trusted loader
 *
 * @param loader Pointer to the trusted loader to initialise
 * @param fn Function pointer to the verifying mechanism
 * @param hash_val System hash value
 * @param hash_len Length of the system hash value
 * @param signature_len Length of the access right table signature
 */
void tsldr_init(trusted_loader_t *loader, crypto_verify_fn fn, seL4_Word hash_val, size_t hash_len, size_t signature_len);


void tsldr_restore_caps(trusted_loader_t *loader, bool self_loading);


void tsldr_remove_caps(trusted_loader_t *loader, bool self_loading);


// FIXME: this function refresh the regions where the client elf should live
seL4_Error tsldr_loading_epilogue(uintptr_t client_exec, uintptr_t client_stack);



seL4_Error tsldr_loading_prologue(trusted_loader_t *loader);

/* grant access to the child's cspaces from the monitor's view */
seL4_Error tsldr_grant_cspace_access(void);

