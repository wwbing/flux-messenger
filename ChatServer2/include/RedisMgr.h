#pragma once
#include "const.h"
#include <hiredis/hiredis.h>
#include <condition_variable>
#include <iostream>
#include <thread>
#include <chrono>
#include <queue>
#include <atomic>
#include <mutex>
#include "Singleton.h"
class RedisConPool {
public:
	RedisConPool(size_t poolSize, const char* host, int port, const char* pwd)
		: poolSize_(poolSize), host_(host), port_(port), b_stop_(false), pwd_(pwd), counter_(0), fail_count_(0){
		for (size_t i = 0; i < poolSize_; ++i) {
			auto* context = redisConnect(host, port);
			if (context == nullptr || context->err != 0) {
				if (context != nullptr) {
					redisFree(context);
				}
				continue;
			}

			auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd);
			if (reply->type == REDIS_REPLY_ERROR) {
				spdlog::error("认证失败, 错误: {}", reply->str);
				//执行成功后释放redisCommand执行后返回的redisReply所占用的内存
				freeReplyObject(reply);
				continue;
			}

			//执行成功后释放redisCommand执行后返回的redisReply所占用的内存
			freeReplyObject(reply);
			spdlog::info("Redis 认证成功");
			connections_.push(context);
		}

		check_thread_ = std::thread([this]() {
			while (!b_stop_) {
				counter_++;
				if (counter_ >= 60) {
					checkThreadPro();
					counter_ = 0;
				}

				std::this_thread::sleep_for(std::chrono::seconds(1)); // 每隔1秒检查一次
			}	
		});

	}

	~RedisConPool() {

	}

	void ClearConnections() {
		std::lock_guard<std::mutex> lock(mutex_);
		while (!connections_.empty()) {
			auto* context = connections_.front();
			redisFree(context);
			connections_.pop();
		}
	}

	redisContext* getConnection() {
		std::unique_lock<std::mutex> lock(mutex_);
		cond_.wait(lock, [this] { 
			if (b_stop_) {
				return true;
			}
			return !connections_.empty(); 
			});
		//如果停止了，直接返回空指针
		if (b_stop_) {
			return  nullptr;
		}
		auto* context = connections_.front();
		connections_.pop();
		return context;
	}

	redisContext* getConNonBlock() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (b_stop_) {
			return nullptr;
		}

		if (connections_.empty()) {
			return nullptr;
		}

		auto* context = connections_.front();
		connections_.pop();
		return context;
	}

	void returnConnection(redisContext* context) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (b_stop_) {
			return;
		}
		connections_.push(context);
		cond_.notify_one();
	}

	void Close() {
		b_stop_ = true;
		cond_.notify_all();
		check_thread_.join();
	}

