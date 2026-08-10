#include <jni.h>
#include <android/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <cstring>
#include <cerrno>
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#include <sys/ioctl.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <condition_variable>
#include <shared_mutex>
#include <netinet/tcp.h>

#define LOG_TAG "usbip_server"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define USBIP_PORT 3240
#define USBIP_VERSION 0x0111

static int g_server_socket = -1;
static std::mutex g_socket_mutex;
static std::thread g_server_thread;
static std::vector<int> g_client_sockets;
static std::mutex g_clients_mutex;
static std::atomic<bool> g_device_fatal_error{false};

static std::unordered_map<std::string, int> g_active_devices;
static std::shared_mutex g_devices_rw_mutex;
static std::condition_variable_any g_device_update_cv;

static JavaVM* g_jvm = nullptr;
static jobject g_service_obj = nullptr;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// Helper to call JNI getters
std::string get_cached_string(const char* method_name) {
    if (!g_jvm || !g_service_obj) return "";
    JNIEnv* env;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        #ifdef __ANDROID__
            g_jvm->AttachCurrentThread(&env, nullptr);
        #else
            g_jvm->AttachCurrentThread((void**)&env, nullptr);
        #endif
        attached = true;
    }
    jclass cls = env->GetObjectClass(g_service_obj);
    jmethodID mid = env->GetMethodID(cls, method_name, "()Ljava/lang/String;");
    jstring jstr = (jstring)env->CallObjectMethod(g_service_obj, mid);
    const char* str = env->GetStringUTFChars(jstr, nullptr);
    std::string result(str);
    env->ReleaseStringUTFChars(jstr, str);
    if (attached) g_jvm->DetachCurrentThread();
    return result;
}

int get_cached_int(const char* method_name) {
    if (!g_jvm || !g_service_obj) return 0;
    JNIEnv* env;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        #ifdef __ANDROID__
            g_jvm->AttachCurrentThread(&env, nullptr);
        #else
            g_jvm->AttachCurrentThread((void**)&env, nullptr);
        #endif
        attached = true;
    }
    jclass cls = env->GetObjectClass(g_service_obj);
    jmethodID mid = env->GetMethodID(cls, method_name, "()I");
    int result = env->CallIntMethod(g_service_obj, mid);
    if (attached) g_jvm->DetachCurrentThread();
    return result;
}

int get_int_for_busid(const char* method_name, const std::string& busid) {
    if (!g_jvm || !g_service_obj) return 0;
    JNIEnv* env;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        #ifdef __ANDROID__
            g_jvm->AttachCurrentThread(&env, nullptr);
        #else
            g_jvm->AttachCurrentThread((void**)&env, nullptr);
        #endif
        attached = true;
    }
    jclass cls = env->GetObjectClass(g_service_obj);
    jmethodID mid = env->GetMethodID(cls, method_name, "(Ljava/lang/String;)I");
    jstring jbusid = env->NewStringUTF(busid.c_str());
    int result = env->CallIntMethod(g_service_obj, mid, jbusid);
    env->DeleteLocalRef(jbusid);
    if (attached) g_jvm->DetachCurrentThread();
    return result;
}

std::string get_string_for_index(const char* method_name, int index) {
    if (!g_jvm || !g_service_obj) return "";
    JNIEnv* env;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        #ifdef __ANDROID__
            g_jvm->AttachCurrentThread(&env, nullptr);
        #else
            g_jvm->AttachCurrentThread((void**)&env, nullptr);
        #endif
        attached = true;
    }
    jclass cls = env->GetObjectClass(g_service_obj);
    jmethodID mid = env->GetMethodID(cls, method_name, "(I)Ljava/lang/String;");
    jstring jstr = (jstring)env->CallObjectMethod(g_service_obj, mid, index);
    if (!jstr) {
        if (attached) g_jvm->DetachCurrentThread();
        return "";
    }
    const char* str = env->GetStringUTFChars(jstr, nullptr);
    std::string result(str);
    env->ReleaseStringUTFChars(jstr, str);
    if (attached) g_jvm->DetachCurrentThread();
    return result;
}

// USB/IP OP codes (handshake)
#define OP_REQ_IMPORT 0x8003
#define OP_REP_IMPORT 0x0003
#define OP_REQ_DEVLIST 0x8005
#define OP_REP_DEVLIST 0x0005

// USB/IP Commands
#define USBIP_CMD_SUBMIT 0x0001
#define USBIP_RET_SUBMIT 0x0003
#define USBIP_CMD_UNLINK 0x0002
#define USBIP_RET_UNLINK 0x0004

struct op_common {
    uint16_t version;
    uint16_t code;
    uint32_t status;
} __attribute__((packed));

