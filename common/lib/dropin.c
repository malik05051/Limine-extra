#if defined(UEFI)

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <efi.h>
#include <lib/dropin.h>
#include <lib/libc.h>
#include <lib/misc.h>
#include <lib/print.h>

#define DROPIN_DIR L"\\EFI\\systemd\\drivers"

// Nothing legitimate comes close; the bound is only there to stop a walk of
// a device path the firmware never terminated.
#define DROPIN_MAX_DEVICE_PATH 4096

// The interface names the drivers a firmware can run for this machine by the
// suffix of their filename, spelt as the UEFI removable media path does.
#if defined(__x86_64__)
#  define DROPIN_SUFFIX "x64.efi"
#elif defined(__i386__)
#  define DROPIN_SUFFIX "ia32.efi"
#elif defined(__aarch64__)
#  define DROPIN_SUFFIX "aa64.efi"
#elif defined(__riscv)
#  define DROPIN_SUFFIX "riscv64.efi"
#elif defined(__loongarch64)
#  define DROPIN_SUFFIX "loongarch64.efi"
#else
#error "Unspecified architecture"
#endif

static size_t wstrlen(const uint16_t *s) {
    size_t len = 0;
    while (s[len] != 0) {
        len++;
    }
    return len;
}

static bool name_is_for_this_machine(const uint16_t *name) {
    static const char suffix[] = DROPIN_SUFFIX;
    size_t suffix_len = SIZEOF_ARRAY(suffix) - 1;

    size_t name_len = wstrlen(name);
    if (name_len < suffix_len) {
        return false;
    }

    const uint16_t *tail = name + (name_len - suffix_len);
    for (size_t i = 0; i < suffix_len; i++) {
        uint16_t c = tail[i];
        if (c >= L'A' && c <= L'Z') {
            c += L'a' - L'A';
        }
        if (c != (uint16_t)suffix[i]) {
            return false;
        }
    }

    return true;
}

// LoadImage() is given a path rather than a buffer so that the firmware
// verifies the driver against its own Secure Boot policy.
static struct efi_device_path_protocol *build_driver_device_path(efi_handle device_handle,
                                                          const uint16_t *name) {
    struct efi_guid device_path_guid = EFI_DEVICE_PATH_PROTOCOL_GUID;
    struct efi_device_path_protocol *volume_path = NULL;

    if (gBS->handle_protocol(device_handle, &device_path_guid,
                            (void **)&volume_path) != EFI_SUCCESS) {
        return NULL;
    }

    size_t prefix_len = 0;
    for (struct efi_device_path_protocol *node = volume_path; !efi_is_device_path_end(node);
         node = efi_next_device_path_node(node)) {
        size_t node_len = efi_device_path_node_length(node);
        if (node_len < sizeof(struct efi_device_path_protocol)
         || prefix_len > DROPIN_MAX_DEVICE_PATH) {
            return NULL;
        }
        prefix_len += node_len;
    }

    size_t prefix_chars = SIZEOF_ARRAY(DROPIN_DIR L"\\") - 1;
    size_t name_chars = wstrlen(name);

    size_t path_item_len = sizeof(struct efi_device_path_protocol)
                         + (prefix_chars + name_chars + 1) * sizeof(uint16_t);
    if (path_item_len > 0xffff) {
        return NULL;
    }

    size_t alloc_len = prefix_len + path_item_len + EFI_END_DEVICE_PATH_LENGTH;

    struct efi_device_path_protocol *device_path = NULL;
    if (gBS->allocate_pool(EFI_LOADER_DATA, alloc_len,
                          (void **)&device_path) != EFI_SUCCESS) {
        return NULL;
    }

    memcpy(device_path, volume_path, prefix_len);

    struct efi_filepath_device_path *path_item = (void *)device_path + prefix_len;
    path_item->header.type      = EFI_MEDIA_DEVICE_PATH;
    path_item->header.sub_type   = EFI_MEDIA_FILEPATH_DP;
    path_item->header.length[0] = path_item_len;
    path_item->header.length[1] = path_item_len >> 8;

    void *path_name = path_item->path_name;
    memcpy(path_name, DROPIN_DIR L"\\", prefix_chars * sizeof(uint16_t));
    memcpy(path_name + prefix_chars * sizeof(uint16_t), name, (name_chars + 1) * sizeof(uint16_t));

    struct efi_device_path_protocol *end_item = (void *)path_item + path_item_len;
    end_item->type      = EFI_END_DEVICE_PATH_TYPE;
    end_item->sub_type   = EFI_END_ENTIRE_DEVICE_PATH_SUBTYPE;
    end_item->length[0] = EFI_END_DEVICE_PATH_LENGTH;
    end_item->length[1] = EFI_END_DEVICE_PATH_LENGTH >> 8;

    return device_path;
}

