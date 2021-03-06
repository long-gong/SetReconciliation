#ifndef _IBLT_HELPERS
#define _IBLT_HELPERS

#include <assert.h>
#include <sys/stat.h>

#include <cstdlib>
#include <iostream>
#include <map>
#include <random>
#include <unordered_map>
#include <unordered_set>

#include "hash_util.hpp"

#define DEBUG 1

#if DEBUG
#  define IBLT_DEBUG(x)  do { std::cerr << x << std::endl; } while(0)
#else
#  define IBLT_DEBUG(x)  do {} while (0)
#endif


/**  checkResults asserts that two sets contain the same information**/
template <typename key_type>
void checkResults(std::unordered_set<key_type>& expected, std::unordered_set<key_type>& actual) {
	if( expected.size() != actual.size() ) {
		printf("Expected: %zu, Actual: %zu\n", expected.size(), actual.size());
		return;
	}

	for(auto it1 = expected.begin(); it1 != expected.end(); ++it1 ) {
		assert( actual.find(*it1) != actual.end() );
	}
}

/**  checkResults asserts that two sets contain the same information**/
template <typename key_type>
void checkResults(std::unordered_map<key_type, std::vector<int> >& expected, 
				  std::unordered_map<key_type, std::vector<int> >& actual) {
	if( expected.size() != actual.size() ) {
		printf("Expected: %zu, Actual: %zu\n", expected.size(), actual.size());
		return;
	}

	for(auto it1 = expected.begin(); it1 != expected.end(); ++it1 ) {
		assert( actual[it1->first] == it1->second );
	}
}

size_t load_buffer_with_file(const std::string& filename, std::vector<char>& buf);

size_t get_file_size(const std::string& filename);

std::string get_SHAHash(const std::string& filename);

// generate_random_file creates a file with len alphanumeric characters
void generate_random_file(const std::string& filename, size_t len);

// generate_similar_file creates a file that has on average pct_similarity characters the same 
// and in the same order as the old_file
void generate_similar_file(const std::string& old_file, 
                           const std::string& new_file, 
                           double pct_similarity);

void generate_block_changed_file(const std::string& old_file, 
                                 const std::string& new_file, 
                                 size_t num_new_blocks, 
                                 size_t block_size);

template <typename key_type, int key_bits = 8*sizeof(key_type)>
class keyGenerator {
  public:
  	std::default_random_engine rng;
	std::uniform_int_distribution<uint64_t> dist;

	keyGenerator(): dist(0, (uint64_t) -1) {}
	keyGenerator(int seed): dist(0, (uint64_t) -1) {
		set_seed(seed);
	}

	key_type generate_key() {
		return (key_type) dist(rng);
	}

	void set_seed(int s) {
		rng.seed(s);
	}
};

template <int key_bits>
class keyGenerator<std::string, key_bits> {
  public:
  	int key_bytes;
  	static const std::string alphanumeric;
        
	std::default_random_engine rng;
	std::uniform_int_distribution<> dist;

  	keyGenerator(): key_bytes(key_bits/8), dist(0, alphanumeric.size() - 1) {}
  	keyGenerator(int s): key_bytes(key_bits/8), dist(0, alphanumeric.size() - 1) {
		set_seed(s);
	}
  	void set_seed(int s) {
  		rng.seed(s);
  	}

  	std::string generate_key() {
  		std::string output;
  		output.reserve(key_bytes);
  		for(int i = 0; i < key_bytes; ++i) {
  			output += alphanumeric[dist(rng)];
  		}
  		return output;
  	}
};

template <int key_bits>
const std::string keyGenerator<std::string, key_bits>::alphanumeric = 
		"abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789 ";

template <typename key_type, 
          typename generator = keyGenerator<key_type, sizeof(key_type)> >
class keyHandler {
  public:
  	generator gen;
	
	keyHandler(): gen(0) {};
	keyHandler(int seed): gen(seed) {};

	void generate_distinct_keys(int num_keys, std::unordered_set<key_type>& keys) {
		int num_inserted = 0;
		while( num_inserted != num_keys) {
			if( keys.insert( gen.generate_key() ).second ) {
				++num_inserted;
			}
		}
	}

	void insert_sample_keys(int num_shared_keys, 
                            int num_distinct_keys, 
                            std::unordered_set<key_type> all_keys,
							std::unordered_set<key_type>& shared_keys, 
                            std::vector<std::unordered_set<key_type> >& key_sets) {
		auto all_key_it = all_keys.begin();
		for(int count = 0; count < num_shared_keys; ++all_key_it, ++count) {
			shared_keys.insert(*all_key_it);
		}

		for(auto it1 = key_sets.begin(); it1 != key_sets.end(); ++it1) {
			for(int count = 0; count < num_distinct_keys; ++all_key_it, ++count) {
				(*it1).insert(*all_key_it);
			}
		}
	}

	void generate_sample_keys(int num_shared_keys, 
                              int num_distinct_keys,
							  std::unordered_set<key_type>& shared_keys,
							  std::vector<std::unordered_set<key_type> >& key_sets) {
		std::unordered_set<key_type> all_keys;
		generate_distinct_keys(num_shared_keys + key_sets.size()*num_distinct_keys, all_keys);
		insert_sample_keys(num_shared_keys, num_distinct_keys, all_keys, shared_keys, key_sets);
	}

	void assign_keys(double insert_prob, 
                     int n_parties,
                     std::unordered_set<key_type>& all_keys, 
					 std::unordered_map<key_type, std::vector<int> >& key_assignments) {
		std::random_device rd;
	    std::mt19937 gen(rd());
	    std::uniform_real_distribution<> dis(0, 1);
		for(auto it = all_keys.begin(); it != all_keys.end(); ++it) {
			std::vector<int> assignments;
			for(int i = 0; i < n_parties; ++i) {
				if( dis(gen) < insert_prob) {
					assignments.push_back(i);
				}
			}
			key_assignments[*it] = assignments;
		}
	}

