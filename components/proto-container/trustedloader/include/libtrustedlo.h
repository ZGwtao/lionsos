
#include <elf_utils.h>
#include <acg.h>
#include <caputils.h>

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
    uint8_t type;
    uint8_t padding[7];
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


typedef int (*crypto_verify_fn)(const unsigned char *signature,
                                const unsigned char *data,
                                size_t data_size,
                                const unsigned char *public_key);


/* Trusted loader metadata / state */
typedef struct {
    /* Access right table */
    AccessRights access_rights;

    size_t child_id;
    bool init;
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

    bool restore;

} trusted_loader_t;



typedef void (*entry_fn_t)(void);


enum {
    TYPE_CHANNEL = 0x01,
    TYPE_IRQ     = 0x02,
    TYPE_MEMORY  = 0x03,
};

void encode_access_rights_to(void *base,
                             const uint64_t *channel_ids, size_t n_channels,
                             const uint64_t *irq_ids,     size_t n_irqs,
                             const uint64_t *memory_vaddrs,size_t n_vaddrs);

seL4_Error tsldr_parse_rights(Elf64_Ehdr *ehdr, char *ref_section[], seL4_Word *size);

/**
 * @brief Populates access rights and verifies signature of the data.
 *
 * @param loader Pointer to where the AccessRights structure to be populated and stored.
 * @param data Pointer to the signed message (signature || data).
 * @return true if the signature is valid, false otherwise.
 */
void tsldr_main_populate_all_rights(trusted_loader_t *loader, void *data);

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
void tsldr_main_try_init_loader(trusted_loader_t *c, size_t id);


void tsldr_main_restore_caps(trusted_loader_t *loader);


void tsldr_main_remove_caps(trusted_loader_t *loader);


// FIXME: this function refresh the regions where the client elf should live
seL4_Error tsldr_loading_epilogue(uintptr_t client_exec, uintptr_t client_stack);



void tsldr_main_loading_prologue(void *metadata_base, trusted_loader_t *loader);

/* grant access to the child's cspaces from the monitor's view */
seL4_Error tsldr_grant_cspace_access(size_t child_id);


__attribute__((noreturn)) void tsldr_main_jump_with_stack(void *new_stack, void (*entry)(void));


void tsldr_main_self_loading(void *metadata_base, void *acrt_stat_base, trusted_loader_t *context, uintptr_t client_elf, uintptr_t client_exec_region, uintptr_t trampoline_elf, uintptr_t trampoline_stack_top);



void tsldr_main_check_elf_integrity(uintptr_t elf);

