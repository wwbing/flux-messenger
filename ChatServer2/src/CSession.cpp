#include "CSession.h"
#include "CServer.h"
#include "ConfigMgr.h"
#include "LogicSystem.h"
#include "RedisMgr.h"
#include <iostream>
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <sstream>

CSession::CSession(boost::asio::io_context &io_context, CServer *server) : _socket(io_context), _server(server), _b_close(false), _b_head_parse(false), _user_uid(0)
{
    boost::uuids::uuid a_uuid = boost::uuids::random_generator()();
    _session_id = boost::uuids::to_string(a_uuid);
    _recv_head_node = make_shared<MsgNode>(HEAD_TOTAL_LEN);
    _last_heartbeat = std::time(nullptr);
}
CSession::~CSession()
{
    spdlog::info("~CSession 析构");
}

tcp::socket &CSession::GetSocket()
{
    return _socket;
}

std::string &CSession::GetSessionId()
{
    return _session_id;
}

void CSession::SetUserId(int uid)
{
    _user_uid = uid;
}

int CSession::GetUserId()
{
    return _user_uid;
}

void CSession::Start()
{
    AsyncReadHead(HEAD_TOTAL_LEN);
}

void CSession::Send(std::string msg, short msgid)
{
    std::lock_guard<std::mutex> lock(_send_lock);
    int send_que_size = _send_que.size();
    if (send_que_size > MAX_SENDQUE) {
        spdlog::error("会话: {} 发送队列已满，当前大小: {}", _session_id, MAX_SENDQUE);
        return;
    }

    _send_que.push(make_shared<SendNode>(msg.c_str(), msg.length(), msgid));
    if (send_que_size > 0) {
        return;
    }
    auto &msgnode = _send_que.front();
    boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
                             std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::Send(char *msg, short max_length, short msgid)
{
    std::lock_guard<std::mutex> lock(_send_lock);
    int send_que_size = _send_que.size();
    if (send_que_size > MAX_SENDQUE) {
        spdlog::error("session: {} send que fulled, size is {}", _session_id, MAX_SENDQUE);
        return;
    }

    _send_que.push(make_shared<SendNode>(msg, max_length, msgid));
    if (send_que_size > 0) {
        return;
    }
    auto &msgnode = _send_que.front();
    boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
                             std::bind(&CSession::HandleWrite, this, std::placeholders::_1, SharedSelf()));
}

void CSession::Close()
{
    std::lock_guard<std::mutex> lock(_session_mtx);
    _socket.close();
    _b_close = true;
}

std::shared_ptr<CSession> CSession::SharedSelf()
{
    return shared_from_this();
}

