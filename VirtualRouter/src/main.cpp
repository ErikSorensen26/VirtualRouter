// Problem where enqueue is dequeueing and then eventually it stopes anqueueing and then the queue gets fu

#include <Logger.h>
#include <Global.h>
#include <getopt.h>
#include <iostream>
#include <CrashHandler.hpp>
#include <RCU.hpp>

#include "cli/session/CliEngine.h"
#include "cli/session/CliSession.h"

#ifdef VR_ENABLE_WEB
#include "web/WebSessionManager.hpp"
#endif

#include <VirtualRouter.h>

struct StartupArgs
{
    std::string unixPath;
    int tcpPort = -1;
    bool debug = false;
    bool noDefault = false;
    bool startupFile = false;
    bool routerConfigFile = false;
    bool hwConfigFile = false;
    cli::StartupFiles fs;
};

void printHelp(const char* prog)
{
    std::cout << "Usage: " << prog << " [options]\n\n"
              << "Options:\n"
              << "    -h, --help                Show this help message and exit\n"
              << "    -u, --unix [PATH]         Enable UNIX socket API, bind to [PATH]\n"
              << "    -t, --tcp [PORT]          Enable TCP socket API, listen on [PORT]\n"
              << "    -D, --no-default          Disable startup session (no auto CLI session)\n"
              << "    -c, --config <FILE>       Active writable config file (running config)\n"
              << "    -s, --startup-config <F>  Read-only startup config file (bootstrap only)\n"
              << "    -H, --hw-config <FILE>    Hardware config file (interfaces, bindings)\n"
              <<"     -d, --debug               Enabled debug mode (crash logging)\n"
              << std::endl;
}

bool handleArgs(int& argc, char* argv[], StartupArgs& opts)
{
    const char* shortOpts = "hu:t:Ddc:s:H:";
    const option longOpts[] = {
        {"help",          no_argument,       nullptr, 'h'},
        {"unix",          required_argument, nullptr, 'u'},
        {"tcp",           required_argument, nullptr, 't'},
        {"no-default",    no_argument,       nullptr, 'D'},
        {"config",        required_argument, nullptr, 'c'},
        {"startup-config",required_argument, nullptr, 's'},
        {"hw-config",     required_argument, nullptr, 'H'},
        {"debug",         no_argument,       nullptr, 'd'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, shortOpts, longOpts, nullptr)) != -1)
    {
        switch (opt)
        {
            case 'h':
                printHelp(argv[0]);
                return 0;
            case 'u':
                opts.unixPath = optarg;
                break;
            case 't':
                opts.tcpPort = std::stoi(optarg);
                break;
            case 'D':
                opts.noDefault = true;
                break;
            case 'c':
                opts.fs.routerConfigFile = optarg;
                opts.routerConfigFile = true;
                break;
            case 's':
                opts.fs.startupFile = optarg;
                opts.startupFile = true;
                break;
            case 'H':
                opts.fs.hwConfigFile = optarg;
                opts.hwConfigFile = true;
                break;
            case 'd':
                opts.debug = true;
                break;
            default:
                printHelp(argv[0]);
                return 1;
        }
    }

    std::cout << "Starting router with:\n";
    if (opts.unixPath.empty()) std::cout << "UNIX socket: " << opts.unixPath << "\n";
    //if (tcpPort != -1) std::cout << "Listening on TCP port: " << tcpPort << "\n";
    if (opts.tcpPort != -1) { std::cout << "Unsupported" << std::endl; exit(0); }
    if (!opts.routerConfigFile) std::cout << "Config file: " << opts.fs.routerConfigFile << "\n";
    if (!opts.startupFile) std::cout << "Startup config file: " << opts.fs.startupFile << "\n";
    if (!opts.hwConfigFile) std::cout << "Hardware config file: " << opts.fs.hwConfigFile << "\n";

    return true;
}

// -------------------------------------------------------------------------

int main(int argc, char* argv[])
{
    StartupArgs opts;
    if (!handleArgs(argc, argv, opts)) return 0;


    if (opts.debug) {
        utils::setupCrashLogging();
    }

    utils::Logger::getInstance().initialize(true, /*isolateMode*/false);

    cli::FileSystem fs;
    core::GlobalProperties props(fs);
    props.enableRouting = true;
    props.stfs = opts.fs;
    props.tree = new cli::tree::CommandTree(COMMAND_TREE, COMMAND_TREE_BIN);

    core::Global* global = new core::Global(props);
    cli::CliEngine* engine = global->engine;

    if (!opts.unixPath.empty())
    {
#ifdef VR_ENABLE_WEB
        web::WebSessionManager* webMgr = new web::WebSessionManager(*global, opts.unixPath);
        webMgr->loop(10);
#else
        std::cerr << "This build has no web support: rebuild with -DVR_ENABLE_WEB=ON to use --unix-path.\n";
        return 1;
#endif
    }
    else if (!opts.noDefault)
    {
        utils::RCU::registerThread();
        auto session = engine->createSession(false);
        session->handlePrompt();
        while (true)
        {
            session->handleInput();
        }
        utils::RCU::unregisterThread();
    }
    else
    {
        while (true) {}
    }
    return 0;
}
