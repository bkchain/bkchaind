#ifndef HASH256_H
#define HASH256_H

#include <cstring>
#include <stdint.h>

/// 256-bit hash.
class Hash256
{
public:
	Hash256() {}

	Hash256(const uint8_t* newHash)
	{
		memcpy(value, newHash, 32);
	}

	bool operator==(const Hash256& other) const
	{
		return memcmp(value, other.value, sizeof(value)) == 0;
	}

	bool operator!=(const Hash256& other) const
	{
		return !(*this == other);
	}

	bool operator<(const Hash256& other) const
	{
		uint8_t* hash1 = (uint8_t*)value;
		uint8_t* hash2 = (uint8_t*)other.value;
		for (int i = 0; i < sizeof(value); ++i)
		{
			if (hash1[i] < hash2[i])
				return true;
			if (hash1[i] > hash2[i])
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

	uint8_t value[32];

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		ar & value;
	}
};

namespace std {
	template <>
	class hash<Hash256>
	{
	public:
		size_t operator()(const Hash256& x) const
		{
			// Use middle of hash because bitcoin might have many hashes with leading 0
			return *(size_t*)&x.value[16];
		}
	};
}

#endif
