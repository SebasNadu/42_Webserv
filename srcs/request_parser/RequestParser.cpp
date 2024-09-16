#include "request_parser/RequestParser.hpp"
#include "request_parser/BodyParser.hpp"
#include "request_parser/FirstLineParser.hpp"
#include "request_parser/HeaderParser.hpp"
#include "request_parser/HttpHeaders.hpp"
#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>
#include <vector>

HttpRequest RequestParser::parseRequest(std::string str)
{
	std::string								method;
	std::string								httpVersion;
	std::string								uri;
	std::multimap<std::string, std::string> rawHeaders;
	// clang-format off
	std::map<std::string, std::vector<std::string> > headers;
	// clang-format on
	std::vector<char> body;

	// Only create map once, c++98 sucks!
	if (!headerAcceptedChars.empty())
		createHeaderAcceptedChars();

	// Replace all occurrences of "\r\n" with "\n" in the HTTP request
	std::string::size_type pos = 0;
	while ((pos = str.find("\r\n", pos)) != std::string::npos)
	{
		str.replace(pos, 2, "\n");
		pos += 1;
	}

	std::istringstream requestStream(str.c_str());

	// Extract the start line
	std::string startLine;
	std::getline(requestStream, startLine, '\n');
	ParseReqFirstLine::checkStartLine(startLine, &method, &uri, &httpVersion);

	// Extract, parse and normalize headers
	HeaderParser::parseHeaders(requestStream, &headers);

	// Extract and check the body
	BodyParser::parseBody(requestStream, method, headers, &body);

	HttpRequest req(method, httpVersion, uri, headers, body);
	return req;
}

// void RequestParser::
// 	// clang-format off
// 	checkTokenSyntax(std::map<std::string, std::vector<std::string> > headers)
// // clang-format on
// {
// 	// clang-format off
// 	for (std::map<std::string, std::vector<std::string> >::const_iterator it
// 		// clang-format on
// 		= headers.begin();
// 		it != headers.end();
// 		++it)
// 	{
// 		if (headerAcceptedChars.find(it->first) != headerAcceptedChars.end())
// 		{
// 			for (std::vector<char>::iterator vIt = it->second;
// 				 vIt != it->second.end();
// 				 ++vIt)
// 			{
// 			}
// 		}
// 	}
// }
