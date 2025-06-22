#include "MysqlDao.h"
#include "ConfigMgr.h"

MysqlDao::MysqlDao()
{
	auto & cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = cfg["Mysql"]["Port"];
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

		// 调用存储过程
		auto result = con->_session->sql("CALL reg_user(?, ?, ?, @result)")
			.bind(name, email, pwd)
			.execute();

		// 获取存储过程的返回值
		auto res = con->_session->sql("SELECT @result AS result").execute();
		auto row = res.fetchOne();
		if (row) {
			int ret = row[0];
			std::cout << "Result: " << ret << std::endl;
			pool_->returnConnection(std::move(con));
			return ret;
		}
		pool_->returnConnection(std::move(con));
		return -1;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		auto res = con->_session->sql("SELECT email FROM user WHERE name = ?")
			.bind(name)
			.execute();

		auto row = res.fetchOne();
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
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd) {
	auto con = pool_->getConnection();
	try {
		if (con == nullptr) {
			return false;
		}

		auto res = con->_session->sql("UPDATE user SET pwd = ? WHERE name = ?")
			.bind(newpwd, name)
			.execute();

		int affected = res.getAffectedItemsCount();
		std::cout << "Updated rows: " << affected << std::endl;
		pool_->returnConnection(std::move(con));
		return affected > 0;
	}
	catch (const mysqlx::Error& e) {
		pool_->returnConnection(std::move(con));
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::CheckPwd(const std::string& name, const std::string& pwd, UserInfo& userInfo) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("SELECT * FROM user WHERE name = ?")
			.bind(name)
			.execute();

		auto row = res.fetchOne();
		if (!row) {
			return false;
		}

		std::string origin_pwd = row[4].get<std::string>(); // pwd column
		if (pwd != origin_pwd) {
			return false;
		}

		userInfo.name = row[2].get<std::string>(); // name column
		userInfo.email = row[3].get<std::string>(); // email column
		userInfo.uid = row[1].get<int>(); // uid column
		userInfo.pwd = origin_pwd;
		return true;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::AddFriendApply(const int& from, const int& to)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("INSERT INTO friend_apply (from_uid, to_uid) VALUES (?, ?) "
			"ON DUPLICATE KEY UPDATE from_uid = from_uid, to_uid = to_uid")
			.bind(from, to)
			.execute();

		return res.getAffectedItemsCount() > 0;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::AuthFriendApply(const int& from, const int& to) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("UPDATE friend_apply SET status = 1 WHERE from_uid = ? AND to_uid = ?")
			.bind(to, from) // 申请时是from，验证时是to
			.execute();

		return res.getAffectedItemsCount() > 0;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::AddFriend(const int& from, const int& to, std::string back_name) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("INSERT INTO friend_list (self_id, friend_id, back_name) VALUES (?, ?, ?)")
			.bind(from, to, back_name)
			.execute();

		return res.getAffectedItemsCount() > 0;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(int uid)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("SELECT * FROM user WHERE uid = ?")
			.bind(uid)
			.execute();

		auto row = res.fetchOne();
		if (!row) {
			return nullptr;
		}

		auto userInfo = std::make_shared<UserInfo>();
		userInfo->uid = row[1].get<int>();         // uid is in column 1
		userInfo->name = row[2].get<std::string>(); // name is in column 2
		userInfo->pwd = row[4].get<std::string>();  // pwd is in column 4
		userInfo->email = row[3].get<std::string>();// email is in column 3
		return userInfo;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return nullptr;
	}
}

std::shared_ptr<UserInfo> MysqlDao::GetUser(std::string name)
{
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return nullptr;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("SELECT * FROM user WHERE name = ?")
			.bind(name)
			.execute();

		auto row = res.fetchOne();
		if (!row) {
			return nullptr;
		}

		auto userInfo = std::make_shared<UserInfo>();
		userInfo->uid = row[1].get<int>();         // uid is in column 1
		userInfo->name = row[2].get<std::string>(); // name is in column 2
		userInfo->pwd = row[4].get<std::string>();  // pwd is in column 4
		userInfo->email = row[3].get<std::string>();// email is in column 3
		return userInfo;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return nullptr;
	}
}

bool MysqlDao::GetApplyList(int touid, std::vector<std::shared_ptr<ApplyInfo>>& applyList, int offset, int limit) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("SELECT a.*, u.name as from_name FROM friend_apply a "
			"LEFT JOIN user u ON a.from_uid = u.uid "
			"WHERE a.to_uid = ? AND a.status = 0 LIMIT ?, ?")
			.bind(touid, offset, limit)
			.execute();

		while (auto row = res.fetchOne()) {
			auto info = std::make_shared<ApplyInfo>(
				row[1].get<int>(),          // from_uid -> _uid (column 2)
				row[4].get<std::string>(),  // from_name -> _name (from joined user table)
				"",                         // _desc
				"",                         // _icon
				"",                         // _nick
				0,                          // _sex
				row[3].get<int>()           // _status (column 4)
			);
			applyList.push_back(info);
		}
		return true;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}

bool MysqlDao::GetFriendList(int self_id, std::vector<std::shared_ptr<UserInfo>>& user_info_list) {
	auto con = pool_->getConnection();
	if (con == nullptr) {
		return false;
	}

	Defer defer([this, &con]() {
		pool_->returnConnection(std::move(con));
	});

	try {
		auto res = con->_session->sql("SELECT u.*, f.back_name FROM friend_list f "
			"LEFT JOIN user u ON f.friend_id = u.uid "
			"WHERE f.self_id = ?")
			.bind(self_id)
			.execute();

		while (auto row = res.fetchOne()) {
			auto info = std::make_shared<UserInfo>();
			info->uid = row[1].get<int>();              // u.uid (column 2)
			info->name = row[2].get<std::string>();     // u.name (column 3)
			info->email = row[3].get<std::string>();    // u.email (column 4)
			info->pwd = row[4].get<std::string>();      // u.pwd (column 5)
			info->back = row[10].get<std::string>();    // f.back (last column)
			user_info_list.push_back(info);
		}
		return true;
	}
	catch (const mysqlx::Error& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return false;
	}
}
