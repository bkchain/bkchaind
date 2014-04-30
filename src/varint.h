#ifndef VARINT_H
#define VARINT_H

template<class T>
struct varint
{
public:
	varint(T* value) : value(value) {}

	template<class Archive>
	void serialize(Archive & ar, const unsigned int version)
	{
		if (Archive::isSaveArchive)
		{
			auto v = *value;
			if (v < 0xfd)
			{
				uint8_t v2 = v;
				ar & v2;
			}
			else if (v <= 0xffff)
			{
				uint8_t v2 = 0xfd;
				ar & v2;
				uint16_t v3 = v;
				ar & v3;
			}
			else if (v <= 0xffffffff)
			{
				uint8_t v2 = 0xfe;
				ar & v2;
				uint32_t v3 = v;
				ar & v3;
			}
			else
			{
				uint8_t v2 = 0xff;
				ar & v2;
				uint64_t v3 = v;
				ar & v3;
			}
		}
		else
		{
			uint8_t v;
			ar & v;
			switch (v)
			{
			case 0xfd:
			{
						 uint16_t v2;
						 ar & v2;
						 *value = v2;
			}
				break;
			case 0xfe:
			{
						 uint32_t v2;
						 ar & v2;
						 *value = v2;
			}
				break;
			case 0xff:
			{
						 uint64_t v2;
						 ar & v2;
						 *value = v2;
			}
				break;
			default:
				*value = v;
				break;
			}
		}
	}
private:
	T* value;
};

template <class T>
varint<T> make_varint(T& t)
{
	return varint<T>(&t);
}

#endif
