#include <fstream>
#include <chrono>
#include <cstring>
#include <csignal>
#include <arpa/inet.h>
#include <atomic>

// Глобальный флаг для обработки сигналов
static std::atomic<bool> g_running{true};

// Обработчик сигналов завершения
void signalHandler(int signum)
{
    g_running = false;
}

class SimpleWebServer
{
    std::ofstream logfile_;
    char logBuffer_[100];

public:
    SimpleWebServer(const std::string& filename) : logfile_(filename.c_str(), std::ios::app)
    { }

    void process(uint16_t port, const std::string& content)
    {
        signal(SIGTERM, signalHandler); // kill
        signal(SIGPIPE, SIG_IGN);

        log("ok - start");

        auto server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0)
        {
            log(strerror(errno));
            return;
        }
        
        log("ok - socket");

        int optVal{1};
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optVal, sizeof(optVal)) < 0)
        {
            log(strerror(errno));
            close(server_fd);
            return;
        }
        
        log("ok - setsockopt");

        struct timeval accept_tv = {1, 0};
        if (setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &accept_tv, sizeof(accept_tv)) < 0)
        {
            log(strerror(errno));
            close(server_fd);
            return;
        }
        
        log("ok - setsockopt");

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
        {
            log(strerror(errno));
            close(server_fd);
            return;
        }

        log("ok - bind");

        if (listen(server_fd, 10) < 0)
        {
            log(strerror(errno));
            close(server_fd);
            return;
        }

        log("ok - listen");

        log("Сервер успешно запущен на порту " + std::to_string(port));

        socklen_t addrLen = sizeof(address);
        char buffer[1024];

        while (g_running)
        {
            int clientSocket = accept(server_fd, (struct sockaddr*)&address, &addrLen);
            if (clientSocket > 0)
            {
                struct timeval tv = {5, 0};
                if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0)
                {
                    auto readBytes = read(clientSocket, buffer, sizeof(buffer) - 1);
                    if (readBytes > 0)
                    {
                        if (send(clientSocket, content.c_str(), content.size(), 0) > 0)
                        {
                            // success
                        }
                        else
                            log(strerror(errno));
                    }
                    else
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                            log("Клиент отключен по таймауту");
                        else
                            log(strerror(errno));
                    }
                }
                else
                    log(strerror(errno));                            
                
                close(clientSocket);
            }
            else
            {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    log(strerror(errno));
            }
        }

        log("Завершение работы сервера...");
        close(server_fd);
    }

    void log(const std::string_view message)
    {
        if (logfile_.is_open())
        {
            auto time = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(time);
            
            tm time_tm;
            localtime_r(&time_t, &time_tm);
    
            const char format[] = "%Y-%m-%d %H:%M:%S";
            strftime(logBuffer_, sizeof(logBuffer_), format, &time_tm);
    
            logfile_ << logBuffer_ << ": " << message << '\n';
            logfile_.flush();
        }
    }
};

int main()
{
    std::string content = "<html><body><img src='https://fs.moex.com/f/11840/moex.jpg' width='600'/></body></html>";
    
    std::string response = "HTTP/1.1 200 OK\r\nContent-Length: "
        + std::to_string(content.size())
        + "\r\n\r\n"
        + content;

    SimpleWebServer server("./webserver.log");
    server.process(8080, response);

    return 0;
}