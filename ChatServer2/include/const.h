#pragma once
#include <functional>
#include "logger.h"


enum ErrorCodes {
    Success = 0,
    Error_Json = 1001,  // Json解析错误
    RPCFailed = 1002,  // RPC调用失败
    VarifyExpired = 1003, // 验证码过期
    VarifyCodeErr = 1004, // 验证码错误
    UserExist = 1005,       // 用户已存在
    PasswdErr = 1006,    // 密码错误
    EmailNotMatch = 1007,  // 邮箱不匹配
    PasswdUpFailed = 1008,  // 密码更新失败
    PasswdInvalid = 1009,   // 密码无效
    TokenInvalid = 1010,   // Token失效
    UidInvalid = 1011,  // uid无效
};


// Defer类
class Defer {
public:
    // 构造时传入lambda表达式或函数指针
    Defer(std::function<void()> func) : func_(func) {}

    // 析构时自动执行传入的函数
    ~Defer() {
        func_();
    }

private:
    std::function<void()> func_;
};

#define MAX_LENGTH  1024*2
// 头部总长度
#define HEAD_TOTAL_LEN 4
// 头部id长度
#define HEAD_ID_LEN 2
// 头部数据长度
#define HEAD_DATA_LEN 2
#define MAX_RECVQUE  10000
#define MAX_SENDQUE 1000


enum MSG_IDS {
    MSG_CHAT_LOGIN = 1005, // 用户登录
    MSG_CHAT_LOGIN_RSP = 1006, // 用户登录回复
    ID_SEARCH_USER_REQ = 1007, // 用户搜索请求
    ID_SEARCH_USER_RSP = 1008, // 搜索用户回复
    ID_ADD_FRIEND_REQ = 1009, // 添加好友请求
    ID_ADD_FRIEND_RSP  = 1010, // 添加好友回复
    ID_NOTIFY_ADD_FRIEND_REQ = 1011,  // 通知用户添加好友请求
    ID_AUTH_FRIEND_REQ = 1013,  // 验证好友请求
    ID_AUTH_FRIEND_RSP = 1014,  // 验证好友回复
    ID_NOTIFY_AUTH_FRIEND_REQ = 1015, // 通知用户验证好友请求
    ID_TEXT_CHAT_MSG_REQ = 1017, // 文本聊天消息请求
    ID_TEXT_CHAT_MSG_RSP = 1018, // 文本聊天消息回复
    ID_NOTIFY_TEXT_CHAT_MSG_REQ = 1019, // 通知用户文本聊天消息
    ID_NOTIFY_OFF_LINE_REQ = 1021, // 通知用户离线
    ID_HEART_BEAT_REQ = 1023,      // 心跳请求
    ID_HEARTBEAT_RSP = 1024,       // 心跳回复
};

#define USERIPPREFIX  "uip_"
#define USERTOKENPREFIX  "utoken_"
#define IPCOUNTPREFIX  "ipcount_"
#define USER_BASE_INFO "ubaseinfo_"
#define LOGIN_COUNT  "logincount"
#define NAME_INFO  "nameinfo_"
#define LOCK_PREFIX "lock_"
#define USER_SESSION_PREFIX "usession_"
#define LOCK_COUNT "lockcount"

// 分布式锁超时时间
#define LOCK_TIME_OUT 10
// 分布式锁获取超时时间
#define ACQUIRE_TIME_OUT 5


