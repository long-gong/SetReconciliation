#ifndef MULTI_IBLT
#define MULTI_IBLT

#include <vector>
#include <stdint.h>
#include <assert.h>
#include <functional>
#include <cstdlib>
#include <deque>
#include <algorithm>
#include <set>
#include <unordered_set>
#include "tabulation_hashing.hpp"
#include "basicField.hpp"

//structure of a bucket within an IBLT
template <int n_parties = 2, typename key_type = uint64_t, int key_bits= 8*sizeof(key_type), typename hash_type = uint64_t>
class multiIBLT_bucket {
  public:
  	typedef multiIBLT_bucket<n_parties, key_type, key_bits, hash_type> this_bucket_type;
	Field<n_parties, key_bits> key_sum;
	Field<n_parties, 8*sizeof(hash_type)> hash_sum;
	int count;

	multiIBLT_bucket(): key_sum(), hash_sum(), count() {}

	void add(const key_type* k, const hash_type* h) {
		key_sum.add(k);
		hash_sum.add(h);
		count++;
	}

	void add(const this_bucket_type& counterparty_bucket) {
		key_sum.add(counterparty_bucket.key_sum);
		hash_sum.add(counterparty_bucket.hash_sum);
		count += counterparty_bucket.count;
	}

	void remove(const key_type* k, const hash_type* h) {
		key_sum.remove(k);
		hash_sum.remove(h);
		count--;
	}

	void remove(const this_bucket_type& counterparty_bucket) {
		key_sum.remove(counterparty_bucket.key_sum);
		hash_sum.remove(counterparty_bucket.hash_sum);
		count -= counterparty_bucket.count;
	}

	void print_contents() const {
		printf("Key_sum:");
		key_sum.print_contents();
		printf("Hash_sum:");
		hash_sum.print_contents();
		printf("Count: %d\n", count);
	}

	bool operator==( const multiIBLT_bucket& other ) const
	{
		return( key_sum == other.key_sum && hash_sum == other.hash_sum );
	}

	bool operator<( const multiIBLT_bucket& other ) const 
	{
		return( key_sum < other.key_sum || hash_sum < other.hash_sum );
	}
};

/**
Parameters:
	num_hashfns: number of hash functions (equivalent to k in the paper)
	hasher: type of hashfunction (should be able to hash keytype)
**/
template <int n_parties = 2, 
	typename key_type = uint64_t,
	int key_bits = 8*sizeof(key_type),
	typename hash_type = uint64_t, 
	typename hasher = TabulationHashing<key_bits, hash_type> >
class multiIBLT {
  public:
  	typedef multiIBLT<n_parties, key_type, key_bits, hash_type, hasher> this_iblt_type;
  	typedef multiIBLT_bucket<n_parties, key_type, key_bits, hash_type> bucket_type;
  	typedef std::vector<bucket_type> IBLT_type;
  	int num_buckets;
	int num_hashfns;
	int buckets_per_subIBLT;
  	std::vector<IBLT_type> subIBLTs;
  	hasher key_hasher;
  	std::vector<hasher> sub_hashers; 

	//seed num_hashfns different hashfunctions for each subIBLT
	multiIBLT(int bucket_count, int num_hashfns): 
							num_buckets(bucket_count), 
							num_hashfns(num_hashfns),
							buckets_per_subIBLT(num_buckets/num_hashfns),
							subIBLTs(num_hashfns),
							sub_hashers(num_hashfns) {
		assert(num_buckets % num_hashfns == 0);
		key_hasher.set_seed(0);
		for(int i = 0; i < num_hashfns; ++i) {
			subIBLTs[i].resize(buckets_per_subIBLT);
			sub_hashers[i].set_seed(i+1); //separate seeds enough
		}
	}

	void add(const this_iblt_type& counterparty) {
		assert( counterparty.buckets_per_subIBLT == buckets_per_subIBLT 
			&&  counterparty.num_hashfns == num_hashfns);
		for(int i = 0; i < num_hashfns; ++i) {
			for(int j = 0; j < buckets_per_subIBLT; ++j) {
				subIBLTs[i][j].add( counterparty.subIBLTs[i][j] );
			}
		}
	}

	void remove(const this_iblt_type& counterparty) {
		assert( counterparty.buckets_per_subIBLT == buckets_per_subIBLT 
			&&  counterparty.num_hashfns == num_hashfns);
		for(int i = 0; i < num_hashfns; ++i) {
			for(int j = 0; j < buckets_per_subIBLT; ++j) {
				subIBLTs[i][j].remove( counterparty.subIBLTs[i][j] );
			}
		}

	}
	//insert a new key into our IBLT
	void insert_key(const key_type& key) {
		int bucket_index;
		hash_type hashval = key_hasher.hash(&key);
		for(int i = 0; i < num_hashfns; ++i) {
			bucket_index = get_bucket_index(&key, i);
			subIBLTs[i][bucket_index].add( &key, &hashval);
		}
	}

