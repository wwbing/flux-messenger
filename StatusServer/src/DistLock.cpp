#include "DistLock.h"
#include <thread>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <hiredis/hiredis.h>


//���嵥��ģʽ
DistLock& DistLock::Inst() {
	static DistLock lock;
	return lock;
}

// ʹ�� Boost UUID ����ȫ��Ψһ��ʶ����UUID��
static std::string generateUUID() {
	boost::uuids::uuid uuid = boost::uuids::random_generator()();
	return to_string(uuid);
}

// ���Ի�ȡ������������Ψһ��ʶ����UUID���������ȡʧ���򷵻ؿ��ַ���
std::string DistLock::acquireLock(redisContext* context, const std::string& lockName,
    int lockTimeout, int acquireTimeout) {
    std::string identifier = generateUUID();
    std::string lockKey = "lock:" + lockName;
    auto endTime = std::chrono::steady_clock::now() + std::chrono::seconds(acquireTimeout);

    while (std::chrono::steady_clock::now() < endTime) {
        // ʹ�� SET ����Լ�����SET lockKey identifier NX EX lockTimeout
        redisReply* reply = (redisReply*)redisCommand(context, "SET %s %s NX EX %d",
            lockKey.c_str(), identifier.c_str(), lockTimeout);
        if (reply != nullptr) {
            // �жϷ��ؽ���Ƿ�Ϊ OK
            if (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK") {
                freeReplyObject(reply);
                return identifier;
            }
            freeReplyObject(reply);
        }
        // ��ͣ 1 ��������ԣ���ֹæ�ȴ�
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return "";
}

// �ͷ�����ֻ�����ĳ����߲����ͷţ������Ƿ�ɹ�
bool DistLock::releaseLock(redisContext* context, const std::string& lockName,
    const std::string& identifier) {
    std::string lockKey = "lock:" + lockName;
    // Lua �ű����ж�����ʶ�Ƿ�ƥ�䣬ƥ����ɾ����
    const char* luaScript = "if redis.call('get', KEYS[1]) == ARGV[1] then \
                                return redis.call('del', KEYS[1]) \
                             else \
                                return 0 \
                             end";
    // ���� EVAL ����ִ�� Lua �ű�����һ������Ϊ�ű�����������Ϊ key ��������key �Լ���Ӧ�Ĳ���
    redisReply* reply = (redisReply*)redisCommand(context, "EVAL %s 1 %s %s",
        luaScript, lockKey.c_str(), identifier.c_str());
    bool success = false;
    if (reply != nullptr) {
        // ����������ֵΪ 1 ʱ����ʾ�ɹ�ɾ������
        if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 1) {
            success = true;
        }
        freeReplyObject(reply);
    }
    return success;
}