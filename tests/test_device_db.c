#include "../native/device_db.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(!fseek(file, 0, SEEK_END));
    long length = ftell(file);
    assert(length >= 0 && length < 8 * 1024 * 1024);
    rewind(file);
    char *text = malloc((size_t)length + 1);
    assert(text);
    assert(fread(text, 1, (size_t)length, file) == (size_t)length);
    fclose(file);
    *size = (size_t)length;
    text[*size] = 0;
    return text;
}

int main(void) {
    const char usb[] = "056a  Wacom\r\n\t037a  CTL-472\r\n"
                       "10ec  Vast USB\n\t0001  USB device\nC 00 class\n\t1234 ignored";
    const char pci[] = "8086  Intel\n\t1234  Intel device\n\t\t1028 5678  Dell board\n"
                       "10ec  Realtek PCI\n\t0001  PCI NIC";
    const char tablets[] = "056A\t037A\twacom\tWacom CTL-472\n"
                           "256C\t006D\thuion\tHuion A\n256C\t006D\tgaomon\tGaomon B\n";
    DeviceDatabase db;
    DeviceMatch match;
    assert(device_db_load(&db, usb, sizeof(usb)-1, pci, sizeof(pci)-1, tablets, sizeof(tablets)-1));
    assert(device_db_match(&db, "hid\\vid_056a&pid_037a&mi_00", &match));
    assert(match.level == DB_TABLET && match.tablet_candidates == 1);
    assert(!strcmp(match.model, "Wacom CTL-472") && !strcmp(match.sources[0], "wacom"));
    assert(device_db_match(&db, "USB\\VID_256C&PID_006D", &match));
    assert(match.tablet_candidates == 2 && strstr(match.model, "Huion") && strstr(match.model, "Gaomon"));
    assert(device_db_match(&db, "PCI\\VEN_8086&DEV_1234&SUBSYS_56781028&REV_01", &match));
    assert(match.level == DB_SUBSYSTEM && !strcmp(match.model, "Dell board"));
    assert(!strcmp(match.sources[0], "dell") && !strcmp(match.sources[1], "intel"));
    assert(device_db_match(&db, "USB\\VID_10EC&PID_0001", &match));
    assert(!strcmp(match.vendor, "Vast USB") && match.source_count == 1);
    assert(device_db_match(&db, "PCI\\VEN_10EC&DEV_0001", &match));
    assert(!strcmp(match.vendor, "Realtek PCI") && !strcmp(match.sources[0], "realtek"));
    assert(!device_db_match(&db, "USB\\VID_056AX&PID_037A", &match));
    assert(!device_db_match(&db, "USB\\VID_056&PID_037A", &match));
    assert(!device_db_match(&db, "BTHENUM\\VID_056A&PID_037A", &match));
    assert(!device_db_match(&db, "arbitrary USB\\VID_056A&PID_037A", &match));
    assert(device_db_match(&db, "USB\\VID_056A", &match) && match.level == DB_VENDOR);
    assert(!match.tablet_candidates);
    memset(&match, 0, sizeof(match));
    assert(device_db_hint_name(&match, "HUION pen tablet"));
    assert(match.level == DB_UNKNOWN && !strcmp(match.sources[0], "huion"));
    assert(!device_db_hint_name(&match, "NotHuion"));
    assert(device_db_hint_name(&match, "绘王手绘屏"));
    assert(match.source_count == 1);
    device_db_free(&db);
    assert(!device_db_load(&db, "", 0, "", 0, "056A\tbroken", 11));
    assert(!db.tablets && !db.usb_vendors.items);

    size_t usb_size, pci_size, tablet_size;
    char *usb_data = read_file("database/usb.ids", &usb_size);
    char *pci_data = read_file("database/pci.ids", &pci_size);
    char *tablet_data = read_file("database/tablets.tsv", &tablet_size);
    assert(device_db_load(&db, usb_data, usb_size, pci_data, pci_size, tablet_data, tablet_size));
    assert(db.usb_products.count > 10000 && db.pci_devices.count > 10000 && db.tablet_count > 400);
    assert(device_db_match(&db, "USB\\VID_056A&PID_037A", &match));
    assert(strstr(match.model, "472"));
    assert(device_db_match(&db, "USB\\VID_256C&PID_006D", &match));
    assert(match.tablet_candidates > 1);
    assert(device_db_match(&db, "PCI\\VEN_8086&DEV_1234", &match));
    printf("USB vendors %zu, products %zu; PCI vendors %zu, devices %zu, subsystems %zu; tablet mappings %zu\n",
           db.usb_vendors.count, db.usb_products.count, db.pci_vendors.count,
           db.pci_devices.count, db.pci_subsystems.count, db.tablet_count);
    device_db_free(&db);
    free(usb_data); free(pci_data); free(tablet_data);
    return 0;
}
