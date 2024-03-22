//#include "xcl2.hpp"
#include <algorithm>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include <omp.h>
#include <unistd.h>

#include "fysat.h"

using std::clog;
using std::cout;
using std::endl;
using std::vector;


/*
#define MAX_HBM_PC_COUNT 32
#define PC_NAME(n) n | XCL_MEM_TOPOLOGY
const int pc[MAX_HBM_PC_COUNT] = {
    PC_NAME(0),  PC_NAME(1),  PC_NAME(2),  PC_NAME(3),  PC_NAME(4),  PC_NAME(5),  PC_NAME(6),  PC_NAME(7),
    PC_NAME(8),  PC_NAME(9),  PC_NAME(10), PC_NAME(11), PC_NAME(12), PC_NAME(13), PC_NAME(14), PC_NAME(15),
    PC_NAME(16), PC_NAME(17), PC_NAME(18), PC_NAME(19), PC_NAME(20), PC_NAME(21), PC_NAME(22), PC_NAME(23),
    PC_NAME(24), PC_NAME(25), PC_NAME(26), PC_NAME(27), PC_NAME(28), PC_NAME(29), PC_NAME(30), PC_NAME(31)};
*/

void fysat(
	tapa::mmaps<bits<tapa::vec_t<length, SDSIZE>>,TPE_NUM> ol_len_off, 
	tapa::mmaps<bits<tapa::vec_t<int, 16>>,TPE_NUM> ClauseList, 
	tapa::mmaps<bits<tapa::vec_t<int, DSIZE>>,TPE_NUM> VarsOccList,
	tapa::mmaps<int,TPE_NUM> Result,
	int numVars, int numClauses, int s, int k, 
	int maxFlip, unsigned long long target_cycles
	);

DEFINE_string(bitstream, "", "path to bitstream file, run csim if empty");

bool verify(int numVars, int numClauses, int k, std::vector<int> VarsAssgmt, 
			std::vector<clength> cls_len_off, 
			std::vector<int, tapa::aligned_allocator<int>> ClauseList) {
	
	for (int c = 1; c <= numClauses; c++) {
		bool cls = false;
		clength clen = cls_len_off[c];
		
		for (int i = 0; i < clen; i++) {
			int lit = ClauseList[2 + c * k + i]; //ignore first two entries
			assert( ABS(lit) <= numVars );
			assert( lit != 0 );
			bool tval = VarsAssgmt[ABS(lit)] > 0;
			if (tval == (lit > 0)) cls = true;
		}
		if (cls == false) return 0;
	}

	return 1;
}

