#include "LogicSystem.h"
#include "CServer.h"
#include "ChatGrpcClient.h"
#include "DistLock.h"
#include "MysqlMgr.h"
#include "RedisMgr.h"
#include "StatusGrpcClient.h"
#include "UserMgr.h"
#include "const.h"
#include <string>
using namespace std;

LogicSystem::LogicSystem() : _b_stop(false), _p_server(nullptr)
{
    RegisterCallBacks();
    _worker_thread = std::thread(&LogicSystem::DealMsg, this);
}

LogicSystem::~LogicSystem()
{
    _b_stop = true;
    _consume.notify_one();
    _worker_thread.join();
}

void LogicSystem::PostMsgToQue(shared_ptr<LogicNode> msg)
{
    std::unique_lock<std::mutex> unique_lk(_mutex);
    _msg_que.push(msg);
    // 从0变为1则通知信号
    if (_msg_que.size() == 1) {
        unique_lk.unlock();
        _consume.notify_one();
    }
}

void LogicSystem::SetServer(std::shared_ptr<CServer> pserver)
{
    _p_server = pserver;
}

void LogicSystem::DealMsg()
{
    for (;;) {
        std::unique_lock<std::mutex> unique_lk(_mutex);
        // 判断队列为空且没有停止则等待条件变量通知
        while (_msg_que.empty() && !_b_stop) {
            _consume.wait(unique_lk);
        }

        // 判断是否为关闭状态，如果是则处理逻辑执行完毕后退出循环
        if (_b_stop) {
            while (!_msg_que.empty()) {
                auto msg_node = _msg_que.front();
                spdlog::info("收到消息ID: {}", msg_node->_recvnode->_msg_id);
                auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
                if (call_back_iter == _fun_callbacks.end()) {
                    _msg_que.pop();
                    continue;
                }
                call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id,
                                       std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
                _msg_que.pop();
            }
            break;
        }

        // 如果没有停止则说明队列有数据处理
        auto msg_node = _msg_que.front();
        spdlog::info("recv_msg id  is {}", msg_node->_recvnode->_msg_id);
        auto call_back_iter = _fun_callbacks.find(msg_node->_recvnode->_msg_id);
        if (call_back_iter == _fun_callbacks.end()) {
            _msg_que.pop();
            spdlog::error("消息ID [{}] 未找到对应的处理函数", msg_node->_recvnode->_msg_id);
            continue;
        }
        call_back_iter->second(msg_node->_session, msg_node->_recvnode->_msg_id, std::string(msg_node->_recvnode->_data, msg_node->_recvnode->_cur_len));
        _msg_que.pop();
    }
}

void LogicSystem::RegisterCallBacks()
{
    _fun_callbacks[MSG_CHAT_LOGIN] = std::bind(&LogicSystem::LoginHandler, this,
                                               placeholders::_1, placeholders::_2, placeholders::_3);

    _fun_callbacks[ID_SEARCH_USER_REQ] = std::bind(&LogicSystem::SearchInfo, this,
                                                   placeholders::_1, placeholders::_2, placeholders::_3);

    _fun_callbacks[ID_ADD_FRIEND_REQ] = std::bind(&LogicSystem::AddFriendApply, this,
                                                  placeholders::_1, placeholders::_2, placeholders::_3);

    _fun_callbacks[ID_AUTH_FRIEND_REQ] = std::bind(&LogicSystem::AuthFriendApply, this,
                                                   placeholders::_1, placeholders::_2, placeholders::_3);

    _fun_callbacks[ID_TEXT_CHAT_MSG_REQ] = std::bind(&LogicSystem::DealChatTextMsg, this,
                                                     placeholders::_1, placeholders::_2, placeholders::_3);

    _fun_callbacks[ID_HEART_BEAT_REQ] = std::bind(&LogicSystem::HeartBeatHandler, this,
                                                  placeholders::_1, placeholders::_2, placeholders::_3);
}

