#pragma once
#include <spdlog/spdlog.h>
#include <iostream>
#include <mutex>

// 使用std::call_once确保日志系统只初始化一次
inline void init_logger_once() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        try {
            // 设置日志格式：[时间] [级别] 消息（去掉毫秒）
            spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
            
            // 设置日志级别为debug
            spdlog::set_level(spdlog::level::debug);
            
            spdlog::info("日志系统初始化成功");
        } catch (const std::exception& e) {
            std::cerr << "日志系统初始化失败: " << e.what() << std::endl;
        }
    });
}

// 静态初始化类，确保在包含此头文件时自动调用初始化
class LoggerInitializer {
public:
    LoggerInitializer() {
        init_logger_once();
    }
};

// 静态实例，确保在包含此头文件时自动初始化
static LoggerInitializer logger_init_instance;