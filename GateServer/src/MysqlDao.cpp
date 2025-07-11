#include "MysqlDao.h"
#include "ConfigMgr.h"

// ################## MySqlPool Implementation ##################

MySqlPool::MySqlPool(const std::string &url, const std::string &user, const std::string &pass, const std::string &schema, int poolSize)
    : url_(url),
      user_(user),
      pass_(pass),
      schema_(schema),
      poolSize_(poolSize),
      b_stop_(false)
{
    try
    {
        for (int i = 0; i < poolSize_; ++i)
        {
            // MySQL X DevAPI format: mysqlx://user:password@host:port/schema
            std::string connection_url = "mysqlx://" + user_ + ":" + pass_ + "@" + url_ + "/" + schema_;
            spdlog::info("尝试建立MySQL X Protocol连接：{}", url_);
            auto session = std::make_shared<mysqlx::Session>(connection_url);

            auto currentTime = std::chrono::system_clock::now().time_since_epoch();
            long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();
            pool_.push(std::make_unique<SqlConnection>(session, timestamp));
            spdlog::info("初始化MySQL连接成功：{}", i);
        }

        _check_thread = std::thread([this]()
                                    {
            while (!b_stop_) {
                checkConnection();
                // Check every 60 seconds
                std::this_thread::sleep_for(std::chrono::seconds(60));
            } });
        _check_thread.detach();
    }
    catch (const mysqlx::Error &e)
    {
        spdlog::error("MySQL连接初始化失败：{}", e.what());
        throw e; // 重新抛出异常，这样上层可以知道初始化失败
    }
}

MySqlPool::~MySqlPool()
{
    Close();
    std::unique_lock<std::mutex> lock(mutex_);
    while (!pool_.empty())
    {
        pool_.pop();
    }
}

void MySqlPool::checkConnection()
{
    std::lock_guard<std::mutex> guard(mutex_);
    int poolsize = pool_.size();
    auto currentTime = std::chrono::system_clock::now().time_since_epoch();
    long long timestamp = std::chrono::duration_cast<std::chrono::seconds>(currentTime).count();

    for (int i = 0; i < poolsize; i++)
    {
        auto con = std::move(pool_.front());
        pool_.pop();

        Defer defer([this, &con]()
                    { pool_.push(std::move(con)); });

        // Only check connections that have been idle for a while
        if (timestamp - con->_last_oper_time < 30)
        {
            continue;
        }

        try
        {
            con->_session->sql("SELECT 1").execute();
            con->_last_oper_time = timestamp;
        }
        catch (const mysqlx::Error &e)
        {
            // Connection is likely broken, try to reconnect
            try
            {
                std::string connection_url = "mysqlx://" + user_ + ":" + pass_ + "@" + url_ + "/" + schema_;
                auto new_session = std::make_shared<mysqlx::Session>(connection_url);
                con->_session = new_session;
                con->_last_oper_time = timestamp;
            }
            catch (const mysqlx::Error &reconnect_e)
            {
                // Reconnect failed, the connection remains broken
            }
        }
    }
}

std::unique_ptr<SqlConnection> MySqlPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);

    // Wait for a connection to be available, with a timeout
    if (!cond_.wait_for(lock, std::chrono::seconds(3), [this]
                        { return !pool_.empty() || b_stop_; }))
    {
        // Timeout occurred
        return nullptr;
    }

    if (b_stop_)
    {
        return nullptr;
    }

    std::unique_ptr<SqlConnection> con(std::move(pool_.front()));
    pool_.pop();
    return con;
}

void MySqlPool::returnConnection(std::unique_ptr<SqlConnection> con)
{
    if (!con)
        return;
    std::unique_lock<std::mutex> lock(mutex_);
    if (b_stop_)
    {
        return;
    }
    pool_.push(std::move(con));
    cond_.notify_one();
}

void MySqlPool::Close()
{
    b_stop_ = true;
    cond_.notify_all();
}

// ################## MysqlDao Implementation ##################

MysqlDao::MysqlDao()
{
	auto & cfg = ConfigMgr::Inst();
	const auto& host = cfg["Mysql"]["Host"];
	const auto& port = cfg["Mysql"]["Port"];
	const auto& pwd = cfg["Mysql"]["Passwd"];
	const auto& schema = cfg["Mysql"]["Schema"];
	const auto& user = cfg["Mysql"]["User"];
	pool_.reset(new MySqlPool(host+":"+port, user, pwd,schema, 5));
}

MysqlDao::~MysqlDao(){
	pool_->Close();
}

int MysqlDao::RegUser(const std::string& name, const std::string& email, const std::string& pwd)
{
    auto con = pool_->getConnection();
    if (!con)
        return -1;

    Defer defer([this, &con]()
                { pool_->returnConnection(std::move(con)); });

    try {
        // 调用注册存储过程
        con->_session->sql("CALL reg_user(?, ?, ?, @result)")
            .bind(name, email, pwd)
            .execute();

        // 获取存储过程的返回值
        mysqlx::SqlResult res = con->_session->sql("SELECT @result AS result").execute();
        mysqlx::Row row = res.fetchOne();

        if (!row)
            return -1;

        return row[0].get<int>();
    }
    catch (const mysqlx::Error &e) {
        std::cerr << "MySQL错误：" << e.what() << std::endl;
        return -1;
    }
}

