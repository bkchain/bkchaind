#ifndef ADDRESS_H
#define ADDRESS_H

#include <cstring>
#include <stdint.h>

/// Address, in a compact binary format (20 bytes).
struct Address
{
public:
	Address() {}

	Address(const uint8_t* newValue)
	{
		memcpy(value, newValue, 20);
	}

	bool operator==(const Address& other) const
	{
		return memcmp(value, other.value, sizeof(value)) == 0;
	}

	bool operator!=(const Address& other) const
	{
		return !(*this == other);
	}

	bool operator<(const Address& other) const
	{
		uint8_t* address1 = (uint8_t*)value;
		uint8_t* address2 = (uint8_t*)other.value;
		for (int i = 0; i < sizeof(value); ++i)
		{
			if (address1[i] < address2[i])
				return true;
			if (address1[i] > address2[i])
				return false;
		}

		return false;
	}

	bool IsNull() const
	{
		for (int i = 0; i < sizeof(value); ++i)
		{
			if (value[i] != 0)
				return false;
		}

		return true;
	}

	uint8_t value[20];
public:
	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & value;
	}
};

namespace std {
	template <>
	class hash<Address>
	{
	public:
		size_t operator()(const Address& x) const
		{
			return *(size_t*)x.value;
		}
	};
}

#endif