	void remove_key(const key_type& key) {
		int bucket_index;
		hash_type hashval = key_hasher.hash(&key);
		for(int i = 0; i < num_hashfns; ++i) {
			bucket_index = get_bucket_index(&key, i);
			subIBLTs[i][bucket_index].remove( &key, &hashval);
		}
	}	

	//peels the keys from an IBLT, returning true upon success, false upon failure
	//TODO: efficient way to do peeling?
	bool peel(std::unordered_set<key_type>& peeled_keys ) {
		std::deque<bucket_type> peelable_keys;
		bucket_type curr_bucket;
		bool has_multiple_keys;

		//if we go through every entry and nothing is peelable, then we stop
		while( true ) {
			//first try to peel all the keys that can be peeled
			while( !peelable_keys.empty() ) {
				curr_bucket = peelable_keys.front();
				peelable_keys.pop_front();
				key_type peeled_key;
				curr_bucket.key_sum.extract_key(&peeled_key, curr_bucket.count);
				//peeled_keys.push_back(peeled_key);
				if( peeled_keys.insert(peeled_key).second ) {
					peel_key( curr_bucket, peelable_keys );
				}
				
				/*else
					printf("Already peeled this key!\n");*/
			}

			//either every bucket has one key or more, in which case we failed,
			//or every bucket has zero keys, in which case we succeeded;
			if( !find_peelable_key(peelable_keys, has_multiple_keys) ) {
				//printf("Peelable keys size is %d\n", peelable_keys.size());
				return !has_multiple_keys;
			}
		}

		//can't reach this point
		assert(1 == 2);
		return false;
	}

	// returns whether was able to find peelable key
	// stores whether during search 
	bool find_peelable_key(std::deque<bucket_type>& peelable_keys, bool& has_multiple_keys) {
		has_multiple_keys = false;
		for(int i = 0; i < num_hashfns; ++i ) {
			for(int j = 0; j < buckets_per_subIBLT; ++j) {
				if( can_peel( subIBLTs[i][j] ) ) {
					peelable_keys.emplace_back(subIBLTs[i][j]);
					return true;
				}
				if( subIBLTs[i][j].count != 0 )
					has_multiple_keys = true;
			}
		}
		return false;
	}
//Below should be private at some point
	
	//need to think about design decision. Use bucket type when really only need key and hash.
	//only really need key, but for efficiency sake, want to keep hash along for the ride
	void peel_key(bucket_type& peelable_bucket, std::deque<bucket_type>& peelable_keys) {
		std::set<bucket_type> new_peelables;
		key_type buf;
		int bucket_index;
		for(int i = 0; i < num_hashfns; ++i) {
			peelable_bucket.key_sum.extract_key(&buf, peelable_bucket.count);
			bucket_index = get_bucket_index(&buf, i);
			subIBLTs[i][bucket_index].remove(peelable_bucket);

			//optimization: if find a new peelable bucket, add it to queue
			//need to be careful that don't add same key twice
			if( can_peel(subIBLTs[i][bucket_index]) ) {
				//printf("Trying to peel key from other buckets\n");
				new_peelables.insert(subIBLTs[i][bucket_index]);
			}
		}
		//only insert new unique keys to the queue
		for(auto it = new_peelables.begin(); it != new_peelables.end(); ++it) {
			// printf("New peelables:\n");

			// (*it).print_contents();
			peelable_keys.emplace_back(*it);
		}
	}
	
	//TODO: handle case of removing non-existent keys
	//Will need to iterate from -nparties+1 to n_parties-1 (skipping 0)
	//TODO: malloc space for key here?
	bool can_peel(bucket_type& curr_bucket) {
		if( curr_bucket.count == 0 || abs(curr_bucket.count) >= n_parties ) {
			//printf("Current bucket count is: %d\n", curr_bucket.count);
			return false;
		}
		hash_type expected_hash = 0;
		key_type buf;
		for(int i = 1; i < n_parties; ++i) {
			if( curr_bucket.key_sum.can_divide_by( i )
				&& curr_bucket.hash_sum.can_divide_by( i )
				&& curr_bucket.count == i ) {
				curr_bucket.key_sum.extract_key(&buf, i);
				curr_bucket.hash_sum.extract_key(&expected_hash, i);
				hash_type actual_hash = key_hasher.hash(&buf);
				if( expected_hash == actual_hash) {
					return true;
				}
			}
		}
		return false;
	}

	//returns the bucket index of given key in given subIBLT
	uint64_t get_bucket_index(const key_type* key, int subIBLT) {
		assert( subIBLT >= 0 && subIBLT < num_hashfns );
		return sub_hashers[subIBLT].hash(key) % buckets_per_subIBLT;
	}

	void print_contents() const {
		for(int i = 0; i < num_hashfns; ++i) {
			for(int j = 0; j < buckets_per_subIBLT; ++j) {
				subIBLTs[i][j].print_contents();
			}
		}
	}
};


#endif