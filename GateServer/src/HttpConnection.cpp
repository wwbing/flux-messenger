#include "HttpConnection.h"
#include "LogicSystem.h"
HttpConnection::HttpConnection(boost::asio::io_context &ioc)
    : _socket(ioc)
{
}

// 处理新连接的数据接收和请求
void HttpConnection::Start()
{
    // shared_from_this实现伪闭包，避免子线程使用过程中self已经没了
    auto self = shared_from_this();

    // 客户端发起的http的请求（post ：登录、注册、找回密码、发验证码 || get ：get_test、test_procedure）
    http::async_read(
        _socket,
        _buffer,
        _request,
        [self](beast::error_code ec, std::size_t bytes_transferred)
        {
            try
            {
                if (ec)
                {
                    spdlog::error("Http 读取错误: {}", ec.what());
                    return;
                }

                // 读取完成，处理请求
                boost::ignore_unused(bytes_transferred);
                self->HandleReq();
                self->CheckDeadline();
            }
            catch (std::exception &exp)
            {
                spdlog::error("异常 {}", exp.what());
            }
        });
}

// char 转为16进制
unsigned char ToHex(unsigned char x)
{
    return x > 9 ? x + 55 : x + 48;
}

// 16进制转为char
unsigned char FromHex(unsigned char x)
{
    unsigned char y;
    if (x >= 'A' && x <= 'Z')
        y = x - 'A' + 10;
    else if (x >= 'a' && x <= 'z')
        y = x - 'a' + 10;
    else if (x >= '0' && x <= '9')
        y = x - '0';
    else
        assert(0);
    return y;
}

std::string UrlEncode(const std::string &str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        // 判断是否为字母或数字
        if (isalnum((unsigned char)str[i]) ||
            (str[i] == '-') ||
            (str[i] == '_') ||
            (str[i] == '.') ||
            (str[i] == '~'))
            strTemp += str[i];
        else if (str[i] == ' ') // 空格
            strTemp += "+";
        else
        {
            // 其他字符需要加前缀%并将高位和低位分别转为16进制
            strTemp += '%';
            strTemp += ToHex((unsigned char)str[i] >> 4);
            strTemp += ToHex((unsigned char)str[i] & 0x0F);
        }
    }
    return strTemp;
}

std::string UrlDecode(const std::string &str)
{
    std::string strTemp = "";
    size_t length = str.length();
    for (size_t i = 0; i < length; i++)
    {
        // 将+还原为空格
        if (str[i] == '+')
            strTemp += ' ';
        // 处理%后面的两位字符转为char并拼接
        else if (str[i] == '%')
        {
            assert(i + 2 < length);
            unsigned char high = FromHex((unsigned char)str[++i]);
            unsigned char low = FromHex((unsigned char)str[++i]);
            strTemp += high * 16 + low;
        }
        else
            strTemp += str[i];
    }
    return strTemp;
}

void HttpConnection::PreParseGetParam()
{
    // 获取 URI
    auto uri = _request.target();
    // 查找查询字符串的起始位置，即 '?' 的位置
    auto query_pos = uri.find('?');
    if (query_pos == std::string::npos)
    {
        _get_url = uri;
        return;
    }

    _get_url = uri.substr(0, query_pos);
    std::string query_string = uri.substr(query_pos + 1);
    std::string key;
    std::string value;
    size_t pos = 0;
    while ((pos = query_string.find('&')) != std::string::npos)
    {
        auto pair = query_string.substr(0, pos);
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos)
        {
            key = UrlDecode(pair.substr(0, eq_pos)); // 使用 url_decode 解码URL参数
            value = UrlDecode(pair.substr(eq_pos + 1));
            _get_params[key] = value;
        }
        query_string.erase(0, pos + 1);
    }
    // 处理最后一个参数，如果没有 & 分隔
    if (!query_string.empty())
    {
        size_t eq_pos = query_string.find('=');
        if (eq_pos != std::string::npos)
        {
            key = UrlDecode(query_string.substr(0, eq_pos));
            value = UrlDecode(query_string.substr(eq_pos + 1));
            _get_params[key] = value;
        }
    }
}

// 处理http请求
void HttpConnection::HandleReq()
{
    // 设置版本
    _response.version(_request.version());

    // 关闭长连接
    _response.keep_alive(false);

    // 设置允许跨域资源访问，实际生产环境应根据需求设置允许的源
    _response.set(boost::beast::http::field::access_control_allow_origin, "*");

    if (_request.method() == http::verb::get)
    {
        PreParseGetParam();
        bool success = LogicSystem::GetInstance()->HandleGet(_get_url, shared_from_this());
        if (!success)
        {
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }

        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }

    if (_request.method() == http::verb::post)
    {
        bool success = LogicSystem::GetInstance()->HandlePost(_request.target(), shared_from_this());
        if (!success)
        {
            _response.result(http::status::not_found);
            _response.set(http::field::content_type, "text/plain");
            beast::ostream(_response.body()) << "url not found\r\n";
            WriteResponse();
            return;
        }

        _response.result(http::status::ok);
        _response.set(http::field::server, "GateServer");
        WriteResponse();
        return;
    }
}

void HttpConnection::CheckDeadline()
{
    auto self = shared_from_this();

    deadline_.async_wait(
        [self](beast::error_code ec)
        {
            if (!ec)
            {
                // Close socket to cancel any outstanding operation.
                boost::system::error_code tmpec = self->_socket.close(ec);
            }
        });
}

void HttpConnection::WriteResponse()
{
    auto self = shared_from_this();

    _response.content_length(_response.body().size());

    http::async_write(
        _socket,
        _response,
        [self](beast::error_code ec, std::size_t)
        {
            boost::system::error_code tmpec = self->_socket.shutdown(tcp::socket::shutdown_send, ec);
            self->deadline_.cancel();
        });
}
