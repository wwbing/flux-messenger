#pragma once
#include "const.h"
#include <thread>
#include <mysqlx/xdevapi.h>
#include <condition_variable>
#include <iostream>
#include <atomic>
#include "data.h"
#include <memory>
#include <queue>
#include <mutex>

class SqlConnection {
public:
	SqlConnection(std::shared_ptr<mysqlx::Session> session, int64_t lasttime)
		: _session(session), _last_oper_time(lasttime) {}
	std::shared_ptr<mysqlx::Session> _session;
	int64_t _last_oper_time;
};

class MySqlPool {
public:
	MySqlPool(const std::string& host, const std::string& port, const std::string& user, 
			 const std::string& pass, const std::string& schema, int poolSize)
		: host_(host), port_(port), user_(user), pass_(pass), schema_(schema), 
		  poolSize_(poolSize), b_stop_(false), _fail_count(0) {
		try {
			for (int i = 0; i < poolSize_; ++i) {
				auto session = mysqlx::Session(host_, std::stoi(port_), user_, pass_);
				session.sql("USE " + schema_).execute();
				
				// 获取当前时间戳
				auto currentTime = std::chrono::system_clock::now().time_since_epoch();
				long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
				
				pool_.push(std::make_unique<SqlConnection>(
					std::make_shared<mysqlx::Session>(std::move(session)), 
					timestamp
				));
				std::cout << "mysql connection init success" << std::endl;
			}

			_check_thread = std::thread([this]() {
				int count = 0;
				while (!b_stop_) {
					if (count >= 60) {
						checkConnectionPro();
						count = 0;            
					}
		
					std::this_thread::sleep_for(std::chrono::seconds(1));
					count++;
				}
			});

			_check_thread.detach();
		}
		catch (const mysqlx::Error& e) {
			std::cout << "mysql pool init failed, error is " << e.what() << std::endl;
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
				std::cout << "Error keeping connection alive: " << e.what() << std::endl;
				auto session = mysqlx::Session(host_, std::stoi(port_), user_, pass_);
				session.sql("USE " + schema_).execute();
				con->_session = std::make_shared<mysqlx::Session>(std::move(session));
				con->_last_oper_time = timestamp;
			}
		}
	}

	void checkConnectionPro() {
		size_t targetCount;
		{
			std::lock_guard<std::mutex> guard(mutex_);
			targetCount = pool_.size();
		}

		size_t processed = 0;
		auto now = std::chrono::system_clock::now().time_since_epoch();
		long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();

		while (processed < targetCount) {
			std::unique_ptr<SqlConnection> con;
			{
				std::lock_guard<std::mutex> guard(mutex_);
				if (pool_.empty()) {
					break;
				}
				con = std::move(pool_.front());
				pool_.pop();
			}

			bool healthy = true;
			if (timestamp - con->_last_oper_time >= 5) {
				try {
					con->_session->sql("SELECT 1").execute();
					con->_last_oper_time = timestamp;
				}
				catch (const mysqlx::Error& e) {
					std::cout << "Error keeping connection alive: " << e.what() << std::endl;
					healthy = false;
					_fail_count++;
				}
			}

			if(healthy) {
				std::lock_guard<std::mutex> guard(mutex_);
				pool_.push(std::move(con));
				cond_.notify_one();
			}

			++processed;
		}

		while (_fail_count > 0) {
			auto b_res = reconnect(timestamp);
			if (b_res) {
				_fail_count--;
			}
			else {
				break;
			}
		}
	}

	bool reconnect(long long timestamp) {
		try {
			auto session = mysqlx::Session(host_, std::stoi(port_), user_, pass_);
			session.sql("USE " + schema_).execute();
			auto newCon = std::make_unique<SqlConnection>(
				std::make_shared<mysqlx::Session>(std::move(session)), 
				timestamp
			);
			{
				std::lock_guard<std::mutex> guard(mutex_);
				pool_.push(std::move(newCon));
			}

			std::cout << "mysql connection reconnect success" << std::endl;
			return true;
		}
		catch (const mysqlx::Error& e) {
			std::cout << "Reconnect failed, error is " << e.what() << std::endl;
			return false;
		}
	}

	std::unique_ptr<SqlConnection> getConnection() {
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [this] { 
			if (b_stop_) {
				return true;
			}        
			return !pool_.empty(); 
		});
		
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
	std::string port_;
	std::string user_;
	std::string pass_;
	std::string schema_;
	int poolSize_;
	std::queue<std::unique_ptr<SqlConnection>> pool_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::atomic<bool> b_stop_;
	std::thread _check_thread;
	std::atomic<int> _fail_count;
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


