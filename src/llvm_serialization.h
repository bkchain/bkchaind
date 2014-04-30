#ifndef LLVM_SERIALIZATION_H
#define LLVM_SERIALIZATION_H

#include <boost/mpl/bool.hpp>
#include <boost/serialization/collections_save_imp.hpp>
#include <boost/serialization/collections_load_imp.hpp>
#include <boost/serialization/array.hpp>
#include "llvm/ADT/SmallVector.h"

namespace boost {
	namespace serialization {

		namespace detail {

			template <class T, unsigned N>
			T* get_data(llvm::SmallVector<T, N>& v)
			{
				return v.empty() ? 0 : &(v[0]);
			}

			template <class T, unsigned N>
			T* get_data(llvm::SmallVector<T, N> const & v)
			{
				return get_data(const_cast<llvm::SmallVector<T, N>&>(v));
			}
		}


		/////////1/////////2/////////3/////////4/////////5/////////6/////////7/////////8
		// vector< T >

		// the default versions

		template<class Archive, class U, unsigned N>
		inline void save(
			Archive & ar,
			const llvm::SmallVector<U, N> &t,
			const unsigned int /* file_version */,
			mpl::false_
			){
			boost::serialization::stl::save_collection<Archive, llvm::SmallVector<U, N> >(
				ar, t
				);
		}

		template<class Archive, class U, unsigned N>
		inline void load(
			Archive & ar,
			llvm::SmallVector<U, N> &t,
			const unsigned int /* file_version */,
			mpl::false_
			){
			boost::serialization::stl::load_collection<
				Archive,
				llvm::SmallVector<U, N>,
				boost::serialization::stl::archive_input_seq<
				Archive, llvm::SmallVector<U, N>
				>,
				boost::serialization::stl::reserve_imp<llvm::SmallVector<U, N> >
			>(ar, t);
		}

		// the optimized versions

		template<class Archive, class U, unsigned N>
		inline void save(
			Archive & ar,
			const llvm::SmallVector<U, N> &t,
			const unsigned int /* file_version */,
			mpl::true_
			){
			const collection_size_type count(t.size());
			ar << BOOST_SERIALIZATION_NVP(count);
			if (!t.empty())
				ar << make_array(detail::get_data(t), t.size());
		}

		template<class Archive, class U, unsigned N>
		inline void load(
			Archive & ar,
			llvm::SmallVector<U, N> &t,
			const unsigned int /* file_version */,
			mpl::true_
			){
			collection_size_type count(t.size());
			ar >> BOOST_SERIALIZATION_NVP(count);
			t.resize(count);
			unsigned int item_version = 0;
			if (BOOST_SERIALIZATION_VECTOR_VERSIONED(ar.get_library_version())) {
				ar >> BOOST_SERIALIZATION_NVP(item_version);
			}
			if (!t.empty())
				ar >> make_array(detail::get_data(t), t.size());
		}

		// dispatch to either default or optimized versions

		template<class Archive, class U, unsigned N>
		inline void save(
			Archive & ar,
			const llvm::SmallVector<U, N> &t,
			const unsigned int file_version
			){
			typedef BOOST_DEDUCED_TYPENAME
				boost::serialization::use_array_optimization<Archive>::template apply<
				BOOST_DEDUCED_TYPENAME remove_const<U>::type
				>::type use_optimized;
			save(ar, t, file_version, use_optimized());
		}

		template<class Archive, class U, unsigned N>
		inline void load(
			Archive & ar,
			llvm::SmallVector<U, N> &t,
			const unsigned int file_version
			){
			typedef BOOST_DEDUCED_TYPENAME
				boost::serialization::use_array_optimization<Archive>::template apply<
				BOOST_DEDUCED_TYPENAME remove_const<U>::type
				>::type use_optimized;
			load(ar, t, file_version, use_optimized());
		}

		// split non-intrusive serialization function member into separate
		// non intrusive save/load member functions
		template<class Archive, class U, unsigned N>
		inline void serialize(
			Archive & ar,
			llvm::SmallVector<U, N> & t,
			const unsigned int file_version
			){
			boost::serialization::split_free(ar, t, file_version);
		}
	} // serialization
} // namespace boost

#endif
