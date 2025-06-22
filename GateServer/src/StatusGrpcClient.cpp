#include "StatusGrpcClient.h"

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
	Defer defer([&stub, this]() {
		pool_->returnConnection(std::move(stub));
		});
	if (status.ok()) {	
		return reply;
	}
	else {
		std::cout << "GetChatServer gRPC failed: " << status.error_code() 
			<< ", message: " << status.error_message() << std::endl;
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
		std::cout << "Login gRPC failed: " << status.error_code() 
			<< ", message: " << status.error_message() << std::endl;
		reply.set_error(ErrorCodes::RPCFailed);
		return reply;
	}
}


StatusGrpcClient::StatusGrpcClient()
{
	auto& gCfgMgr = ConfigMgr::Inst();
	std::string host = gCfgMgr["StatusServer"]["Host"];
	std::string port = gCfgMgr["StatusServer"]["Port"];
	std::cout << "StatusGrpcClient config read - Host: " << host << ", Port: " << port << std::endl;
	

	pool_.reset(new StatusConPool(5, host, port));
	std::cout << "StatusGrpcClient connection pool created successfully" << std::endl;
}
