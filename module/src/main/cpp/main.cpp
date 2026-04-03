#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <sys/stat.h>
#include "zygisk.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Global-CamHook", __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

int (*orig_openat)(int dirfd, const char *pathname, int flags, mode_t mode);
int (*orig_ioctl)(int fd, unsigned long request, ...);

char target_node[64] = "/dev/video1"; 
char fake_node[64] = "/dev/video0";   

void load_config() {
    FILE *fp = fopen("/data/local/tmp/camera_config.txt", "r");
    if (fp) {
        fscanf(fp, "%63s %63s", target_node, fake_node);
        fclose(fp);
    }
}

int my_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (pathname != nullptr && strstr(pathname, target_node)) {
        LOGD("【全局拦截】成功捕获节点读取，强行重定向");
        return orig_openat(dirfd, fake_node, flags, mode);
    }
    return orig_openat(dirfd, pathname, flags, mode);
}

int my_ioctl(int fd, unsigned long request, void* argp) {
    int ret = orig_ioctl(fd, request, argp);
    if (request == VIDIOC_QUERYCAP && ret == 0) {
        struct v4l2_capability *cap = (struct v4l2_capability *)argp;
        if (strstr((char*)cap->card, "Dummy")) {
            LOGD("【全局伪装】成功篡改底层特征为高通 Qcamera2");
            strncpy((char*)cap->card, "Qcamera2", sizeof(cap->card) - 1);
            strncpy((char*)cap->driver, "msm_v4l2", sizeof(cap->driver) - 1);
        }
    }
    return ret;
}

// 终极侦察兵：硬刚 Zygisk v4 的苛刻要求，自动扒出 libc.so 的设备号和节点号！
void hookLibc(Api *api, const char *symbol, void *newFunc, void **oldFunc) {
    FILE *fp = fopen("/proc/self/maps", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            // 在内存表里寻找 libc.so 的绝对路径
            if (strstr(line, "libc.so") && strchr(line, '/')) {
                char path[256];
                sscanf(strchr(line, '/'), "%255s", path);
                struct stat st;
                // 用 stat 读取出设备的 dev 和 inode
                if (stat(path, &st) == 0) {
                    // 完美满足 5 个参数的苛刻要求！
                    api->pltHookRegister(st.st_dev, st.st_ino, symbol, newFunc, oldFunc);
                    break; // 挂载成功，退出侦察
                }
            }
        }
        fclose(fp);
    }
}

class GlobalCameraHookModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        hookLibc(api, "openat", (void *)my_openat, (void **)&orig_openat);
        hookLibc(api, "ioctl", (void *)my_ioctl, (void **)&orig_ioctl);
        api->pltHookCommit();
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        load_config(); 
        hookLibc(api, "openat", (void *)my_openat, (void **)&orig_openat);
        hookLibc(api, "ioctl", (void *)my_ioctl, (void **)&orig_ioctl);
        api->pltHookCommit();
    }

private:
    Api *api;
    JNIEnv *env;
};

REGISTER_ZYGISK_MODULE(GlobalCameraHookModule)
