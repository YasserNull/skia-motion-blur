#define _POSIX_C_SOURCE 200809L
#include "input_monitor.h"
#include "control.h"
#include "policy.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MB_INPUT_DEVICE_COUNT 32
#define MB_TOUCH_WINDOW_MS 250
#define MB_TOUCH_WRITE_INTERVAL_MS 40
#define MB_BITS_PER_WORD (sizeof(unsigned long) * CHAR_BIT)
#define MB_BIT_WORD(bit) ((bit) / MB_BITS_PER_WORD)
#define MB_BIT_MASK(bit) (1ul << ((bit) % MB_BITS_PER_WORD))

typedef struct {
    int fd;
    char path[PATH_MAX];
    bool multitouch;
    bool active;
    int slot;
    bool slots[32];
} MbInputDevice;

static MbInputDevice devices[MB_INPUT_DEVICE_COUNT];
static atomic_uint_fast64_t latest_touch;
static uint64_t last_written;

static bool has_bit(const unsigned long *bits, unsigned bit) {
    return (bits[MB_BIT_WORD(bit)] & MB_BIT_MASK(bit)) != 0;
}

static bool touch_device(int fd, bool *multitouch) {
    unsigned long events[(EV_MAX + MB_BITS_PER_WORD) / MB_BITS_PER_WORD] = {0};
    unsigned long keys[(KEY_MAX + MB_BITS_PER_WORD) / MB_BITS_PER_WORD] = {0};
    unsigned long axes[(ABS_MAX + MB_BITS_PER_WORD) / MB_BITS_PER_WORD] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(events)), events) < 0 || !has_bit(events, EV_ABS) ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(axes)), axes) < 0) return false;
    bool button = has_bit(events, EV_KEY) && ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) == 0 &&
                  has_bit(keys, BTN_TOUCH);
    bool mt = has_bit(axes, ABS_MT_POSITION_X) && has_bit(axes, ABS_MT_POSITION_Y) &&
              (has_bit(axes, ABS_MT_TRACKING_ID) || button);
    bool single = has_bit(axes, ABS_X) && has_bit(axes, ABS_Y) && button;
    if (!mt && !single) return false;
    *multitouch = mt;
    return true;
}

static bool known_device(const char *path) {
    for (size_t i = 0; i < MB_INPUT_DEVICE_COUNT; ++i)
        if (devices[i].fd >= 0 && !strcmp(devices[i].path, path)) return true;
    return false;
}

static void discover_devices(void) {
    DIR *directory = opendir("/dev/input");
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (strncmp(entry->d_name, "event", 5) || !entry->d_name[5]) continue;
        bool numeric = true;
        for (const char *at = entry->d_name + 5; *at; ++at)
            if (*at < '0' || *at > '9') numeric = false;
        if (!numeric) continue;
        char path[PATH_MAX];
        int size = snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
        if (size < 0 || (size_t)size >= sizeof(path) || known_device(path)) continue;
        size_t index = 0;
        while (index < MB_INPUT_DEVICE_COUNT && devices[index].fd >= 0) ++index;
        if (index == MB_INPUT_DEVICE_COUNT) break;
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        bool multitouch = false;
        if (!touch_device(fd, &multitouch)) { close(fd); continue; }
        devices[index].fd = fd;
        devices[index].multitouch = multitouch;
        devices[index].slot = 0;
        strcpy(devices[index].path, path);
    }
    closedir(directory);
}

static void record_touch(bool force) {
    uint64_t deadline = mb_millis() + MB_TOUCH_WINDOW_MS;
    uint64_t previous = atomic_load_explicit(&latest_touch, memory_order_relaxed);
    if (!force && deadline <= previous + MB_TOUCH_WRITE_INTERVAL_MS) return;
    atomic_store_explicit(&latest_touch, deadline, memory_order_relaxed);
    if (!force && deadline <= last_written + MB_TOUCH_WRITE_INTERVAL_MS) return;
    char text[32];
    snprintf(text, sizeof(text), "%llu\n", (unsigned long long)deadline);
    if (mb_write_control("/data/misc/skia-motion-blur/skia-motion-blur.touch", text))
        last_written = deadline;
}

static void process_event(MbInputDevice *device, const struct input_event *event) {
    if (event->type == EV_KEY && event->code == BTN_TOUCH) {
        device->active = event->value != 0;
        record_touch(true);
        return;
    }
    if (event->type != EV_ABS) return;
    if (device->multitouch && event->code == ABS_MT_SLOT) {
        if (event->value >= 0 && event->value < (int)(sizeof(device->slots) / sizeof(device->slots[0])))
            device->slot = event->value;
        return;
    }
    if (device->multitouch && event->code == ABS_MT_TRACKING_ID) {
        device->slots[device->slot] = event->value >= 0;
        device->active = false;
        for (size_t i = 0; i < sizeof(device->slots) / sizeof(device->slots[0]); ++i)
            device->active |= device->slots[i];
        record_touch(true);
        return;
    }
    bool position = device->multitouch ?
        event->code == ABS_MT_POSITION_X || event->code == ABS_MT_POSITION_Y :
        event->code == ABS_X || event->code == ABS_Y;
    if (position && device->active) record_touch(false);
}

static void close_device(MbInputDevice *device) {
    if (device->fd >= 0) close(device->fd);
    memset(device, 0, sizeof(*device));
    device->fd = -1;
}

static void *monitor(void *unused) {
    (void)unused;
    for (size_t i = 0; i < MB_INPUT_DEVICE_COUNT; ++i) devices[i].fd = -1;
    uint64_t next_discovery = 0;
    while (true) {
        uint64_t now = mb_millis();
        if (now >= next_discovery) {
            discover_devices();
            next_discovery = now + 2000;
        }
        struct pollfd descriptors[MB_INPUT_DEVICE_COUNT];
        MbInputDevice *mapped[MB_INPUT_DEVICE_COUNT];
        nfds_t count = 0;
        for (size_t i = 0; i < MB_INPUT_DEVICE_COUNT; ++i) {
            if (devices[i].fd < 0) continue;
            descriptors[count] = (struct pollfd){ .fd = devices[i].fd, .events = POLLIN };
            mapped[count++] = &devices[i];
        }
        int result = poll(descriptors, count, 500);
        if (result <= 0) continue;
        for (nfds_t i = 0; i < count; ++i) {
            if (descriptors[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                close_device(mapped[i]);
                continue;
            }
            if (!(descriptors[i].revents & POLLIN)) continue;
            struct input_event events[32];
            ssize_t bytes = read(descriptors[i].fd, events, sizeof(events));
            if (bytes <= 0) continue;
            size_t event_count = (size_t)bytes / sizeof(events[0]);
            for (size_t j = 0; j < event_count; ++j) process_event(mapped[i], &events[j]);
        }
    }
    return NULL;
}

void mb_input_monitor_start(void) {
    pthread_t thread;
    if (!pthread_create(&thread, NULL, monitor, NULL)) pthread_detach(thread);
}
