/*
 * Copyright 2023, UNSW
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * What even is this file?
 *
 * The purpose of this file is to include binaries that the VMM needs to setup,
 * such as the Linux kernel image, the DTB, and the initial RAM disk. What
 * happens is that the assembler includes the binary data in an ELF section
 * that we specify. We can then have global variables that essentially point to
 * where the data has been included.
 *
 * For each ELF section, we define a name and also some configuration. The
 * attributes "aw" is to say that the section is allocatable and that it is
 * writeable. We also have "@progbits", this is to indicate that the section
 * contains data (rather than code or something else).
 *
 */

#if defined(BM_PROTOCON_PATH)
.section .bm_pc, "aw", @progbits
.global _bm_proto_container, _bm_proto_container_end
_bm_proto_container:
.incbin BM_PROTOCON_PATH
_bm_proto_container_end:
#endif

#if defined(BM_TRAMPOLINE_PATH)
.section .bm_trampoline, "aw", @progbits
.global _bm_trampoline, _bm_trampoline_end
_bm_trampoline:
.incbin BM_TRAMPOLINE_PATH
_bm_trampoline_end:
#endif

#if defined(BM_PAYLOAD_PATH)
.section .bm_payload, "aw", @progbits
.global _bm_payload, _bm_payload_end
_bm_payload:
.incbin BM_PAYLOAD_PATH
_bm_payload_end:
#endif
