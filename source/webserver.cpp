#include <fstream>
#include <sstream>
#include <chrono>
#include <string>
#include <cstring>
#include <csignal>
#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <unistd.h>

class Logger
{
    std::ofstream logfile;
    std::mutex mutex_;

public:
    Logger(const std::string& filename) : logfile(filename, std::ios::app)
    { }

    void log(const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!logfile.is_open())
            return;

        auto time = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(time);
        
        tm time_tm;
        localtime_r(&time_t, &time_tm);

        char buffer[100]{};
        const char format[] = "%Y-%m-%d %H:%M:%S";
        strftime(buffer, sizeof(buffer), format, &time_tm);

        logfile << buffer << ": " << message << std::endl;
        //logfile.flush(); // Сбрасываем буфер, чтобы запись происходила сразу
    }
};

class WebServer
{
public:
    WebServer(uint16_t port, Logger& logger)
        : port_(port),
        logger_(logger),
        server_fd_(-1)
    {
        content_ = "<html><body><img src='https://fs.moex.com/f/11840/moex.jpg' width='600'/></body></html>";
    }

    ~WebServer()
    {
        if (server_fd_ != -1)
            close(server_fd_);
    }

    bool start()
    {
        signal(SIGPIPE, SIG_IGN);

        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0)
        {
            logger_.log("Error: socket failed " + std::string(strerror(errno)));
            return false;
        }

        int optVal{1};
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optVal, sizeof(optVal));

        address_.sin_family = AF_INET;
        address_.sin_addr.s_addr = INADDR_ANY;
        address_.sin_port = htons(port_);

        if (bind(server_fd_, (struct sockaddr*)&address_, sizeof(address_)) < 0)
        {
            logger_.log("Error: bind failed " + std::string(strerror(errno)));
            return false;
        }

        if (listen(server_fd_, 10) < 0)
        {
            logger_.log("Error: listen " + std::string(strerror(errno)));
            return false;
        }

        logger_.log("Сервер успешно запущен на порту " + std::to_string(port_));

        process();

        return true;
    }

private:
    uint16_t port_;
    Logger& logger_;
    int server_fd_;
    struct sockaddr_in address_;
    std::string content_;    

    void process()
    {
        socklen_t addrLen = sizeof(address_);

        while (true)
        {
            int clientSocket = accept(server_fd_, (struct sockaddr*)&address_, &addrLen);
            if (clientSocket < 0)
            {
                logger_.log("Error: accept " + std::string(strerror(errno)));
                continue;
            }
            
            std::thread thread_(&WebServer::handleClient, this, clientSocket);
            thread_.detach();
        }
    }
    
    void handleClient(int clientSocket)
    {
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
        setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
        char buffer[1024] = {0};
        auto readBytes = read(clientSocket, buffer, sizeof(buffer) - 1);
        if (readBytes > 0)
        {
            // --- МАРШРУТИЗАЦИЯ ---
            std::string request(buffer);
            std::stringstream ss(request);
            std::string method, path;
            
            // Первая строка запроса: [METHOD] [PATH] [PROTOCOL]
            ss >> method >> path; 

            logger_.log("Запрос: " + method + " " + path);

            std::string response;
            if (path == "/api/time")
            {
                auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                char* strTime = ctime(&now);
                strTime[strlen(strTime) - 1] = '\0'; // Удаляем \n
                response = makeResponse(200, "application/json", "{\"time\": \"" + std::string(strTime) + "\"}");
            } 
            else
            {
                response = makeResponse(200, "text/html", content_);
                //response = makeResponse(404, "text/html", "<h1>404 Not Found</h1>");
            }

            logger_.log("Запрос: " + std::string(buffer).substr(0, 50) + "..."); // Логируем только начало

            send(clientSocket, response.c_str(), response.length(), 0);
        }
        else if (readBytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                logger_.log("Клиент отключен по таймауту");
            else
                logger_.log("Ошибка: read: " + std::string(strerror(errno)));
        }

        close(clientSocket);
    }    

    std::string makeResponse(int code, const std::string& type, const std::string& body)
    {
        std::string status = (code == 200) ? "200 OK" : "404 Not Found";

        return "HTTP/1.1 " + status + "\r\n"
               "Content-Type: " + type + "\r\n"
               "Content-Length: " + std::to_string(body.length()) + "\r\n"
               "Connection: close\r\n"
               "\r\n" + body;
    }
};

int main()
{
    Logger logger("/var/log/webserver.log");

    WebServer server(8080, logger);

    auto result = server.start();

    return result;
}