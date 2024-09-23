#include "../include/HttpRequest.hpp"
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <ostream>
#include <string>

HttpRequest::HttpRequest(
	std::string &method,
	std::string &httpVersion,
	std::string &uri,
	// clang-format off
	std::map<std::string, std::vector<std::string> > &headers,
	// clang-format on
	std::vector<char> &body
)
{
	normalizeRequest_(method, httpVersion, uri, headers, body);

	return;
}

HttpRequest::HttpRequest(const HttpRequest &src)
{
	method_ = src.method_;
	httpVersion_ = src.httpVersion_;
	target_ = src.target_;
	headers_ = src.headers_;
	body_ = src.body_;

	return;
}

HttpRequest::~HttpRequest(void)
{
	return;
}

void HttpRequest::setMethod(std::string &newMethod)
{
	method_ = newMethod;
}

void HttpRequest::setHttpVersion(std::string &newHttpVersion)
{
	httpVersion_ = newHttpVersion;
}

void HttpRequest::setTarget(std::string &newTarget)
{
	target_ = newTarget;
}

void HttpRequest::setUri(std::string &newUri)
{
	uri_ = newUri;
}

void HttpRequest::setHeaders(
	// clang-format off
	std::map<std::string, std::vector<std::string> > &newHeaders
	// clang-format on
)
{
	headers_ = newHeaders;
}

void HttpRequest::setBody(std::vector<char> &newBody)
{
	body_ = newBody;
}

const std::string &HttpRequest::getMethod(void) const
{
	return method_;
}

const std::string &HttpRequest::getHttpVersion(void) const
{
	return httpVersion_;
}

const std::string &HttpRequest::getTarget(void) const
{
	return target_;
}

const std::string &HttpRequest::getUri(void) const
{
	return uri_;
}

const std::string &HttpRequest::getHost(void) const
{
	return host_;
}

const unsigned long &HttpRequest::getPort(void) const
{
	return port_;
}

// clang-format off
const std::map<std::string, std::vector<std::string> > &
// clang-format on
HttpRequest::getHeaders(void) const
{
	return headers_;
}

const std::vector<char> &HttpRequest::getBody(void) const
{
	return body_;
}

std::ostream &operator<<(std::ostream &os, const HttpRequest &rhs)
{
	os << "Method: " << rhs.getMethod() << std::endl;
	os << "HTTP version: " << rhs.getHttpVersion() << std::endl;
	os << "Target: " << rhs.getTarget() << std::endl;
	os << "URI: " << rhs.getUri() << std::endl;
	os << "HEADERS:" << std::endl;
	// clang-format off
	const std::map<std::string, std::vector<std::string> > &headersCpy
		= rhs.getHeaders();
	for (std::map<std::string, std::vector<std::string> >::const_iterator it
		 = headersCpy.begin();
		// clang-format on
		it != headersCpy.end();
		it++)
	{
		if (it->second.size() == 1)
		{
			os << it->first << " : " << it->second[0] << std::endl;
		}
		else
		{
		}
	}
	os << "BODY:" << std::endl;
	const std::vector<char> &bodyCpy = rhs.getBody();
	for (std::vector<char>::const_iterator it = bodyCpy.begin();
		 it != bodyCpy.end();
		 it++)
	{
		os << *it;
	}
	os << std::endl;

	return os;
}

/**
 * @brief Extracts the port number from a given string.
 *
 * @param str Pointer to the string from which to extract the port number.
 * @return The extracted port number, or the default port (80) if no valid
 *         port number is found.
 *
 * @note This function currently extracts the port from the Host header.
 *       In the future, it can be extended to also extract the port from
 *       the URI if needed.
 */
unsigned long HttpRequest::extractPort(std::string *str)
{
	unsigned long port = 80; // default port
	size_t		  colonPos = str->find_last_of(':');
	if (colonPos != std::string::npos)
	{
		std::string tmpPort = host_.substr(colonPos + 1);
		bool		isDigit = true;
		for (size_t i = 0; i < tmpPort.length(); ++i)
		{
			if (!isdigit(tmpPort[i]))
			{
				isDigit = false;
				break;
			}
		}
		if (isDigit && !tmpPort.empty())
		{
			port = atol(tmpPort.c_str());
			*str = str->substr(0, colonPos);
		}
	}
	return port;
}

/**
 * @brief Normalizes the HTTP request by extracting and storing relevant components.
 *
 * This function takes the method, HTTP version, URI, headers, and body of an HTTP
 * request and normalizes them by extracting and storing the relevant components
 * into the HttpRequest object. It also extracts the port number from the Host
 * header using the `extractPort` function.
 *
 * @param method The HTTP method (e.g., GET, POST).
 * @param httpVersion The HTTP version (e.g., HTTP/1.1).
 * @param uri The URI of the request.
 * @param inputHeaders A map of the request headers.
 * @param body The body of the request.
 *
 * @note The port extraction is currently done from the Host header. In the future,
 *       this can be extended to also extract the port from the URI if needed.
 */
void HttpRequest::normalizeRequest_(
	std::string &method,
	std::string &httpVersion,
	std::string &uri,
	// clang-format off
	std::map<std::string, std::vector<std::string> > &inputHeaders,
	// clang-format on
	std::vector<char> &body
)
{
	method_ = method;
	httpVersion_ = httpVersion;
	uri_ = uri;
	host_ = inputHeaders.at("Host")[0];
	port_ = extractPort(&host_);
	target_ = host_ + uri_;
	headers_ = inputHeaders;
	body_ = body;

	// Store body length if present
	// clang-format off
	std::map<std::string, std::vector<std::string> >::const_iterator it
		= headers_.begin();
	// clang-format on
	bodyLength_
		= (it != headers_.end()) ? std::atol(it->second[0].c_str()) : -1;
}
