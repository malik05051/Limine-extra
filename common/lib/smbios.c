#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lib/smbios.h>
#include <lib/acpi.h>

static size_t smbios_struct_size(struct smbios_struct_header *hdr, size_t remaining) {
    if (remaining < sizeof(struct smbios_struct_header)
     || hdr->length < sizeof(struct smbios_struct_header)
     || hdr->length > remaining) {
        return 0;
    }

    const char *string_data = (void *)((uintptr_t)hdr + hdr->length);
    size_t string_area_max = remaining - hdr->length;
    size_t i = 1;
    for (; i < string_area_max && (string_data[i - 1] != '\0' || string_data[i] != '\0'); i++) {}
    if (i >= string_area_max) {
        return 0;
    }
    return hdr->length + i + 1;
}

bool smbios_walk_start(struct smbios_walk *walk) {
    struct smbios_entry_point_32 *smbios_entry_32 = NULL;
    struct smbios_entry_point_64 *smbios_entry_64 = NULL;
    acpi_get_smbios((void **)&smbios_entry_32, (void **)&smbios_entry_64);

    void *table = NULL;
    walk->length = 0;
    walk->count = 0;

    if (smbios_entry_64 != NULL) {
        table = smbios_get_table(
            smbios_entry_64->table_address, smbios_entry_64->table_maximum_size
        );
        walk->length = smbios_entry_64->table_maximum_size;
    }
    if (table == NULL && smbios_entry_32 != NULL) {
        table = smbios_get_table(smbios_entry_32->table_address, smbios_entry_32->table_length);
        walk->count = smbios_entry_32->number_of_structures;
        walk->length = smbios_entry_32->table_length;
    }

    walk->table = (uintptr_t)table;
    walk->offset = 0;
    walk->index = 0;
    return table != NULL && walk->length != 0;
}

struct smbios_struct_header *smbios_walk_next(struct smbios_walk *walk, size_t *size) {
    if (walk->count != 0 && walk->index >= walk->count) {
        return NULL;
    }

    size_t remaining = walk->length - walk->offset;
    if (remaining < sizeof(struct smbios_struct_header)) {
        return NULL;
    }

    struct smbios_struct_header *hdr = (void *)(walk->table + walk->offset);

    // SMBIOS 3.9 section 7.49 has End-of-Table used like an Inactive
    // structure, the whole table staying indexable, so stop at it only
    // when there is no structure count to go by.
    if (hdr->type == 127 && walk->count == 0) {
        return NULL;
    }

    *size = smbios_struct_size(hdr, remaining);
    if (*size == 0) {
        return NULL;
    }

    walk->offset += *size;
    walk->index++;
    return hdr;
}

#if defined(UEFI)

#include <lib/bli.h>
#include <lib/libc.h>
#include <lib/misc.h>
#include <lib/tpm.h>
#include <mm/pmm.h>

// Event tags shared with systemd, so that a policy written against one boot
// loader's log reads the other's.
#define SMBIOS_TYPE1_EVENT_TAG_ID 0xd5cb7cbc
#define SMBIOS_TYPE2_EVENT_TAG_ID 0xe0d47bc8
#define SMBIOS_TYPE11_EVENT_TAG_ID 0xc0b3bd23

// The wake-up type of a type 1 structure says how the machine was powered on,
// so it differs between a cold boot and a resume and would make the digest
// unreproducible.
#define SMBIOS_TYPE1_WAKE_UP_TYPE_OFFSET 24

static void smbios_measure_type1(const struct smbios_struct_header *hdr, size_t size) {
    const void *p = hdr;
    uint8_t *copy = NULL;

    // Whether the field exists at all is governed by the formatted area's
    // length, which comes from firmware and cannot be assumed.
    if (hdr->length > SMBIOS_TYPE1_WAKE_UP_TYPE_OFFSET) {
        copy = ext_mem_alloc(size);
        memcpy(copy, hdr, size);
        copy[SMBIOS_TYPE1_WAKE_UP_TYPE_OFFSET] = 0;
        p = copy;
    }

    tpm_measure_tagged(TPM_PCR_PLATFORM_CONFIG, SMBIOS_TYPE1_EVENT_TAG_ID,
                       p, size, "smbios:type1");

    if (copy != NULL) {
        pmm_free(copy, size);
    }
}

void smbios_measure(void) {
    if (!measured_boot) {
        return;
    }

    // Re-extending PCR 1 would invalidate a value something earlier in the
    // chain already published.
    if (bli_smbios_pcr_recorded()) {
        return;
    }

    struct smbios_walk walk;
    if (!smbios_walk_start(&walk)) {
        return;
    }

    bool measured = false;
    struct smbios_struct_header *hdr;
    size_t size;

    while ((hdr = smbios_walk_next(&walk, &size)) != NULL) {
        switch (hdr->type) {
            case 1: {
                smbios_measure_type1(hdr, size);
                measured = true;
                break;
            }
            case 2: {
                tpm_measure_tagged(TPM_PCR_PLATFORM_CONFIG, SMBIOS_TYPE2_EVENT_TAG_ID,
                                   hdr, size, "smbios:type2");
                measured = true;
                break;
            }
            case 11: {
                tpm_measure_tagged(TPM_PCR_PLATFORM_CONFIG, SMBIOS_TYPE11_EVENT_TAG_ID,
                                   hdr, size, "smbios:type11");
                measured = true;
                break;
            }
            default: {
                break;
            }
        }
    }

    if (measured) {
        bli_set_smbios_pcr(TPM_PCR_PLATFORM_CONFIG);
    }
}

#endif