void LogicSystem::LoginHandler(shared_ptr<CSession> session, const short &msg_id, const string &msg_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);
    auto uid = root["uid"].asInt();
    auto token = root["token"].asString();
    spdlog::info("用户登录，用户ID: {} 用户令牌: {}", uid, token);

    Json::Value rtvalue;
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, MSG_CHAT_LOGIN_RSP);
    });

    // ��redis��ȡ�û�token�Ƿ���ȷ
    std::string uid_str = std::to_string(uid);
    std::string token_key = USERTOKENPREFIX + uid_str;
    std::string token_value = "";
    bool success = RedisMgr::GetInstance()->Get(token_key, token_value);
    if (!success) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    if (token_value != token) {
        rtvalue["error"] = ErrorCodes::TokenInvalid;
        return;
    }

    rtvalue["error"] = ErrorCodes::Success;

    std::string base_key = USER_BASE_INFO + uid_str;
    auto user_info = std::make_shared<UserInfo>();
    bool b_base = GetBaseInfo(base_key, uid, user_info);
    if (!b_base) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }
    rtvalue["uid"] = uid;
    rtvalue["pwd"] = user_info->pwd;
    rtvalue["name"] = user_info->name;
    rtvalue["email"] = user_info->email;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
    rtvalue["icon"] = user_info->icon;

    // �����ݿ��ȡ�����б�
    std::vector<std::shared_ptr<ApplyInfo>> apply_list;
    auto b_apply = GetFriendApplyInfo(uid, apply_list);
    if (b_apply) {
        for (auto &apply : apply_list) {
            Json::Value obj;
            obj["name"] = apply->_name;
            obj["uid"] = apply->_uid;
            obj["icon"] = apply->_icon;
            obj["nick"] = apply->_nick;
            obj["sex"] = apply->_sex;
            obj["desc"] = apply->_desc;
            obj["status"] = apply->_status;
            rtvalue["apply_list"].append(obj);
        }
    }

    // ��ȡ�����б�
    std::vector<std::shared_ptr<UserInfo>> friend_list;
    bool b_friend_list = GetFriendList(uid, friend_list);
    for (auto &friend_ele : friend_list) {
        Json::Value obj;
        obj["name"] = friend_ele->name;
        obj["uid"] = friend_ele->uid;
        obj["icon"] = friend_ele->icon;
        obj["nick"] = friend_ele->nick;
        obj["sex"] = friend_ele->sex;
        obj["desc"] = friend_ele->desc;
        obj["back"] = friend_ele->back;
        rtvalue["friend_list"].append(obj);
    }

    auto server_name = ConfigMgr::Inst().GetValue("SelfServer", "Name");
    {
        // �˴����ӷֲ�ʽ�����ø��̶߳�ռ��¼
        // ƴ���û�ip��Ӧ��key
        auto lock_key = LOCK_PREFIX + uid_str;
        auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
        // ����defer����
        Defer defer2([this, identifier, lock_key]() {
            RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
        });
        // �˴��жϸ��û��Ƿ��ڱ𴦻��߱���������¼

        std::string uid_ip_value = "";
        auto uid_ip_key = USERIPPREFIX + uid_str;
        bool b_ip = RedisMgr::GetInstance()->Get(uid_ip_key, uid_ip_value);
        // ˵���û��Ѿ���¼�ˣ��˴�Ӧ���ߵ�֮ǰ���û���¼״̬
        if (b_ip) {
            // ��ȡ��ǰ������ip��Ϣ
            auto &cfg = ConfigMgr::Inst();
            auto self_name = cfg["SelfServer"]["Name"];
            // ���֮ǰ��¼�ķ������͵�ǰ��ͬ����ֱ���ڱ��������ߵ�
            if (uid_ip_value == self_name) {
                // ���Ҿ��е�����
                auto old_session = UserMgr::GetInstance()->GetSession(uid);

                // �˴�Ӧ�÷���������Ϣ
                if (old_session) {
                    old_session->NotifyOffline(uid);
                    // ����ɵ�����
                    _p_server->ClearSession(old_session->GetSessionId());
                }

            } else {
                // ������Ǳ�����������֪ͨgrpc֪ͨ�����������ߵ�
                // ����֪ͨ
                KickUserReq kick_req;
                kick_req.set_uid(uid);
                ChatGrpcClient::GetInstance()->NotifyKickUser(uid_ip_value, kick_req);
            }
        }

        // session���û�uid
        session->SetUserId(uid);
        // Ϊ�û����õ�¼ip server������
        std::string ipkey = USERIPPREFIX + uid_str;
        RedisMgr::GetInstance()->Set(ipkey, server_name);
        // uid��session�󶨹���,�����Ժ����˲���
        UserMgr::GetInstance()->SetUserSession(uid, session);
        std::string uid_session_key = USER_SESSION_PREFIX + uid_str;
        RedisMgr::GetInstance()->Set(uid_session_key, session->GetSessionId());
    }

    return;
}