// Opening a plain file as though it were a directory succeeds, and reading
// it would hand us its contents where the entry names should be.
static bool file_is_directory(struct efi_file_protocol *file) {
    struct efi_guid info_guid = EFI_FILE_INFO_ID;
    uint64_t buf[64];
    size_t size = sizeof(buf);

    if (file->get_info(file, &info_guid, &size, buf) != EFI_SUCCESS) {
        return false;
    }

    return (((struct efi_file_info *)buf)->attribute & EFI_FILE_DIRECTORY) != 0;
}

static bool load_one_driver(efi_handle parent_image, efi_handle device_handle,
                            const uint16_t *name) {
    struct efi_device_path_protocol *device_path = build_driver_device_path(device_handle, name);
    if (device_path == NULL) {
        return false;
    }

    efi_handle image = NULL;
    efi_status status = gBS->load_image(false, parent_image, device_path, NULL, 0, &image);

    gBS->free_pool(device_path);

    if (status != EFI_SUCCESS) {
        printv("dropin: LoadImage() failure (%X)\n", (uint64_t)status);
        return false;
    }

    struct efi_guid loaded_img_prot_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    struct efi_loaded_image_protocol *loaded_image = NULL;
    status = gBS->handle_protocol(image, &loaded_img_prot_guid, (void **)&loaded_image);

    // Anything that is not driver code would be started as an application.
    if (status != EFI_SUCCESS
     || (loaded_image->image_code_type != EFI_BOOT_SERVICES_CODE
      && loaded_image->image_code_type != EFI_RUNTIME_SERVICES_CODE)) {
        printv("dropin: Image is not a driver, refusing it\n");
        gBS->unload_image(image);
        return false;
    }

    status = gBS->start_image(image, NULL, NULL);
    if (status != EFI_SUCCESS) {
        // A driver that only initialises says so by returning EFI_ABORTED
        // on success, so that it is unloaded again afterwards.
        if (status != EFI_ABORTED) {
            printv("dropin: StartImage() failure (%X)\n", (uint64_t)status);
        }
        gBS->unload_image(image);
        return false;
    }

    return true;
}

static void reconnect_all_controllers(void) {
    size_t handle_count = 0;
    efi_handle *handles = NULL;

    if (gBS->locate_handle_buffer(EFI_ALL_HANDLES, NULL, NULL,
                                &handle_count, &handles) != EFI_SUCCESS) {
        return;
    }

    // Handles go stale as their controllers are reconnected, and firmware
    // hands out bogus ones besides, so failures here are expected.
    for (size_t i = 0; i < handle_count; i++) {
        gBS->connect_controller(handles[i], NULL, NULL, true);
    }

    gBS->free_pool(handles);
}

void dropin_load_drivers(efi_handle parent_image, efi_handle device_handle) {
    if (device_handle == NULL) {
        return;
    }

    struct efi_guid fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    struct efi_simple_file_system_protocol *fs = NULL;
    if (gBS->handle_protocol(device_handle, &fs_guid, (void **)&fs) != EFI_SUCCESS) {
        return;
    }

    struct efi_file_protocol *root = NULL;
    if (fs->open_volume(fs, &root) != EFI_SUCCESS) {
        return;
    }

    struct efi_file_protocol *dir = NULL;
    efi_status status = root->open(root, &dir, DROPIN_DIR, EFI_FILE_MODE_READ, 0);

    root->close(root);

    if (status != EFI_SUCCESS) {
        return;
    }

    if (!file_is_directory(dir)) {
        dir->close(dir);
        return;
    }

    size_t info_size = EFI_SIZE_OF_EFI_FILE_INFO + 256 * sizeof(uint16_t);
    struct efi_file_info *info = NULL;
    if (gBS->allocate_pool(EFI_LOADER_DATA, info_size, (void **)&info) != EFI_SUCCESS) {
        dir->close(dir);
        return;
    }

    size_t loaded = 0;
    for (;;) {
        size_t read_size = info_size;
        status = dir->read(dir, &read_size, info);

        if (status == EFI_BUFFER_TOO_SMALL) {
            struct efi_file_info *bigger = NULL;
            if (gBS->allocate_pool(EFI_LOADER_DATA, read_size, (void **)&bigger) != EFI_SUCCESS) {
                break;
            }
            gBS->free_pool(info);
            info = bigger;
            info_size = read_size;
            continue;
        }

        // A zero-length read is the end of the directory.
        if (status != EFI_SUCCESS || read_size == 0) {
            break;
        }

        if (info->file_name[0] == L'.'
         || (info->attribute & EFI_FILE_DIRECTORY) != 0
         || !name_is_for_this_machine(info->file_name)) {
            continue;
        }

        if (load_one_driver(parent_image, device_handle, info->file_name)) {
            loaded++;
        }
    }

    gBS->free_pool(info);
    dir->close(dir);

    if (loaded > 0) {
        printv("dropin: Loaded %u drop-in driver(s)\n", (unsigned)loaded);
        reconnect_all_controllers();
    }
}

#endif
