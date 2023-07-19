#include "xcl2.hpp"
#include <algorithm>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include <unistd.h>

#include "wsat_hls.h"

#define DSSIZE_C 1500000000
#define DSSIZE 100000000
#define MAX_HBM_PC_COUNT 32
#define PC_NAME(n) n | XCL_MEM_TOPOLOGY
const int pc[MAX_HBM_PC_COUNT] = {
    PC_NAME(0),  PC_NAME(1),  PC_NAME(2),  PC_NAME(3),  PC_NAME(4),  PC_NAME(5),  PC_NAME(6),  PC_NAME(7),
    PC_NAME(8),  PC_NAME(9),  PC_NAME(10), PC_NAME(11), PC_NAME(12), PC_NAME(13), PC_NAME(14), PC_NAME(15),
    PC_NAME(16), PC_NAME(17), PC_NAME(18), PC_NAME(19), PC_NAME(20), PC_NAME(21), PC_NAME(22), PC_NAME(23),
    PC_NAME(24), PC_NAME(25), PC_NAME(26), PC_NAME(27), PC_NAME(28), PC_NAME(29), PC_NAME(30), PC_NAME(31)};

short varR_off[MAXNLIT];

bool verify(int numClauses, std::vector<char, aligned_allocator<char>>& vaArr, 
			std::vector<clength, aligned_allocator<clength>>& cls_len_off, 
			std::vector<int, aligned_allocator<int>>& ClauseList) {
	
	for (int c = 1; c <= numClauses; c++) {
		bool cls = false;
		clength clen = cls_len_off[c];
		
		for (int i = 0; i < clen; i++) {
			int lit = ClauseList[c * MAXK + i];
			bool tval = vaArr[ABS(lit)] > 0;
			if (tval == (lit > 0)) cls = true;
		}
		if (cls == false) return 0;
	}

	return 1;
}

void init(std::vector<int, aligned_allocator<int>>& ClauseList, std::vector<int, aligned_allocator<int>>& VarsOccList, std::vector<int, aligned_allocator<int>>& VarsOccList_CPU, 
    std::vector<clength, aligned_allocator<clength>>& cls_len_off, std::vector<length, aligned_allocator<length>>& ol_len_off,
    std::string fileName, int& numVars, int& numClauses, int k, int r) {

	std::cout << "init starts\n";
    std::ifstream fileDIMACS(fileName);

	int maxk = 0;
	int maxr = 0;

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

		int nextLit;
		int clauseCounted = 1;

		char p;
		int nvinc = 0;	// #var in a clause
		while (!fileDIMACS.eof()) {
			fileDIMACS.ignore(256, '\n');
			p = fileDIMACS.peek();
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

				// inside clause line
				while (true) {
					fileDIMACS >> nextLit;

					int idx = clauseCounted * k + nvinc;
					ClauseList[idx] = nextLit;

					if (nextLit == 0){	// end of line
						if (nvinc > maxk) {
							maxk = nvinc;
						}
						cls_len_off[clauseCounted] = nvinc;
						nvinc = 0;
						clauseCounted++;
						break;
					}
					nvinc++;

					for (int i = 0; i < r; i++) {
						if (VarsOccList_CPU[GETPOS(nextLit) * r * DSIZE + i] == 0) {
							VarsOccList_CPU[GETPOS(nextLit) * r * DSIZE + i] = clauseCounted;

							varR_off[GETPOS(nextLit)] = i + 1;
							if (i + 1 > maxr) {
								maxr = i + 1;
							}
							break;
						}
						if (i == r - 1) {
							std::cout << "r excessed at " << nextLit << "\n";
						}
					}
				}
			}
		}

		if (clauseCounted - 1 != numClauses) {
			std::cout << "#clauses error || written: " << numClauses << " counted: " << clauseCounted - 1 << "\n";
		}

	} else {
		std::cerr << "Cannot open file: " << fileName << "\n";
	}

	ol_len_off[0] = 0;

	for (int v = 0; v < numVars * 2; v++) {
		std::vector<int> waitlist[DSIZE];

		int rlen = varR_off[v];
		int start = ol_len_off[v];

		for (int i = 0; i < rlen; i++) {
			int loc = VarsOccList_CPU[v * r * DSIZE + i]; 
			waitlist[loc % DSIZE].push_back(loc);
		}

		for (int t = 0; t < r; t++) {	// all lines
			bool finished = true;

			for (int i = 0; i < DSIZE; i++) {	// waitlist one line
				int idx = start + t;
				if (not waitlist[i].empty()) {
					finished = false;

					VarsOccList[idx * DSIZE + i] = waitlist[i][0];
					waitlist[i].erase(waitlist[i].begin());
				}
				else {
					VarsOccList[idx * DSIZE + i] = 0;
				}
			}
			if (finished) {
				ol_len_off[v + 1] = start + t;
				break;
			}
		}
	}
	std::cout << "numvars: " << numVars << " numcls: " << numClauses << " maxk: " << maxk << " maxr: " << maxr << "\n";
}