void init(std::vector<int, tapa::aligned_allocator<int>> & ClauseList, std::vector<int, tapa::aligned_allocator<int>>& VarsOccList,  
    std::vector<clength>& cls_len_off, std::vector<length, tapa::aligned_allocator<length>>& ol_len_off,
    std::string fileName, int& numVars, int& numClauses, int & k) {

	std::cout << "Reading input file." << std::endl;

	std::ifstream fileDIMACS(fileName);
	if(fileDIMACS.is_open()){
		std::string line;
		char pp;
		while ((pp = fileDIMACS.peek()) == 'c') {
			fileDIMACS.ignore(256, '\n');
		}

		// parsing the first line
		fileDIMACS >> line;	// p
		fileDIMACS >> line;	// cnf
		fileDIMACS >> numVars;	// nv
		fileDIMACS >> numClauses;	// nc

		std::vector<std::vector<int>> tempCList(numClauses+1);
		std::vector<std::vector<std::vector<int>>> tempOList(2*numVars, std::vector<std::vector<int>>(DSIZE, std::vector<int>(0)));

		int cnum = 1;

		while (!fileDIMACS.eof()) {
			fileDIMACS.ignore(256, '\n');
			char p = fileDIMACS.peek();
			if (p == EOF || fileDIMACS.eof()) {
				break;
			}
			if (p == 'c') {
				fileDIMACS.ignore(256, '\n');
				continue;
			}
			if ((p > '0' && p <= '9') || p == '-' || p == ' ') {
				if (p == ' ') {
					fileDIMACS.ignore(256, ' ');
				}

				while(true) {
					int nextLit;
					fileDIMACS >> nextLit;
					if( nextLit != 0 ){
						tempCList[cnum].push_back(nextLit);
						int vpos = GETPOS(nextLit);
						int mod = cnum%DSIZE;
						tempOList[vpos][mod].push_back(cnum);
						//cout << "lit: " << nextLit << ", vpos: " << vpos << ", mod: " << mod << endl;
					}
					else{
						cnum++;
						break;
					}
				}
			}
		}
		std::cout << "Done reading input file. Generating FPGA input data." << std::endl;

		int maxk = 0;
		for(int i = 0; i < numClauses+1; i++){
			if( tempCList[i].size() > maxk ){
				maxk = tempCList[i].size();
			}
		}

		int maxo = 0;
		double avgo = 0;
		double imbalance = 0;
		for(int i = 0; i < 2*numVars; i++){
			int sum = 0;
			int max_temp = 0;
			int temp[DSIZE];
			for(int j = 0; j < DSIZE; j++){
				temp[j] = tempOList[i][j].size();
				sum += temp[j];
				if( temp[j] > max_temp ){
					max_temp = temp[j];
				} 
			}
			avgo += sum;
			if( sum > maxo ){
				maxo = sum;
			}
			if( max_temp != 0 ) 
			for(int j = 0; j < DSIZE; j++){
				imbalance += ((double)max_temp - temp[j])/max_temp;
			}
		}

		k = ((maxk+1)/16+1)*16; // 2 more fields needed
		std::cout << "numvars: " << numVars << " numcls: " << numClauses << " maxk: " << maxk << " k: " << k << " maxo: " << maxo << " avgo: " << avgo / 2 / numVars << " imbalance: " << imbalance / 2 / numVars / DSIZE << endl;


		assert(numVars + 3 <= MAXNVAR);
		assert(numClauses < MAXNCLS);
		assert(k <= MAXK);

		for(int i = 0; i < numClauses+1; i++){
			cls_len_off[i] = tempCList[i].size();
			ClauseList.push_back(i); //clause id
			if( i != 0 ) assert( cls_len_off[i] != 0 );
			ClauseList.push_back(cls_len_off[i]); //literal length

			for(int j = 0; j < cls_len_off[i]; j++){
				ClauseList.push_back(tempCList[i][j]);	
			}	
			for(int j = cls_len_off[i]+2; j < k; j++){
				ClauseList.push_back(0); //must be padded with 0
			}
		}

		int addr = 0;
		ol_len_off.push_back(addr);

		for(int i = 0; i < 2*numVars; i++){
			int lit_len = 0;
			for(int j = 0; j < DSIZE; j++){
				if( tempOList[i][j].size() > lit_len ){
					lit_len = tempOList[i][j].size();
				}
			}		
			for(int l = 0; l < lit_len; l++){
				for(int j = 0; j < DSIZE; j++){
					int tempO;
					if( l < tempOList[i][j].size() ){
						tempO = tempOList[i][j][l];
					}
					else{
						tempO = 0; //must be padded with 0
					}
					VarsOccList.push_back(tempO);
					//cout << "i: " << i << ", l: " << l << ", j: " << j << ", O: " << tempO << endl;
				}
				addr++; //addr in unit of DSIZE
			}
			ol_len_off.push_back(addr);
		}

		//make it multiple of 16
		int ol_len_size = ol_len_off.size();
		for( int i = ol_len_size; i < ((ol_len_size-1)/16+1)*16; i++){
			ol_len_off.push_back(0);
		} 
	} 
	else {
		std::cerr << "Cannot open file: " << fileName << "\n";
		exit(1);
	}
}

