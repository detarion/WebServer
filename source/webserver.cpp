#include "SyncServer.h"

int main()
{
    uint16_t port {8080};
    std::string content = "<html><body><h3>Webserver: sync simple</h3><img src='https://fs.moex.com/f/11840/moex.jpg' width='600'/></body></html>";
 
    SyncServer server;

    if (server.init(port))
    {
        server.load(content);
        server.start();
    }

    return 0;
}