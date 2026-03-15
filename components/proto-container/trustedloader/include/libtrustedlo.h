
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <microkit.h>
#include <miscutils.h>

/* use ED25519 algorithm for encryption now */
#define PUBLIC_KEY_BYTES        32

/* number of access rights (for seL4 capabilities only) */
#define MAX_ACCESS_RIGHTS       MICROKIT_MAX_CHANNELS * 3


// Structure to hold each access right entry
typedef struct {
    uint8_t type;
    uint8_t padding[7];
    seL4_Word data; // For CHANNEL and IRQ: ID; For MEMORY: VADDR
} tsldr_acrt_entry_t;

// Structure to hold all access rights
typedef struct {
    uint32_t num_entries;
    tsldr_acrt_entry_t entries[MAX_ACCESS_RIGHTS];
} tsldr_acrt_table_t;

// Structure for memory mapping
typedef struct {
    seL4_Word vaddr;
    seL4_Word page;
    seL4_Word number_of_pages;
    seL4_Word page_size;
    seL4_Word rights;
    seL4_Word attrs;
} tsldr_mapping_t;


typedef struct {
    size_t        child_id;
    seL4_Word     system_hash;
    unsigned char public_key[PUBLIC_KEY_BYTES];
    uint8_t       channels[MICROKIT_MAX_CHANNELS];
    uint8_t       cstate[MICROKIT_MAX_CHANNELS];
    seL4_Word     irqs[MICROKIT_MAX_CHANNELS];
    tsldr_mapping_t mappings[MICROKIT_MAX_CHANNELS];
    bool          init;
} tsldr_mdinfo_t;


/* each template PD has one */
typedef struct {
    uint8_t avails;
    /* maximum is 16 per monitor */
    tsldr_mdinfo_t infodb[16];
} tsldr_mdinfodb_t;


/* Trusted loader metadata / state */
typedef struct {

    size_t child_id;

    tsldr_acrt_table_t acrt_required_table;

    bool restore;
    bool init;

    bool allowed_channels[MICROKIT_MAX_CHANNELS];

    bool allowed_irqs[MICROKIT_MAX_CHANNELS];

    int mp_cnt;
    seL4_Word allowed_mappings[MICROKIT_MAX_CHANNELS];

} tsldr_context_t;



typedef void (*entry_fn_t)(void);


enum {
    TYPE_CHANNEL = 0x01,
    TYPE_IRQ     = 0x02,
    TYPE_MEMORY  = 0x03,
};


#define TSLDR_ASSERT(cond)                     \
    do {                                       \
        if (!(cond)) {                         \
            microkit_internal_crash(-1);       \
        }                                      \
    } while (0)


void tsldr_main_pd_restore_caps_for_required_rights(tsldr_context_t *context, void *mdinfo);
void tsldr_main_pd_remove_caps_for_redundant_rights(tsldr_context_t *context, void *mdinfo);


/**
 * @brief Populates access rights and verifies signature of the data.
 *
 * @param loader Pointer to where the tsldr_acrt_table_t structure to be populated and stored.
 * @param data Pointer to the signed message (signature || data).
 * @return true if the signature is valid, false otherwise.
 */
void tsldr_main_declare_required_rights(tsldr_context_t *loader, void *data);

/**
 * @brief Applies access rights to build allowed lists
 *
 * @param loader Pointer to the loader which contains recorded access rights table
 */
void tsldr_main_pin_required_rights_before_pola(tsldr_context_t *loader, void *mdinfo);


void tsldr_main_monitor_init_mdinfo(tsldr_mdinfodb_t *db, size_t id, void *mdinfo);

/**
 * @brief Initialise a trusted loader
 *
 * @param loader Pointer to the trusted loader to initialise
 * @param id The id of child PD (for a template PD)
 */
void tsldr_main_try_init_loader(tsldr_context_t *c, size_t id);


void tsldr_main_restore_caps(tsldr_context_t *loader, void *mdinfo);


void tsldr_main_remove_caps(tsldr_context_t *loader, void *mdinfo);


// FIXME: this function refresh the regions where the client elf should live
void tsldr_main_loading_epilogue(uintptr_t client_exec, uintptr_t client_stack);


void tsldr_main_loading_prologue(void *mdinfo, tsldr_context_t *loader);


__attribute__((noreturn)) void tsldr_main_jump_with_stack(void *new_stack, void (*entry)(void));


void tsldr_main_check_elf_integrity(uintptr_t elf);


void tsldr_main_handle_access_rights(tsldr_context_t *context, void *acrt_stat_base, void *mdinfo);


void tsldr_main_self_loading(void *mdinfo, void *acrt_stat_base, tsldr_context_t *context, uintptr_t client_elf, uintptr_t client_exec_region, uintptr_t trampoline_elf, uintptr_t trampoline_stack_top);


void tsldr_main_monitor_privilege_pd(seL4_Word cid);


void tsldr_main_monitor_encode_required_rights(void *base, seL4_Word channels[], size_t n_channels, seL4_Word irqs[], size_t n_irqs, seL4_Word mappings[], size_t n_mps);

