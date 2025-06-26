#include "RedisMgr.h"
#include "const.h"
#include "ConfigMgr.h"
#include "DistLock.h"
RedisMgr::RedisMgr() {
	auto& gCfgMgr = ConfigMgr::Inst();
	auto host = gCfgMgr["Redis"]["Host"];
	auto port = gCfgMgr["Redis"]["Port"];
	auto pwd = gCfgMgr["Redis"]["Passwd"];
	_con_pool.reset(new RedisConPool(10, host.c_str(), atoi(port.c_str()), pwd.c_str()));
}

RedisMgr::~RedisMgr() {
	
}



bool RedisMgr::Get(const std::string& key, std::string& value)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	 auto reply = (redisReply*)redisCommand(connect, "GET %s", key.c_str());
	if (reply == NULL) {
	 spdlog::error("[ GET  {} ] 失败: 响应为空，连接错误: {}", key, connect->errstr);
	 _con_pool->returnConnection(connect);
	 return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
	 spdlog::info("[ GET {} ] 键不存在", key);
	 freeReplyObject(reply);
	 _con_pool->returnConnection(connect);
	 return false;
	}

	if (reply->type != REDIS_REPLY_STRING) {
	 spdlog::error("[ GET {} ] 类型错误: {}", key, reply->type);
	 freeReplyObject(reply);
	 _con_pool->returnConnection(connect);
	 return false;
	}

	 value = reply->str;
	 freeReplyObject(reply);

	 spdlog::info("成功执行命令 [ GET {}  ]", key);
	 _con_pool->returnConnection(connect);
	 return true;
}

bool RedisMgr::Set(const std::string &key, const std::string &value){
	//执行redis命令
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "SET %s %s", key.c_str(), value.c_str());

	//如果返回NULL则说明执行失败
	if (NULL == reply)
	{
		spdlog::error("执行命令 [ SET {}  {} ] 失败！", key, value);
		//freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	//如果执行失败则释放连接
	if (!(reply->type == REDIS_REPLY_STATUS && (strcmp(reply->str, "OK") == 0 || strcmp(reply->str, "ok") == 0)))
	{
		spdlog::error("Execut command [ SET {}  {} ] failure ! ", key, value);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	//执行成功 释放redisCommand执行后返回的redisReply所占用的内存
	freeReplyObject(reply);
	spdlog::info("执行命令 [ SET {}  {} ] 成功！", key, value);
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::LPush(const std::string &key, const std::string &value)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "LPUSH %s %s", key.c_str(), value.c_str());
	if (NULL == reply)
	{
		spdlog::error("执行命令 [ LPUSH {}  {} ] 失败！", key, value);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
		spdlog::error("执行命令 [ LPUSH {}  {} ] 失败！", key, value);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	spdlog::info("执行命令 [ LPUSH {}  {} ] 成功！", key, value);
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::LPop(const std::string &key, std::string& value){
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "LPOP %s ", key.c_str());
	if (reply == nullptr ) {
		spdlog::error("执行命令 [ LPOP {} ] 失败！", key);
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		spdlog::error("Execut command [ LPOP {} ] failure ! ", key);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	value = reply->str;
	spdlog::info("执行命令 [ LPOP {} ] 成功！", key);
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::RPush(const std::string& key, const std::string& value) {
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "RPUSH %s %s", key.c_str(), value.c_str());
	if (NULL == reply)
	{
		spdlog::error("执行命令 [ RPUSH {}  {} ] 失败！", key, value);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER || reply->integer <= 0) {
		spdlog::error("Execut command [ RPUSH {}  {} ] failure ! ", key, value);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	spdlog::info("执行命令 [ RPUSH {}  {} ] 成功！", key, value);
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}
bool RedisMgr::RPop(const std::string& key, std::string& value) {
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "RPOP %s ", key.c_str());
	if (reply == nullptr ) {
		spdlog::error("执行命令 [ RPOP {} ] 失败！", key);
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type == REDIS_REPLY_NIL) {
		spdlog::error("Execut command [ RPOP {} ] failure ! ", key);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}
	value = reply->str;
	spdlog::info("执行命令 [ RPOP {} ] 成功！", key);
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::HSet(const std::string &key, const std::string &hkey, const std::string &value) {
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "HSET %s %s %s", key.c_str(), hkey.c_str(), value.c_str());	if (reply == nullptr ) {
		spdlog::error("执行命令 [ HSet {}  {}  {} ] 失败！", key, hkey, value);
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		spdlog::error("执行命令 [ HSet {}  {}  {} ] 失败！", key, hkey, value);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	spdlog::info("执行命令 [ HSet {}  {}  {} ] 成功！", key, hkey, value);
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}

bool RedisMgr::HSet(const char* key, const char* hkey, const char* hvalue, size_t hvaluelen)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	 const char* argv[4];
	 size_t argvlen[4];
	 argv[0] = "HSET";
	argvlen[0] = 4;
	argv[1] = key;
	argvlen[1] = strlen(key);
	argv[2] = hkey;
	argvlen[2] = strlen(hkey);
	argv[3] = hvalue;
	argvlen[3] = hvaluelen;

	auto reply = (redisReply*)redisCommandArgv(connect, 4, argv, argvlen);
	if (reply == nullptr ) {
		spdlog::error("执行命令 [ HSet {}  {}  {} ] 失败！", key, hkey, hvalue);
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER) {
		spdlog::error("Execut command [ HSet {}  {}  {} ] failure ! ", key, hkey, hvalue);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}
	spdlog::info("执行命令 [ HSet {}  {}  {} ] 成功！", key, hkey, hvalue);
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}

std::string RedisMgr::HGet(const std::string &key, const std::string &hkey)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return "";
	}
	const char* argv[3];
	size_t argvlen[3];
	argv[0] = "HGET";
	argvlen[0] = 4;
	argv[1] = key.c_str();
	argvlen[1] = key.length();
	argv[2] = hkey.c_str();
	argvlen[2] = hkey.length();
	
	auto reply = (redisReply*)redisCommandArgv(connect, 3, argv, argvlen);
	if (reply == nullptr) {
		spdlog::error("[ HGet {} {} ] 失败：响应为空，连接错误: {}", key, hkey, connect->errstr);
		_con_pool->returnConnection(connect);
		return "";
	}

	if (reply->type == REDIS_REPLY_NIL) {
		freeReplyObject(reply);
		spdlog::info("[ HGet {} {} ] 字段不存在", key, hkey);
		_con_pool->returnConnection(connect);
		return "";
	}

	if (reply->type != REDIS_REPLY_STRING) {
		freeReplyObject(reply);
		spdlog::error("[ HGet {} {} ] 类型错误: {}", key, hkey, reply->type);
		_con_pool->returnConnection(connect);
		return "";
	}

	std::string value = reply->str;
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	spdlog::info("执行命令 [ HGet {} {} ] 成功！", key, hkey);
	return value;
}

