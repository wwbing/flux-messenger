#include "CServer.h"
#include <iostream>
#include "AsioIOServicePool.h"
#include "UserMgr.h"
#include "RedisMgr.h"
#include "ConfigMgr.h"

CServer::CServer(boost::asio::io_context &io_context, short port)
    : _io_context(io_context),
      _port(port),
      _acceptor(io_context, tcp::endpoint(tcp::v4(),port)), _timer(_io_context, std::chrono::seconds(60))
{
	cout << "ChatServer 2 启动成功, 正在监听端口 : " << _port << endl;

	StartAccept();
}

CServer::~CServer() {
	cout << "ChatServer 2 关闭成功, 关闭监听端口 : " << _port << endl;
	
}

void CServer::HandleAccept(shared_ptr<CSession> new_session, const boost::system::error_code& error){
	if (!error) {
		new_session->Start();
		lock_guard<mutex> lock(_mutex);
		_sessions.insert(make_pair(new_session->GetSessionId(), new_session));
	}
	else {
		cout << "Tcp长连接建立失败:accept fail " << error.what() << endl;
	}

	StartAccept();
}

void CServer::StartAccept()
{
    /*
		和客户端登录的http请求同理，这里对应的是用 ioc池中的32个ioc来管理很多很多个tcp池的tcp长连接（每个CSession实例对应一个tcp长连接）
	*/
	auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();
	shared_ptr<CSession> new_session = make_shared<CSession>(io_context, this);
	_acceptor.async_accept(new_session->GetSocket(), std::bind(&CServer::HandleAccept, this, new_session, placeholders::_1));
}

// 清理session，根据id删除session，并移除用户的session关联关系
void CServer::ClearSession(std::string session_id) {
	
	lock_guard<mutex> lock(_mutex);
	if (_sessions.find(session_id) != _sessions.end()) {
		auto uid = _sessions[session_id]->GetUserId();

		// 移除用户的session关联关系
		UserMgr::GetInstance()->RmvUserSession(uid, session_id);
	}

	_sessions.erase(session_id);
	
}

// 根据用户id获取session
shared_ptr<CSession> CServer::GetSession(std::string uuid) {
	lock_guard<mutex> lock(_mutex);
	auto it = _sessions.find(uuid);
	if (it != _sessions.end()) {
		return it->second;
	}
	return nullptr;
}

bool CServer::CheckValid(std::string uuid)
{
	lock_guard<mutex> lock(_mutex);
	auto it = _sessions.find(uuid);
	if (it != _sessions.end()) {
		return true;
	}
	return false;
}

void CServer::on_timer(const boost::system::error_code& ec) {
	if (ec) {
		std::cout << "定时器错误: " << ec.message() << std::endl;
		return;
	}

	std::vector<std::shared_ptr<CSession>> _expired_sessions;
	int session_count = 0;

	std::map<std::string, shared_ptr<CSession>> sessions_copy;
	{
		lock_guard<mutex> lock(_mutex);
		sessions_copy = _sessions;
	}

	time_t now = std::time(nullptr);
	for (auto iter = sessions_copy.begin(); iter != sessions_copy.end(); iter++) {
		auto b_expired = iter->second->IsHeartbeatExpired(now);
		if (b_expired) {
			//关闭socket, 实际上也会触发async_read的结束
			iter->second->Close();
			//记录过期session信息
			_expired_sessions.push_back(iter->second);
			continue;
		}
		session_count++;
	}


	//上报session数量
	auto& cfg = ConfigMgr::Inst();
	auto self_name = cfg["SelfServer"]["Name"];
    auto count_str = std::to_string(session_count);
    cout<<"定时器上报ChatServer2 的连接数到redis中，当前连接数: "<<count_str<<endl;
	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, self_name, count_str);

	// 处理异常session，防止资源泄漏
	for (auto &session : _expired_sessions) {
		session->DealExceptionSession();
	}
	
	// 再次定时，下一次10s后
	_timer.expires_after(std::chrono::seconds(10));
	_timer.async_wait([this](boost::system::error_code ec) {
		on_timer(ec);
	});
}

void CServer::StartTimer()
{
	// 启动定时器
	auto self(shared_from_this());
	_timer.async_wait([self](boost::system::error_code ec) {
		self->on_timer(ec);
		});
}

void CServer::StopTimer()
{
	_timer.cancel();
}