void CSession::AsyncReadBody(int total_len)
{
    auto self = shared_from_this();
    asyncReadFull(total_len, [self, this, total_len](const boost::system::error_code &ec, std::size_t bytes_transfered) {
        try {
            if (ec) {
                spdlog::error("读取数据失败，错误信息: {}", ec.what());
                Close();
                DealExceptionSession();
                return;
            }

            if (bytes_transfered < total_len) {
                spdlog::error("读取长度不匹配，已读取 [{}] 字节, 总长度 [{}] 字节", bytes_transfered, total_len);
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            // 判断会话是否有效
            if (!_server->CheckValid(_session_id)) {
                Close();
                return;
            }

            memcpy(_recv_msg_node->_data, _data, bytes_transfered);
            _recv_msg_node->_cur_len += bytes_transfered;
            _recv_msg_node->_data[_recv_msg_node->_total_len] = '\0';
            spdlog::info("接收到的数据: {}", _recv_msg_node->_data);
            // 更新session心跳时间
            UpdateHeartbeat();
            // 此处将消息投递到逻辑处理队列
            LogicSystem::GetInstance()->PostMsgToQue(make_shared<LogicNode>(shared_from_this(), _recv_msg_node));
            // 继续监听头部数据接收事件
            AsyncReadHead(HEAD_TOTAL_LEN);
        } catch (std::exception &e) {
            spdlog::error("异常代码: {}", e.what());
        }
    });
}

void CSession::AsyncReadHead(int total_len)
{
    auto self = shared_from_this();
    asyncReadFull(HEAD_TOTAL_LEN, [self, this](const boost::system::error_code &ec, std::size_t bytes_transfered) {
        try {
            if (ec) {
                spdlog::error("handle read failed, error is {}", ec.what());
                Close();
                DealExceptionSession();
                return;
            }

            if (bytes_transfered < HEAD_TOTAL_LEN) {
                spdlog::error("read length not match, read [{}] , total [{}]", bytes_transfered, HEAD_TOTAL_LEN);
                Close();
                _server->ClearSession(_session_id);
                return;
            }

            // 判断会话是否有效
            if (!_server->CheckValid(_session_id)) {
                Close();
                return;
            }

            _recv_head_node->Clear();
            memcpy(_recv_head_node->_data, _data, bytes_transfered);

            // 获取头部MSGID数据
            short msg_id = 0;
            memcpy(&msg_id, _recv_head_node->_data, HEAD_ID_LEN);
            // 网络字节序转换为主机字节序
            msg_id = boost::asio::detail::socket_ops::network_to_host_short(msg_id);
            spdlog::info("消息ID: {}", msg_id);
            // 检查id是否有效
            if (msg_id > MAX_LENGTH) {
                spdlog::error("无效的消息ID: {}", msg_id);
                _server->ClearSession(_session_id);
                return;
            }
            short msg_len = 0;
            memcpy(&msg_len, _recv_head_node->_data + HEAD_ID_LEN, HEAD_DATA_LEN);
            // 网络字节序转换为主机字节序
            msg_len = boost::asio::detail::socket_ops::network_to_host_short(msg_len);
            spdlog::info("消息长度: {}", msg_len);

            // 检查长度是否有效
            if (msg_len > MAX_LENGTH) {
                spdlog::error("无效的数据长度: {}", msg_len);
                _server->ClearSession(_session_id);
                return;
            }

            _recv_msg_node = make_shared<RecvNode>(msg_len, msg_id);
            AsyncReadBody(msg_len);
        } catch (std::exception &e) {
            spdlog::error("Exception code is {}", e.what());
        }
    });
}

void CSession::HandleWrite(const boost::system::error_code &error, std::shared_ptr<CSession> shared_self)
{
    // 处理异常情况
    try {
        auto self = shared_from_this();
        if (!error) {
            std::lock_guard<std::mutex> lock(_send_lock);
            // cout << "send data " << _send_que.front()->_data+HEAD_LENGTH << endl;
            _send_que.pop();
            if (!_send_que.empty()) {
                auto &msgnode = _send_que.front();
                boost::asio::async_write(_socket, boost::asio::buffer(msgnode->_data, msgnode->_total_len),
                                         std::bind(&CSession::HandleWrite, this, std::placeholders::_1, shared_self));
            }
        } else {
            spdlog::error("写入数据失败，错误信息: {}", error.what());
            Close();
            DealExceptionSession();
        }
    } catch (std::exception &e) {
        spdlog::error("异常代码: {}", e.what());
    }
}

// 读取完整数据包
void CSession::asyncReadFull(std::size_t maxLength, std::function<void(const boost::system::error_code &, std::size_t)> handler)
{
    ::memset(_data, 0, MAX_LENGTH);
    asyncReadLen(0, maxLength, handler);
}

// 读取指定字节数
void CSession::asyncReadLen(std::size_t read_len, std::size_t total_len,
                            std::function<void(const boost::system::error_code &, std::size_t)> handler)
{
    auto self = shared_from_this();
    _socket.async_read_some(boost::asio::buffer(_data + read_len, total_len - read_len),
                            [read_len, total_len, handler, self](const boost::system::error_code &ec, std::size_t bytesTransfered) {
                                if (ec) {
                                    // 出现错误，调用回调函数
                                    handler(ec, read_len + bytesTransfered);
                                    return;
                                }

                                if (read_len + bytesTransfered >= total_len) {
                                    // 长度够了就调用回调函数
                                    handler(ec, read_len + bytesTransfered);
                                    return;
                                }

                                // 没有错误且长度不够继续读取
                                self->asyncReadLen(read_len + bytesTransfered, total_len, handler);
                            });
}

void CSession::NotifyOffline(int uid)
{

    Json::Value rtvalue;
    rtvalue["error"] = ErrorCodes::Success;
    rtvalue["uid"] = uid;

    std::string return_str = rtvalue.toStyledString();

    Send(return_str, ID_NOTIFY_OFF_LINE_REQ);
    return;
}

LogicNode::LogicNode(shared_ptr<CSession> session,
                     shared_ptr<RecvNode> recvnode) : _session(session), _recvnode(recvnode)
{
}

bool CSession::IsHeartbeatExpired(std::time_t &now)
{
    double diff_sec = std::difftime(now, _last_heartbeat);
    if (diff_sec > 20) {
        spdlog::error("心跳包过期，会话ID: {}", _session_id);
        return true;
    }

    return false;
}

void CSession::UpdateHeartbeat()
{
    time_t now = std::time(nullptr);
    _last_heartbeat = now;
}

void CSession::DealExceptionSession()
{
    auto self = shared_from_this();
    // 处理异常session
    auto uid_str = std::to_string(_user_uid);
    auto lock_key = LOCK_PREFIX + uid_str;
    auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
    Defer defer([identifier, lock_key, self, this]() {
        _server->ClearSession(_session_id);
        RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
    });

    if (identifier.empty()) {
        return;
    }
    std::string redis_session_id = "";
    auto bsuccess = RedisMgr::GetInstance()->Get(USER_SESSION_PREFIX + uid_str, redis_session_id);
    if (!bsuccess) {
        return;
    }

    if (redis_session_id != _session_id) {
        // 说明有其他客户端连接，当前连接应该断开
        return;
    }

    RedisMgr::GetInstance()->Del(USER_SESSION_PREFIX + uid_str);
    // 清理用户登录信息
    RedisMgr::GetInstance()->Del(USERIPPREFIX + uid_str);
}
