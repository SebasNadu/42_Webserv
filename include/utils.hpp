#pragma once

#include <sstream>
#include <string>
#include <vector>
#include <map>

// Fourty-two namespace contains utility functions.
namespace ft
{

template <typename T> std::string toString(T const &value)
{
	std::ostringstream oss;
	oss << value;
	return oss.str();
}

template <> std::string toString<bool>(bool const &value);

std::string &trim(std::string &str, std::string const &chars = "\t\n\v\f\r ");
std::vector<std::string> &split(
	std::vector<std::string> &result,
	std::string const		 &str,
	std::string const		 &delimiters = " \t\n"
);
bool isStrOfDigits(std::string const &str);
// isUShort checks if a string is a valid unsigned short, useful to check the
// port number.
bool isUShort(std::string const &str);
// strToUShort converts a string to an unsigned short..
bool		   strToUShort(std::string const &str, unsigned short &value);
unsigned short strToUShort(std::string const &str);
// isValidIPv4 checks if a string is a valid IPv4 address.
bool		  isValidIPv4(std::string const &str);
unsigned long stringToULong(std::string const &str);

bool isURI(std::string const &str);
bool isURL(std::string const &str);

std::string getDirectory(std::string const &path);
std::map<std::string, std::string> const createMimeTypesMap(void);

std::string readFile(const std::string &filePath);

template <typename C, typename T>
typename C::iterator find(C &container, T const &value)
{
	typename C::iterator it = container.begin();
	typename C::iterator ite = container.end();
	while (it != ite)
	{
		if (*it == value)
			return it;
		++it;
	}
	return it;
}

template <typename Iterator, typename T>
Iterator find(Iterator begin, Iterator end, T const &value)
{
	for (Iterator it = begin; it != end; ++it)
	{
		if (*it == value)
			return it;
	}
	return end;
}

} // namespace ft