struct usbip_usb_device {
    char path[256];
    char busid[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
} __attribute__((packed));

struct usbip_usb_interface {
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t padding;
} __attribute__((packed));

struct endpoint_info {
    uint8_t addr;
    uint8_t type;
    uint16_t max_packet_size;
};

struct usbip_header {
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
    uint32_t transfer_flags;
    uint32_t transfer_buffer_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t interval;
    uint8_t setup[8];
} __attribute__((packed));

struct usbip_ret_submit {
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
    uint32_t status;
    uint32_t actual_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t error_count;
    uint8_t padding[8];
} __attribute__((packed));

struct async_urb_context {
    int client_fd;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
    uint8_t* payload_buffer;
    struct usbdevfs_urb urb; // Must be at the end
};

std::atomic<int> in_flight_urbs_count{0};
std::mutex in_flight_mutex;
std::unordered_map<uint32_t, async_urb_context*> active_urbs;

void cleanup_zombie_urbs(int device_fd, int client_fd) {
    std::lock_guard<std::mutex> lock(in_flight_mutex);
    int discarded_count = 0;

    for (auto const& [seq, ctx] : active_urbs) {
        if (ctx->client_fd == client_fd) {
            LOGI("Watchdog: Triggering hardware discard for URB: seq=%u", ntohl(ctx->seqnum));
            ioctl(device_fd, USBDEVFS_DISCARDURB, &ctx->urb);
            discarded_count++;
        }
    }
    LOGI("Watchdog: Triggered hardware discard for %d zombie URBs. Reaper will perform final cleanup.", discarded_count);
}

void reap_thread(std::string busid, int device_fd, std::shared_ptr<std::atomic<bool>> is_connected) {
    LOGI("URB Reaper thread started for bus %s.", busid.c_str());
    // Continue reaping as long as connected OR there are pending URBs in the system
    while (is_connected->load() || in_flight_urbs_count.load() > 0) {
        struct usbdevfs_urb *urb = nullptr;
        int res = ioctl(device_fd, USBDEVFS_REAPURBNDELAY, &urb);
        if (res < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            LOGE("REAPURB failed on bus %s: %s", busid.c_str(), strerror(errno));
            // If the device is gone, wait for a new FD if it was a hotplug event
            if (errno == ENODEV || errno == EBADF) {
                LOGW("reap_thread: Device lost on bus %s. Waiting for new FD...", busid.c_str());
                std::unique_lock<std::shared_mutex> lock(g_devices_rw_mutex);
                if (g_device_update_cv.wait_for(lock, std::chrono::seconds(10), [&busid]{
                    return g_active_devices.count(busid) && g_active_devices[busid] != -1;
                })) {
                    device_fd = g_active_devices[busid];
                    LOGI("reap_thread: Resuming on bus %s with new FD %d", busid.c_str(), device_fd);
                    continue;
                }
                LOGE("reap_thread: Timeout waiting for new device FD on bus %s.", busid.c_str());
                g_device_fatal_error.store(true);
                break;
            }
            continue;
        }

        auto *ctx = (struct async_urb_context *)urb->usercontext;
        if (!ctx) continue;

        {
            std::lock_guard<std::mutex> lock(in_flight_mutex);
            active_urbs.erase(ntohl(ctx->seqnum));
            in_flight_urbs_count--;
        }

        struct usbip_ret_submit ret{};
        ret.command = htonl(USBIP_RET_SUBMIT);
        ret.seqnum = ctx->seqnum;
        ret.devid = ctx->devid;
        ret.direction = ctx->direction;
        ret.ep = ctx->ep;

        // Linux kernel's actual_length represents only the data phase
        ret.status = htonl((uint32_t)urb->status);
        ret.actual_length = htonl(urb->actual_length);

        if (urb->status != 0 && urb->status != -ENOENT) {
            if (urb->status == -EPIPE) {
                LOGW("W [G29 Profile] Mode switch stall detected on bus %s. Awaiting re-enumeration...", busid.c_str());
                // Pause and wait for new FD from Kotlin hotplug
                std::unique_lock<std::shared_mutex> lock(g_devices_rw_mutex);
                g_device_update_cv.wait_for(lock, std::chrono::seconds(3), [&busid]{
                    return g_active_devices.count(busid) && g_active_devices[busid] != -1;
                });
                if (g_active_devices.count(busid) && g_active_devices[busid] != -1) {
                    device_fd = g_active_devices[busid];
                }
            } else {
                LOGE("URB failed with status %d (ep=%u, seq=%u) on bus %s", urb->status, ntohl(ctx->ep), ntohl(ctx->seqnum), busid.c_str());
            }
        }

        // Send results only if the specific connection is still active
        // But ALWAYS proceed to the cleanup logic below
        if (is_connected->load()) {
            // 1. Send the 48-byte RET_SUBMIT header
            if (send(ctx->client_fd, &ret, sizeof(ret), 0) < 0) {
                LOGE("Failed to send RET_SUBMIT header (socket closed?)");
            } else if (urb->actual_length > 0 && ntohl(ctx->direction) == 1) {
                // 2. IN transfer detected: immediately send data payload
                uint8_t* data_ptr = ctx->payload_buffer;
                if (ctx->urb.type == USBDEVFS_URB_TYPE_CONTROL) {
                    data_ptr += 8; // Skip setup packet in control buffer
                }
                if (send(ctx->client_fd, data_ptr, urb->actual_length, 0) < 0) {
                    LOGE("Failed to send RET_SUBMIT data (socket closed?)");
                }
            }

            LOGI("<<< RET_SUBMIT (Async): seq=%u, status=%d, len=%u",
                 ntohl(ctx->seqnum), (int32_t)ntohl(ret.status), urb->actual_length);
        } else {
            LOGI("URB reaped after disconnect, suppressing network send: seq=%u", ntohl(ctx->seqnum));
        }

        // CRITICAL: Memory cleanup MUST happen for every reaped URB
        if (ctx->payload_buffer) delete[] ctx->payload_buffer;
        free(ctx);
    }
    LOGI("URB Reaper thread exiting.");
}

ssize_t recv_all(int fd, void *buf, size_t len) {
    size_t total = 0;
    char *p = (char *)buf;
    while (total < len) {
        ssize_t n = recv(fd, p + total, len - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

void get_device_info(int device_fd, struct usbip_usb_device *dev, std::vector<struct usbip_usb_interface> *intfs, std::vector<endpoint_info> *eps, const char *busid) {
    struct usb_device_descriptor desc{};
    struct usbdevfs_ctrltransfer ctrl{};
    ctrl.bRequestType = 0x80;
    ctrl.bRequest = 0x06;
    ctrl.wValue = 0x0100;
    ctrl.wLength = sizeof(desc);
    ctrl.timeout = 1000;
    ctrl.data = &desc;

    memset(dev, 0, sizeof(*dev));
    // Populate path and busid with zero padding
    snprintf(dev->path, sizeof(dev->path), "/sys/devices/virtual/usbip/%s", busid);
    strncpy(dev->busid, busid, sizeof(dev->busid) - 1);

    dev->busnum = htonl(1);
    dev->devnum = htonl(2);
    dev->speed = htonl(3); // High speed (3)

    if (device_fd == -1 || ioctl(device_fd, USBDEVFS_CONTROL, &ctrl) < 0) {
        if (device_fd != -1 && (errno == ENODEV || errno == EBADF)) {
            LOGW("get_device_info: Device temporarily gone (ENODEV), falling back to cache.");
        } else {
            LOGE("get_device_info: IOCTL failed, using cached metadata.");
        }

        // Use cached metadata from Kotlin (Bus-specific)
        std::string s_busid(busid);
        dev->idVendor = htons((uint16_t)get_int_for_busid("getVidForBusId", s_busid));
        dev->idProduct = htons((uint16_t)get_int_for_busid("getPidForBusId", s_busid));
        dev->bcdDevice = htons(0x0100);
        dev->bDeviceClass = 0;
        dev->bDeviceSubClass = 0;
        dev->bDeviceProtocol = 0;
        dev->bConfigurationValue = 1;
        dev->bNumConfigurations = 1;
        dev->bNumInterfaces = (uint8_t)get_int_for_busid("getInterfaceCountForBusId", s_busid);
        if (dev->bNumInterfaces == 0) dev->bNumInterfaces = 1;

        intfs->clear();
        struct usbip_usb_interface i = { 3, 0, 0, 0 }; // Default to HID
        intfs->push_back(i);
        return;
    }

    dev->idVendor = htons(desc.idVendor);
    dev->idProduct = htons(desc.idProduct);
    dev->bcdDevice = htons(desc.bcdDevice);
    dev->bDeviceClass = desc.bDeviceClass;
    dev->bDeviceSubClass = desc.bDeviceSubClass;
    dev->bDeviceProtocol = desc.bDeviceProtocol;
    dev->bConfigurationValue = 1;
    dev->bNumConfigurations = desc.bNumConfigurations;

    intfs->clear();
    eps->clear();
    std::vector<uint8_t> config_desc(1024);
    ctrl.wValue = 0x0200;
    ctrl.wLength = (uint16_t)config_desc.size();
    ctrl.data = config_desc.data();
    int len = ioctl(device_fd, USBDEVFS_CONTROL, &ctrl);
    if (len >= 9) {
        int pos = 0;
        while (pos < len) {
            uint8_t d_len = config_desc[pos];
            if (d_len < 2 || pos + d_len > len) break;
            uint8_t d_type = config_desc[pos + 1];
            if (d_type == 0x04) { // Interface
                struct usbip_usb_interface i;
                i.bInterfaceClass = config_desc[pos + 5];
                i.bInterfaceSubClass = config_desc[pos + 6];
                i.bInterfaceProtocol = config_desc[pos + 7];
                i.padding = 0;
                intfs->push_back(i);
            } else if (d_type == 0x05) { // Endpoint
                endpoint_info info;
                info.addr = config_desc[pos + 2];
                info.type = config_desc[pos + 3] & 0x03;
                info.max_packet_size = config_desc[pos + 4] | (config_desc[pos + 5] << 8);
                eps->push_back(info);
            }
            pos += d_len;
        }
    }
    dev->bNumInterfaces = (uint8_t)intfs->size();
    if (dev->bNumInterfaces == 0) dev->bNumInterfaces = 1;
}

void handle_client(int client_fd, int device_fd) {
    auto is_connected = std::make_shared<std::atomic<bool>>(true);
    std::string current_busid = "1-1";

    uint8_t header_buf[8];
    ssize_t bytes_read = recv(client_fd, header_buf, 8, MSG_WAITALL);
    if (bytes_read < 8) {
        LOGW("handle_client: Failed to read 8-byte header (got %zd). Closing.", bytes_read);
        is_connected->store(false);
        return;
    }

    // Safely extract version and code from Big-Endian header
    uint16_t version = (header_buf[0] << 8) | header_buf[1];
    uint16_t code = (header_buf[2] << 8) | header_buf[3];
    uint32_t status = (header_buf[4] << 24) | (header_buf[5] << 16) | (header_buf[6] << 8) | header_buf[7];

    if (code == OP_REQ_DEVLIST) {
        LOGI("Handling OP_REQ_DEVLIST (Hardcoded Raw Payload)");

        // 8 (header) + 4 (ndev) + 312 (device summary) + 4 (1 interface) = 328 bytes
        uint8_t reply[328];
        memset(reply, 0, sizeof(reply));

        // 1. op_common header
        reply[0] = 0x01; reply[1] = 0x11; // version (0x0111)
        reply[2] = 0x00; reply[3] = 0x05; // reply_code (OP_REP_DEVLIST)
        // 4,5,6,7 are status (0x00000000)

        // 2. ndev (Number of exported devices = 1)
        reply[11] = 0x01;

        // 3. usbip_device_summary
        // path (256 bytes) [12 to 267]
        strncpy((char*)&reply[12], "/sys/devices/pci0000:00/0000:00:01.2/usb1/1-1", 255);
        // busid (32 bytes) [268 to 299]
        strncpy((char*)&reply[268], "1-1", 31);
        // busnum (4 bytes) [300 to 303]
        reply[303] = 0x01;
        // devnum (4 bytes) [304 to 307]
        reply[307] = 0x02;
        // speed (4 bytes) [308 to 311] (2 = USB_SPEED_HIGH)
        reply[311] = 0x02;
        // idVendor (2 bytes) [312 to 313] (0x046D = 1133)
        reply[312] = 0x04; reply[313] = 0x6D;
        // idProduct (2 bytes) [314 to 315] (0xC260 = 49760 from logcat)
        reply[314] = 0xC2; reply[315] = 0x60;
        // bcdDevice (2 bytes) [316 to 317]
        reply[316] = 0x01; reply[317] = 0x11;
        // bDeviceClass, SubClass, Protocol [318, 319, 320] remain 0x00
        // bConfigurationValue [321]
        reply[321] = 0x01;
        // bNumConfigurations [322]
        reply[322] = 0x01;
        // bNumInterfaces [323]
        reply[323] = 0x01; // Tells Windows to expect 1 interface block

        // 4. usbip_usb_interface (4 bytes) [324 to 327]
        reply[324] = 0x03; // bInterfaceClass (0x03 = HID)
        // 325, 326, 327 remain 0x00 for SubClass, Protocol, and Padding

        // 5. Send exact payload
        ssize_t sent = send(client_fd, reply, sizeof(reply), 0);
        if (sent < 0) {
            LOGE("OP_REQ_DEVLIST: Failed to send raw reply: %s", strerror(errno));
        } else {
            LOGI("OP_REQ_DEVLIST: Sent hardcoded 328-byte payload.");
            // Graceful teardown: Tell Windows we are done transmitting, but keep socket alive to flush buffer
            shutdown(client_fd, SHUT_WR);
            // Wait 100ms for Windows to read the packet before physically destroying the socket in the caller
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        is_connected->store(false);
        return;
    }


    if (code == OP_REQ_IMPORT) {
        uint8_t busid_buf[32];
        if (recv(client_fd, busid_buf, 32, MSG_WAITALL) < 32) {
            LOGW("OP_REQ_IMPORT: Failed to read 32-byte Bus ID. Closing.");
            is_connected->store(false);
            return;
        }
        std::string busid((char*)busid_buf);
        LOGI("Handling OP_REQ_IMPORT for busid %s", busid.c_str());

        // Resolve FD for this specific Bus ID
        int resolved_fd = -1;
        {
            std::shared_lock<std::shared_mutex> lock(g_devices_rw_mutex);
            if (g_active_devices.count(busid)) {
                resolved_fd = g_active_devices[busid];
            }
        }

        if (resolved_fd == -1) {
            LOGW("OP_REQ_IMPORT: Bus ID %s not found in native map. querying Kotlin...", busid.c_str());
            resolved_fd = get_int_for_busid("getFdForBusId", busid);
        }

        if (resolved_fd == -1) {
            LOGE("OP_REQ_IMPORT: No device found for Bus ID %s", busid.c_str());
            is_connected->store(false);
            return;
        }
        device_fd = resolved_fd;
        current_busid = busid;

        // Reset device on import to clear any stuck state
        if (ioctl(device_fd, USBDEVFS_RESET) < 0) {
            LOGE("Warning: USBDEVFS_RESET failed: %s", strerror(errno));
        } else {
            LOGI("Device reset successful on import");
        }

        struct usbip_usb_device dev;
        std::vector<struct usbip_usb_interface> intfs;
        std::vector<endpoint_info> eps;
        get_device_info(device_fd, &dev, &intfs, &eps, busid.c_str());

        struct op_common reply_header = { htons(USBIP_VERSION), htons(OP_REP_IMPORT), htonl(0) };
        if (send(client_fd, &reply_header, sizeof(reply_header), 0) < 0) {
            LOGE("OP_REQ_IMPORT: Failed to send header: %s", strerror(errno));
            is_connected->store(false); return;
        }
        if (send(client_fd, &dev, sizeof(dev), 0) < 0) {
            LOGE("OP_REQ_IMPORT: Failed to send device info: %s", strerror(errno));
            is_connected->store(false); return;
        }

        LOGI("Handshake complete. Starting URB reaper thread for bus %s...", current_busid.c_str());
        std::thread(reap_thread, current_busid, device_fd, is_connected).detach();

        LOGI("Evicting Android kernel drivers...");

        // Try to disconnect and claim the maximum possible interfaces (0-15)
        for (int i = 0; i < 16; i++) {
            struct usbdevfs_ioctl disconnect;
            disconnect.ifno = i;
            disconnect.ioctl_code = USBDEVFS_DISCONNECT;
            disconnect.data = nullptr;

            // 1. Attempt to detach Android's native driver
            int disconnect_ret = ioctl(device_fd, USBDEVFS_IOCTL, &disconnect);

            // 2. Attempt to claim it for our server
            int intf = i;
            int claim_ret = ioctl(device_fd, USBDEVFS_CLAIMINTERFACE, &intf);

            if (claim_ret == 0) {
                if (disconnect_ret == 0) {
                    LOGI("Kicked Android driver and claimed Interface %d", i);
                } else {
                    LOGI("Claimed Interface %d (No Android driver was attached)", i);
                }
            }
            // If claim_ret < 0, the interface simply doesn't exist on this device, which is fine.
        }

        LOGI("Entering CMD_SUBMIT loop.");

        while (true) {
            struct usbip_header header{};
            ssize_t bytes_read = recv(client_fd, &header, sizeof(header), MSG_WAITALL);
            if (bytes_read <= 0) {
                LOGI("Client disconnected (recv returned %zd). errno: %d", bytes_read, errno);
                break;
            }

            uint32_t command = ntohl(header.command);
            uint32_t seqnum  = ntohl(header.seqnum);
            uint32_t ep      = ntohl(header.ep) & 0x7F;
            uint32_t dir     = ntohl(header.direction);
            uint32_t transfer_len = ntohl(header.transfer_buffer_length);

            if (command == USBIP_CMD_SUBMIT) {
                LOGI(">>> CMD_SUBMIT: seq=%u, ep=%u, dir=%s, len=%u",
                     seqnum, ep, (dir ? "IN" : "OUT"), transfer_len);

                // Check for device FD update before submitting (Shared lock)
                {
                    std::shared_lock<std::shared_mutex> lock(g_devices_rw_mutex);
                    if (g_active_devices.count(current_busid) && g_active_devices[current_busid] != -1 && g_active_devices[current_busid] != device_fd) {
                        device_fd = g_active_devices[current_busid];
                    }
                }

                auto *ctx = (async_urb_context *)calloc(1, sizeof(async_urb_context));
                memset(&ctx->urb, 0, sizeof(struct usbdevfs_urb)); // Explicit zero-init

                ctx->client_fd = client_fd;
                ctx->seqnum = header.seqnum;
                ctx->devid = header.devid;
                ctx->direction = header.direction;
                ctx->ep = header.ep;

                // 1. Allocate buffer and receive OUT data if necessary
                if (ep == 0) {
                    ctx->payload_buffer = new uint8_t[8 + transfer_len];
                    memcpy(ctx->payload_buffer, header.setup, 8);
                    if (dir == 0 && transfer_len > 0) { // OUT
                        if (recv_all(client_fd, ctx->payload_buffer + 8, transfer_len) != (ssize_t)transfer_len) {
                            LOGE("Failed to receive OUT payload for ep0");
                            delete[] ctx->payload_buffer; free(ctx); break;
                        }
                    }
                    ctx->urb.type = USBDEVFS_URB_TYPE_CONTROL;
                    ctx->urb.buffer = ctx->payload_buffer;
                    ctx->urb.buffer_length = 8 + transfer_len;
                } else {
                    if (transfer_len > 0) {
                        ctx->payload_buffer = new uint8_t[transfer_len];
                        if (dir == 0) { // OUT
                            if (recv_all(client_fd, ctx->payload_buffer, transfer_len) != (ssize_t)transfer_len) {
                                LOGE("Failed to receive OUT payload for ep%u", ep);
                                delete[] ctx->payload_buffer; free(ctx); break;
                            }

                            // Verify data integrity for Bulk OUT
                            if (transfer_len >= 4) {
                                LOGI("Bulk OUT Payload Signature: %c%c%c%c",
                                     ctx->payload_buffer[0], ctx->payload_buffer[1],
                                     ctx->payload_buffer[2], ctx->payload_buffer[3]);
                            }

                            // Phase 25: CBW Hex Dump Watchdog for Mass Storage
                            if (transfer_len >= 31 && ctx->payload_buffer[0] == 'U' && ctx->payload_buffer[1] == 'S' && ctx->payload_buffer[2] == 'B' && ctx->payload_buffer[3] == 'C') {
                                char hex[128];
                                int pos = 0;
                                for(int i=0; i<16 && i<(int)transfer_len; i++) pos += sprintf(hex+pos, "%02X ", ctx->payload_buffer[i]);
                                LOGI("CBW Detected: %s", hex);
                            }
                        }
                    }
                    ctx->urb.buffer = ctx->payload_buffer;
                    ctx->urb.buffer_length = transfer_len;

                    // Detect endpoint type
                    uint8_t ep_addr = ep | (dir ? 0x80 : 0);
                    uint8_t ep_type = 0x02; // Default BULK
                    for (const auto& info : eps) {
                        if (info.addr == ep_addr) { ep_type = info.type; break; }
                    }

                    if (ep_type == 0x01) { // ISOC fallback
                        LOGW("ISOC detected on 0x%02X. Dropping.", ep_addr);
                        struct usbip_ret_submit ret{};
                        ret.command = htonl(USBIP_RET_SUBMIT);
                        ret.seqnum = ctx->seqnum; ret.devid = ctx->devid;
                        ret.direction = ctx->direction; ret.ep = ctx->ep;
                        ret.status = htonl((uint32_t)-EOPNOTSUPP);
                        send(client_fd, &ret, sizeof(ret), 0);
                        if (ctx->payload_buffer) delete[] ctx->payload_buffer;
                        free(ctx); continue;
                    }
                    ctx->urb.type = (ep_type == 0x03) ? 1 : 3; // 1=Interrupt, 3=Bulk
                }

                ctx->urb.usercontext = ctx;
                ctx->urb.endpoint = ep | (dir == 1 ? 0x80 : 0);

                // Control Transfer Interceptor for Endpoint 0
                if (ep == 0) {
                    uint8_t bmRequestType = header.setup[0];
                    uint8_t bRequest      = header.setup[1];
                    uint16_t wValue = header.setup[2] | (header.setup[3] << 8);
                    uint16_t wIndex = header.setup[4] | (header.setup[5] << 8);

                    if (bmRequestType == 0x00 && bRequest == 0x09) {
                        // SET_CONFIGURATION
                        LOGI("Intercepted SET_CONFIGURATION");
                        // Must release interfaces before setting config
                        for (int i = 0; i < 16; i++) {
                            int intf = i;
                            ioctl(device_fd, USBDEVFS_RELEASEINTERFACE, &intf);
                        }

                        unsigned int config_val = wValue;
                        int res = ioctl(device_fd, USBDEVFS_SETCONFIGURATION, &config_val);
                        LOGI("SET_CONFIGURATION to %u, res=%d", config_val, res);

                        // Re-secure the interfaces
                        for (int i = 0; i < 16; i++) {
                            int intf = i;
                            ioctl(device_fd, USBDEVFS_CLAIMINTERFACE, &intf);
                        }

                        struct usbip_ret_submit ret{};
                        ret.command = htonl(USBIP_RET_SUBMIT);
                        ret.seqnum = ctx->seqnum;
                        ret.devid = ctx->devid;
                        ret.direction = ctx->direction;
                        ret.ep = ctx->ep;
                        ret.status = (res < 0) ? htonl((uint32_t)-errno) : 0;
                        ret.actual_length = 0;
                        send(client_fd, &ret, sizeof(ret), 0);

                        if (ctx->payload_buffer) delete[] ctx->payload_buffer;
                        free(ctx);
                        continue;
                    } else if (bmRequestType == 0x01 && bRequest == 0x0B) {
                        // SET_INTERFACE
                        LOGI("Intercepted SET_INTERFACE");
                        struct usbdevfs_setinterface setintf;
                        setintf.interface = wIndex;
                        setintf.altsetting = wValue;
                        int res = ioctl(device_fd, USBDEVFS_SETINTERFACE, &setintf);
                        LOGI("SET_INTERFACE %u alt %u, res=%d", wIndex, wValue, res);

                        struct usbip_ret_submit ret{};
                        ret.command = htonl(USBIP_RET_SUBMIT);
                        ret.seqnum = ctx->seqnum;
                        ret.devid = ctx->devid;
                        ret.direction = ctx->direction;
                        ret.ep = ctx->ep;
                        ret.status = (res < 0) ? htonl((uint32_t)-errno) : 0;
                        ret.actual_length = 0;
                        send(client_fd, &ret, sizeof(ret), 0);

                        if (ctx->payload_buffer) delete[] ctx->payload_buffer;
                        free(ctx);
                        continue;
                    } else if (bmRequestType == 0x00 && bRequest == 0x05) {
                        // SET_ADDRESS
                        LOGI("Intercepted SET_ADDRESS");

                        struct usbip_ret_submit ret{};
                        ret.command = htonl(USBIP_RET_SUBMIT);
                        ret.seqnum = ctx->seqnum;
                        ret.devid = ctx->devid;
                        ret.direction = ctx->direction;
                        ret.ep = ctx->ep;
                        ret.status = 0;
                        ret.actual_length = 0;
                        send(client_fd, &ret, sizeof(ret), 0);

                        if (ctx->payload_buffer) delete[] ctx->payload_buffer;
                        free(ctx);
                        continue;
                    } else {
                        // ALL OTHER CONTROL TRANSFERS (Let them pass to the physical device)
                        // -> Proceed with ioctl(USBDEVFS_SUBMITURB)
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(in_flight_mutex);
                    active_urbs[ntohl(ctx->seqnum)] = ctx;
                    in_flight_urbs_count++;
                }

                LOGI("Submitting URB: seq=%d, ep_addr=0x%02X, type=%d, alloc_len=%d",
                     ntohl(ctx->seqnum), ctx->urb.endpoint, ctx->urb.type, ctx->urb.buffer_length);

                if (ioctl(device_fd, USBDEVFS_SUBMITURB, &ctx->urb) < 0) {
                    if (errno == ENODEV || errno == EBADF) {
                        LOGW("Device lost on bus %s during URB submission. Waiting for new FD...", current_busid.c_str());
                        std::unique_lock<std::shared_mutex> lock(g_devices_rw_mutex);
                        if (g_device_update_cv.wait_for(lock, std::chrono::seconds(10), [&current_busid]{
                            return g_active_devices.count(current_busid) && g_active_devices[current_busid] != -1;
                        })) {
                            device_fd = g_active_devices[current_busid];
                            LOGI("Resubmitting URB with new FD %d on bus %s", device_fd, current_busid.c_str());
                            if (ioctl(device_fd, USBDEVFS_SUBMITURB, &ctx->urb) >= 0) {
                                continue;
                            }
                        }
                        LOGE("Failed to recover device FD during submission on bus %s.", current_busid.c_str());
                        g_device_fatal_error.store(true);
                        if (ctx->payload_buffer) delete[] ctx->payload_buffer;
                        free(ctx);
                        break;
                    }
                    LOGE("URB Submit Failed! seq=%u, ep=%u, type=%u, errno=%d: %s",
                         ntohl(ctx->seqnum), ctx->urb.endpoint, ctx->urb.type, errno, strerror(errno));
                    {
                        std::lock_guard<std::mutex> lock(in_flight_mutex);
                        active_urbs.erase(ntohl(ctx->seqnum));
                        in_flight_urbs_count--;
                    }
                    struct usbip_ret_submit ret{};
                    ret.command = htonl(USBIP_RET_SUBMIT);
                    ret.seqnum = ctx->seqnum;
                    ret.devid = ctx->devid;
                    ret.direction = ctx->direction;
                    ret.ep = ctx->ep;
                    ret.status = htonl((uint32_t)-errno);
                    ret.actual_length = 0;
                    send(client_fd, &ret, sizeof(ret), 0);
                    if (ctx->payload_buffer) delete[] ctx->payload_buffer;
                    free(ctx);
                }
            } else if (command == USBIP_CMD_UNLINK) {
                uint32_t target_seqnum = ntohl(header.transfer_flags);
                LOGI("CMD_UNLINK received for target seqnum %u", target_seqnum);

                // 1. Send RET_UNLINK immediately to prevent client hang
                struct usbip_ret_submit ret_unlink{};
                memset(&ret_unlink, 0, sizeof(ret_unlink));
                ret_unlink.command = htonl(USBIP_RET_UNLINK);
                ret_unlink.seqnum  = header.seqnum; // Echo UNLINK request seqnum (already in net byte order)
                ret_unlink.status  = 0;
                send(client_fd, &ret_unlink, sizeof(ret_unlink), 0);

                // 2. Discard the target URB if it's still in flight
                {
                    std::lock_guard<std::mutex> lock(in_flight_mutex);
                    auto it = active_urbs.find(target_seqnum);
                    if (it != active_urbs.end()) {
                        LOGI("Watchdog: Forcefully discarding in-flight URB: seq=%u", target_seqnum);
                        ioctl(device_fd, USBDEVFS_DISCARDURB, &it->second->urb);
                        // The reaper thread will handle the memory cleanup when DISCARDURB completes
                    } else {
                        LOGW("CMD_UNLINK: Target seqnum %u not found in active map", target_seqnum);
                    }
                }
            } else {
                LOGE("Unknown command: 0x%08x", command);
                break;
            }
        }
    }

    LOGI("Cleaning up connection...");
    is_connected->store(false);
    cleanup_zombie_urbs(device_fd, client_fd);
}

void run_server(int device_fd_raw) {
    int device_fd = -1;
    if (device_fd_raw != -1) {
        device_fd = dup(device_fd_raw);
        if (device_fd < 0) {
            LOGE("Failed to dup device_fd: %s", strerror(errno));
        }
    }
    LOGI("Native server daemon started (Initial FD %d)", device_fd);

    int server_fd, client_fd;
    struct sockaddr_in address{};
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        LOGE("Socket creation failed: %s", strerror(errno));
        close(device_fd);
        return;
    }

    // Set SO_REUSEADDR immediately before bind
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // TCP Low-Latency: Disable Nagle's algorithm
    int nodelay = 1;
    if (setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        LOGW("Warning: Failed to set TCP_NODELAY: %s", strerror(errno));
    }

    // Save socket descriptor thread-safely
    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        g_server_socket = server_fd;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(USBIP_PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOGE("Bind failed: %s", strerror(errno));
        {
            std::lock_guard<std::mutex> lock(g_socket_mutex);
            g_server_socket = -1;
        }
        close(server_fd);
        close(device_fd);
        return;
    }

    if (listen(server_fd, 3) < 0) {
        LOGE("Listen failed: %s", strerror(errno));
        {
            std::lock_guard<std::mutex> lock(g_socket_mutex);
            g_server_socket = -1;
        }
        close(server_fd);
        close(device_fd);
        return;
    }

    LOGI("USB/IP Server listening on port %d", USBIP_PORT);

    std::vector<std::thread> sessions;
    while (true) {
        if (g_device_fatal_error.load()) {
            LOGI("run_server: Fatal device error detected. Exiting server thread.");
            break;
        }

        client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (client_fd < 0) {
            std::lock_guard<std::mutex> lock(g_socket_mutex);
            if (g_server_socket == -1) {
                LOGI("Accept loop terminated by shutdown");
                break;
            }
            if (errno == EINTR) continue;
            LOGE("Accept failed: %s", strerror(errno));
            break;
        }

        // Set TCP_NODELAY on client socket for low latency
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        LOGI("Client connected (fd=%d)", client_fd);

        {
            std::lock_guard<std::mutex> lock(g_clients_mutex);
            g_client_sockets.push_back(client_fd);
        }

        sessions.emplace_back([client_fd]() {
            int current_fd = -1;
            {
                std::shared_lock<std::shared_mutex> lock(g_devices_rw_mutex);
                if (!g_active_devices.empty()) {
                    current_fd = g_active_devices.begin()->second;
                }
            }
            handle_client(client_fd, current_fd);

            // Cleanup client tracking
            {
                std::lock_guard<std::mutex> lock(g_clients_mutex);
                g_client_sockets.erase(std::remove(g_client_sockets.begin(), g_client_sockets.end(), client_fd), g_client_sockets.end());
            }
            close(client_fd);
            LOGI("Client session thread exiting (fd=%d)", client_fd);
        });
    }

    LOGI("Server loop exited. Joining %zu active sessions...", sessions.size());
    for (auto& t : sessions) {
        if (t.joinable()) t.join();
    }

    LOGI("Closing server socket and exiting run_server thread.");
    close(server_fd);
    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        if (g_server_socket == server_fd) g_server_socket = -1;
    }
    close(device_fd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_mizukos_usbip_UsbServerService_startNativeServer(JNIEnv *env, jobject thiz, jint device_fd) {
    if (g_service_obj) env->DeleteGlobalRef(g_service_obj);
    g_service_obj = env->NewGlobalRef(thiz);

    std::lock_guard<std::mutex> lock(g_socket_mutex);
    g_device_fatal_error.store(false);
    {
        std::unique_lock<std::shared_mutex> dev_lock(g_devices_rw_mutex);
        if (device_fd != -1) {
            g_active_devices["1-1"] = device_fd;
        }
    }
    if (g_server_thread.joinable()) {
        LOGW("Warning: Server thread already joinable. Joining before restart.");
        g_server_thread.join();
    }
    g_server_thread = std::thread(run_server, device_fd);
}

extern "C" JNIEXPORT void JNICALL
Java_com_mizukos_usbip_UsbServerService_stopNativeServer(JNIEnv *env, jobject thiz) {
    if (g_service_obj) {
        env->DeleteGlobalRef(g_service_obj);
        g_service_obj = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_socket_mutex);
        if (g_server_socket >= 0) {
            LOGI("Force closing native TCP listener socket...");
            shutdown(g_server_socket, SHUT_RDWR);
            close(g_server_socket);
            g_server_socket = -1;
        }
    }

    // Unblock any client threads stuck in recv()
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        for (int fd : g_client_sockets) {
            shutdown(fd, SHUT_RDWR);
        }
    }

    if (g_server_thread.joinable()) {
        LOGI("Joining native server thread...");
        g_server_thread.join();
        LOGI("Native server thread joined.");
    }
    {
        std::unique_lock<std::shared_mutex> dev_lock(g_devices_rw_mutex);
        for (auto const& [busid, fd] : g_active_devices) {
            if (fd != -1) close(fd);
        }
        g_active_devices.clear();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_mizukos_usbip_UsbServerService_updateDeviceFd(JNIEnv *env, jobject thiz, jstring jbusid, jint new_fd) {
    const char* busid_ptr = env->GetStringUTFChars(jbusid, nullptr);
    std::string busid(busid_ptr);
    env->ReleaseStringUTFChars(jbusid, busid_ptr);

    LOGI("updateDeviceFd: Received new FD %d for bus %s", new_fd, busid.c_str());
    {
        std::unique_lock<std::shared_mutex> lock(g_devices_rw_mutex);
        if (g_active_devices.count(busid) && g_active_devices[busid] != -1 && g_active_devices[busid] != new_fd) {
            close(g_active_devices[busid]);
        }
        g_active_devices[busid] = new_fd;
        g_device_fatal_error.store(false);
    }

    // Claim interfaces 0 and 1 as requested for G29 robustness
    for (int i = 0; i < 2; i++) {
        int intf = i;
        if (ioctl(new_fd, USBDEVFS_CLAIMINTERFACE, &intf) < 0) {
            if (errno == ENOENT || errno == ENODEV) {
                LOGI("updateDeviceFd: Interface %d not present, skipping.", i);
            } else {
                LOGW("updateDeviceFd: Failed to claim interface %d: %s", i, strerror(errno));
            }
        }
    }

    g_device_update_cv.notify_all();
}

extern "C" JNIEXPORT void JNICALL
Java_com_mizukos_usbip_UsbServerService_invalidateDeviceFd(JNIEnv *env, jobject thiz, jstring jbusid) {
    const char* busid_ptr = env->GetStringUTFChars(jbusid, nullptr);
    std::string busid(busid_ptr);
    env->ReleaseStringUTFChars(jbusid, busid_ptr);

    LOGI("invalidateDeviceFd: Device detached on bus %s.", busid.c_str());
    {
        std::unique_lock<std::shared_mutex> lock(g_devices_rw_mutex);
        if (g_active_devices.count(busid) && g_active_devices[busid] != -1) {
            close(g_active_devices[busid]);
        }
        g_active_devices[busid] = -1;
    }
}
