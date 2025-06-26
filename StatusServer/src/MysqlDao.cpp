#include "MysqlDao.h"
#include "ConfigMgr.h"

MysqlDao::MysqlDao()
{
	auto & cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = std::stoi(cfg["Mysql"]["Port"]);
	const auto& pwd = cfg["Mysql"]["Passwd"];
	const auto& schema = cfg["Mysql"]["Schema"];
	const auto& user = cfg["Mysql"]["User"];
	pool_.reset(new MySqlPool(host, port, user, pwd, schema, 5));
}

MysqlDao::~MysqlDao(){
	pool_->Close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return -1;
		}

		auto result = con->_session->sql("CALL reg_user(?, ?, ?, @result)")
			.bind(name, email, pwd)
			.execute();

		auto res = con->_session->sql("SELECT @result AS result").execute();
		auto row = res.fetchOne();
		if (row) {
			int result_value = row[0];
			pool_->returnConnection(std::move(con));
			return result_value;
		}
		pool_->returnConnection(std::move(con));
		return -1;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return -1;
	}
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		auto result = con->_session->sql("SELECT email FROM user WHERE name = ?")
			.bind(name)
			.execute();

		auto row = result.fetchOne();
		if (row) {
			std::string db_email = row[0].get<std::string>();
			spdlog::info("检查邮箱: {}", db_email);
			pool_->returnConnection(std::move(con));
			return email == db_email;
		}
		pool_->returnConnection(std::move(con));
		return false;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		auto result = con->_session->sql("UPDATE user SET pwd = ? WHERE name = ?")
			.bind(newpwd, name)
			.execute();

		spdlog::info("UpdatePwd 影响行数: {}", result.getAffectedItemsCount());
		pool_->returnConnection(std::move(con));
		return true;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		spdlog::error("Mysql 异常: {}", e.what());
		return false;
	}
}

bool MysqlDao::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	try {
		auto result = con->_session->sql("SELECT * FROM user WHERE name = ?")
			.bind(name)
			.execute();

		auto row = result.fetchOne();
		if (!row) {
			pool_->returnConnection(std::move(con));
			return false;
		}

		std::string db_pwd = row[2].get<std::string>();  // Assuming pwd is the third column
		if (pwd != db_pwd) {
			pool_->returnConnection(std::move(con));
			return false;
		}

		userInfo.name = row[1].get<std::string>();       // name
		userInfo.pwd = db_pwd;                           // pwd
		userInfo.uid = row[0].get<int>();               // uid
		userInfo.email = row[3].get<std::string>();      // email

		pool_->returnConnection(std::move(con));
		return true;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return false;
	}
}


