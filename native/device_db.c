#include "device_db.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { const char *start; size_t length; } Span;

static int hex_digit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static int parse_hex(const char *text, size_t length, size_t digits, uint32_t *value) {
    if (length < digits) return 0;
    *value = 0;
    for (size_t index = 0; index < digits; ++index) {
        int digit = hex_digit(text[index]);
        if (digit < 0) return 0;
        *value = (*value << 4) | (unsigned)digit;
    }
    return 1;
}

static Span next_line(const char **cursor, const char *end) {
    const char *start = *cursor;
    while (*cursor < end && **cursor != '\n') ++*cursor;
    size_t size = (size_t)(*cursor - start);
    if (*cursor < end) ++*cursor;
    while (size && (start[size - 1] == '\r' || start[size - 1] == ' ')) --size;
    return (Span){start, size};
}

static int add_id(IdTable *table, uint64_t key, const char *name, size_t length) {
    if (table->count == table->capacity) {
        size_t capacity = table->capacity ? table->capacity * 2 : 256;
        IdEntry *items = realloc(table->items, capacity * sizeof(*items));
        if (!items) return 0;
        table->items = items;
        table->capacity = capacity;
    }
    table->items[table->count++] = (IdEntry){key, name, length};
    return 1;
}

static int parse_ids(const char *text, size_t size, IdTable *vendors,
                     IdTable *devices, IdTable *subsystems) {
    const char *cursor = text, *end = text + size;
    uint32_t vendor = 0, device = 0;
    int have_vendor = 0, have_device = 0;
    while (cursor < end) {
        Span line = next_line(&cursor, end);
        if (!line.length || line.start[0] == '#') continue;
        size_t tabs = 0;
        while (tabs < line.length && line.start[tabs] == '\t') ++tabs;
        uint32_t first, second;
        const char *name = line.start + tabs;
        size_t length = line.length - tabs;
        if (!parse_hex(name, length, 4, &first) || length <= 4 || name[4] != ' ') {
            if (!tabs) have_vendor = have_device = 0;
            else if (tabs == 1) have_device = 0;
            continue;
        }
        name += 4; length -= 4;
        while (length && *name == ' ') { ++name; --length; }
        if (!tabs) {
            vendor = first; have_vendor = 1; have_device = 0;
            if (!add_id(vendors, vendor, name, length)) return 0;
        } else if (tabs == 1 && have_vendor) {
            device = first; have_device = 1;
            if (!add_id(devices, ((uint64_t)vendor << 16) | device, name, length)) return 0;
        } else if (tabs == 2 && subsystems && have_vendor && have_device) {
            if (!parse_hex(name, length, 4, &second) || length <= 4 || name[4] != ' ') continue;
            name += 4; length -= 4;
            while (length && *name == ' ') { ++name; --length; }
            uint64_t key = ((uint64_t)vendor << 48) | ((uint64_t)device << 32) |
                           ((uint64_t)first << 16) | second;
            if (!add_id(subsystems, key, name, length)) return 0;
        }
    }
    return 1;
}

static int compare_id(const void *a, const void *b) {
    uint64_t left = ((const IdEntry *)a)->key, right = ((const IdEntry *)b)->key;
    return (left > right) - (left < right);
}

static int compare_tablet(const void *a, const void *b) {
    uint32_t left = ((const TabletEntry *)a)->key, right = ((const TabletEntry *)b)->key;
    return (left > right) - (left < right);
}

static int parse_tablets(DeviceDatabase *db, const char *text, size_t size) {
    const char *cursor = text, *end = text + size;
    while (cursor < end) {
        Span line = next_line(&cursor, end);
        uint32_t vid, pid;
        if (!line.length || line.start[0] == '#') continue;
        if (line.length < 12 || line.start[4] != '\t' || line.start[9] != '\t' ||
            !parse_hex(line.start, line.length, 4, &vid) ||
            !parse_hex(line.start + 5, line.length - 5, 4, &pid)) return 0;
        const char *separator = memchr(line.start + 10, '\t', line.length - 10);
        if (!separator || separator == line.start + line.length - 1) return 0;
        size_t source_length = (size_t)(separator - (line.start + 10));
        if (!source_length || source_length >= sizeof(db->tablets[0].source)) return 0;
        if (db->tablet_count == db->tablet_capacity) {
            size_t capacity = db->tablet_capacity ? db->tablet_capacity * 2 : 256;
            TabletEntry *items = realloc(db->tablets, capacity * sizeof(*items));
            if (!items) return 0;
            db->tablets = items; db->tablet_capacity = capacity;
        }
        TabletEntry *entry = &db->tablets[db->tablet_count++];
        memset(entry, 0, sizeof(*entry));
        entry->key = (vid << 16) | pid;
        memcpy(entry->source, line.start + 10, source_length);
        entry->name = separator + 1;
        entry->length = (size_t)(line.start + line.length - entry->name);
    }
    return 1;
}

