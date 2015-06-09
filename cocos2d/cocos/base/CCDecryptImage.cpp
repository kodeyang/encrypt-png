#include "CCDecryptImage.h"

#include <sstream>
#include "CCAES.h"
#include "ccMacros.h"

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WIN32)
#include <WinSock.h>
#pragma comment(lib, "ws2_32.lib")
#elif (CC_TARGET_PLATFORM == CC_PLATFORM_IOS || CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include <netinet/in.h>
#endif

namespace cocos2d
{
	/* CRC码长度 */
	static const uint32_t CRC_SIZE = 4;

	/* 文件头部 */
	static const unsigned char HEAD_DATA[] = { 0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a };

	/* IEND CRC码 */
	static const unsigned char IEND_DATA[] = { 0xae, 0x42, 0x60, 0x82 };

	/* 数据块头部（用于验证解密是否成功） */
	static const unsigned char BLOCK_HEAD[] = { 0x45, 0x6e, 0x63, 0x72, 0x79, 0x70, 0x74, 0x50, 0x4e, 0x47 };

	/* 默认密钥 */
	static const aes_key DEAULT_KEY = { 0x31, 0x32, 0x33, 0x34, 0x35, 0x36 };

#pragma pack(push, 1)

	struct Block
	{
		char name[4];
		uint32_t pos;
		uint32_t size;
	};

	struct IHDRBlock
	{
		Block block;
		char data[13 + CRC_SIZE];
	};

#pragma pack(pop)

	std::array<std::string, 2> splitext(const std::string &file_path)
	{
		std::string::size_type pos = file_path.rfind('.');
		std::array<std::string, 2> text;
		if (std::string::npos != pos)
		{
			text[1] = file_path.substr(pos);
			text[0] = file_path.substr(0, pos);
		}
		else
		{
			text[0] = file_path;
		}
		return text;
	}

	template <int _Value, typename _Stream>
	std::array<char, _Value> ReadSome(_Stream &stream)
	{
		std::array<char, _Value> buffer;
		for (unsigned int i = 0; i < _Value; ++i) buffer[i] = stream.get();
		return buffer;
	}

	void DecryptBlock(std::stringstream &ss, const aes_key &key)
	{
		const std::streamoff contents_size = ss.tellp() - ss.tellg();
		const uint32_t block_size = (uint32_t)(contents_size + AES_BLOCK_SIZE - contents_size % AES_BLOCK_SIZE);
		std::vector<uint8_t> buffer;
		buffer.resize(block_size);
		for (uint32_t i = 0; i < contents_size; ++i) buffer[i] = ss.get();
		AES::DecryptData(&buffer[0], block_size, key);
		ss.seekg(0); ss.seekp(0);
		for (uint32_t i = 0; i < block_size; ++i) ss.put(buffer[i]);
	}

	std::vector<unsigned char> DecryptImage(const std::string &filename, Data &data)
	{
		CCAssert(!data.isNull(), "data is null!");

		// 获取数据块信息位置
		const uint32_t block_start_pos = ntohl(*reinterpret_cast<uint32_t *>(data.getBytes() + data.getSize() - sizeof(uint32_t)));

		// 获取数据块信息
		std::stringstream block_info;
		for (uint32_t i = block_start_pos; i < data.getSize() - sizeof(uint32_t); ++i)
		{
			block_info.put(*(data.getBytes() + i));
		}

		// 解密数据块信息
		DecryptBlock(block_info, DEAULT_KEY);

		// 验证数据块信息是否解密成功
		auto block_head = ReadSome<sizeof(BLOCK_HEAD)>(block_info);
		for (unsigned int i = 0; i < block_head.size(); ++i)
		{
			if (block_head[i] != BLOCK_HEAD[i])
			{
				CCAssert(false, "the key is wrong!");
			}
		}

		// 写入文件头信息
		std::vector<unsigned char> image_data;
		image_data.reserve(data.getSize());
		for (auto ch : HEAD_DATA) image_data.push_back(ch);

		// 写入数据块信息
		while (true)
		{
			Block block;
			memcpy(&block, &(ReadSome<sizeof(Block)>(block_info)[0]), sizeof(Block));
			if (block_info.eof())
			{
				CCAssert(false, "");
				CCLOG("the %s file format error!", filename.c_str());
			}

			// 写入数据块长度和名称
			char size_buffer[sizeof(block.size)];
			memcpy(size_buffer, &block.size, sizeof(size_buffer));
			for (auto ch : size_buffer) image_data.push_back(ch);
			for (auto ch : block.name) image_data.push_back(ch);

			block.pos = ntohl(block.pos);
			block.size = ntohl(block.size);

			std::string s_name(block.name, sizeof(block.name));
			if (strcmp(s_name.c_str(), "IHDR") == 0)
			{
				IHDRBlock ihdr;
				memcpy(&ihdr, &block, sizeof(Block));
				memcpy(((char *)&ihdr) + sizeof(Block), &ReadSome<sizeof(IHDRBlock) - sizeof(Block)>(block_info)[0], sizeof(IHDRBlock) - sizeof(Block));
				for (auto ch : ihdr.data) image_data.push_back(ch);
			}
			else if (strcmp(s_name.c_str(), "IEND") == 0)
			{
				for (auto ch : IEND_DATA) image_data.push_back(ch);
				CCLOG("decrypt %s success!", filename.c_str());
				break;
			}
			else
			{
				for (uint32_t i = 0; i < block.size + CRC_SIZE; ++i)
				{
					image_data.push_back(*(data.getBytes() + block.pos + i));
				}
			}
		}
		return image_data;
	}
}