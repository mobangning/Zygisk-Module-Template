#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <android/log.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include "zygisk.hpp"

// 日志前缀改成了通用的 Global-CamHook
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "Global-CamHook", __VA_ARGS__)

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

int (*orig_openat)(int dirfd, const char *pathname, int flags, mode_t mode);
int (*orig_ioctl)(int fd, unsigned long request, ...);

// 默认节点
char target_node[64] = "/dev/video1"; 
char fake_node[64] = "/dev/video0";   

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
        LOGD("【全局拦截】成功捕获节点读取 %s，强行重定向至 %s", pathname, fake_node);
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
            strlcpy((char*)cap->card, "Qcamera2", sizeof(cap->card));
            strlcpy((char*)cap->driver, "msm_v4l2", sizeof(cap->driver));
        }
    }
    return ret;
}

// 模块类名已重构为通用类名
class GlobalCameraHookModule : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    // 注入系统的底层框架服务
    void preServerSpecialize(ServerSpecializeArgs *args) override {
        api->pltHook("libc.so", "openat", (void *)my_openat, (void **)&orig_openat);
        api->pltHook("libc.so", "ioctl", (void *)my_ioctl, (void **)&orig_ioctl);
    }

    // 无差别注入所有被 Zygote 孵化的 App（系统相机、所有第三方应用）
    void preAppSpecialize(AppSpecializeArgs *args) override {
        load_config(); // 注入时加载一次路径配置
        api->pltHook("libc.so", "openat", (void *)my_openat, (void **)&orig_openat);
        api->pltHook("libc.so", "ioctl", (void *)my_ioctl, (void **)&orig_ioctl);
    }

private:
    Api *api;
    JNIEnv *env;
};

// 注册激活全局模块
REGISTER_ZYGISK_MODULE(GlobalCameraHookModule)
