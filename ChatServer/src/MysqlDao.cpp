#include "MysqlDao.h"
#include "ConfigMgr.h"

MysqlDao::MysqlDao()
{
	auto& cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = std::stoi(cfg["Mysql"]["Port"]);
	const auto& pwd = cfg["Mysql"]["Passwd"];
	const auto& schema = cfg["Mysql"]["Schema"];
	const auto& user = cfg["Mysql"]["User"];
	pool_.reset(new MySqlPool(host, port, user, pwd, schema, 5));
}

MysqlDao::~MysqlDao() {
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
			int result_value = row[0].get<int>();
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
			std::cout << "Check Email: " << db_email << std::endl;
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

		std::cout << "Updated rows: " << result.getAffectedItemsCount() << std::endl;
		pool_->returnConnection(std::move(con));
		return true;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
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

bool MysqlDao::AddFriendApply(const int& from, const int& to)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	try {
		auto result = con->_session->sql("INSERT INTO friend_apply (from_uid, to_uid) VALUES (?, ?) "
			"ON DUPLICATE KEY UPDATE from_uid = from_uid, to_uid = to_uid")
			.bind(from, to)
			.execute();
			
		bool success = result.getAffectedItemsCount() > 0;
		pool_->returnConnection(std::move(con));
		return success;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::AuthFriendApply(const int& from, const int& to)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	try {
		auto result = con->_session->sql("UPDATE friend_apply SET status = 1 WHERE from_uid = ? AND to_uid = ?")
			.bind(from, to)
			.execute();
			
		bool success = result.getAffectedItemsCount() > 0;
		pool_->returnConnection(std::move(con));
		return success;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::AddFriend(const int& from, const int& to, std::string back_name)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	try {
		auto result = con->_session->sql("INSERT INTO friend_list (self_id, friend_id, back_name) VALUES (?, ?, ?)")
			.bind(from, to, back_name)
			.execute();
			
		bool success = result.getAffectedItemsCount() > 0;
		pool_->returnConnection(std::move(con));
		return success;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return false;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	try {
		auto result = con->_session->sql("SELECT * FROM user WHERE uid = ?")
			.bind(uid)
			.execute();

		auto row = result.fetchOne();
		if (row) {
			auto userInfo = std::make_shared<UserInfo>();
			userInfo->uid = row[0].get<int>();
			userInfo->name = row[1].get<std::string>();
			userInfo->email = row[2].get<std::string>();
			userInfo->pwd = row[3].get<std::string>();
			pool_->returnConnection(std::move(con));
			return userInfo;
		}
		pool_->returnConnection(std::move(con));
		return nullptr;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return nullptr;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(std::string name)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	try {
		auto result = con->_session->sql("SELECT * FROM user WHERE name = ?")
			.bind(name)
			.execute();

		auto row = result.fetchOne();
		if (row) {
			auto userInfo = std::make_shared<UserInfo>();
			userInfo->uid = row[0].get<int>();
			userInfo->name = row[1].get<std::string>();
			userInfo->email = row[2].get<std::string>();
			userInfo->pwd = row[3].get<std::string>();
			pool_->returnConnection(std::move(con));
			return userInfo;
		}
		pool_->returnConnection(std::move(con));
		return nullptr;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return nullptr;
	}
}

bool MysqlDao::GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int offset, int limit)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	try {
		auto result = con->_session->sql("SELECT a.*, u.name as from_name FROM friend_apply a LEFT JOIN user u ON a.from_uid = u.uid WHERE a.to_uid = ? AND a.status = 0 LIMIT ?, ?")
			.bind(touid, offset, limit)
			.execute();
			
		auto rows = result.fetchAll();
		for (auto row : rows) {
			// 创建 ApplyInfo 对象，使用正确的构造函数参数
			auto applyInfo = std::make_shared<ApplyInfo>(
			    row[0].get<int>(),          // _uid (from_uid)
			    row[3].get<std::string>(),  // _name (from_name)
			    "",                         // _desc
			    "",                         // _icon
			    "",                         // _nick
			    0,                          // _sex
			    row[2].get<int>()           // _status
			);
			applyList.push_back(applyInfo);
		}
		pool_->returnConnection(std::move(con));
		return true;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_info_list)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	try {
		auto result = con->_session->sql("SELECT u.*, f.back_name FROM friend_list f LEFT JOIN user u ON f.friend_id = u.uid WHERE f.self_id = ?")
			.bind(self_id)
			.execute();
			
		auto rows = result.fetchAll();
		for (auto row : rows) {
			auto userInfo = std::make_shared<UserInfo>();
			userInfo->uid = row[0].get<int>();
			userInfo->name = row[1].get<std::string>();
			userInfo->email = row[2].get<std::string>();
			userInfo->pwd = row[3].get<std::string>();
			userInfo->back = row[4].get<std::string>();
			user_info_list.push_back(userInfo);
		}
		pool_->returnConnection(std::move(con));
		return true;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "MySQL Error: " << e.what() << std::endl;
		return false;
	}
}
