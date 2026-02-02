#include <stdint.h>
#include <string>
#include <arpa/inet.h>
#include <unistd.h>
#include <csignal>
#include <atomic>

// Глобальный флаг для безопасной остановки из обработчика сигналов
std::atomic<bool> stopFlag{false};

class SyncServer
{
public:    
    ~SyncServer()
    {
        if (serverFileDescriptor_ != -1)
        {
            close(serverFileDescriptor_);
            serverFileDescriptor_ = -1;
        }
    }

    bool init(uint16_t port)
    {
        // Регистрация сигналов
        std::signal(SIGPIPE, SIG_IGN); // клиент разорвал соединение
        std::signal(SIGINT, SyncServer::handleSignal); // Ctrl+C
        std::signal(SIGTERM, SyncServer::handleSignal); // kill
        
        int optVal{1};
        struct timeval accept_tv = {1, 0};
        
        struct sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);
        
        if (serverFileDescriptor_ != -1)
            return false;

        serverFileDescriptor_ = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFileDescriptor_ < 0)
            return false;

        if (setsockopt(serverFileDescriptor_, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal)) < 0)
            goto error;
        
        if (setsockopt(serverFileDescriptor_, SOL_SOCKET, SO_REUSEPORT, &optVal, sizeof(optVal)) < 0)
            goto error;
        
        if (setsockopt(serverFileDescriptor_, SOL_SOCKET, SO_RCVTIMEO, &accept_tv, sizeof(accept_tv)) < 0)
            goto error;

        if (bind(serverFileDescriptor_, (struct sockaddr*)&address, sizeof(address)) < 0)
            goto error;

        if (listen(serverFileDescriptor_, 10) < 0)
            goto error;

        return true;
error:
        close(serverFileDescriptor_);
        serverFileDescriptor_ = -1;

        return false;
    }

    void load(const std::string& content)
    {    
        response_ = "HTTP/1.1 200 OK\r\nContent-Length: "
            + std::to_string(content.size())
            + "\r\n\r\n"
            + content;
    }

    bool start()
    {
        if (serverFileDescriptor_ == -1)
            return false;

        while (!stopFlag.load()) // Проверка флага на каждой итерации
        {
            struct sockaddr_in clientSockAddrIn{};
            socklen_t clientLength = sizeof(clientSockAddrIn);
            
            auto clientFileDescription = accept(serverFileDescriptor_, (struct sockaddr*)&clientSockAddrIn, &clientLength);
            
            if (clientFileDescription < 0)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    continue;

                // Если работа была прервана сигналом, это не ошибка, просто выходим
                else if (errno == EINTR)
                    break;                    

                return false; 
            }

            send(clientFileDescription, response_.c_str(), response_.size(), 0);            
            close(clientFileDescription);
        }
        
        return true;
    }

private:
    int serverFileDescriptor_{-1};
    std::string response_;

    static void handleSignal(int signal)
    {
        if (signal == SIGINT || signal == SIGTERM)
        {
            stopFlag.store(true);
        }
    }
};