int MysqlDao::RegUserTransaction(const std::string& name, const std::string& email, const std::string& pwd, const std::string& icon)
{
    auto con = pool_->getConnection();
    if (!con)
        return -1;

    Defer defer([this, &con]()
                { pool_->returnConnection(std::move(con)); });

    try {
        con->_session->startTransaction();

        // 检查email是否存在
        mysqlx::SqlResult res_email = con->_session->sql("SELECT 1 FROM user WHERE email = ?")
            .bind(email)
            .execute();
        
        if (res_email.fetchOne()) {
            con->_session->rollback();
            spdlog::error("邮箱 {} 已存在", email);
            return 0;
        }

        // 检查用户名是否存在
        mysqlx::SqlResult res_name = con->_session->sql("SELECT 1 FROM user WHERE name = ?")
            .bind(name)
            .execute();
        
        if (res_name.fetchOne()) {
            con->_session->rollback();
            spdlog::error("用户名 {} 已存在", name);
            return 0;
        }

        // 更新用户ID
        con->_session->sql("UPDATE user_id SET id = id + 1").execute();

        // 获取新ID
        mysqlx::SqlResult res_uid = con->_session->sql("SELECT id FROM user_id").execute();
        mysqlx::Row row_uid = res_uid.fetchOne();
        
        if (!row_uid) {
            spdlog::error("从 user_id 表获取 id 失败");
            con->_session->rollback();
            return -1;
        }

        int newId = row_uid[0].get<int>();

        // 插入用户信息
        con->_session->sql("INSERT INTO user (uid, name, email, pwd, nick, icon) VALUES (?, ?, ?, ?, ?, ?)")
            .bind(newId, name, email, pwd, name, icon)
            .execute();

        con->_session->commit();
        spdlog::info("新用户信息插入成功");
        return newId;
    }
    catch (const mysqlx::Error &e) {
        spdlog::error("注册用户事务失败：{}", e.what());
        try {
            con->_session->rollback();
        }
        catch (...) {
            // Ignore rollback errors
        }
        return -1;
    }
}

bool MysqlDao::CheckEmail(const std::string& name, const std::string& email)
{
    auto con = pool_->getConnection();
    if (!con)
        return false;

    Defer defer([this, &con]()
                { pool_->returnConnection(std::move(con)); });

    try {
        mysqlx::SqlResult res = con->_session->sql("SELECT email FROM user WHERE name = ?")
            .bind(name)
            .execute();
        
        mysqlx::Row row = res.fetchOne();
        if (!row)
            return false;

        return email == row[0].get<std::string>();
    }
    catch (const mysqlx::Error &e) {
        spdlog::error("检查邮箱失败：{}", e.what());
        return false;
    }
}

bool MysqlDao::UpdatePwd(const std::string& name, const std::string& newpwd)
{
    auto con = pool_->getConnection();
    if (!con)
        return false;

    Defer defer([this, &con]()
                { pool_->returnConnection(std::move(con)); });

    try {
        mysqlx::SqlResult res = con->_session->sql("UPDATE user SET pwd = ? WHERE name = ?")
            .bind(newpwd, name)
            .execute();

        return res.getAffectedItemsCount() > 0;
    }
    catch (const mysqlx::Error &e) {
        spdlog::error("更新密码失败：{}", e.what());
        return false;
    }
}

bool MysqlDao::CheckPwd(const std::string& email, const std::string& pwd, UserInfo& userInfo)
{
    spdlog::info("开始检查密码，邮箱：{}", email);
    
    auto con = pool_->getConnection();
    if (!con) {
        spdlog::error("获取数据库连接失败");
        return false;
    }
    
    spdlog::info("成功获取数据库连接");

    //RAII： defer析构的时候调用这个回调
    Defer defer(
        [this, &con]()
        { 
            spdlog::info("归还数据库连接");
            pool_->returnConnection(std::move(con));
        }
    );

    try {
        spdlog::info("执行SQL查询: SELECT * FROM user WHERE email = {}", email);
        
        mysqlx::SqlResult res = con->_session->sql("SELECT * FROM user WHERE email = ?").bind(email).execute();
        
        mysqlx::Row row = res.fetchOne();
        if (!row) {
            spdlog::error("未找到匹配的用户记录，邮箱：{}", email);
            return false;
        }
        
        spdlog::info("找到用户记录，开始验证密码");

        std::string stored_pwd = row[4].get<std::string>();  // 假设pwd是第4列
        spdlog::info("数据库中存储的密码：{}", stored_pwd);
        spdlog::info("用户提供的密码：{}", pwd);
        
        if (pwd != stored_pwd) {
            spdlog::error("密码验证失败");
            return false;
        }
        
        userInfo.uid = row[1].get<int>();
        userInfo.name = row[2].get<std::string>();
        userInfo.email = row[3].get<std::string>();
        userInfo.pwd = stored_pwd;
        
        return true;
    }
    catch (const mysqlx::Error &e) {
        spdlog::error("检查密码时发生MySQL错误：{}", e.what());
        spdlog::error("错误代码：{}", e.what());
        return false;
    }
    catch (const std::exception &e) {
        spdlog::error("检查密码时发生标准异常：{}", e.what());
        return false;
    }
    catch (...) {
        spdlog::error("检查密码时发生未知异常");
        return false;
    }
}

bool MysqlDao::TestProcedure(const std::string& email, int& uid, std::string& name)
{
    auto con = pool_->getConnection();
    if (!con)
        return false;

    Defer defer([this, &con]()
                { pool_->returnConnection(std::move(con)); });

    try {
        con->_session->sql("CALL test_procedure(?, @userId, @userName)")
            .bind(email)
            .execute();

        mysqlx::SqlResult res = con->_session->sql("SELECT @userId AS uid").execute();
        mysqlx::Row row = res.fetchOne();
        if (!row)
            return false;
        uid = row[0].get<int>();

        res = con->_session->sql("SELECT @userName AS name").execute();
        row = res.fetchOne();
        if (!row)
            return false;
        name = row[0].get<std::string>();

        return true;
    }
    catch (const mysqlx::Error &e) {
        spdlog::error("测试存储过程失败：{}", e.what());
        return false;
    }
}
