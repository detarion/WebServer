#include <fstream>
#include <chrono>
#include <cstring>
#include <csignal>
#include <arpa/inet.h>
#include <atomic>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <liburing.h>

static std::atomic<bool> g_running{true};

void signalHandler(int)
{
    g_running = false;
}

// Утилита для перевода сокета в неблокирующий режим
int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

class SimpleWebServer
{
    std::ofstream logfile_;
    char logBuffer_[100];

public:
    SimpleWebServer(const std::string& filename) : logfile_(filename, std::ios::app)
    { }

    void process(uint16_t port, const std::string& content)
    {
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);
        signal(SIGPIPE, SIG_IGN);

        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0)
        {
            log(strerror(errno));
            return;
        }

        int optVal = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optVal, sizeof(optVal));
        set_nonblocking(server_fd);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
        {
            log(strerror(errno)); close(server_fd);
            return;
        }

        listen(server_fd, SOMAXCONN);

        // Инициализация epoll
        int epoll_fd = epoll_create1(0);
        epoll_event event{}, events[64];
        event.events = EPOLLIN; // Отслеживаем входящие соединения
        event.data.fd = server_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event);

        log("Сервер epoll запущен на порту " + std::to_string(port));

        char buffer[1024]{};
        
        while (g_running)
        {
            int nfds = epoll_wait(epoll_fd, events, 64, 500); // Таймаут 500мс
            
            for (int i = 0; i < nfds; ++i)
            {
                if (events[i].data.fd == server_fd)
                {
                    // Новое соединение
                    int client_fd = accept(server_fd, nullptr, nullptr);
                    if (client_fd > 0)
                    {
                        set_nonblocking(client_fd);
                        event.events = EPOLLIN | EPOLLET; // Edge-Triggered
                        event.data.fd = client_fd;
                        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
                    }
                }
                else
                {
                    // Данные от клиента
                    int client_fd = events[i].data.fd;
                    int bytes = read(client_fd, buffer, sizeof(buffer));
                    if (bytes > 0)
                    {
                        send(client_fd, content.c_str(), content.size(), 0);
                    }

                    // Закрываем и удаляем из epoll
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr);
                    close(client_fd);
                }
            }
        }

        close(server_fd);
        close(epoll_fd);
        log("Сервер остановлен.");
    }

    void log(const std::string_view message)
    {
        if (!logfile_.is_open())
            return;

        auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        tm tm_time;
        localtime_r(&time, &tm_time);
        strftime(logBuffer_, sizeof(logBuffer_), "%Y-%m-%d %H:%M:%S", &tm_time);
        logfile_ << logBuffer_ << ": " << message << std::endl;
    }
};

// Типы операций для идентификации в Completion Queue
enum { EVENT_ACCEPT, EVENT_READ, EVENT_WRITE };

struct Request
{
    int fd;
    int type;
    char buffer[1024];
};

class IouringWebServer
{
    std::ofstream logfile_;
    struct io_uring ring;

public:
    IouringWebServer(const std::string& filename) : logfile_(filename, std::ios::app)
    {
        io_uring_queue_init(256, &ring, 0);
    }

    ~IouringWebServer()
    {
        io_uring_queue_exit(&ring);
    }

    void process(uint16_t port, const std::string& content)
    {
        signal(SIGINT, signalHandler);
        
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;

        bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
        listen(server_fd, SOMAXCONN);

        socklen_t addr_len = sizeof(addr);
        add_accept(server_fd, &addr, &addr_len);

        log("io_uring сервер запущен на порту " + std::to_string(port));

        while (g_running)
        {
            struct io_uring_cqe* cqe;

            // Ждем хотя бы одно событие с таймаутом
            struct __kernel_timespec ts {0, 500000000}; // 500ms
            int ret = io_uring_wait_cqe_timeout(&ring, &cqe, &ts);
            
            if (ret < 0)
                continue;

            auto* req = (Request*)io_uring_cqe_get_data(cqe);

            if (cqe->res >= 0)
            {
                if (req->type == EVENT_ACCEPT)
                {
                    add_read(cqe->res); // res в данном случае — это новый client_fd
                    add_accept(server_fd, &addr, &addr_len); // Снова слушаем новые подключения
                }
                else if (req->type == EVENT_READ)
                {
                    add_write(req->fd, content);
                }
                else if (req->type == EVENT_WRITE)
                {
                    shutdown(req->fd, SHUT_RDWR);
                    close(req->fd);
                }
            }

            delete req;
            io_uring_cqe_seen(&ring, cqe);
        }
    }

private:
    void add_accept(int server_fd, sockaddr_in* addr, socklen_t* len)
    {
        auto* req = new Request{server_fd, EVENT_ACCEPT, {0}};
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_accept(sqe, server_fd, (struct sockaddr*)addr, len, 0);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring);
    }

    void add_read(int client_fd)
    {
        auto* req = new Request{client_fd, EVENT_READ, {0}};
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_read(sqe, client_fd, req->buffer, sizeof(req->buffer), 0);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring);
    }

    void add_write(int client_fd, const std::string& content)
    {
        auto* req = new Request{client_fd, EVENT_WRITE, {0}};
        struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
        io_uring_prep_send(sqe, client_fd, content.c_str(), content.size(), 0);
        io_uring_sqe_set_data(sqe, req);
        io_uring_submit(&ring);
    }

    void log(const std::string& msg)
    {
        logfile_ << msg << std::endl;
    }
};

int main()
{
    //std::string content = "<html><body><h3>Webserver: single-threaded, async epoll</h3><img src='https://fs.moex.com/f/11840/moex.jpg' width='600'/></body></html>";
    std::string content = "<html><body><h3>Webserver: single-threaded, async io_uring</h3><img src='https://fs.moex.com/f/11840/moex.jpg' width='600'/></body></html>";

    std::string response = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(content.size()) + "\r\nConnection: close\r\n\r\n" + content;

    SimpleWebServer server("./webserver.log");
    server.process(8080, response);
    return 0;
}