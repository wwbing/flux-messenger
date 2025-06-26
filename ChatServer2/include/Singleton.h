#pragma once
#include <memory>
#include <mutex>
#include <iostream>
#include "const.h"
using namespace std;
template <typename T>
class Singleton {
protected:
	Singleton() = default;
	Singleton(const Singleton<T>&) = delete;
	Singleton& operator=(const Singleton<T>& st) = delete;
	
	static std::shared_ptr<T> _instance;
public:
	static std::shared_ptr<T> GetInstance() {
		static std::once_flag s_flag;
		std::call_once(s_flag, [&]() {
			_instance = shared_ptr<T>(new T);
			});

		return _instance;
	}
	void PrintAddress() {
		spdlog::info("单例地址: {}", _instance.get());
	}
	~Singleton() {
		spdlog::warn("单例析构调用");
	}
};

template <typename T>
std::shared_ptr<T> Singleton<T>::_instance = nullptr;
