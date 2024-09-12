#pragma once

#include "HttpRequest.hpp"

/* @class RequestParser
 * @brief Parses and validates the syntax of HTTP requests.
 *
 * The RequestParser class is responsible for checking the syntax of the
 * received HTTP request. It ensures that the request adheres to the HTTP/1.1
 * specification in terms of structure and format. However, it does not validate
 * the actual content of the fields. For example, a URI might pass all syntax
 * checks but still not correspond to any valid target on the server. On
 * successful parsing, RequestParser returns an HttpRequest object.
 */
class RequestParser
{
  public:
	static HttpRequest parseRequest(std::string str);

  private:
	RequestParser(void);
	~RequestParser(void);
	RequestParser(const RequestParser &src);
	RequestParser &operator=(const RequestParser &rhs);

	// Start line checks
	static void checkStartLine(
		std::string &startLine,
		std::string *method,
		std::string *uri,
		std::string *httpVersion
	);
	static void checkMethod(std::string &method);
	static void checkUri(std::string &uri);
	static void checkHttpVersion(std::string &httpVersion);

	// Headers checks
	static void
	checkHeaders(const std::multimap<std::string, std::string> &headers);
	static void checkSingleHeader(std::string &headerLine);
	static bool isValidHeaderName(std::string headerName);
	static bool isValidHeaderValue(std::string headerValue);

	// Body checks
	static void checkBody(
		const std::string							  &method,
		const std::multimap<std::string, std::string> &headers,
		const std::vector<char>						  &body
	);

	static const std::string repeatableHeaders[20];
};