void device_db_free(DeviceDatabase *db) {
    free(db->usb_vendors.items); free(db->usb_products.items);
    free(db->pci_vendors.items); free(db->pci_devices.items); free(db->pci_subsystems.items);
    free(db->tablets);
    memset(db, 0, sizeof(*db));
}

int device_db_load(DeviceDatabase *db, const char *usb, size_t usb_size,
                   const char *pci, size_t pci_size, const char *tablets, size_t tablet_size) {
    memset(db, 0, sizeof(*db));
    if (!parse_ids(usb, usb_size, &db->usb_vendors, &db->usb_products, NULL) ||
        !parse_ids(pci, pci_size, &db->pci_vendors, &db->pci_devices, &db->pci_subsystems) ||
        !parse_tablets(db, tablets, tablet_size)) {
        device_db_free(db);
        return 0;
    }
    IdTable *tables[] = {&db->usb_vendors, &db->usb_products, &db->pci_vendors,
                        &db->pci_devices, &db->pci_subsystems};
    for (size_t index = 0; index < sizeof(tables) / sizeof(tables[0]); ++index)
        if (tables[index]->count > 1)
            qsort(tables[index]->items, tables[index]->count, sizeof(IdEntry), compare_id);
    if (db->tablet_count > 1) qsort(db->tablets, db->tablet_count, sizeof(TabletEntry), compare_tablet);
    return 1;
}

static const IdEntry *find_id(const IdTable *table, uint64_t key) {
    IdEntry search = {.key = key};
    return table->count ? bsearch(&search, table->items, table->count, sizeof(IdEntry), compare_id) : NULL;
}

static void copy_name(char *target, size_t size, const char *name, size_t length) {
    if (length >= size) length = size - 1;
    memcpy(target, name, length); target[length] = 0;
}

static int starts_with(const char *text, const char *prefix) {
    for (; *prefix; ++prefix, ++text) {
        char a = *text, b = *prefix;
        if (a >= 'a' && a <= 'z') a -= 'a' - 'A';
        if (b >= 'a' && b <= 'z') b -= 'a' - 'A';
        if (a != b) return 0;
    }
    return 1;
}

static int id_field(const char *id, const char *field, size_t digits, uint32_t *out) {
    for (const char *part = id; *part; ++part) {
        if (part != id && part[-1] != '\\' && part[-1] != '&') continue;
        if (!starts_with(part, field)) continue;
        const char *value = part + strlen(field);
        size_t length = strlen(value);
        if (!parse_hex(value, length, digits, out)) return 0;
        return length == digits || value[digits] == '&' || value[digits] == '\\';
    }
    return 0;
}

static void add_source(DeviceMatch *match, const char *source) {
    if (!source || !strcmp(source, "-")) return;
    for (int index = 0; index < match->source_count; ++index)
        if (!strcmp(match->sources[index], source)) return;
    if (match->source_count < 8)
        snprintf(match->sources[match->source_count++], sizeof(match->sources[0]), "%s", source);
}

static const char *oem_source(uint32_t vendor) {
    switch (vendor) {
    case 0x17aa: return "lenovo"; case 0x1028: return "dell"; case 0x103c: return "hp";
    case 0x1043: return "asus"; case 0x1462: return "msi"; case 0x1458: return "gigabyte";
    case 0x1025: return "acer"; default: return NULL;
    }
}

static const char *vendor_source(uint32_t vendor, int usb) {
    if (usb) {
        switch (vendor) {
        case 0x056a: case 0x0531: return "wacom"; case 0x046d: return "logitech"; case 0x04b8: return "epson";
        case 0x04a9: return "canon"; case 0x04f9: return "brother"; case 0x03f0: return "hp";
        case 0x0bda: return "realtek"; case 0x8087: return "intel"; case 0x1a86: return "wch";
        case 0x10c4: return "silabs"; case 0x0403: return "ftdi"; case 0x2feb: return "veikk";
        case 0x067b: return "prolific"; case 0x0b57: return "hanvon"; case 0x1235: return "focusrite";
        case 0x0499: return "yamaha"; case 0x1532: return "razer"; case 0x1b1c: return "corsair";
        case 0x1038: return "steelseries"; case 0x0951: return "kingston"; case 0x04e8: return "samsung";
        case 0x17ef: return "lenovo"; case 0x413c: return "dell"; case 0x0b05: return "asus";
        case 0x0db0: return "msi"; case 0x0502: return "acer";
        default: return NULL;
        }
    }
    switch (vendor) {
    case 0x8086: return "intel"; case 0x10de: return "nvidia";
    case 0x1002: case 0x1022: return "amd"; case 0x10ec: return "realtek";
    case 0x144d: return "samsung"; case 0x2646: return "kingston";
    default: return oem_source(vendor);
    }
}

