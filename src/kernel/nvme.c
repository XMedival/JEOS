#include "print.h"
#include "nvme.h"
#include "mem.h"
#include "pci.h"

void nvme_init() {
    struct pci_device *nvme_dev = 0;
    for (u32 i = 0; i < pci_device_count; i++) {
        struct pci_device *dev = &pci_devices[i];
        if (dev->hdr.general.h.class_code == PCI_CLASS_STORAGE && dev->hdr.general.h.subclass == PCI_SUBCLASS_NVME) {
            nvme_dev = dev;
            break;
        }
    }

    if (!nvme_dev) {
        klog("NVME", "no controller found");
        return;
    }

    klog("NVME", "controller at %u:%u.%u", nvme_dev->bus, nvme_dev->slot, nvme_dev->func);

    u64 abar_phys = pci_read_bar(nvme_dev->bus, nvme_dev->slot, nvme_dev->func, 0);
    map_mmio(abar_phys, PAGE_SIZE);
    klog_ok("NVME", "MMIO mapped");
}
