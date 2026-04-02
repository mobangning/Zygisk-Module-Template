#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include "zygisk.hpp"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Global-CamHook", __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

int (*orig_openat)(int dirfd, const char *pathname, int flags, mode_t mode);
int (*orig_ioctl)(int fd, unsigned long request, ...);

// 【核心变量区】这里绝对不能漏复制，它是配置文件的灵魂！
char target_node[64] = "/dev/video1"; 
char fake_node[64] = "/dev/video0";   

// 读取外部配置的逻辑
void load_config() {
    FILE *fp = fopen("/data/local/tmp/camera_config.txt", "r");
    if (fp) {
        fscanf(fp, "%63s %63s", target_node, fake_node);
        fclose(fp);
    }
}

// 核心战术 1：全局改门牌
int my_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (pathname != nullptr && strstr(pathname, target_node)) {
        LOGD("【全局拦截】成功捕获节点读取，强行重定向");
        return orig_openat(dirfd, fake_node, flags, mode);
    }
    return orig_openat(dirfd, pathname, flags, mode);
}

// 核心战术 2：全局造假证
int my_ioctl(int fd, unsigned long request, void* argp) {
    int ret = orig_ioctl(fd, request, argp);
    if (request == VIDIOC_QUERYCAP && ret == 0) {
        struct v4l2_capability *cap = (struct v4l2_capability *)argp;
        if (strstr((char*)cap->card, "Dummy")) {
            LOGD("【全局伪装】成功篡改底层特征为高通 Qcamera2");
            // 换用绝对安全标准的 strncpy 防止 NDK 报错
            strncpy((char*)cap->card, "Qcamera2", sizeof(cap->card) - 1);
            strncpy((char*)cap->driver, "msm_v4l2", sizeof(cap->driver) - 1);
        }
    }
    return ret;
}

class GlobalCameraHookModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preServerSpecialize(ServerSpecializeArgs *args) override {
        api->pltHook("libc.so", "openat", (void *)my_openat, (void **)&orig_openat);
        api->pltHook("libc.so", "ioctl", (void *)my_ioctl, (void **)&orig_ioctl);
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        load_config(); 
        api->pltHook("libc.so", "openat", (void *)my_openat, (void **)&orig_openat);
        api->pltHook("libc.so", "ioctl", (void *)my_ioctl, (void **)&orig_ioctl);
    }

private:
    Api *api;
    JNIEnv *env;
};

REGISTER_ZYGISK_MODULE(GlobalCameraHookModule)
