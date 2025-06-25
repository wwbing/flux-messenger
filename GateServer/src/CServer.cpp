#include "CServer.h"
#include "AsioIOServicePool.h"
#include "HttpConnection.h"
#include <iostream>
CServer::CServer(boost::asio::io_context &ioc, unsigned short &port)
    : _ioc(ioc),
      _acceptor(ioc, tcp::endpoint(tcp::v4(), port)) // tcp::v4()表示监听本机所有的地址，默认就是端口复用的
{
}

void CServer::Start()
{
    auto self = shared_from_this();

    /*
        并发处理：
            从ioc管理池子中取出的这个ioc，IO管理器用来管理具体的每一个http的连接
            池子目前初始化IOC个数和cpu核数一致(轮询的方式取出)，保证每一个ioc尽可能使并行

            可能有很多个http的连接，每次监听到会取池子中的下一个ioc来创建新的socket管理http的请求
            并发的情况是：假设8000个连接过来了，我们会实例化8000个http的类，但是实例化这8000个类用到的ioc是轮番的拿池子中的32个
						 所以一个ioc会管理很多http的连接请求
	*/
    auto &io_context = AsioIOServicePool::GetInstance()->GetIOService();
    std::shared_ptr<HttpConnection> new_con = std::make_shared<HttpConnection>(io_context);

    /*
        .async_accept
            (第一个参数是用来通信的socket，监听到连接侯会把连接的所有权交给该socket管理)
			(第二个参数是：交给socket后我们需要调用的回调函数) bind或者lamda都可以，注意函数签名（参数是er，返回值是void）
	*/
    _acceptor.async_accept(
        new_con->GetSocket(),
        [self, new_con](beast::error_code ec)
        {
            try
            {
                if (ec)					// 如果有错误，继续监听
                {
                    self->Start();
                    return;
                }

                
                new_con->Start();		// 启动连接，处理 HttpConnection
               
                self->Start(); 			// 会从ioc池子取下一个
            }
            catch (std::exception &exp)
            {
                spdlog::error("异常 {}", exp.what());
                self->Start();
            }
        });
    
}