	void assign_keys(double insert_prob, 
                     int n_parties, 
                     int num_keys, 
					 std::vector<std::unordered_set<key_type> >& key_assignments) {
		std::unordered_set<key_type> all_keys;
		generate_distinct_keys(num_keys, all_keys);
		std::unordered_map<key_type, std::vector<int> > key_map;
		assign_keys( insert_prob, n_parties, all_keys, key_map);
		transform_keys(key_map, key_assignments);
	}

	void transform_keys(std::unordered_map<key_type, std::vector<int> >& key_map, 
                        std::vector<std::unordered_set<key_type> >& key_vec) {
		for(auto it = key_map.begin(); it != key_map.end(); ++it) {
			for(auto it2 = it->second.begin(); it2 != it->second.end(); ++it2 ) {
				key_vec[*it2].insert(it->first);
			}
		}	
	}
 
	void assign_keys(double insert_prob, 
                     int n_parties, 
                     int num_keys, 
					 std::unordered_map<key_type, std::vector<int> >& key_assignments) {
		std::unordered_set<key_type> all_keys;
		generate_distinct_keys(num_keys, all_keys);
		assign_keys( insert_prob, n_parties, all_keys, key_assignments);
	}

	void set_union(std::vector<std::unordered_set<key_type> >& key_sets,
                   std::unordered_set<key_type>& final_set) {
		for(auto it1 = key_sets.begin(); it1 != key_sets.end(); ++it1) {
			for(auto it2 = (*it1).begin(); it2 != (*it1).end(); ++it2) {
				final_set.insert(*it2);
			}
		}
	}

	void set_union(std::unordered_set<key_type>& key1,
                   std::unordered_set<key_type>& key2, 
                   std::unordered_set<key_type>& final_set) {
		for(auto it = key1.begin(); it != key1.end(); ++it) {
			final_set.insert(*it);
		}
		for(auto it = key2.begin(); it != key2.end(); ++it) {
			final_set.insert(*it);
		}
	}
	void set_intersection(std::unordered_set<key_type>& key1, 
                          std::unordered_set<key_type>& key2, 
                          std::unordered_set<key_type>& intersection) {
		for(auto it1 = key1.begin(); it1 != key1.end(); ++it1) {
			if( key2.find(*it1) != key2.end() ) {
				intersection.insert(*it1);
			}
		}
	}
	// Returns all the keys in keys1 - keys2
	void set_difference(std::unordered_set<key_type>& keys1, 
                        std::unordered_set<key_type>& keys2, 
                        std::unordered_set<key_type>& result) {
		for(auto it = keys1.begin(); it != keys1.end(); ++it) {
			if( keys2.find(*it) == keys2.end() ) {
				result.insert(*it);
			}
		}
	}
	
	//Returns all the keys in (keys1 U keys2) - (keys1 intersect keys2)
	void distinct_keys(std::unordered_set<key_type>& keys1, 
                       std::unordered_set<key_type>& keys2, 
                       std::unordered_set<key_type>& result) {
		set_difference(keys1, keys2, result);
		set_difference(keys2, keys1, result);
	}

	void distinct_keys(std::vector<std::unordered_set<key_type> >& key_assignments,
                       std::unordered_set<key_type>& result) {
		std::unordered_set<key_type> I, U;
		set_intersection(key_assignments, I);
		set_union(key_assignments, U);
		set_difference(U, I, result);
	}

	void set_intersection(std::vector<std::unordered_set<key_type>>& keys,
                          std::unordered_set<key_type>& intersection) {
		for(auto it = keys[0].begin(); it != keys[0].end(); ++it) {
			int in_intersection = true;
			for(size_t i = 1; i < keys.size(); ++i) {
				if(keys[i].find(*it) == keys[i].end()) {
					in_intersection = false;
					break;
				}
			}
			if( in_intersection ) {
				intersection.insert(*it);
			}
		}
	}

	void set_counts(std::vector<std::unordered_set<key_type> >& key_sets,
					std::unordered_map<key_type, int>& counts) {
		for(auto it1 = key_sets.begin(); it1 != key_sets.end(); ++it1) {
			for(auto it2 = (*it1).begin(); it2 != (*it1).end(); ++it2) {
				if( counts.find(*it1) == counts.end() ) {
					counts[*it1] = 1;
				} else {
					counts[*it1]++;
				}
			}
		}
	}

	void set_counts(std::unordered_map<key_type, std::vector<int> >& key_assignments,
					std::unordered_map<key_type, int>& counts) {
		for(auto it = key_assignments.begin(); it != key_assignments.end(); ++it) {
			counts[it->first] = (it->second).size();
		}
	}

	void set_difference(size_t n_parties,
                        std::unordered_map<key_type, 
                        std::vector<int> >& key_assignments,
						std::unordered_set<key_type>& keys) {
		for(auto it = key_assignments.begin(); it != key_assignments.end(); ++it) {
			if((it->second.size()) > 0 && (it->second.size() < n_parties))
				keys.insert(it->first);
		}
	}

	void set_difference(size_t n_parties,
                        std::unordered_map<key_type, std::vector<int> >& key_assignments,
						std::unordered_map<key_type, std::vector<int> >& keys) {
		for(auto it = key_assignments.begin(); it != key_assignments.end(); ++it) {
			if((it->second.size()) > 0 && (it->second.size() < n_parties)) {
				keys[it->first] = it->second;
			} 
		}
	}	
};

#endif
