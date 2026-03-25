// WebInterface.hpp

#ifndef WEB_SESSION_MANAGER_HPP
#define WEB_SESSION_MANAGER_HPP

#include <Global.h>
#include "cli/runtime/Console.h"
#include "UnixApi.h"
#include "WebConsole.hpp"

#include <json.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <unordered_map>
#include <cstdio>
#include <future>

namespace web
{

struct WebSession
{
    std::promise<bool> promise;
    std::future<bool> future;

    WebSession(cli::CliSession* ses) : future(promise.get_future()), session(ses) {}

    cli::CliSession* session;
};

class WebSessionManager
{
public:
    explicit WebSessionManager(core::Global& g, std::string uds)
        : global(g), api(), udsPath(std::move(uds))
    {
        api.setHandler([this](int fd, const nlohmann::json& msg, const UnixApi&) {
            this->onClientJson(fd, msg);
        });
        api.setOnClose([this](int fd, const UnixApi&) {
            this->onClientClosed(fd);
        });

        api.bindAndListen(udsPath, 64);
    }

    void loop(int timeoutMs)
    {
        while (true)
        {
            api.pollOnce(timeoutMs);

            for (auto& kv : cid2ses)
            {
                const std::string& cid = kv.first;
                cli::CliSession* ses = kv.second;
                if (!ses) continue;

                auto itFd = cid2fd.find(cid);
                if (itFd == cid2fd.end()) continue;

                if (WebConsole* wc = getWebConsole(ses))
                {
                    wc->flushCommands(*ses, cid, false);
                }
            }
        }
    }

private:
    core::Global& global;
    UnixApi api;
    
    std::string udsPath;
    std::string rmBuf;

    std::unordered_map<std::string, cli::CliSession*> cid2ses;
    std::unordered_map<std::string, int> cid2fd;
    std::unordered_map<int, std::string> fd2cid;

    static web::WebConsole* getWebConsole(cli::CliSession* ses)
    {
        if (!ses) return nullptr;
        cli::ConsoleController* ic = &ses->controller;
        return dynamic_cast<web::WebConsole*>(ic);
    }

    void bindCidToFd(const std::string& cid, int fd)
    {
        cid2fd[cid] = fd;
        fd2cid[fd] = cid;
    }

    void unbindFd(int fd)
    {
        auto it = fd2cid.find(fd);
        if (it == fd2cid.end()) return;
        const std::string cid = it->second;
        fd2cid.erase(it);

        auto it2 = cid2fd.find(cid);
        if (it2 != cid2fd.end() && it2->second == fd)
            cid2fd.erase(it2);
    }

    void destroySession(const std::string& cid)
    {
        auto it = cid2ses.find(cid);
        if (it == cid2ses.end()) return;
        if (it->second) delete it->second;
        cid2ses.erase(it);
    }

    void onClientJson(int fd, const nlohmann::json& msg)
    {
        std::cout << msg.dump(4) << std::endl;
        std::string action = msg.value("action", "");
        std::string type = msg.value("type", "");
        std::string cid = msg.value("client_id", "");

        if (cid.empty() || type != "router") return;

        if (action == "subscribe")
        {
            auto it = cid2ses.find(cid);
            if (it == cid2ses.end())
            {
                auto* wc = new WebConsole(api, fd, global);
                auto* ses = global.engine.createSession(wc);
                wc->beginSession();
                cid2ses[cid] = ses;
            }
            else
            {
                if (WebConsole* wc = getWebConsole(it->second))
                {
                    wc->beginSession();
                }
            }

            bindCidToFd(cid, fd);
            api.sendJson(fd, {
                {"type", "subscribe"},
                {"client_id", cid}
            });

            for (const auto& [cid, _] : cid2ses) {
                std::cout << cid << std::endl;
            }
            for (const auto& [cid, fd] : cid2fd) {
                std::cout << cid << " " << fd << std::endl;
            }
            for (const auto& [fd, cid] : fd2cid) {
                std::cout << fd << " " << cid << std::endl;
            }
            std::cout << "\n\n";


            return;
        }

        if (action == "unsubscribe")
        {
            unbindFd(fd);
            destroySession(cid);
            api.sendJson(fd, {
                {"type", "unsubscribe"},
                {"client_id", cid}
            });
            return;
        }

        if (action == "cmd")
        {
            auto it = cid2ses.find(cid);
            if (it == cid2ses.end() || !it->second) return;

            cli::CliSession* ses = it->second;

            const std::string line = msg.value("data", "");
            ses->handleInput(line);

            if (web::WebConsole* wc = getWebConsole(ses))
            {
                wc->flushCommands(*ses, cid, false);
            }
            return;
        }
    }

    void onClientClosed(int fd)
    {
        auto it = fd2cid.find(fd);
        if (it == fd2cid.end()) return;

        std::string cid = it->second;
        unbindFd(fd);

        destroySession(cid);
    }
};

} // namespace web

#endif // WEB_SESSION_MANAGER_HPP