void LogicSystem::SearchInfo(std::shared_ptr<CSession> session, const short &msg_id, const string &msg_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);
    auto uid_str = root["uid"].asString();
    spdlog::info("用户搜索，搜索ID: {}", uid_str);

    Json::Value rtvalue;

    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_SEARCH_USER_RSP);
    });

    bool b_digit = isPureDigit(uid_str);
    if (b_digit) {
        GetUserByUid(uid_str, rtvalue);
    } else {
        GetUserByName(uid_str, rtvalue);
    }
    return;
}

void LogicSystem::AddFriendApply(std::shared_ptr<CSession> session, const short &msg_id, const string &msg_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);
    auto uid = root["uid"].asInt();
    auto applyname = root["applyname"].asString();
    auto bakname = root["bakname"].asString();
    auto touid = root["touid"].asInt();
    spdlog::info("添加好友申请，用户ID: {} 申请人昵称: {} 备注名: {} 目标用户ID: {}", uid, applyname, bakname, touid);

    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;
    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_ADD_FRIEND_RSP);
    });

    // �ȸ������ݿ�
    MysqlMgr::GetInstance()->AddFriendApply(uid, touid);

    // ��ѯredis ����touid��Ӧ��server ip
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;
    std::string to_ip_value = "";
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
    if (!b_ip) {
        return;
    }

    auto &cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];

    std::string base_key = USER_BASE_INFO + std::to_string(uid);
    auto apply_info = std::make_shared<UserInfo>();
    bool b_info = GetBaseInfo(base_key, uid, apply_info);

    // ֱ��֪ͨ�Է���������Ϣ
    if (to_ip_value == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            // ���ڴ�����ֱ�ӷ���֪ͨ�Է�
            Json::Value notify;
            notify["error"] = ErrorCodes::Success;
            notify["applyuid"] = uid;
            notify["name"] = applyname;
            notify["desc"] = "";
            if (b_info) {
                notify["icon"] = apply_info->icon;
                notify["sex"] = apply_info->sex;
                notify["nick"] = apply_info->nick;
            }
            std::string return_str = notify.toStyledString();
            session->Send(return_str, ID_NOTIFY_ADD_FRIEND_REQ);
        }

        return;
    }

    AddFriendReq add_req;
    add_req.set_applyuid(uid);
    add_req.set_touid(touid);
    add_req.set_name(applyname);
    add_req.set_desc("");
    if (b_info) {
        add_req.set_icon(apply_info->icon);
        add_req.set_sex(apply_info->sex);
        add_req.set_nick(apply_info->nick);
    }

    // ����֪ͨ
    ChatGrpcClient::GetInstance()->NotifyAddFriend(to_ip_value, add_req);
}

