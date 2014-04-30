#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <iostream>
#include <stdint.h>
#include <boost/iostreams/stream.hpp>
#include "varint.h"

class BinaryInputArchive
{
public:
	BinaryInputArchive(std::istream& stream)
		: m_sb(*stream.rdbuf())
	{
	}

	BinaryInputArchive& operator&(bool& t)
	{
		t = m_sb.sbumpc() != 0;
		return *this;
	}
	BinaryInputArchive& operator&(uint8_t& t)
	{
		t = m_sb.sbumpc();
		return *this;
	}
	BinaryInputArchive& operator&(uint16_t& t)
	{
		m_sb.sgetn((char*)&t, sizeof(t));
		return *this;
	}
	BinaryInputArchive& operator&(uint32_t& t)
	{
		m_sb.sgetn((char*)&t, sizeof(t));
		return *this;
	}
	BinaryInputArchive& operator&(uint64_t& t)
	{
		m_sb.sgetn((char*)&t, sizeof(t));
		return *this;
	}
	BinaryInputArchive& operator&(int32_t& t)
	{
		m_sb.sgetn((char*)&t, sizeof(t));
		return *this;
	}
	BinaryInputArchive& operator&(int64_t& t)
	{
		m_sb.sgetn((char*)&t, sizeof(t));
		return *this;
	}
	BinaryInputArchive& operator&(float& t)
	{
		m_sb.sgetn((char*)&t, sizeof(t));
		return *this;
	}
	BinaryInputArchive& operator&(double& t)
	{
		m_sb.sgetn((char*)&t, sizeof(t));
		return *this;
	}

	template <class T, unsigned N>
	BinaryInputArchive& operator&(T(&t)[N])
	{
		m_sb.sgetn((char*)t, sizeof(t));
		return *this;
	}

	template <class T1, class T2>
	BinaryInputArchive& operator&(std::pair<T1, T2>& t)
	{
		*this & t.first;
		*this & t.second;
		return *this;
	}

	template <class T>
	BinaryInputArchive& operator&(std::vector<T>& t)
	{
		uint32_t count;
		*this & make_varint(count);
		t.clear();
		t.reserve(count);
		while (count-- > 0)
		{
			T item;
			*this & item;
			t.push_back(item);
		}
		return *this;
	}

	template <class T, unsigned N>
	BinaryInputArchive& operator&(llvm::SmallVector<T, N>& t)
	{
		uint32_t count;
		*this & make_varint(count);
		t.clear();
		t.reserve(count);
		while (count-- > 0)
		{
			T item;
			*this & item;
			t.push_back(item);
		}
		return *this;
	}

	template <unsigned N>
	BinaryInputArchive& operator&(llvm::SmallVector<uint8_t, N>& t)
	{
		uint32_t count;
		*this & make_varint(count);
		t.resize(count);
		if (count > 0)
			m_sb.sgetn((char*)&t[0], sizeof(t[0]) * count);
		return *this;
	}

	template <class T>
	BinaryInputArchive& operator&(const T& t)
	{
		const_cast<T&>(t).serialize(*this, 0);
		return *this;
	}

	static const bool isSaveArchive = false;
	static const bool isLoadArchive = true;
private:
	std::streambuf& m_sb;
};

class BinaryOutputArchive
{
public:
	BinaryOutputArchive(std::ostream& stream)
		: m_sb(*stream.rdbuf())
	{

	}

	BinaryOutputArchive& operator&(bool t)
	{
		m_sb.sputc(t ? 1 : 0);
		return *this;
	}
	BinaryOutputArchive& operator&(uint8_t t)
	{
		m_sb.sputc(t);
		return *this;
	}
	BinaryOutputArchive& operator&(uint16_t t)
	{
		m_sb.sputn((const char*)&t, sizeof(t));
		return *this;
	}
	BinaryOutputArchive& operator&(uint32_t t)
	{
		m_sb.sputn((const char*)&t, sizeof(t));
		return *this;
	}
	BinaryOutputArchive& operator&(uint64_t t)
	{
		m_sb.sputn((const char*)&t, sizeof(t));
		return *this;
	}
	BinaryOutputArchive& operator&(int32_t t)
	{
		m_sb.sputn((const char*)&t, sizeof(t));
		return *this;
	}
	BinaryOutputArchive& operator&(int64_t t)
	{
		m_sb.sputn((const char*)&t, sizeof(t));
		return *this;
	}
	BinaryOutputArchive& operator&(float t)
	{
		m_sb.sputn((const char*)&t, sizeof(t));
		return *this;
	}
	BinaryOutputArchive& operator&(double t)
	{
		m_sb.sputn((const char*)&t, sizeof(t));
		return *this;
	}

	template <class T, unsigned N>
	BinaryOutputArchive& operator&(T(&t)[N])
	{
		m_sb.sputn((const char*)t, sizeof(t));
		return *this;
	}

	template <class T1, class T2>
	BinaryOutputArchive& operator&(const std::pair<T1, T2>& t)
	{
		*this & t.first;
		*this & t.second;
		return *this;
	}

	template <class T>
	BinaryOutputArchive& operator&(const std::vector<T>& t)
	{
		uint32_t count = t.size();
		*this & make_varint(count);
		auto it = t.begin();
		while (count-- > 0)
		{
			*this & *it++;
		}
		return *this;
	}

	template <class T, unsigned N>
	BinaryOutputArchive& operator&(const llvm::SmallVector<T, N>& t)
	{
		uint32_t count = t.size();
		*this & make_varint(count);
		auto it = t.begin();
		while (count-- > 0)
		{
			*this & *it++;
		}
		return *this;
	}

	template <unsigned N>
	BinaryOutputArchive& operator&(const llvm::SmallVector<uint8_t, N>& t)
	{
		uint32_t count = t.size();
		*this & make_varint(count);
		if (count > 0)
			m_sb.sputn((const char*)&t[0], sizeof(t[0]) * count);
		return *this;
	}

	template <class T>
	BinaryOutputArchive& operator&(const T& t)
	{
		const_cast<T&>(t).serialize(*this, 0);
		return *this;
	}

	static const bool isSaveArchive = true;
	static const bool isLoadArchive = false;
private:
	std::streambuf& m_sb;
};

template <class T>
void Serialize(std::string &serial_str, const T& obj)
{
	struct SerializeData
	{
		SerializeData()
		: sink(buffer), stream(sink)
		{
		}

		char buffer[1024 * 1024 * 8];
		boost::iostreams::array_sink sink;
		boost::iostreams::stream<boost::iostreams::array_sink> stream;
	};

	static boost::thread_specific_ptr<SerializeData> serializeDataHolder;

	auto serializeData = serializeDataHolder.get();
	if (serializeData == NULL)
	{
		serializeDataHolder.reset(serializeData = new SerializeData());
	}

	auto& stream = serializeData->stream;
	stream.seekp(0);
	BinaryOutputArchive out_archive(stream);
	out_archive & obj;

	auto pos = stream.tellp();
	serial_str.assign(serializeData->buffer, pos);
}

template <class T>
void Deserialize(const std::string &serial_str, T& obj)
{
	// wrap buffer inside a stream and deserialize serial_str into obj
	boost::iostreams::basic_array_source<char> device(serial_str.data(), serial_str.size());
	boost::iostreams::stream<boost::iostreams::basic_array_source<char> > s(device);

	BinaryInputArchive ia(s);

	ia & obj;
}

#endif
