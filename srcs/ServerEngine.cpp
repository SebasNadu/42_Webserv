#include "ServerEngine.hpp"
#include "HttpRequest.hpp"
#include "HttpResponse.hpp"
#include "Logger.hpp"
#include "macros.hpp"
#include "request_parser/ARequestParser.hpp"
#include "utils.hpp"

#include <algorithm>
#include <csignal>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

extern bool g_shutdown;

ServerEngine::ServerEngine(
	// clang-format off
	std::vector<std::map<std::string, ConfigValue> > const &servers
	// clang-format on
)
	: numServers_(servers.size())
{
	Logger::log(Logger::INFO)
		<< "Initializing the Server Engine with " << this->numServers_
		<< " servers..." << std::endl;
	size_t globalServerIndex(0);
	for (size_t serverIndex = 0; serverIndex < this->numServers_; ++serverIndex)
	{
		globalServerIndex
			+= servers[serverIndex].at("listen").getMapValue("port").size();
	}
	this->servers_.reserve(globalServerIndex);
	globalServerIndex = 0;
	for (size_t serverIndex = 0; serverIndex < this->numServers_; ++serverIndex)
	{
		this->initServer_(servers[serverIndex], serverIndex, globalServerIndex);
	}
	this->totalServerInstances_ = globalServerIndex;
}

ServerEngine::~ServerEngine()
{
	Logger::log(Logger::DEBUG)
		<< "Shutting down the server engine" << std::endl;
	for (size_t i = 0; i < pollFds_.size(); ++i)
	{
		if (!this->isPollFdServer_(pollFds_[i].fd) && pollFds_[i].fd != -1)
		{
			close(pollFds_[i].fd);
			pollFds_[i].fd = -1;
		}
	}
}

std::string const &ServerEngine::getStatusCodeReason(unsigned int statusCode
) const
{
	static std::map<int, std::string> httpStatusCodes;
	if (httpStatusCodes.empty())
	{
		httpStatusCodes[200] = "OK";
		httpStatusCodes[201] = "Created";
		httpStatusCodes[202] = "Accepted";
		httpStatusCodes[204] = "No Content";
		httpStatusCodes[301] = "Moved Permanently";
		httpStatusCodes[302] = "Found";
		httpStatusCodes[303] = "See Other";
		httpStatusCodes[304] = "Not Modified";
		httpStatusCodes[400] = "Bad Request";
		httpStatusCodes[401] = "Unauthorized";
		httpStatusCodes[403] = "Forbidden";
		httpStatusCodes[404] = "Not Found";
		httpStatusCodes[405] = "Method Not Allowed";
		httpStatusCodes[408] = "Request Timeout";
		httpStatusCodes[411] = "Length Required";
		httpStatusCodes[413] = "Payload Too Large";
		httpStatusCodes[414] = "URI Too Long";
		httpStatusCodes[415] = "Unsupported Media Type";
		httpStatusCodes[500] = "Internal Server Error";
		httpStatusCodes[501] = "Not Implemented";
		httpStatusCodes[505] = "HTTP Version Not Supported";
	}
	if (httpStatusCodes.find(statusCode) == httpStatusCodes.end())
		return httpStatusCodes[500];
	return httpStatusCodes[statusCode];
}

void ServerEngine::initServer_(
	std::map<std::string, ConfigValue> const &serverConfig,
	size_t									 &serverIndex,
	size_t									 &globalServerIndex
)
{
	size_t listenSize = serverConfig.at("listen").getMapValue("port").size();
	for (size_t listenIndex = 0; listenIndex < listenSize; ++listenIndex)
	{
		this->servers_.push_back(
			Server(serverConfig, globalServerIndex, listenIndex)
		);
		this->servers_[globalServerIndex].init();
		Logger::log(Logger::INFO)
			<< "Server " << serverIndex + 1 << "[" << listenIndex << "] "
			<< "| " << "Listen: " << this->servers_[globalServerIndex].getIPV4()
			<< ':' << this->servers_[globalServerIndex].getPort() << std::endl;
		++globalServerIndex;
	}
}

