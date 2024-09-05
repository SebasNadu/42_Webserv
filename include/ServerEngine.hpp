#pragma once

#include "ConfigValue.hpp"
#include "Server.hpp"

#include <map>
#include <poll.h>
#include <string>
#include <cstring>

class ServerEngine
{
  public:
	ServerEngine();
	ServerEngine(std::vector<std::map<std::string, ConfigValue> > const &servers);
	~ServerEngine();

	void start(void);

  private:
	ServerEngine(ServerEngine const &src);
	ServerEngine &operator=(ServerEngine const &src);

	unsigned int		numServers_;
	std::vector<pollfd> pollFds_;
	std::vector<Server> servers_;

	void initPollFds_(void);
	bool isPollFdServer_(int &fd);
	void handleClient_(size_t &index);
	void acceptConnection_(size_t &index);
};