int main(int argc, char** argv) {
    if (argc != 7) {
        std::cout << "Usage: " << argv[0] << " <XCLBIN File> <fileName> <seed> <maxFlips> <k> <r>" << std::endl;
        return EXIT_FAILURE;
    }

	int randseed, k, r;
	unsigned long long maxFlip;
   
    std::string binaryFile = argv[1];

	char the_path[256];
	getcwd(the_path, 255);
	strcat(the_path, "/../test/");
	strcat(the_path, argv[2]);

    std::string fileName(the_path);
	std::string ss(argv[3]);
	std::string fstr(argv[4]);
	std::string kstr(argv[5]);
	std::string rstr(argv[6]);
	
	randseed = stoi(ss);
	if (randseed == 0) {randseed = time(0);}

	maxFlip = stoi(fstr);
	if (maxFlip == 0) {maxFlip = MAXFLIP;}

	k = stoi(kstr);
	if (k == 0) {k = MAXK;}

	r = stoi(rstr);
	if (r == 0) {r = MAXR;}

	srand(randseed);

	auto estart = std::chrono::steady_clock::now();

    cl_int err;
    cl::Context context;
    cl::Kernel krnl_yalsat;
    cl::CommandQueue q;
    
    std::vector<int, aligned_allocator<int>> ClauseList(DSSIZE, 0);
	std::vector<int, aligned_allocator<int>> VarsOccList(DSSIZE, 0);
	std::vector<int, aligned_allocator<int>> VarsOccList_CPU(DSSIZE_C, 0);
	
	std::vector<clength, aligned_allocator<clength>> cls_len_off(MAXNCLS, 0);
	std::vector<length, aligned_allocator<length>> ol_len_off(MAXNLIT, 0);
	
	std::vector<unsigned long long, aligned_allocator<unsigned long long>> flipcnt(1, 0);
	std::vector<char, aligned_allocator<char>> VarsAssgmt(MAXNLIT, 0);

    int numVars, numClauses;

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

    std::cout << fileName << " seed: " << randseed << " k: " << k << " max occ: " << r << "\n";
    init(ClauseList, VarsOccList, VarsOccList_CPU, cls_len_off, ol_len_off, fileName, numVars, numClauses, k, r);

	printf("Done initializing vectors\n");
	// exit(0);

    auto devices = xcl::get_xil_devices();
    auto fileBuf = xcl::read_binary_file(binaryFile);
    cl::Program::Binaries bins{{fileBuf.data(), fileBuf.size()}};
    bool valid_device = false;
    for (unsigned int i = 0; i < devices.size(); i++) {
        auto device = devices[i];
        // Creating Context and Command Queue for selected Device
        OCL_CHECK(err, context = cl::Context(device, nullptr, nullptr, nullptr, &err));
        OCL_CHECK(err, q = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err));
        std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
        cl::Program program(context, {device}, bins, nullptr, &err);
        if (err != CL_SUCCESS) {
            std::cout << "Failed to program device[" << i << "] with xclbin file!\n";
        } else {
            std::cout << "Device[" << i << "]: program successful!\n";
            OCL_CHECK(err, krnl_yalsat = cl::Kernel(program, "fysat", &err));
            valid_device = true;
            break; // we break because we found a valid device
        }
    }
    if (!valid_device) {
        std::cout << "Failed to program any device found, exit!\n";
        exit(EXIT_FAILURE);
    }
	
    OCL_CHECK(err, cl::Buffer b_ol_len_off(context, CL_MEM_EXT_PTR_XILINX | CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(length)* MAXNLIT, &ol_len_buf[0], &err));
    OCL_CHECK(err, cl::Buffer b_cls_len_off(context, CL_MEM_EXT_PTR_XILINX | CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(clength)* MAXNCLS, &cls_len_buf[0], &err));
    OCL_CHECK(err, cl::Buffer b_CL(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(int)* DSSIZE, ClauseList.data(), &err));
    OCL_CHECK(err, cl::Buffer b_VOL(context, CL_MEM_USE_HOST_PTR | CL_MEM_READ_ONLY, sizeof(int)* DSSIZE, VarsOccList.data(), &err));
	
	OCL_CHECK(err, cl::Buffer b_flipcnt(context, CL_MEM_EXT_PTR_XILINX | CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, sizeof(unsigned long long)*1, &flipcnt_buf[0], &err));
	OCL_CHECK(err, cl::Buffer b_answer(context, CL_MEM_EXT_PTR_XILINX | CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, sizeof(char)* MAXNLIT, &answer_buf[0], &err));

    OCL_CHECK(err, err = krnl_yalsat.setArg(0, b_ol_len_off));
    OCL_CHECK(err, err = krnl_yalsat.setArg(1, b_cls_len_off));
    OCL_CHECK(err, err = krnl_yalsat.setArg(2, b_CL));
    OCL_CHECK(err, err = krnl_yalsat.setArg(3, b_VOL));
    OCL_CHECK(err, err = krnl_yalsat.setArg(4, numVars));
    OCL_CHECK(err, err = krnl_yalsat.setArg(5, numClauses));
    OCL_CHECK(err, err = krnl_yalsat.setArg(6, randseed));
	OCL_CHECK(err, err = krnl_yalsat.setArg(7, k));
	OCL_CHECK(err, err = krnl_yalsat.setArg(8, maxFlip));
	OCL_CHECK(err, err = krnl_yalsat.setArg(9, b_flipcnt));
	OCL_CHECK(err, err = krnl_yalsat.setArg(10, b_answer));

	OCL_CHECK(err, err = q.enqueueMigrateMemObjects({b_ol_len_off, b_cls_len_off, b_CL, b_VOL}, 0 /* 0 means from host*/));
	q.finish();
	
	std::cout << "Running FPGA...\n";
	auto start = std::chrono::steady_clock::now();

	OCL_CHECK(err, err = q.enqueueTask(krnl_yalsat));
	q.finish();

	auto end = std::chrono::steady_clock::now();
	std::cout << "Done.\n";
	double exec_time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
	double exec_time_ext = std::chrono::duration_cast<std::chrono::nanoseconds>(end - estart).count();
	unsigned long long flips;

	OCL_CHECK(err, err = q.enqueueMigrateMemObjects({b_flipcnt, b_answer}, CL_MIGRATE_MEM_OBJECT_HOST));
	q.finish();

	bool issat = verify(numClauses, VarsAssgmt, cls_len_off, ClauseList);

	if (issat) { // if (flipcnt[0] > 0) {
		std::cout << "Solver found a solution.\n";
		flips = flipcnt[0];
	}
	else {
		std::cout << "Solver could not find a solution.\n";
		flips = maxFlip;
	}


	double Mfs = (double)flips / (exec_time * 1e-9);

	std::cout << "Time: " << std::setw(19) << exec_time*1e-9 << std::endl;
	std::cout << "HW + CPU Time: " << std::setw(10) << exec_time_ext*1e-9 << std::endl;
	std::cout << "Flips: " << std::setw(18) << flips << std::endl;
	std::cout << "flips/s: " << std::setw(16) << Mfs << std::endl;

	return EXIT_SUCCESS;
}
