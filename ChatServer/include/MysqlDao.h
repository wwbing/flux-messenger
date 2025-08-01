#pragma once
#include "const.h"
#include <thread>
#include <mysqlx/xdevapi.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <chrono>
#include <iostream>
#include "data.h"

// 使用 X DevAPI 的 Session
class SqlConnection {
public:
	SqlConnection(std::shared_ptr<mysqlx::Session> session, int64_t lasttime) :
		_session(session),
		_last_oper_time(lasttime)
	{
	}

	std::shared_ptr<mysqlx::Session> _session;
	int64_t _last_oper_time;
};

// 使用 X DevAPI 的连接池
class MySqlPool {
public:
	MySqlPool(const std::string& host, int port, const std::string& user, 
			  const std::string& pass, const std::string& schema, int poolSize)
		: host_(host), port_(port), user_(user), pass_(pass), 
		  schema_(schema), poolSize_(poolSize), b_stop_(false) {
		try {
			for (int i = 0; i < poolSize_; ++i) {
				mysqlx::SessionSettings settings(
					host_, port_,
					user_, pass_,
					schema_
				);
				auto session = std::make_shared<mysqlx::Session>(settings);
				auto currentTime = std::chrono::system_clock::now().time_since_epoch();
				long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
				pool_.push(std::make_unique<SqlConnection>(session, timestamp));
			}

			_check_thread = std::thread([this]() {
				while (!b_stop_) {
					checkConnection();
					std::this_thread::sleep_for(std::chrono::seconds(60));
				}
			});

			_check_thread.detach();
		}
		catch (const mysqlx::Error& e) {
			spdlog::error("Mysql 连接池初始化失败: {}", e.what());
		}
	}

	void checkConnection() {
		std::lock_guard<std::mutex> guard(mutex_);
		int poolsize = pool_.size();
		auto currentTime = std::chrono::system_clock::now().time_since_epoch();
		long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
		
		for (int i = 0; i < poolsize; i++) {
			auto con = std::move(pool_.front());
			pool_.pop();
			Defer defer([this, &con]() {
				pool_.push(std::move(con));
			});

			if (timestamp - con->_last_oper_time < 5) {
				continue;
			}

			try {
				con->_session->sql("SELECT 1").execute();
				con->_last_oper_time = timestamp;
			}
			catch (const mysqlx::Error& e) {
				spdlog::error("Mysql 连接池保持连接失败: {}", e.what());
				mysqlx::SessionSettings settings(
					host_, port_,
					user_, pass_,
					schema_
				);
				con->_session = std::make_shared<mysqlx::Session>(settings);
				con->_last_oper_time = timestamp;
			}
		}
	}

	std::unique_ptr<SqlConnection> getConnection() {
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [this] { 
			if (b_stop_) {
				return true;
			}       
			return !pool_.empty(); });
		if (b_stop_) {
			return nullptr;
		}
		std::unique_ptr<SqlConnection> con(std::move(pool_.front()));
		pool_.pop();
		return con;
	}

	void returnConnection(std::unique_ptr<SqlConnection> con) {
		std::unique_lock<std::mutex> lock(mutex_);
		if (b_stop_) {
			return;
		}
		pool_.push(std::move(con));
		cond_.notify_one();
	}

	void Close() {
		b_stop_ = true;
		cond_.notify_all();
	}

	~MySqlPool() {
		std::unique_lock<std::mutex> lock(mutex_);
		while (!pool_.empty()) {
			pool_.pop();
		}
	}

private:
	std::string host_;
	int port_;
	std::string user_;
	std::string pass_;
	std::string schema_;
	int poolSize_;
	std::queue<std::unique_ptr<SqlConnection>> pool_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::atomic<bool> b_stop_;
	std::thread _check_thread;
};

class MysqlDao
{
public:
	MysqlDao();
	~MysqlDao();
	int RegUser(const std::string& name, const std::string& email, const std::string& pwd);
	bool CheckEmail(const std::string& name, const std::string& email);
	bool UpdatePwd(const std::string& name, const std::string& newpwd);
	bool CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo);
	bool AddFriendApply(const int& from, const int& to);
	bool AuthFriendApply(const int& from, const int& to);
	bool AddFriend(const int& from, const int& to, std::string back_name);
	std::shared_ptr<UserInfo> GetUser(int uid);
	std::shared_ptr<UserInfo> GetUser(std::string name);
	bool GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int offset, int limit);
	bool GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_info);
private:
	std::unique_ptr<MySqlPool> pool_;
};


