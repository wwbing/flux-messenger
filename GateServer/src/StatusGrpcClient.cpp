#include "StatusGrpcClient.h"
#include "const.h"

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
	ClientContext context;
	// 设置5秒超时
	auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
	context.set_deadline(deadline);
	
	GetChatServerRsp reply;
	GetChatServerReq request;
    request.set_uid(uid);
    
	auto stub = pool_->getConnection();
	if (!stub) {
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
    Status status = stub->GetChatServer(&context, request, &reply);

    Defer defer(
        [&stub, this]()
        {
            pool_->returnConnection(std::move(stub));
        });
    
	if (status.ok()) {	
		return reply;
	}
	else {
		spdlog::error("GRPC请求发出，但是获取ChatServer失败，错误码：{}，错误信息：{}", static_cast<int>(status.error_code()), status.error_message());
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
}

LoginRsp StatusGrpcClient::Login(int uid, std::string token)
{
	ClientContext context;
	// 设置5秒超时
	auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
	context.set_deadline(deadline);
	
	LoginRsp reply;
	LoginReq request;
	request.set_uid(uid);
	request.set_token(token);

	auto stub = pool_->getConnection();
	if (!stub) {
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
	Status status = stub->Login(&context, request, &reply);
	Defer defer([&stub, this]() {
		pool_->returnConnection(std::move(stub));
		});
	if (status.ok()) {
		return reply;
	}
	else {
		std::cout << "登录gRPC失败，错误码：" << status.error_code() 
			<< "，错误信息：" << status.error_message() << std::endl;
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
}


StatusGrpcClient::StatusGrpcClient()
{
    // 也是个单例，第一次调用的时候会初始化连接池
	auto& gCfgMgr = ConfigMgr::Inst();
	std::string host = gCfgMgr["StatusServer"]["Host"];
	std::string port = gCfgMgr["StatusServer"]["Port"];
	spdlog::info("StatusGrpcClient 读取参数 - Host: {}，Port: {}", host, port);
	

	pool_.reset(new StatusConPool(5, host, port));
	spdlog::info("StatusGrpcClient连接池创建成功");
}
