#if defined (UEFI)

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <efi.h>
#include <lib/hii.h>
#include <lib/libc.h>
#include <lib/misc.h>

// EFI_HII_KEYBOARD_LAYOUT. The GUID starts at offset 2, so nothing that
// follows it lands on its natural alignment.
struct hii_keyboard_layout {
    uint16_t layout_length;
    EFI_GUID guid;
    uint32_t descriptor_string_offset;
    uint8_t descriptor_count;
} __attribute__((packed));

// The bundle descriptor_string_offset points at. Each of the
// description_count entries is a language tag, a U+0020, then a NUL
// terminated description; the tag itself is only delimited by that space.
struct hii_description_string_bundle {
    uint16_t description_count;
    wchar_t strings[];
} __attribute__((packed));

bool hii_get_keyboard_layout(wchar_t *buf, size_t buf_size) {
    EFI_GUID hii_db_guid = EFI_HII_DATABASE_PROTOCOL_GUID;
    EFI_HII_DATABASE_PROTOCOL *hii_db = NULL;

    if (gBS->LocateProtocol(&hii_db_guid, NULL, (void **)&hii_db) != EFI_SUCCESS
     || hii_db == NULL) {
        return false;
    }

    // The sizing call is defined to fail; a success means the firmware did
    // not understand the request.
    EFI_UINT16 length = 0;
    if (hii_db->GetKeyboardLayout(hii_db, NULL, &length, NULL) != EFI_BUFFER_TOO_SMALL
     || length < sizeof(struct hii_keyboard_layout)) {
        return false;
    }

    struct hii_keyboard_layout *layout = NULL;
    if (gBS->AllocatePool(EfiLoaderData, length, (void **)&layout) != EFI_SUCCESS) {
        return false;
    }

    bool ret = false;

    EFI_UINT16 got = length;
    if (hii_db->GetKeyboardLayout(hii_db, NULL, &got, (EFI_HII_KEYBOARD_LAYOUT *)layout) != EFI_SUCCESS
     || got != length || layout->layout_length != length) {
        goto out;
    }

    uint32_t offset = layout->descriptor_string_offset;
    if (offset > length
     || length - offset < sizeof(struct hii_description_string_bundle)) {
        goto out;
    }

    struct hii_description_string_bundle *bundle = (void *)layout + offset;
    if (bundle->description_count == 0) {
        goto out;
    }

    size_t avail = (length - offset - sizeof(struct hii_description_string_bundle))
                 / sizeof(wchar_t);

    size_t len = 0;
    while (len < avail && bundle->strings[len] != L' ') {
        len++;
    }
    if (len == 0 || len == avail || len + 1 > buf_size) {
        goto out;
    }

    memcpy(buf, bundle->strings, len * sizeof(wchar_t));
    buf[len] = L'\0';
    ret = true;

out:
    gBS->FreePool(layout);
    return ret;
}

#endif