bool RedisMgr::HDel(const std::string& key, const std::string& field)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}

	Defer defer([&connect, this]() {
		_con_pool->returnConnection(connect);
		});

	redisReply* reply = (redisReply*)redisCommand(connect, "HDEL %s %s", key.c_str(), field.c_str());
	if (reply == nullptr) {
		spdlog::error("HDEL命令执行失败");
		return false;
	}

	bool success = false;
	if (reply->type == REDIS_REPLY_INTEGER) {
		success = reply->integer > 0;
	}

	freeReplyObject(reply);
	return success;
}

bool RedisMgr::Del(const std::string &key)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}
	auto reply = (redisReply*)redisCommand(connect, "DEL %s", key.c_str());
	if (reply == nullptr ) {
		spdlog::error("执行命令 [ Del {} ] 失败！", key);
		_con_pool->returnConnection(connect);
		return false;
	}

	if ( reply->type != REDIS_REPLY_INTEGER) {
		spdlog::error("Execut command [ Del {} ] failure ! ", key);
		freeReplyObject(reply);
		_con_pool->returnConnection(connect);
		return false;
	}

	spdlog::info("执行命令 [ Del {} ] 成功！", key);
	 freeReplyObject(reply);
	 _con_pool->returnConnection(connect);
	 return true;
}

bool RedisMgr::ExistsKey(const std::string &key)
{
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}

	auto reply = (redisReply*)redisCommand(connect, "exists %s", key.c_str());
	if (reply == nullptr ) {
		spdlog::info("未找到键 [ {} ]！", key);
		_con_pool->returnConnection(connect);
		return false;
	}

	if (reply->type != REDIS_REPLY_INTEGER || reply->integer == 0) {
		spdlog::info("Not Found [ Key {} ]  ! ", key);
		_con_pool->returnConnection(connect);
		freeReplyObject(reply);
		return false;
	}
	spdlog::info("找到键 [ {} ]！", key);
	freeReplyObject(reply);
	_con_pool->returnConnection(connect);
	return true;
}


std::string RedisMgr::acquireLock(const std::string& lockName,
	int lockTimeout, int acquireTimeout) {

	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return "";
	}

	Defer defer([&connect, this]() {
		_con_pool->returnConnection(connect);
	});

	return DistLock::Inst().acquireLock(connect, lockName, lockTimeout, acquireTimeout);
}

bool RedisMgr::releaseLock(const std::string& lockName,
	const std::string& identifier) {
	if (identifier.empty()) {
		return true;
	}
	auto connect = _con_pool->getConnection();
	if (connect == nullptr) {
		return false;
	}


	Defer defer([&connect, this]() {
		_con_pool->returnConnection(connect);
		});

	return DistLock::Inst().releaseLock(connect, lockName, identifier);
}

void RedisMgr::IncreaseCount(std::string server_name)
{
	auto lock_key = LOCK_COUNT;
	auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
	//设置defer回调
	Defer defer2([this, identifier, lock_key]() {
		RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
		});

	//获取登录计数器
	auto rd_res = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name);
	int count = 0;
	if (!rd_res.empty()) {
		count = std::stoi(rd_res);
	}

	count++;
	auto count_str = std::to_string(count);
	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, count_str);
}

void RedisMgr::DecreaseCount(std::string server_name)
{
	auto lock_key = LOCK_COUNT;
	auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
	//设置defer回调
	Defer defer2([this, identifier, lock_key]() {
		RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
		});

	//获取登录计数器
	auto rd_res = RedisMgr::GetInstance()->HGet(LOGIN_COUNT, server_name);
	int count = 0;
	if (!rd_res.empty()) {
		count = std::stoi(rd_res);
		if (count > 0) {
			count--;
		}
		
	}

	auto count_str = std::to_string(count);
	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, count_str);
}


void RedisMgr::InitCount(std::string server_name) {
	auto lock_key = LOCK_COUNT;
	auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
	//设置defer回调
	Defer defer2([this, identifier, lock_key]() {
		RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
		});

	RedisMgr::GetInstance()->HSet(LOGIN_COUNT, server_name, "0");
}

void RedisMgr::DelCount(std::string server_name) {
	auto lock_key = LOCK_COUNT;
	auto identifier = RedisMgr::GetInstance()->acquireLock(lock_key, LOCK_TIME_OUT, ACQUIRE_TIME_OUT);
	//设置defer回调
	Defer defer2([this, identifier, lock_key]() {
		RedisMgr::GetInstance()->releaseLock(lock_key, identifier);
		});

	RedisMgr::GetInstance()->HDel(LOGIN_COUNT, server_name);
}