/* The serverFd is closed and initialized again, than the pollFds_ vector is
updated with the new file descriptor. */
void ServerEngine::restartServer_(size_t &index)
{
	Logger::log(Logger::INFO)
		<< "Restarting server[" << index << "]" << std::endl;
	this->servers_[index].resetServer();
	pollfd serverPollFd = {servers_[index].getServerFd(), POLLIN, 0};
	pollFds_[index] = serverPollFd;
	Logger::log(Logger::INFO)
		<< "Server[" << index << "] restarted" << std::endl;
}

void ServerEngine::initServerPollFds_(void)
{
	// Initialize pollFds_ vector
	pollFds_.clear();
	pollFds_.reserve(this->totalServerInstances_);

	Logger::log(Logger::DEBUG)
		<< "Initializing pollFds_ vector with ServerFds" << std::endl;

	// Create pollfd struct for the server socket and add it to the vector
	for (size_t i = 0; i < this->totalServerInstances_; ++i)
	{
		pollfd serverPollFd = {servers_[i].getServerFd(), POLLIN, 0};
		pollFds_.push_back(serverPollFd);
	}
}

bool ServerEngine::isPollFdServer_(int &fd)
{
	for (size_t i = 0; i < this->totalServerInstances_; ++i)
	{
		if (fd == this->servers_[i].getServerFd())
			return true;
	}
	return false;
}

