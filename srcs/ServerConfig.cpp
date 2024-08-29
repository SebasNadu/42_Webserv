/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ServerConfig.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: sebasnadu <johnavar@student.42berlin.de>   +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2024/08/29 11:17:21 by sebasnadu         #+#    #+#             */
/*   Updated: 2024/08/29 15:04:16 by sebasnadu        ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "ServerConfig.hpp"
#include "ServerException.hpp"

#include <fcntl.h>

ServerConfig::ServerConfig(std::string const& filepath)
	: filepath(filepath), _file(filepath)
{
	if (!this->_file.is_open())
		throw ServerException("Could not open the file [%]", errno, filepath);
}

ServerConfig::ServerConfig(ServerConfig const& src)
	: filepath(src.filepath), _file(src.filepath)
{
	if (!this->_file.is_open())
		throw ServerException("Could not open the file [%]", errno, filepath);
}

ServerConfig& ServerConfig::operator=(ServerConfig const& src)
{
	if (this != &src)
	{
		this->filepath = src.filepath;

		if (this->_file.is_open())
		{
			this->_file.close();
		}

		this->_file.clear();
		this->_file.open(this->filepath);

		if (!this->_file.is_open())
			throw ServerException("Could not open the file [%]", errno, filepath);
	}
	return *this;
}

ServerConfig::~ServerConfig()
{
	if (this->_file.is_open())
		this->_file.close();
}

std::ifstream	&ServerConfig::getFile(void)
{
	return this->_file;
}

#include <iostream>

void	ServerConfig::parseFile(bool isTest, bool isTestPrint)
{
	std::string line;
	while (std::getline(this->_file, line))
	{
		if (isTest && isTestPrint)
			std::cout << line << std::endl;
	}
}