int main(int argc, char** argv) {
	gflags::ParseCommandLineFlags(&argc, &argv, /*remove_flags=*/true);
//    if (argc != 7) {
//        std::cout << "Usage: " << argv[0] << " <XCLBIN File> <fileName> <seed> <maxFlips> <k> <r>" << std::endl;
//        return EXIT_FAILURE;
//    }

   
	//std::string binaryFile = argv[1];

	char the_path[256];
	getcwd(the_path, 255);
	//strcat(the_path, "/../test/");
	strcat(the_path, "/");
	strcat(the_path, argv[1]);

	std::string fileName(the_path);
	std::string ss(argv[2]);
	std::string fstr(argv[3]);
	std::string tstr(argv[4]);
	//std::string kstr(argv[4]);
	//std::string rstr(argv[5]);
	
	int randseed = stoi(ss);
	if (randseed == 0) {randseed = time(0) & 0xfff;}

	int maxFlip = stoi(fstr);
	if (maxFlip == 0) {maxFlip = MAXFLIP;}

	double target_time = stod(tstr);
	if(target_time == 0){target_time = 1.0;}

	unsigned long long target_cycles = target_time * 200000000;


	std::cout << fileName << " seed: " << randseed << ", maxflip: " << maxFlip << ", target_cycles: " << target_cycles << endl;

	//k = stoi(kstr);
	//if (k == 0) {k = MAXK;}

	//r = stoi(rstr);
	//if (r == 0) {r = MAXR;}

	srand(randseed);
        
	std::vector<std::vector<int, tapa::aligned_allocator<int>>> ClauseList(TPE_NUM);
	std::vector<std::vector<int, tapa::aligned_allocator<int>>> VarsOccList(TPE_NUM);
	
	std::vector<clength> cls_len_off(MAXNCLS+16, 0);
	std::vector<std::vector<length, tapa::aligned_allocator<length>>> ol_len_off(TPE_NUM);
	
	std::vector<std::vector<int, tapa::aligned_allocator<int>>> Result(TPE_NUM, std::vector<int, tapa::aligned_allocator<int>>(MAXNVAR+16, 0));

	int numVars, numClauses;

/*
	auto estart = std::chrono::steady_clock::now();

	cl_int err;
    cl::Context context;
    cl::Kernel krnl_yalsat;
    cl::CommandQueue q;


	std::vector<cl_mem_ext_ptr_t> cls_len_buf(1);
    std::vector<cl_mem_ext_ptr_t> ol_len_buf(1);
	std::vector<cl_mem_ext_ptr_t> flipcnt_buf(1);
	std::vector<cl_mem_ext_ptr_t> answer_buf(1);
    
	cls_len_buf[0].obj = cls_len_off.data();
	cls_len_buf[0].param = 0;
	cls_len_buf[0].flags = pc[0];

	ol_len_buf[0].obj = ol_len_off.data();
	ol_len_buf[0].param = 0;
	ol_len_buf[0].flags = pc[1];

	flipcnt_buf[0].obj = flipcnt.data();
	flipcnt_buf[0].param = 0;
	flipcnt_buf[0].flags = pc[2];

	answer_buf[0].obj = VarsAssgmt.data(); //flipcnt.data();
	answer_buf[0].param = 0;
	answer_buf[0].flags = pc[3];
*/
	auto init_start = std::chrono::steady_clock::now();

	int k;
	init(ClauseList[0], VarsOccList[0], cls_len_off, ol_len_off[0], fileName, numVars, numClauses, k);

	for(int t = 1; t < TPE_NUM; t++){
		std::copy(ClauseList[0].begin(), ClauseList[0].end(), std::back_inserter(ClauseList[t]));
		std::copy(VarsOccList[0].begin(), VarsOccList[0].end(), std::back_inserter(VarsOccList[t]));
		std::copy(ol_len_off[0].begin(), ol_len_off[0].end(), std::back_inserter(ol_len_off[t]));
	}
	
	auto init_end = std::chrono::steady_clock::now();
	double init_time = std::chrono::duration_cast<std::chrono::nanoseconds>(init_end - init_start).count();
	std::cout << "Init time: " << init_time*1e-9 << std::endl;

	std::cout << "Done generating FPGA input data." << std::endl;
	// exit(0);

	int64_t kernel_time_ns = tapa::invoke(fysat, FLAGS_bitstream,
			tapa::read_only_mmaps<length,TPE_NUM>(ol_len_off).reinterpret<bits<tapa::vec_t<length, SDSIZE>>>(),
			tapa::read_only_mmaps<int,TPE_NUM>(ClauseList).reinterpret<bits<tapa::vec_t<int, 16>>>(),
			tapa::read_only_mmaps<int,TPE_NUM>(VarsOccList).reinterpret<bits<tapa::vec_t<int, DSIZE>>>(),
			tapa::write_only_mmaps<int,TPE_NUM>(Result),
			numVars, numClauses, randseed, k, maxFlip, target_cycles
	);

	double exec_time = kernel_time_ns; 

	int tot_try_num = 0;
	int tot_flip_num = 0;	
	for(int t = 0; t < TPE_NUM; t++){
		int try_num = Result[t][0];
		int flip_num = Result[t][1];

		std::vector<int> VarsAssgmt(MAXNVAR+1, 0);
		for(int i = 1; i <= numVars; i++ ){
			VarsAssgmt[i] = Result[t][i+1];
		}
#ifdef ENABLE_DBG_UCBNUM
		std::cout << "dbg_max_ucb" << t << " : " << Result[t][numVars+2] << endl;
#endif

		bool issat = verify(numVars, numClauses, k, VarsAssgmt, cls_len_off, ClauseList[0]);

		if (issat) { // if (flipcnt[0] > 0) {
			std::cout << "Solver" << t << " found a solution at try " << try_num << ", flip " << flip_num << endl;
			for(int i = 1; i <= numVars; i++ ){
				std::cout << "v" << i << ":" << VarsAssgmt[i] << ", ";
			}
			std::cout << std::endl;
		}
		else {
			std::cout << "Solver" << t << " could not find a solution.\n";
		}
		tot_try_num += try_num;
		tot_flip_num += (try_num-1)*maxFlip + flip_num;
	}

	double Mfs = (double)tot_flip_num / (exec_time * 1e-9);

	std::cout << "FPGA ExecTime: " << std::setw(19) << exec_time*1e-9 << std::endl;
	std::cout << "Tot tries: " << std::setw(14) << tot_try_num << std::endl;
	std::cout << "Tot flips: " << std::setw(14) << tot_flip_num << std::endl;
	std::cout << "Flips/s: " << std::setw(16) << Mfs << std::endl;


	return EXIT_SUCCESS;
}