void ServerEngine::acceptConnection_(size_t &index)
{
	Logger::log(Logger::DEBUG) << "Accepting client connection on the server["
							   << index << ']' << std::endl;
	sockaddr_in serverAddr = this->servers_[index].getServerAddr();
	int			addrLen = sizeof(serverAddr);
	int			clientFd = accept(
		this->servers_[index].getServerFd(),
		(struct sockaddr *)&serverAddr,
		(socklen_t *)&addrLen
	);
	if (clientFd < 0)
	{
		if (errno != EWOULDBLOCK && errno != EAGAIN)
		{
			Logger::log(Logger::ERROR, true)
				<< "Failed to accept client connection: ("
				<< ft::toString(errno) << ") " << strerror(errno) << std::endl;
		}
		return;
	}
	Logger::log(Logger::DEBUG) << "Client connection accepted" << std::endl;

	// Set the client socket to non-blocking mode
	int flags = fcntl(clientFd, F_GETFL, 0);
	if (flags == -1)
	{
		Logger::log(Logger::ERROR, true)
			<< "Failed to get client socket flags: (" << ft::toString(errno)
			<< ") " << strerror(errno) << std::endl;
		close(clientFd);
		return;
	}
	if (fcntl(clientFd, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		Logger::log(Logger::ERROR, true)
			<< "Failed to set client socket flags: (" << ft::toString(errno)
			<< ") " << strerror(errno) << std::endl;
		close(clientFd);
		return;
	}
	Logger::log(Logger::DEBUG)
		<< "Client socket set to non-blocking mode" << std::endl;

	pollfd clientPollFd = {clientFd, POLLIN, 0};
	pollFds_.push_back(clientPollFd);
	Logger::log(Logger::DEBUG)
		<< "Client connection added to pollFds_[" << index << "]" << std::endl;
}

void ServerEngine::pollFdError_(size_t &index)
{
	std::string error("");
	if (pollFds_[index].revents & POLLERR)
		error += "|POLLERR|";
	if (pollFds_[index].revents & POLLHUP)
		error += "|POLLHUP|";
	if (pollFds_[index].revents & POLLNVAL)
		error += "|POLLNVAL|";

	if (error == "|POLLHUP|")
	{
		Logger::log(Logger::DEBUG)
			<< "Client disconnected on pollFds_[" << index
			<< "]: " << pollFds_[index].fd << std::endl;
	}
	else
	{
		Logger::log(Logger::ERROR, true)
			<< "Descriptor error on pollFds_[" << index
			<< "]: " << pollFds_[index].fd << " : (" << error << ") "
			<< std::endl;
	}
	if (!this->isPollFdServer_(this->pollFds_[index].fd))
	{
		Logger::log(Logger::DEBUG)
			<< "Closing and deleting client socket: pollFds_[" << index
			<< "]: " << pollFds_[index].fd << std::endl;
		close(pollFds_[index].fd);
		this->pollFds_.erase(this->pollFds_.begin() + index);
	}
	else
	{
		restartServer_(index);
	}
}

void ServerEngine::initializePollEvents()
{
	int pollCount = poll(pollFds_.data(), pollFds_.size(), POLL_TIMEOUT);
	if (pollCount == -1)
	{
		Logger::log(Logger::ERROR, true)
			<< "poll() failed: (" << ft::toString(errno) << ") "
			<< strerror(errno) << std::endl;
		return;
	}
	Logger::log(Logger::DEBUG)
		<< "poll() returned " << pollCount << " events" << std::endl;
}

// TODO: parse request in parts; append buffer to previously parsed request
// until full request parsed
void ServerEngine::readClientRequest_(size_t &index)
{
	Logger::log(Logger::INFO)
		<< "Reading client request at pollFds_[" << index << ']' << std::endl;

	this->bytesRead_ = read(
		this->pollFds_[index].fd, this->clientRequestBuffer_, BUFFER_SIZE
	);

	if (this->bytesRead_ < 0)
	{
		if (errno != EWOULDBLOCK && errno != EAGAIN)
		{
			Logger::log(Logger::ERROR, true)
				<< "Failed to read from client: (" << ft::toString(errno)
				<< ") " << strerror(errno) << std::endl;
		}
		return;
	}
	else if (this->bytesRead_ == 0)
	{
		// Client disconnected
		Logger::log(Logger::DEBUG)
			<< "Client disconnected: pollFds_[" << index << "]"
			<< ", closing socket and deleting it from pollFds_" << std::endl;
		close(pollFds_[index].fd);
		pollFds_.erase(pollFds_.begin() + index);
		return;
	}

	// After reading the request, prepare to send a response
	pollFds_[index].events = POLLOUT;

	Logger::log(Logger::DEBUG)
		<< "Read " << this->bytesRead_ << " bytes" << std::endl;
}

// TODO: The client Request Buffer should be for each client connection
// and not a global buffer, bytesRead_ is also not optimal
void ServerEngine::sendClientResponse_(size_t &index)
{
	std::string	 response;
	HttpRequest *request = NULL;
	try
	{
		std::string requestStr(this->clientRequestBuffer_, this->bytesRead_);
		request = new HttpRequest(ARequestParser::parseRequest(requestStr));
		Logger::log(Logger::DEBUG) << "Request received:\n\nBuffer:\n"
								   << requestStr << "Request:\n"
								   << *request << std::flush;
	}
	catch (std::exception &e)
	{
		response = handleDefaultErrorResponse_(400, true);
		Logger::log(Logger::ERROR, true)
			<< "Failed to parse the reguest: " << e.what() << std::endl;
	}
	if (request != NULL)
	{
		response = createResponse(*request);
		Logger::log(Logger::DEBUG) << "Sending response" << std::endl;
	}

	int retCode
		= send(pollFds_[index].fd, response.c_str(), response.size(), 0);
	if (retCode < 0)
	{
		Logger::log(Logger::ERROR, true)
			<< "Failed to send response to client: (" << ft::toString(errno)
			<< ") " << strerror(errno) << std::endl;
	}
	else if (retCode == 0)
	{
		Logger::log(Logger::DEBUG)
			<< "Client disconnected pollFds_[" << index
			<< "], closing socket and deleting it from pollFds_" << std::endl;
	}
	if (retCode <= 0)
	{
		close(pollFds_[index].fd);
		pollFds_.erase(pollFds_.begin() + index);
		delete request;
		return;
	}
	// After sending the response, prepare to read the next request
	pollFds_[index].events = POLLIN;

	delete request;
}

void ServerEngine::processPollEvents()
{
	for (size_t i = 0; i < pollFds_.size(); ++i)
	{
		// Check if fd has some errors(pollerr, pollnval) or if the
		// connection was broken(pollhup)
		if (pollFds_[i].revents & (POLLERR | POLLHUP | POLLNVAL))
			pollFdError_(i);
		else if (pollFds_[i].revents & POLLIN)
		{
			Logger::log(Logger::DEBUG)
				<< "pollFds_[" << i << "] is ready for read" << std::endl;
			// If fd is a server socket accept a new client connection
			if (this->isPollFdServer_(pollFds_[i].fd))
				acceptConnection_(i);
			else
				readClientRequest_(i);
		}
		else if (pollFds_[i].revents & POLLOUT)
			sendClientResponse_(i);
	}
}

void ServerEngine::start()
{
	Logger::log(Logger::INFO) << "Starting the Server Engine" << std::endl;
	this->initServerPollFds_();

	while (!g_shutdown)
	{
		initializePollEvents();
		processPollEvents();
	}
}

std::string ServerEngine::createResponse(const HttpRequest &request)
{
	int serverIndex = this->findServer_(request.getHost(), request.getPort());
	if (serverIndex == -1)
		return this->handleDefaultErrorResponse_(404, true);
	if (request.getMethod() == "GET")
	{
		Logger::log(Logger::DEBUG) << "Handling GET" << std::endl;
		return handleGetRequest_(request, this->servers_[serverIndex]);
	}
	else if (request.getMethod() == "POST")
	{
		Logger::log(Logger::DEBUG) << "Handling POST" << std::endl;
		return handlePostRequest_(request, this->servers_[serverIndex]);
	}
	else if (request.getMethod() == "DELETE")
	{
		Logger::log(Logger::DEBUG) << "Handling DELETE" << std::endl;
		return handleDeleteRequest_(request, this->servers_[serverIndex]);
	}
	else
		return this->handleDefaultErrorResponse_(501, true);
}

int ServerEngine::findServer_(
	std::string const	 &host,
	unsigned short const &port
)
{
	for (size_t i = 0; i < this->totalServerInstances_; ++i)
	{
		std::vector<std::string> const &serverNames
			= this->servers_[i].getServerName();
		for (size_t j = 0; j < serverNames.size(); ++j)
		{
			if (serverNames[j] == host && this->servers_[i].getPort() == port)
				return i;
		}
	}
	return -1;
}

// clang-format off
std::string ServerEngine::handleRedirection_(
	std::map<std::string, std::vector<std::string> > const &location,
	bool												   &keepAlive
)
{
	std::map<std::string, std::vector<std::string> >::const_iterator it
		= location.find("return");
	if (it != location.end() && !it->second.empty())
		return this->handleReturnDirective_(it->second, keepAlive);
	return "";
}
// clang-format on

// clang-format off
bool ServerEngine::validateMethod_(
	const std::map<std::string, std::vector<std::string> > &location,
	const std::string									  &method
)
{
	std::map<std::string, std::vector<std::string> >::const_iterator it
		= location.find("limit_except");
	if (it != location.end() && !it->second.empty())
	{
		if (std::find(it->second.begin(), it->second.end(), method)
			== it->second.end())
			return false;
	}
	return true;
}
// clang-format on

// clang-format off
bool ServerEngine::validateBodySize_(
	const std::map<std::string, std::vector<std::string> > &location,
	const HttpRequest									  &request,
	const Server										  &server
)
{
	std::map<std::string, std::vector<std::string> >::const_iterator it
		= location.find("client_body_size");
	if (it != location.end() && !it->second.empty())
	{
		if (request.getBody().size() > ft::stringToULong(it->second[0]))
			return false;
	}
	else if (request.getBody().size() > server.getClientMaxBodySize())
		return false;
	return true;
}

std::string ServerEngine::getFilePath_(
	const std::string									  &uri,
	const std::map<std::string, std::vector<std::string> > &location,
	const Server										  &server
)
{
	std::string rootdir = location.find("root") != location.end()
								  && !location.at("root").empty()
							  ? location.at("root")[0]
							  : server.getRoot();
	return rootdir + uri;
}

bool ServerEngine::isCgiRequest_(
	const std::map<std::string, std::vector<std::string> > &location,
	const std::string									  &uri
)
{
	std::map<std::string, std::vector<std::string> >::const_iterator it
		= location.find("cgi");
	if (it != location.end() && !it->second.empty())
	{
		std::string cgiExtension = it->second[0]; // ".py"
		return uri.find(cgiExtension) != std::string::npos;
	}
	return false;
}

std::string ServerEngine::getCgiInterpreter_(
	const std::map<std::string, std::vector<std::string> > &location
)
{
	return location.find("cgi")->second[1]; // "/usr/bin/python3"
}

bool ServerEngine::isAutoIndexEnabled_(
	const std::map<std::string, std::vector<std::string> > &location
)
{
	return location.find("autoindex") != location.end()
		   && location.at("autoindex")[0] == "on";
}

std::string ServerEngine::findIndexFile_(
	const std::string									  &filepath,
	const std::map<std::string, std::vector<std::string> > &location,
	const Server										  &server
)
{
	std::vector<std::string> indexFiles
		= location.find("index") != location.end()
				  && !location.at("index").empty()
			  ? location.at("index")
			  : server.getIndex();

	struct stat fileStat;
	for (std::vector<std::string>::const_iterator it = indexFiles.begin();
		 it != indexFiles.end();
		 ++it)
	{
		std::string indexFilePath = filepath + "/" + *it;
		if (stat(indexFilePath.c_str(), &fileStat) == 0
			&& S_ISREG(fileStat.st_mode))
		{
			return indexFilePath;
		}
	}
	return filepath;
}

bool ServerEngine::isDirectory_(std::string const &filepath)
{
	struct stat fileStat;
	if (stat(filepath.c_str(), &fileStat) == 0)
		return S_ISDIR(fileStat.st_mode);
	return false;
}

std::string ServerEngine::getRootDir_(
	const std::map<std::string, std::vector<std::string> > &location,
	const Server										  &server
)
{
	std::string rootdir = location.find("root") != location.end()
								  && !location.at("root").empty()
							  ? location.at("root")[0]
							  : server.getRoot();
	return rootdir;
}
// clang-format on

std::string ServerEngine::createFileResponse_(
	std::string const &filepath,
	std::string const &rootdir,
	Server const	  &server,
	bool			  &keepAlive
)
{
	HttpResponse  response;
	std::ifstream file(filepath);

	if (file.is_open())
	{
		Logger::log(Logger::DEBUG) << "Handling GET: file opened" << std::endl;

		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string body = buffer.str();

		response.setStatusCode(200);
		response.setReasonPhrase("OK");
		response.setHeader("Server", SERVER_NAME);
		response.setHeader("Date", createTimestamp_());
		response.setHeader("Content-Type", getMimeType(filepath));
		response.setHeader("Content-Length", std::to_string(body.size()));
		if (keepAlive)
			response.setHeader("Connection", "keep-alive");
		else
			response.setHeader("Connection", "close");
		response.setBody(body);
	}
	else
	{
		Logger::log(Logger::DEBUG)
			<< "Handling GET: file not found" << std::endl;
		std::string errorResponse
			= handleServerErrorResponse_(server, 404, rootdir, keepAlive);
		if (!errorResponse.empty())
			return errorResponse;
		else
			return this->handleDefaultErrorResponse_(404);
	}

	Logger::log(Logger::DEBUG) << "Handling GET: responding" << std::endl;
	return response.toString();
}

// clang-format off
std::string ServerEngine::handleGetRequest_(
	const HttpRequest &request,
	Server const	  &server
)
{
	std::string uri = request.getUri();
	bool keepAlive = request.getKeepAlive();
	std::map<std::string, std::vector<std::string> > location;

	if (server.isThisLocation(uri))
		location = server.getThisLocation(uri);
	else
		return this->handleDefaultErrorResponse_(404, !keepAlive);

	std::map<std::string, std::vector<std::string> >::const_iterator it;

	// Check for redirections
	std::string redirection = handleRedirection_(location, keepAlive);
	if (!redirection.empty())
		return redirection;

	// Check for authorized methods
	if (!validateMethod_(location, "GET"))
		return this->handleDefaultErrorResponse_(405, !keepAlive);

	// Check for max body size
	if (!validateBodySize_(location, request, server))
		return this->handleDefaultErrorResponse_(413, !keepAlive);

	std::string filepath = getFilePath_(uri, location, server);
	std::string rootdir = getRootDir_(location, server);

	if (isCgiRequest_(location, uri))
		return handleCgiRequest_(
			filepath, getCgiInterpreter_(location), request, keepAlive
		);

	// Check if the request is for a directory and handle autoindex
	if (isDirectory_(filepath))
	{
		if (isAutoIndexEnabled_(location))
			return handleAutoIndex_(rootdir, uri, keepAlive);
		// Search for index file in the directory
		filepath = findIndexFile_(filepath, location, server);
	}

	// Check if the file exists and return the response
	return createFileResponse_(filepath, rootdir, server, keepAlive);
}
// clang-format on

// TODO: implement check for file/directory, coordinate with Seba
std::string ServerEngine::handleDeleteRequest_(
	const HttpRequest &request,
	Server const	  &server
)
{
	(void)request;
	(void)server;
	std::cout << request << std::endl;

	// Get root path from config of server
	std::string rootdir = servers_[0].getRoot();
	// Combine root path with uri from request
	std::string filepath = rootdir + request.getUri();

	Logger::log(Logger::DEBUG) << "Filepath: " << filepath << std::endl;

	HttpResponse response;

	if (remove(filepath.c_str()) == 0)
	{
		std::string body
			= "<!DOCTYPE html>\n<html>\n<head><title>200 OK</title></head>\n"
			  "<body><h1>File deleted.</h1></body>\n</html>\n";
		response.setStatusCode(200);
		response.setReasonPhrase("OK");
		response.setHeader("Server", SERVER_NAME);
		response.setHeader("Date", createTimestamp_());
		response.setHeader("Content-Type", "text/html; charset=UTF-8");
		response.setHeader("Content-Length", std::to_string(body.size()));
		response.setHeader("Connection", "keep-alive");
		response.setBody(body);
		Logger::log(Logger::INFO)
			<< "DELETE " << request.getUri() << " -> 200 OK" << std::endl;
	}
	else
	{
		std::string body = ft::readFile(rootdir + "/404.html");
		response.setStatusCode(404);
		response.setReasonPhrase("Not Found");
		response.setHeader("Server", SERVER_NAME);
		response.setHeader("Date", createTimestamp_());
		response.setHeader("Content-Type", "text/html; charset=UTF-8");
		response.setHeader("Content-Length", std::to_string(body.size()));
		response.setHeader("Connection", "keep-alive");
		response.setBody(body);
		Logger::log(Logger::INFO) << "DELETE " << request.getUri()
								  << " -> 404 Not Found" << std::endl;
	}

	Logger::log(Logger::DEBUG) << "Handling DELETE: responding" << std::endl;
	return response.toString();
}

// TODO: implement
std::string ServerEngine::handlePostRequest_(
	const HttpRequest &request,
	Server const	  &server
)
{
	(void)request;
	(void)server;
	Logger::log(Logger::DEBUG) << "Handling POST: responding" << std::endl;
	return "POST test\n";
}

std::string ServerEngine::createTimestamp_()
{
	time_t	   now = time(0);
	struct tm *tstruct = localtime(&now);
	if (tstruct == nullptr)
	{
		throw std::runtime_error("Failed to get local time");
	}

	char buf[80];
	strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", tstruct);
	return std::string(buf);
}

std::string ServerEngine::handleServerErrorResponse_(
	Server const	  &server,
	int				   statusCode,
	std::string const &rootdir,
	bool			  &keepAlive
)
{
	HttpResponse response;
	std::string	 errorURI;

	if (server.getErrorPageValue(statusCode, errorURI))
	{
		response.setStatusCode(statusCode);
		response.setReasonPhrase(getStatusCodeReason(statusCode));
		response.setHeader("Server", SERVER_NAME);
		response.setHeader("Date", createTimestamp_());
		response.setHeader("Content-Type", getMimeType(errorURI));

		std::string body = ft::readFile(rootdir + errorURI);

		response.setHeader("Content-Length", std::to_string(body.size()));
		if (keepAlive)
			response.setHeader("Connection", "keep-alive");
		else
			response.setHeader("Connection", "close");
		response.setBody(body);

		return response.toString();
	}
	return "";
}

std::string
ServerEngine::handleDefaultErrorResponse_(int statusCode, bool closeConnection)
{
	Logger::log(Logger::DEBUG)
		<< "Handling default error response: [" << statusCode << "] "
		<< getStatusCodeReason(statusCode) << std::endl;

	HttpResponse response;
	response.setStatusCode(statusCode);
	response.setReasonPhrase(getStatusCodeReason(statusCode));
	response.setHeader("Server", SERVER_NAME);
	response.setHeader("Date", createTimestamp_());
	response.setHeader("Content-Type", "text/html; charset=UTF-8");

	std::string body
		= ft::readFile("./www/" + std::to_string(statusCode) + ".html");
	if (body.empty())
	{
		if (statusCode >= 400 && statusCode < 500)
			body = ft::readFile("./www/4xx.html");
		else if (statusCode >= 500 && statusCode < 600)
			body = ft::readFile("./www/5xx.html");
		else if (statusCode >= 300 && statusCode < 400)
			body = ft::readFile("./www/3xx.html");
	}
	if (body.empty())
		body = "<!DOCTYPE html>\n<html>\n<head><title>"
			   + std::to_string(statusCode) + " "
			   + getStatusCodeReason(statusCode) + "</title></head>\n<body><h1>"
			   + std::to_string(statusCode) + " "
			   + getStatusCodeReason(statusCode) + "</h1></body>\n</html>\n";

	response.setHeader("Content-Length", std::to_string(body.size()));
	if (closeConnection)
		response.setHeader("Connection", "close");
	response.setBody(body);

	return response.toString();
}

std::string ServerEngine::handleReturnDirective_(
	std::vector<std::string> const &returnDirective,
	bool						   &keepAlive
)
{
	HttpResponse response;
	int			 statusCode = 301;

	if (returnDirective.size() == 2)
	{
		statusCode = std::atoi(returnDirective[0].c_str());
		response.setHeader("Location", returnDirective[1]);
	}
	else if (returnDirective.size() == 1)
	{
		response.setHeader("Location", returnDirective[0]);
	}

	Logger::log(Logger::DEBUG)
		<< "Handling return directive: [" << statusCode << "] "
		<< getStatusCodeReason(statusCode)
		<< "To: " << response.getHeader("Location") << std::endl;

	response.setStatusCode(statusCode);
	response.setReasonPhrase(getStatusCodeReason(statusCode));
	response.setHeader("Server", SERVER_NAME);
	response.setHeader("Date", createTimestamp_());
	response.setHeader("Content-Type", "text/html; charset=UTF-8");
	response.setHeader("Content-Length", "0");
	if (keepAlive)
		response.setHeader("Connection", "keep-alive");
	else
		response.setHeader("Connection", "close");

	return response.toString();
}

std::string ServerEngine::generateAutoIndexPage_(
	// Set root, index and file path
	std::string const &root,
	std::string const &uri
)
{
	std::stringstream html;

	html << "<!DOCTYPE html>\n<html>\n<head><title>Index of " << uri
		 << "</title></head>\n<body><h1>Index of " << uri << "</h1>\n";
	html << "<ul>\n";

	if (uri != "/")
		html << "<li><a href=\"" << uri + "../\">Parent Directory</a></li>\n";

	DIR *dir = opendir((root + uri).c_str());
	if (dir == NULL)
	{
		Logger::log(Logger::ERROR, true)
			<< "Failed to open directory: " << root + uri << std::endl;
		return handleDefaultErrorResponse_(404, true);
	}

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL)
	{
		std::string filename(entry->d_name);

		// Skip "." and ".." directories
		if (filename == "." || filename == "..")
			continue;

		std::string fullPath = root + uri + "/" + filename;

		// Check if it is a directory
		struct stat fileStat;
		if (stat(fullPath.c_str(), &fileStat) == -1)
		{
			Logger::log(Logger::ERROR, true)
				<< "Failed to get file stats: " << fullPath << "Error: ["
				<< errno << "] " << strerror(errno) << std::endl;
			continue;
		}
		else if (S_ISDIR(fileStat.st_mode))
			filename += "/";

		// Generate links for each file/directory
		html << "<li><a href=\"" << uri << filename << "\">" << filename
			 << "</a></li>\n";
	}
	closedir(dir);
	html << "</ul>\n</body>\n</html>\n";
	return html.str();
}

