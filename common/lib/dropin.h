#ifndef LIB__DROPIN_H
#define LIB__DROPIN_H

#if defined(UEFI)

#include <efi.h>

// Loads and starts every EFI driver dropped into `\EFI\systemd\drivers` on
// the volume `device_handle` names, then reconnects the firmware's device
// handles so the drivers take effect.
void dropin_load_drivers(efi_handle parent_image, efi_handle device_handle);

#endif

#endif