void LogicSystem::AuthFriendApply(std::shared_ptr<CSession> session, const short &msg_id, const string &msg_data)
{

    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);

    auto uid = root["fromuid"].asInt();
    auto touid = root["touid"].asInt();
    auto back_name = root["back"].asString();
    spdlog::info("好友验证，发起人ID: {} 验证目标ID: {}", uid, touid);

    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;
    auto user_info = std::make_shared<UserInfo>();

    std::string base_key = USER_BASE_INFO + std::to_string(touid);
    bool b_info = GetBaseInfo(base_key, touid, user_info);
    if (b_info) {
        rtvalue["name"] = user_info->name;
        rtvalue["nick"] = user_info->nick;
        rtvalue["icon"] = user_info->icon;
        rtvalue["sex"] = user_info->sex;
        rtvalue["uid"] = touid;
    } else {
        rtvalue["error"] = ErrorCodes::UidInvalid;
    }

    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_AUTH_FRIEND_RSP);
    });

    // �ȸ������ݿ�
    MysqlMgr::GetInstance()->AuthFriendApply(uid, touid);

    // �������ݿ����Ӻ���
    MysqlMgr::GetInstance()->AddFriend(uid, touid, back_name);

    // ��ѯredis ����touid��Ӧ��server ip
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;
    std::string to_ip_value = "";
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
    if (!b_ip) {
        return;
    }

    auto &cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];
    // ֱ��֪ͨ�Է�����֤ͨ����Ϣ
    if (to_ip_value == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            // ���ڴ�����ֱ�ӷ���֪ͨ�Է�
            Json::Value notify;
            notify["error"] = ErrorCodes::Success;
            notify["fromuid"] = uid;
            notify["touid"] = touid;
            std::string base_key = USER_BASE_INFO + std::to_string(uid);
            auto user_info = std::make_shared<UserInfo>();
            bool b_info = GetBaseInfo(base_key, uid, user_info);
            if (b_info) {
                notify["name"] = user_info->name;
                notify["nick"] = user_info->nick;
                notify["icon"] = user_info->icon;
                notify["sex"] = user_info->sex;
            } else {
                notify["error"] = ErrorCodes::UidInvalid;
            }

            std::string return_str = notify.toStyledString();
            session->Send(return_str, ID_NOTIFY_AUTH_FRIEND_REQ);
        }

        return;
    }

    AuthFriendReq auth_req;
    auth_req.set_fromuid(uid);
    auth_req.set_touid(touid);

    // ����֪ͨ
    ChatGrpcClient::GetInstance()->NotifyAuthFriend(to_ip_value, auth_req);
}

void LogicSystem::DealChatTextMsg(std::shared_ptr<CSession> session, const short &msg_id, const string &msg_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);

    auto uid = root["fromuid"].asInt();
    auto touid = root["touid"].asInt();

    const Json::Value arrays = root["text_array"];

    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;
    rtvalue["text_array"] = arrays;
    rtvalue["fromuid"] = uid;
    rtvalue["touid"] = touid;

    Defer defer([this, &rtvalue, session]() {
        std::string return_str = rtvalue.toStyledString();
        session->Send(return_str, ID_TEXT_CHAT_MSG_RSP);
    });

    // ��ѯredis ����touid��Ӧ��server ip
    auto to_str = std::to_string(touid);
    auto to_ip_key = USERIPPREFIX + to_str;
    std::string to_ip_value = "";
    bool b_ip = RedisMgr::GetInstance()->Get(to_ip_key, to_ip_value);
    if (!b_ip) {
        return;
    }

    auto &cfg = ConfigMgr::Inst();
    auto self_name = cfg["SelfServer"]["Name"];
    // ֱ��֪ͨ�Է�����֤ͨ����Ϣ
    if (to_ip_value == self_name) {
        auto session = UserMgr::GetInstance()->GetSession(touid);
        if (session) {
            // ���ڴ�����ֱ�ӷ���֪ͨ�Է�
            std::string return_str = rtvalue.toStyledString();
            session->Send(return_str, ID_NOTIFY_TEXT_CHAT_MSG_REQ);
        }

        return;
    }

    TextChatMsgReq text_msg_req;
    text_msg_req.set_fromuid(uid);
    text_msg_req.set_touid(touid);
    for (const auto &txt_obj : arrays) {
        auto content = txt_obj["content"].asString();
        auto msgid = txt_obj["msgid"].asString();
        spdlog::info("消息内容: {}", content);
        spdlog::info("消息ID: {}", msgid);
        auto *text_msg = text_msg_req.add_textmsgs();
        text_msg->set_msgid(msgid);
        text_msg->set_msgcontent(content);
    }

    // ����֪ͨ todo...
    ChatGrpcClient::GetInstance()->NotifyTextChatMsg(to_ip_value, text_msg_req, rtvalue);
}