std::string ServerEngine::handleAutoIndex_(
	std::string const &root,
	std::string const &uri,
	bool			  &keepAlive
)
{
	Logger::log(Logger::DEBUG)
		<< "Handling auto index on: " << root << uri << std::endl;

	std::string body = generateAutoIndexPage_(root, uri);

	HttpResponse response;
	response.setStatusCode(200);
	response.setReasonPhrase("OK");
	response.setHeader("Server", SERVER_NAME);
	response.setHeader("Date", createTimestamp_());
	response.setHeader("Content-Type", "text/html; charset=UTF-8");
	response.setHeader("Content-Length", ft::toString(body.size()));
	if (keepAlive)
		response.setHeader("Connection", "keep-alive");
	else
		response.setHeader("Connection", "close");
	response.setBody(body);
	return response.toString();
}

std::string ServerEngine::handleCgiRequest_(
	std::string const &filepath,
	std::string const &interpreter,
	HttpRequest const &request,
	bool			  &keepAlive
)
{
	int pipefd[2];
	if (pipe(pipefd) == -1)
	{
		Logger::log(Logger::ERROR, true) << "Pipe creation failed" << std::endl;
		return this->handleDefaultErrorResponse_(500);
	}

	pid_t pid = fork();
	if (pid == -1)
	{
		Logger::log(Logger::ERROR, true) << "Fork failed" << std::endl;
		return this->handleDefaultErrorResponse_(500);
	}
	else if (pid == 0)
	{
		close(pipefd[0]);

		std::string cgiDir = ft::getDirectory(filepath);
		if (chdir(cgiDir.c_str()) == -1)
		{
			Logger::log(Logger::ERROR, true)
				<< "Failed to change directory to: " << cgiDir << std::endl;
			exit(EXIT_FAILURE);
		}

		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);

		std::vector<std::string> envVariables;
		envVariables.push_back("GATEWAY_INTERFACE=CGI/1.1");
		envVariables.push_back("SERVER_PROTOCOL=HTTP/1.1");
		envVariables.push_back("REQUEST_METHOD=" + request.getMethod());
		envVariables.push_back("SCRIPT_FILENAME=" + filepath);
		envVariables.push_back("PATH_INFO=" + request.getUri());

		std::vector<char *> envp;
		for (std::vector<std::string>::iterator it = envVariables.begin();
			 it != envVariables.end();
			 ++it)
		{
			envp.push_back(const_cast<char *>(it->c_str()));
		}
		envp.push_back(NULL);

		char *argv[]
			= {const_cast<char *>(interpreter.c_str()),
			   const_cast<char *>(filepath.c_str()),
			   NULL};

		execve(interpreter.c_str(), argv, &envp[0]);

		exit(EXIT_FAILURE);
	}
	else
	{
		close(pipefd[1]);

		char			  buffer[1024];
		std::stringstream output;
		ssize_t			  bytesRead;

		// TODO: ask Manu how to handle chunked requests
		// if (request.isChunked())
		// {
		//     while ()
		//     {
		//     }
		// }

		while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer))) > 0)
			output.write(buffer, bytesRead);

		close(pipefd[0]);

		int status;
		waitpid(pid, &status, 0);

		if (status != 0)
		{
			Logger::log(Logger::ERROR, true)
				<< "CGI script execution failed" << std::endl;
			return this->handleDefaultErrorResponse_(500);
		}

		HttpResponse response;
		response.setStatusCode(200);
		response.setReasonPhrase("OK");
		response.setHeader("Server", SERVER_NAME);
		response.setHeader("Date", createTimestamp_());
		response.setHeader("Content-Type", "text/html; charset=UTF-8");
		response.setHeader("Content-Length", ft::toString(output.str().size()));
		if (keepAlive)
			response.setHeader("Connection", "keep-alive");
		else
			response.setHeader("Connection", "close");
		response.setBody(output.str());

		return response.toString();
	}
}

std::string ServerEngine::getMimeType(std::string const &filePath) const
{
	static std::map<std::string, std::string> const mimeTypes
		= ft::createMimeTypesMap();

	size_t dotPos = filePath.rfind('.');
	if (dotPos != std::string::npos)
	{
		std::string extension = filePath.substr(dotPos);
		std::map<std::string, std::string>::const_iterator it
			= mimeTypes.find(extension);
		if (it != mimeTypes.end())
		{
			return it->second;
		}
	}

	return "application/octet-stream";
}
