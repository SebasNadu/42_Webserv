#pragma once

#include "ConfigValue.hpp"
#include "HttpRequest.hpp"
#include "Server.hpp"
#include <cstring>
#include <map>
#include <poll.h>
#include <string>

extern bool g_shutdown;

class ServerEngine
{
  public:
	// clang-format off
	ServerEngine(std::vector<std::map<std::string, ConfigValue> > const &servers
	);
	// clang-format on
	~ServerEngine();

	void start(void);

	// TODO: move createResponse() to private as only public for testing
	std::string createResponse(const HttpRequest &request);

  private:
	ServerEngine();
	ServerEngine(ServerEngine const &src);
	ServerEngine &operator=(ServerEngine const &src);

	unsigned int		numServers_;
	unsigned int		totalServerInstances_;
	std::vector<pollfd> pollFds_;
	std::vector<Server> servers_;

	void initServer_(
		std::map<std::string, ConfigValue> const &serverConfig,
		size_t									 &serverIndex,
		size_t									 &globalServerIndex
	);
	void initPollFds_(void);
	bool isPollFdServer_(int &fd);
	void handleClient_(size_t &index);
	void acceptConnection_(size_t &index);
	void restartServer_(size_t &index);
	void pollFdError_(size_t &index);

	std::string handleGetRequest(const HttpRequest &request);
	std::string handlePostRequest(const HttpRequest &request);
	std::string handleDeleteRequest(const HttpRequest &request);
	std::string handleNotImplementedRequest();

	std::string createTimestamp();
	std::string readFile(const std::string &filePath);
	static void signalHandler(int signum);
};