void LogicSystem::HeartBeatHandler(std::shared_ptr<CSession> session, const short &msg_id, const string &msg_data)
{
    Json::Reader reader;
    Json::Value root;
    reader.parse(msg_data, root);
    auto uid = root["fromuid"].asInt();
    spdlog::info("收到心跳包，用户ID: {}", uid);
    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;
    session->Send(rtvalue.toStyledString(), ID_HEARTBEAT_RSP);
}

bool LogicSystem::isPureDigit(const std::string &str)
{
    for (char c : str) {
        if (!std::isdigit(c)) {
            return false;
        }
    }
    return true;
}

void LogicSystem::GetUserByUid(std::string uid_str, Json::Value &rtvalue)
{
    rtvalue["error"] = ErrorCodes::Success;

    std::string base_key = USER_BASE_INFO + uid_str;

    // 首先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        auto uid = root["uid"].asInt();
        auto name = root["name"].asString();
        auto pwd = root["pwd"].asString();
        auto email = root["email"].asString();
        auto nick = root["nick"].asString();
        auto desc = root["desc"].asString();
        auto sex = root["sex"].asInt();
        auto icon = root["icon"].asString();
        spdlog::info("用户信息 - ID: {} 用户名: {} 密码: {} 邮箱: {} 头像: {}", uid, name, pwd, email, icon);

        rtvalue["uid"] = uid;
        rtvalue["pwd"] = pwd;
        rtvalue["name"] = name;
        rtvalue["email"] = email;
        rtvalue["nick"] = nick;
        rtvalue["desc"] = desc;
        rtvalue["sex"] = sex;
        rtvalue["icon"] = icon;
        return;
    }

    auto uid = std::stoi(uid_str);
    // redis中没有则查询mysql
    // 查询数据库
    std::shared_ptr<UserInfo> user_info = nullptr;
    user_info = MysqlMgr::GetInstance()->GetUser(uid);
    if (user_info == nullptr) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    // 将数据库数据写入redis缓存
    Json::Value redis_root;
    redis_root["uid"] = user_info->uid;
    redis_root["pwd"] = user_info->pwd;
    redis_root["name"] = user_info->name;
    redis_root["email"] = user_info->email;
    redis_root["nick"] = user_info->nick;
    redis_root["desc"] = user_info->desc;
    redis_root["sex"] = user_info->sex;
    redis_root["icon"] = user_info->icon;

    RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

    // 返回数据
    rtvalue["uid"] = user_info->uid;
    rtvalue["pwd"] = user_info->pwd;
    rtvalue["name"] = user_info->name;
    rtvalue["email"] = user_info->email;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
    rtvalue["icon"] = user_info->icon;
}