private:

	void checkThreadPro() {
		size_t pool_size;
		{
			//获取当前连接数
			std::lock_guard<std::mutex> lock(mutex_);
			pool_size = connections_.size();
		}

		for (int i = 0; i < pool_size && !b_stop_; i++) {
			redisContext* context = nullptr;
			//1.取出一个连接(非阻塞)
			context = getConNonBlock();
			if (context == nullptr) {
				break;
			}

			redisReply* reply = nullptr;
			try {
				reply = (redisReply*)redisCommand(context, "PING");
				//2.检查底层I/O协议层是否有错
				if (context->err) {
					spdlog::error("Redis 连接失败: {}", context->err);
					if (reply) {
						freeReplyObject(reply);
					}

					redisFree(context);
					fail_count_++;
					continue;
				}

				//3.检查Redis服务器返回的是否是ERROR
				if (!reply || reply->type == REDIS_REPLY_ERROR) {
					spdlog::error("Redis 连接失败: {}", reply->str);
					if (reply) {
						freeReplyObject(reply);
					}

					redisFree(context);
					fail_count_++;
					continue;
				}

				freeReplyObject(reply);
				returnConnection(context);
			}catch(std::exception& exp){
				if (reply) {
					freeReplyObject(reply);
				}

				redisFree(context);
				fail_count_++;
			}
		}

		//执行重连操作
		while (fail_count_ > 0) {
			auto res = reconnect();
			if (res) {
					fail_count_--;
			}
			else {
				//等待下次检查
				break;
			}
		}
	}

	bool reconnect() {
		auto* context = redisConnect(host_, port_);
		if (context == nullptr || context->err != 0) {
			if (context != nullptr) {
				redisFree(context);
			}
			return false;
		}

		auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd_);
		if (reply->type == REDIS_REPLY_ERROR) {
			spdlog::error("Redis 认证失败, 错误: {}", reply->str);
			//执行释放操作
			freeReplyObject(reply);
			redisFree(context);
			return false;
		}

		//执行成功后释放redisCommand执行后返回的redisReply所占用的内存
		freeReplyObject(reply);
		spdlog::info("Redis 认证成功");
		returnConnection(context);
		return true;
	}

	void checkThread() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (b_stop_) {
			return;
		}
		auto pool_size = connections_.size();
		for (int i = 0; i < pool_size && !b_stop_; i++) {
			auto* context = connections_.front();
			connections_.pop();
			try {
				auto reply = (redisReply*)redisCommand(context, "PING");
				if (!reply) {
					spdlog::error("Redis 回复为空，连接失败");
					connections_.push(context);
					continue;
				}
				freeReplyObject(reply);
				connections_.push(context);
			}
			catch(std::exception& exp){
				spdlog::error("Redis 连接失败, 错误: {}", exp.what());
				redisFree(context);
				context = redisConnect(host_, port_);
				if (context == nullptr || context->err != 0) {
					if (context != nullptr) {
						redisFree(context);
					}
					continue;
				}

				auto reply = (redisReply*)redisCommand(context, "AUTH %s", pwd_);
				if (reply->type == REDIS_REPLY_ERROR) {
					spdlog::error("Redis 认证失败, 错误: {}", reply->str);
					//执行成功后释放redisCommand执行后返回的redisReply所占用的内存
					freeReplyObject(reply);
					continue;
				}

				//执行成功后释放redisCommand执行后返回的redisReply所占用的内存
				freeReplyObject(reply);
				spdlog::info("Redis 认证成功");
				connections_.push(context);
			}
		}
	}
	std::atomic<bool> b_stop_;
	size_t poolSize_;
	const char* host_;
	const char* pwd_;
	int port_;
	std::queue<redisContext*> connections_;
	std::mutex mutex_;
	std::condition_variable cond_;
	std::thread  check_thread_;
	int counter_;
	std::atomic<int> fail_count_;
};

class RedisMgr: public Singleton<RedisMgr>, 
	public std::enable_shared_from_this<RedisMgr>
{
	friend class Singleton<RedisMgr>;
public:
	~RedisMgr();
	bool Get(const std::string &key, std::string& value);
	bool Set(const std::string &key, const std::string &value);
	bool LPush(const std::string &key, const std::string &value);
	bool LPop(const std::string &key, std::string& value);
	bool RPush(const std::string& key, const std::string& value);
	bool RPop(const std::string& key, std::string& value);
	bool HSet(const std::string &key, const std::string  &hkey, const std::string &value);
	bool HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen);
	std::string HGet(const std::string &key, const std::string &hkey);
	bool HDel(const std::string& key, const std::string& field);
	bool Del(const std::string &key);
	bool ExistsKey(const std::string &key);
	void Close() {
		_con_pool->Close();
		_con_pool->ClearConnections();
	}

	std::string acquireLock(const std::string& lockName,
		int lockTimeout, int acquireTimeout);

	bool releaseLock(const std::string& lockName,
		const std::string& identifier);

	void IncreaseCount(std::string server_name);
	void DecreaseCount(std::string server_name);
	void InitCount(std::string server_name);
	void DelCount(std::string server_name);
private:
	RedisMgr();
	unique_ptr<RedisConPool>  _con_pool;
};