int device_db_match(const DeviceDatabase *db, const char *id, DeviceMatch *match) {
    uint32_t vendor, device = 0, subsystem;
    int usb = starts_with(id, "USB\\") || starts_with(id, "HID\\");
    int pci = starts_with(id, "PCI\\"), audio = starts_with(id, "HDAUDIO\\");
    memset(match, 0, sizeof(*match));
    if ((!usb && !pci && !audio) || !id_field(id, usb ? "VID_" : "VEN_", 4, &vendor)) return 0;
    int have_device = id_field(id, usb ? "PID_" : "DEV_", 4, &device);
    const IdEntry *entry = find_id(usb ? &db->usb_vendors : &db->pci_vendors, vendor);
    if (entry) { copy_name(match->vendor, sizeof(match->vendor), entry->name, entry->length); match->level = DB_VENDOR; }
    if (have_device && !audio) {
        uint64_t key = ((uint64_t)vendor << 16) | device;
        entry = find_id(usb ? &db->usb_products : &db->pci_devices, key);
        if (entry) { copy_name(match->model, sizeof(match->model), entry->name, entry->length); match->level = DB_DEVICE; }
        if (pci && id_field(id, "SUBSYS_", 8, &subsystem)) {
            uint32_t subvendor = subsystem & 0xffff, subdevice = subsystem >> 16;
            entry = find_id(&db->pci_subsystems, (key << 32) | ((uint64_t)subvendor << 16) | subdevice);
            if (entry) { copy_name(match->model, sizeof(match->model), entry->name, entry->length); match->level = DB_SUBSYSTEM; }
            add_source(match, oem_source(subvendor));
        }
        if (usb) {
            size_t low = 0, high = db->tablet_count;
            while (low < high) {
                size_t middle = low + (high - low) / 2;
                if (db->tablets[middle].key < key) low = middle + 1;
                else high = middle;
            }
            size_t used = 0;
            for (size_t index = low; index < db->tablet_count && db->tablets[index].key == key; ++index) {
                const TabletEntry *tablet = &db->tablets[index];
                if (!match->tablet_candidates) match->model[0] = 0;
                ++match->tablet_candidates;
                add_source(match, tablet->source);
                if (match->tablet_candidates <= 3 && used + tablet->length + 4 < sizeof(match->model)) {
                    if (used) { memcpy(match->model + used, " / ", 3); used += 3; }
                    memcpy(match->model + used, tablet->name, tablet->length);
                    used += tablet->length; match->model[used] = 0;
                }
            }
            if (match->tablet_candidates) {
                match->level = DB_TABLET;
                add_source(match, "opentabletdriver");
            }
        }
    }
    add_source(match, vendor_source(vendor, usb));
    add_source(match, "microsoft-catalog");
    return match->level != DB_UNKNOWN || match->source_count > 1;
}

int device_db_hint_name(DeviceMatch *match, const char *name) {
    static const struct { const char *name, *source; } hints[] = {
        {"Wacom", "wacom"}, {"Huion", "huion"}, {"GAOMON", "gaomon"},
        {"XP-Pen", "xppen"}, {"XPPen", "xppen"}, {"UGEE", "ugee"},
        {"Xencelabs", "xencelabs"}, {"VEIKK", "veikk"}, {"Parblo", "parblo"},
        {"Artisul", "artisul"}, {"BOSTO", "bosto"}, {"Hanvon", "hanvon"},
        {"绘王", "huion"}, {"高漫", "gaomon"}, {"友基", "ugee"}, {"和冠", "wacom"}, {"汉王", "hanvon"}
    };
    int found = 0;
    for (size_t index = 0; index < sizeof(hints)/sizeof(hints[0]); ++index) {
        for (const char *part = name; *part; ++part) {
            if (part != name && ((part[-1] >= 'A' && part[-1] <= 'Z') ||
                (part[-1] >= 'a' && part[-1] <= 'z'))) continue;
            if (!starts_with(part, hints[index].name)) continue;
            char following = part[strlen(hints[index].name)];
            if ((following >= 'A' && following <= 'Z') || (following >= 'a' && following <= 'z')) continue;
            add_source(match, hints[index].source);
            found = 1;
            break;
        }
    }
    return found;
}
