#include "IBLT_helpers.hpp"
#include "file_sync.hpp"
#include "file_sync.pb.h"
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include "json/json.h"
#include <unistd.h>
#include <stdio.h>

namespace po = boost::program_options;
Json::Value info;
Json::StyledWriter writer;

void testFullProtocol(std::string& file1, std::string& file2, int avg_block_size) {
	typedef uint64_t hash_type;
	typedef FileSynchronizer<hash_type> fsync_type;
	
	GOOGLE_PROTOBUF_VERIFY_VERSION;

	//Determine estimated file difference
	fsync_type file_sync_A(file1, avg_block_size), file_sync_B(file2, avg_block_size);
	std::string strata_encoding = file_sync_A.send_strata_encoding();
	int diff_est = file_sync_B.receive_strata_encoding(strata_encoding);
	std::string iblt_encoding = file_sync_A.send_IBLT_encoding(diff_est);
	std::string rd2_encoding = file_sync_B.receive_IBLT_encoding(iblt_encoding);
	file_sync_A.receive_rd2_encoding(rd2_encoding);
	//file_sync_B.my_rd2.print_size_info();
	
	int total_bytes_no_strata = iblt_encoding.size() + rd2_encoding.size();
	int total_bytes = total_bytes_no_strata + strata_encoding.size();
	int file1_size = boost::filesystem::file_size(file1);
	int file2_size = boost::filesystem::file_size(file2); 

	char* buf;
	load_buffer_with_file(file1.c_str(), &buf);
	std::string file1_string(buf, file1_size);
	delete[] buf;
	load_buffer_with_file(file2.c_str(), &buf);
	std::string file2_string(buf, file2_size);
	delete[] buf;
	std::string file1_compressed = compress_string(file1_string);
	std::string file2_compressed = compress_string(file2_string);
	
	Json::Value block_size(avg_block_size);
	Json::Value diff(diff_est), tot_no_strata(total_bytes_no_strata), tot_with_strata(total_bytes);
	Json::Value f1_sz(file1_size), f2_sz(file2_size);
	Json::Value f1_szc((int) file1_compressed.size()), f2_szc((int) file2_compressed.size());
	info["block_size"] = block_size;
	info["difference_estimate"] = diff;
	info["total_bytes_no_strata"] = tot_no_strata;
	info["total_bytes_with_strata"] = tot_with_strata;
	info["file1_size"] = f1_sz;
	info["file2_size"] = f2_sz;
	info["file1_size_compressed"] = f1_szc;
	info["file2_size_compressed"] = f2_szc;
}

void testRsync(std::string& file1, std::string& file2, int block_size) {
	int pipe_fd[2];
	pipe(pipe_fd);

	int pid = fork();
	char block_sz[15] = {0}, rsync_bytes[15] = {0};
	sprintf( block_sz, "%d", block_size );
	if( pid == 0 ) {
		char* argv[] = { const_cast<char*>("./parseRsync.sh"), block_sz, const_cast<char*>(file2.c_str()), NULL };
		dup2(pipe_fd[1], STDOUT_FILENO); 
		close(pipe_fd[1]); close(pipe_fd[0]);
		execvp(argv[0], argv);
		std::cerr << "Unknown command" << std::endl;
		exit(1);
	} else {
		dup2(pipe_fd[0], STDIN_FILENO);
		close(pipe_fd[1]); close(pipe_fd[0]);
		read(STDIN_FILENO, rsync_bytes, 15);
	}
	Json::Value rsync_b(atoi(rsync_bytes)), rsync_block_size(block_size);
	info["rsync_bytes"] = rsync_b;
	info["rsync_block_size"] = rsync_block_size;
}



int main(int argc, char* argv[]) {
	std::string f1, f2;
	double error_prob;
	int block_changes, block_changes_size, file_len, avg_block_size;
	bool use_rsync;
	
	po::options_description desc("Allowed options");
	desc.add_options()
		("help", "produce help message")
		("f1", po::value<std::string>(&f1)->default_value("A/f1.txt"), "First file name")
		("f2", po::value<std::string>(&f2)->default_value("B/f1.txt"), "Second file name")
		("file-len", po::value<int>(&file_len)->default_value(100000), "File length")
		("error-prob", po::value<double>(&error_prob), "random error probability")
		("num-changes", po::value<int>(&block_changes), "number of block changes")
		("change-size", po::value<int>(&block_changes_size)->default_value(5), "size of block changes")
		("block-size", po::value<int>(&avg_block_size)->default_value(700), "avg block size")
		("rsync", po::value<bool>(&use_rsync)->default_value(false), "whether to include rsync data")
	;

	po::variables_map vm;
	po::store(po::parse_command_line(argc, argv, desc), vm);
	po::notify(vm);    

	if(vm.count("help")) {
    		std::cout << desc << "\n";
    		return 1;
	}

	if(vm.count("error-prob")) {
		Json::Value test_type("random"), file_length(file_len), error_probability(error_prob);
		info["test_type"] = test_type;
		info["file_length"] = file_length;
		info["error_prob"] = error_probability;
		generate_random_file(f1.c_str(), file_len);
		generate_similar_file(f1.c_str(), f2.c_str(), 1-error_prob);
	} else if( vm.count("num-changes") ) {
		Json::Value test_type("block"), file_length(file_len), num_block_changes(block_changes);
		info["test_type"] = test_type;
		info["file_length"] = file_length;
		info["num_block_changes"] = num_block_changes;
		generate_random_file(f1.c_str(), file_len);
		generate_random_file(f1.c_str(), file_len);
		generate_block_changed_file(f1.c_str(), f2.c_str(), block_changes, block_changes_size);
	} else {
		Json::Value test_type("actual"), file1(f1), file2(f2);
		info["test_type"] = test_type;
		info["file1"] = file1;
		info["file2"] = file2;
	}

	testFullProtocol(f1, f2, avg_block_size);

	if(vm.count("rsync") ) {
		testRsync(f1, f2, avg_block_size);
	}

	std::cout << writer.write( info ) << std::endl;
	return 1;
}