void LogicSystem::GetUserByName(std::string name, Json::Value &rtvalue)
{
    rtvalue["error"] = ErrorCodes::Success;

    std::string base_key = NAME_INFO + name;

    // 首先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        auto uid = root["uid"].asInt();
        auto name = root["name"].asString();
        auto pwd = root["pwd"].asString();
        auto email = root["email"].asString();
        auto nick = root["nick"].asString();
        auto desc = root["desc"].asString();
        auto sex = root["sex"].asInt();
        spdlog::info("用户信息 - ID: {} 用户名: {} 密码: {} 邮箱: {}", uid, name, pwd, email);

        rtvalue["uid"] = uid;
        rtvalue["pwd"] = pwd;
        rtvalue["name"] = name;
        rtvalue["email"] = email;
        rtvalue["nick"] = nick;
        rtvalue["desc"] = desc;
        rtvalue["sex"] = sex;
        return;
    }

    // redis中没有则查询mysql
    // 查询数据库
    std::shared_ptr<UserInfo> user_info = nullptr;
    user_info = MysqlMgr::GetInstance()->GetUser(name);
    if (user_info == nullptr) {
        rtvalue["error"] = ErrorCodes::UidInvalid;
        return;
    }

    // 将数据库数据写入redis缓存
    Json::Value redis_root;
    redis_root["uid"] = user_info->uid;
    redis_root["pwd"] = user_info->pwd;
    redis_root["name"] = user_info->name;
    redis_root["email"] = user_info->email;
    redis_root["nick"] = user_info->nick;
    redis_root["desc"] = user_info->desc;
    redis_root["sex"] = user_info->sex;

    RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());

    // 返回数据
    rtvalue["uid"] = user_info->uid;
    rtvalue["pwd"] = user_info->pwd;
    rtvalue["name"] = user_info->name;
    rtvalue["email"] = user_info->email;
    rtvalue["nick"] = user_info->nick;
    rtvalue["desc"] = user_info->desc;
    rtvalue["sex"] = user_info->sex;
}

bool LogicSystem::GetBaseInfo(std::string base_key, int uid, std::shared_ptr<UserInfo> &userinfo)
{
    // 首先查redis中查询用户信息
    std::string info_str = "";
    bool b_base = RedisMgr::GetInstance()->Get(base_key, info_str);
    if (b_base) {
        Json::Reader reader;
        Json::Value root;
        reader.parse(info_str, root);
        userinfo->uid = root["uid"].asInt();
        userinfo->name = root["name"].asString();
        userinfo->pwd = root["pwd"].asString();
        userinfo->email = root["email"].asString();
        userinfo->nick = root["nick"].asString();
        userinfo->desc = root["desc"].asString();
        userinfo->sex = root["sex"].asInt();
        userinfo->icon = root["icon"].asString();
        spdlog::info("用户登录信息 - ID: {} 用户名: {} 密码: {} 邮箱: {}", userinfo->uid, userinfo->name, userinfo->pwd, userinfo->email);
    } else {
        // redis中没有则查询mysql
        // 查询数据库
        std::shared_ptr<UserInfo> user_info = nullptr;
        user_info = MysqlMgr::GetInstance()->GetUser(uid);
        if (user_info == nullptr) {
            return false;
        }

        userinfo = user_info;

        // 将数据库数据写入redis缓存
        Json::Value redis_root;
        redis_root["uid"] = uid;
        redis_root["pwd"] = userinfo->pwd;
        redis_root["name"] = userinfo->name;
        redis_root["email"] = userinfo->email;
        redis_root["nick"] = userinfo->nick;
        redis_root["desc"] = userinfo->desc;
        redis_root["sex"] = userinfo->sex;
        redis_root["icon"] = userinfo->icon;
        RedisMgr::GetInstance()->Set(base_key, redis_root.toStyledString());
    }

    return true;
}

bool LogicSystem::GetFriendApplyInfo(int to_uid, std::vector<std::shared_ptr<ApplyInfo>> &list)
{
    // 从mysql获取好友申请列表
    return MysqlMgr::GetInstance()->GetApplyList(to_uid, list, 0, 10);
}

bool LogicSystem::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>> &user_list)
{
    // 从mysql获取好友列表
    return MysqlMgr::GetInstance()->GetFriendList(self_id, user_list);
}
