#include "wsat_hls.h"

extern "C" {

inline int psrandom(int& seed) {
	ap_uint<32> lfsr = seed;
	bool b_32 = lfsr.get_bit(32-32);
	bool b_22 = lfsr.get_bit(32-22);
	bool b_2 = lfsr.get_bit(32-2);
	bool b_1 = lfsr.get_bit(32-1);
	bool new_bit = b_32 ^ b_22 ^ b_2 ^ b_1;
	lfsr = lfsr >> 1;
	lfsr.set_bit(31, 0);
	lfsr.set_bit(30, new_bit);
	seed = lfsr.to_int();
	return lfsr.to_int();
}

inline int sliceRand(int r1, int r2, int num) {
	if (num == 1) return 0;

	int two_iplus1 = 2;
	int prev_i = 0;
	for (int i = 0; i < 20; i++) {
#pragma HLS PIPELINE II = 1
		if (num < two_iplus1) {
			prev_i = i;
			break;
		}
		two_iplus1 *= 2;
	}

	int p = two_iplus1 / 2 - 1;
	int ret = (r2 % 2) ? (num - 1) - (r1 & p) : (r1 & p);
	return ret;
}

struct upd_bv {
	int vidx;
	int val;
};

struct upd_bv_arr {
	int vidx[DSIZE];
	bscore val[DSIZE];
};

struct num_line {
	int vidx;
	int l1start;
	int l1size;
	int l2start;
	int l2size;
};

struct rsp_ol {
	int oidx[DSIZE];
};

hls::stream<upd_bv> upd_m2b;
hls::stream<upd_bv_arr> upd_m2b_arr;

hls::stream<int> clen_c2m;
hls::stream<int> vars_c2m;

hls::stream<int> req_m2c;
hls::stream<int> rsp_c2b;
hls::stream<int> rsp_b2m;

hls::stream<num_line> req_m2l;
hls::stream<rsp_ol> rsp_l2m_arr;
hls::stream<rsp_ol> rsp_l2m_arr_f;

hls::vector<int, DSIZE> ClauseList_c[MAXNCLS * (MAXK / DSIZE)];
hls::vector<int, DSIZE> VarsOccList_c[410000];

cls tl_XORed_partd_c[MAXNCLS / DSIZE + 1][DSIZE];	// XORed true literals
cost cost_partd_c[MAXNCLS / DSIZE + 1][DSIZE];

int numOfUCs = 0;
cls UCB_partd_c[UCBSIZE / DSIZE + 1][DSIZE];
ucbidx posInUCB_c[MAXNCLS / DSIZE + 1][DSIZE];	// last line garbage @@@@@@@@
cls UCB_partd_len_c[DSIZE];

bscore bsArr_c[MAXNVAR + 1];		// break score
bscore bs_dif_partd_c[MAXNVAR + 1][DSIZE];

clength cls_len_c[MAXNCLS + 1];
length ol_len_c[MAXNLIT];
bool vaArr_c[MAXNVAR + 1];	// variable assignment	-> ap_uint<1>

int Ncls, Nvar, seed;
int seed_bs;
unsigned long long maxFlip;
int S, K;

int bs2probs_c[100] = {40710, 8734, 3655, 1985, 1240, 846, 612, 463, 362, 291, 238, 199, 168, 144, 125, 109, 96, 86, 76, 69,
			62, 56, 51, 47, 43, 40, 37, 34, 32, 29, 27, 26, 24, 23, 21, 20, 19, 18, 17, 16,
			15, 14, 14, 13, 12, 12, 11, 11, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 7, 7,
			6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3,
			3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2};


void forcsim(hls::vector<length, SDSIZE> ol_len_off[], hls::vector<clength, CDSIZE> cls_len_off[],
		hls::vector<int, DSIZE> ClauseList[], hls::vector<int, DSIZE> VarsOccList[], int numClauses, int numVars, unsigned long long mmaxFlip) {
	
	for (int i = 0; i < 410000; i++) {
		for (int j = 0; j < DSIZE; j++) {
			VarsOccList_c[i][j] = VarsOccList[i][j];
		}
	}

	for (int i = 0; i < MAXNCLS * K / DSIZE; i++) {
		for (int j = 0; j < DSIZE; j++) {
			ClauseList_c[i][j] = ClauseList[i][j];
		}
	}

	int maxcnt = (numClauses - 1) / CDSIZE + 1;
	for (int i = 0; i <= maxcnt; i++) {
		hls::vector<clength, CDSIZE> t = cls_len_off[i];
		for (int j = 0; j < CDSIZE; j++) {
			cls_len_c[i * CDSIZE + j] = t[j];
		}
	}

	copy_ol_len: for (int i = 0; i <= numVars * 2 / SDSIZE + 1; i++) {
#pragma HLS PIPELINE II = 1
		hls::vector<length, SDSIZE> t = ol_len_off[i];
		for (int j = 0; j < SDSIZE; j++) {
			ol_len_c[i * SDSIZE + j] = t[j];
		}
	}

	for (int i = 0; i <= MAXNVAR; i++) {
		for (int j = 0; j < DSIZE; j++) {
			bs_dif_partd_c[i][j] = 0;
		}
	}

	maxFlip = mmaxFlip;

	for (int i = 1; i <= numClauses; i++) {
		tl_XORed_partd_c[i / DSIZE][i % DSIZE] = 0;
	}


//	for (int i = 1; i <= numClauses; i++) {
//		std::cout << cls_len_c[i] << " ";
//		if (i % 16 == 0) std::cout << "\n";
// 	}

//
//	for (int i = 0; i <= numClauses; i++) {
//		std::cout << cls_len_off[i] << " | ";
//		for (int j = 0; j < cls_len_off[i]; j++) {
//			std::cout << ClauseList[i * K + j] << " ";
//		}
//		std::cout << "\n";
//	}
//
////
//	for (int v = 0; v < numVars * 2; v++) {
//		std::cout << ol_len_off[v / SDSIZE][v % SDSIZE] << " | ";
//		for (int j = ol_len_off[v / SDSIZE][v % SDSIZE]; j < ol_len_off[(v + 1) / SDSIZE][(v + 1) % SDSIZE]; j++) {
//			for (int k = 0; k < DSIZE; k++) {
//				std::cout << VarsOccList[j][k] << " ";
//			}
//		}
//		std::cout << "\n";
//	}


}

void clean() {
	while(!upd_m2b.empty()) { upd_m2b.read(); }
	while(!upd_m2b_arr.empty()) { upd_m2b_arr.read(); }
	while(!clen_c2m.empty()) { clen_c2m.read(); }
	while(!vars_c2m.empty()) { vars_c2m.read(); }
	while(!req_m2c.empty()) { req_m2c.read(); }
	while(!rsp_c2b.empty()) { rsp_c2b.read(); }
	while(!rsp_b2m.empty()) { rsp_b2m.read(); }
	while(!req_m2l.empty()) { req_m2l.read(); }
	while(!rsp_l2m_arr.empty()) { rsp_l2m_arr.read(); }
	while(!rsp_l2m_arr_f.empty()) { rsp_l2m_arr_f.read(); }

	for (int i = 0; i < 16; i++) {
		UCB_partd_len_c[i] = 0;
	}
	numOfUCs = 0;
}

bool verify() {
	bool result = true;
	verify_loop: for (int c = 1; c <= Ncls; c++) {
		bool cls = false;

		int clen = cls_len_c[c];
		int num_blk = ((clen - 1) / DSIZE) + 1;

		int issat = false;

		c_1line: for (int b = 0; b < num_blk; b++) {
			hls::vector<int, DSIZE> t = ClauseList_c[c * (K / DSIZE) + b];
			int size = (b == num_blk - 1) ? ((clen - 1) % DSIZE) + 1 : DSIZE;

			c_send: for (int i = 0; i < size; i++) {
				int lit = t[i];
				if (vaArr_c[ABS(lit)] == (lit > 0)) cls = true;
			}
		}
	}

	return result;
}

void mod_loc() {
	num_line ll = req_m2l.read();
	if (ll.vidx == 0) {
		return;
	}

	bool dir = ll.vidx < 0;
	int idx = ll.vidx < 0 ? ll.vidx : -ll.vidx;
	int stadd = ll.l1start < ll.l2start ? ll.l1start : ll.l2start;

	int i = 0;
	loc_req: while (i < ll.l1size + ll.l2size) {
#pragma HLS PIPELINE II = 1
		if (!rsp_l2m_arr.full() && !rsp_l2m_arr_f.full()) {

			hls::vector<int, DSIZE> rspol = VarsOccList_c[stadd + i];
			rsp_ol temp;
			loc_1line1: for (int j = 0; j < DSIZE; j++) {
#pragma HLS UNROLL
				temp.oidx[j] = rspol[j];

				if (temp.oidx[j] < 0 || temp.oidx[j] > Ncls) {
					std::cout << temp.oidx[j] << "temp.oidx[j] err" << std::endl;
					exit(0);
				}
			}

			if (dir) {
				if (i < ll.l1size) {
					rsp_l2m_arr.write(temp);
				}
				else {
					rsp_l2m_arr_f.write(temp);
				}
			}
			else {
				if (i < ll.l2size) {
					rsp_l2m_arr_f.write(temp);
				}
				else {
					rsp_l2m_arr.write(temp);
				}
			}

			i++;
		}
	}
}

void mod_cls_1() {

//	copy_cls_len: for (int i = 0; i <= Ncls; i++) {
//		if (i % 16 == 0) {
//			std::cout << "\n";
//		}
//		std::cout << cls_len_c[i] << " ";
//	}
//
//	std::cout << std::endl;

	for (int c = 1; c <= Ncls; c++) {		//// tot ch fin
		int clen = cls_len_c[c];
		int num_blk = ((clen - 1) / DSIZE) + 1;
		clen_c2m.write(clen);

		c_1line: for (int b = 0; b < num_blk; b++) {
			hls::vector<int, DSIZE> t = ClauseList_c[c * (K / DSIZE) + b];
			int size = (b == num_blk - 1) ? ((clen - 1) % DSIZE) + 1 : DSIZE;

			c_send: for (int i = 0; i < size; i++) {
				vars_c2m.write(t[i]);
			}
		}
	}
}
void mod_cls_2() {
	int cidx = req_m2c.read();
	if (cidx == -1) {
		rsp_c2b.write(-1);
		return;
	}

	int clen = cls_len_c[cidx];
	int num_blk = ((clen - 1) / DSIZE) + 1;
	
	rsp_c2b.write(clen);

	int cnt = 0;
	uc_send: for (int b = 0; b < num_blk; b++) {
		hls::vector<int, DSIZE> t = ClauseList_c[cidx * (K / DSIZE) + b];
		int size = (b == num_blk - 1) ? ((clen - 1) % DSIZE) + 1 : DSIZE;
	

		int i = 0;
		while (i < size) {
			if (!rsp_c2b.full()) {
				rsp_c2b.write(t[i]);
				if (vaArr_c[ABS(t[i])] == (t[i] > 0)) {
					cnt++;
				}

				i++;
			}
		}
	}

	if (cnt != 0) {
		std::cout << cidx << " " << cnt << "cost err" << std::endl;
		exit(0);
	}
}

void mod_break_1() {

	var: for (int i = 0; i <= MAXNVAR; i++) {
		bsArr_c[i] = 0;
	}

	while (true) {
		upd_bv init = upd_m2b.read();

		if (init.vidx == -1) { break; }
		bscore prev = bsArr_c[init.vidx];
		bsArr_c[init.vidx] = prev + (bscore)init.val;
	}
}

void mod_break_2() {
	int var_flip;

	int tempcls[K];
	int tempbrk[K];

	int probs[K];
	int sumProb = 0;

	int clen = rsp_c2b.read();
	if (clen == -1) return;

	lookup_break: for (int i = 0; i < clen; i++) {
		int vidx = rsp_c2b.read();
		int bv = bsArr_c[ABS(vidx)];

		int sum = 0;
		for (int s = 0; s < DSIZE; s++) {
			sum += bs_dif_partd_c[ABS(vidx)][s];
		}
		bv += sum;

		if (bv < 0 || bv > Ncls) {
			std::cout << vidx << " " << bv << " bs err " << std::endl;
		}

		int p = (bv >= 100 ? 1 : bs2probs_c[bv]);
		sumProb += p;
		probs[i] = sumProb;
		tempcls[i] = vidx;
	}

	int r8b = psrandom(seed_bs) & 255;
	int randPosition = r8b * (sumProb / 256);

	if (randPosition > 2100000000) {
		std::cout << randPosition << " randPosition exceeds 2b" << std::endl;
		exit(0);
	}

	choose_var: for (int i = 0; i < clen; i++) {
		if (probs[i] >= randPosition) {
			var_flip = tempcls[i];
			break;
		}
	}

	rsp_b2m.write(var_flip);

	upd_brk: for (int i = 0; i < clen; i++) {
		int var = tempcls[i];
		int bv = bsArr_c[ABS(var)];

		int sum = 0;
		for (int s = 0; s < DSIZE; s++) {
			sum += bs_dif_partd_c[ABS(var)][s];
			bs_dif_partd_c[ABS(var)][s] = 0;
		}

		//std::cout << var << "update " << bsArr_c[ABS(var)] << " = " << bv + sum << std::endl;
		bsArr_c[ABS(var)] = bv + sum;

		if (bsArr_c[ABS(var)] < 0 || bsArr_c[ABS(var)] > Ncls) {
			std::cout << var << " " << bsArr_c[ABS(var)] << " bs err2 " << std::endl;
			exit(0);
		}
	}
}

void mod_break_3() {

	upd_bv temp;
	int b1len, b2len;
	temp = upd_m2b.read();
	b1len = temp.val;
	temp = upd_m2b.read();
	b2len = temp.val;

	int b = 0;
	bv_upd_d1: while (b < b1len + b2len) {

		if (!upd_m2b_arr.empty()) {
#pragma HLS PIPELINE II = 1
			upd_bv_arr updline = upd_m2b_arr.read();

			d1_1line: for (int i = 0; i < DSIZE; i++) {
#pragma HLS UNROLL
				bscore upd = bs_dif_partd_c[updline.vidx[i]][i];
				if (updline.vidx[i] != -1) {
					upd = upd + updline.val[i];

					if (ABS(updline.val[i]) != 1) {
						std::cout << updline.val[i] << "updline.val[i] err" << std::endl;
						exit(0);
					}
				}
				bs_dif_partd_c[updline.vidx[i]][i] = upd;
			}

			b++;
		}
	}
	
	temp = upd_m2b.read();
	bscore prev = bsArr_c[temp.vidx];
	bsArr_c[temp.vidx] = prev + (bscore)temp.val;
}

inline void upd_l2m_arr(int var_flip, int b1len,
						int& ucbdec_tot, int& bvinc_tot) {
	
	int t = 0;
	cost_inc_loop: while (t < b1len) {
#pragma HLS pipeline II=1

		if (!rsp_l2m_arr.empty()) {
			rsp_ol ol_elem = rsp_l2m_arr.read();
			upd_bv_arr bvdec_1line;
			int ucbdec_1line = 0;
			int bvinc_1line = 0;

			loc_1line_1: for (int i = 0; i < DSIZE; i++) {
				int cn = ol_elem.oidx[i];
				int ucbdec = 0;
				int bvinc = 0;
				int tbvidx = -1;
				bscore tbvval = 0;

				if (cn > 0) {
					int row = cn / DSIZE;
					cost cost = cost_partd_c[row][i];

					if (cost > K) {
						std::cout << cost << " cost > K" << std::endl;
						exit(0);
					}
					
					int critv = tl_XORed_partd_c[row][i];

					if (cost == 0) {	// ucbdelete

						int row_ucb = UCB_partd_len_c[i];
						int outIdx = posInUCB_c[row][i];
						int replaceElem = UCB_partd_c[row_ucb - 1][i];

						UCB_partd_c[outIdx][i] = replaceElem;
						posInUCB_c[replaceElem / DSIZE][i] = outIdx;

						ucbdec = 1;

						bvinc = 1;								// bv++
						critv = ABS(var_flip);

						UCB_partd_len_c[i] = row_ucb - 1;


						if (UCB_partd_c[posInUCB_c[replaceElem / DSIZE][i]][i] != replaceElem) {
							std::cout << "ucbdelete err" << std::endl;
							exit(0);
						}

					} else if (cost == 1) {
						tbvidx = critv;

						if (tbvidx < 0) {
							std::cout << "l2marr tbvidx err" << std::endl;
							exit(0);
						}


						tbvval = -1;
						critv = critv ^ ABS(var_flip);

						int clen = cls_len_c[cn];
						int num_blk = ((clen - 1) / DSIZE) + 1;
						
						int cnt = 0;
						for (int b = 0; b < num_blk; b++) {
							hls::vector<int, DSIZE> t = ClauseList_c[cn * (K / DSIZE) + b];
							int size = (b == num_blk - 1) ? ((clen - 1) % DSIZE) + 1 : DSIZE;						// mod 2

							
							for (int ss = 0; ss < size; ss++) {
								int lit = t[ss];
								if (vaArr_c[ABS(lit)] == (lit > 0)) {
									cnt++;

									if (ABS(lit) != tl_XORed_partd_c[row][i]) {
										std::cout << cn << " " << lit << " " << tl_XORed_partd_c[row][i] << "xor err" << std::endl;
										exit(0);
									}
								}
							}
							
						}
						if (cnt != 1) {
							std::cout << "cost 1 err" << std::endl;
							exit(0);
						}

					} else {
						critv = critv ^ ABS(var_flip);
					}

					tl_XORed_partd_c[row][i] = (cls)critv;
					cost_partd_c[row][i] = cost + 1;

				}
				ucbdec_1line += ucbdec;
				bvinc_1line += bvinc;
				bvdec_1line.vidx[i] = tbvidx;
				bvdec_1line.val[i] = tbvval;
			}
			upd_m2b_arr.write(bvdec_1line);

			ucbdec_tot += ucbdec_1line;
			bvinc_tot += bvinc_1line;

			if (ucbdec_1line > DSIZE || bvinc_1line > DSIZE) {
				std::cout << ucbdec_1line << " ucbdec_1line > 16 " << bvinc_1line << " bvinc_1line > 16" << std::endl;
				exit(0);
			}
	
			t++;
		}
	}
}

inline void upd_l2m_arr_f(	int var_flip, int b2len,
							int& ucbinc_tot, int& bvdec_tot) {

	int t = 0;
	cost_dec_loop: while (t < b2len) {
#pragma HLS pipeline II=1

		if (!rsp_l2m_arr_f.empty()) {
			rsp_ol ol_elem = rsp_l2m_arr_f.read();
			upd_bv_arr bvinc_1line;
			int ucbinc_1line = 0;
			int bvdec_1line = 0;

			loc_1line_2: for (int i = 0; i < DSIZE; i++) {
				int cn = ol_elem.oidx[i];
				int ucbinc = 0;
				int bvdec = 0;
				int tbvidx = -1;
				int tbvval = 0;

				if (cn > 0) {
					int row = cn / DSIZE;
					int critv = tl_XORed_partd_c[row][i];
					cost cost = cost_partd_c[row][i];
					
					if (cost > K || cost == 0) {
						std::cout << cost << " cost > K" << std::endl;
						exit(0);
					}

					tl_XORed_partd_c[row][i] = (cls)(critv ^ ABS(var_flip));

					if (cost == 1) {
						ucbinc = 1;
						bvdec = 1;		// bv--

						cls row_ucb = UCB_partd_len_c[i];
						UCB_partd_c[row_ucb][i] = cn;
						posInUCB_c[row][i] = row_ucb;
						UCB_partd_len_c[i] = row_ucb + 1;

						if (UCB_partd_c[UCB_partd_len_c[i] - 1][i] != cn) {
							std::cout << "ucbinsert err" << std::endl;
							exit(0);
						}

						int clen = cls_len_c[cn];
						int num_blk = ((clen - 1) / DSIZE) + 1;
						int cnt = 0;

						for (int b = 0; b < num_blk; b++) {
							hls::vector<int, DSIZE> t = ClauseList_c[cn * (K / DSIZE) + b];
							int size = (b == num_blk - 1) ? ((clen - 1) % DSIZE) + 1 : DSIZE;						// mod 2

							
							for (int ss = 0; ss < size; ss++) {
								int lit = t[ss];
								if (vaArr_c[ABS(lit)] == (lit > 0)) {
									cnt++;

									if (ABS(lit) != critv) {
										std::cout << cn << " " <<  lit << " " << critv << "xor err2 " << std::endl;
										exit(0);
									}
								}
							}
							
						}
						if (cnt != 1) {
							std::cout << "cost 1 err" << std::endl;
							exit(0);
						}

					}
					else if (cost == 2) {
						tbvidx = critv ^ ABS(var_flip);
						if (tbvidx < 0) {
							std::cout << "tbvidx err" << std::endl;
							exit(0);
						}

						tbvval = +1;
					}

					cost_partd_c[row][i] = cost - 1;
				}
				ucbinc_1line += ucbinc;
				bvdec_1line += bvdec;
				bvinc_1line.vidx[i] = tbvidx;
				bvinc_1line.val[i] = tbvval;
			}
			upd_m2b_arr.write(bvinc_1line);

			ucbinc_tot += ucbinc_1line;
			bvdec_tot += bvdec_1line;

			if (ucbinc_1line > DSIZE || bvdec_1line > DSIZE) {
				std::cout << ucbinc_1line << " ucbinc_1line > 16 " << bvdec_1line << " bvdec_1line > 16" << std::endl;
				exit(0);
			}

			t++;
		}
	}
}

void mod_main(int numVars, int numClauses, int s, unsigned long long flipcnt[]) {

	std::cout << "main starts" << std::endl;

	unsigned long long flips = 0;
	Ncls = numClauses;
	Nvar = numVars;
	seed = s;
	seed_bs = s;

	var: for (int i = 0; i <= Nvar; i++) {
		//vaArr_c[i] = psrandom(seed) % 2 == 0 ? true : false;
		vaArr_c[i] = i % 2 == 0 ? true : false;
	}

	for (int i = 0; i < DSIZE; i++) {
		UCB_partd_len_c[i] = 0;
	}

	/////////////////////// [[  INIT  ]] //////////////////////////

	mod_cls_1();

	for (cls c = 1; c <= Ncls; c++) {		//// tot ch fin
		int num_blk = clen_c2m.read();
		cost totcost = 0;
		int tl = 0;

		for (int i = 0; i < num_blk; i++) {
			int lit = vars_c2m.read();
			if (vaArr_c[ABS(lit)] == (lit > 0)) {
				totcost++;
				tl = tl ^ ABS(lit);
			}
		}

		if (totcost == 0) {
			int col_ucb = c % DSIZE;
			cls row_ucb = UCB_partd_len_c[col_ucb];
			UCB_partd_c[row_ucb][col_ucb] = c;
			posInUCB_c[c / DSIZE][col_ucb] = row_ucb;

			numOfUCs++;
			UCB_partd_len_c[col_ucb]++;

		}
		else if (totcost == 1) {
			upd_bv upd;
			upd.vidx = tl;
			upd.val = +1;
			upd_m2b.write(upd);
		}


		int row = c / DSIZE;
		int col = c % DSIZE;
		tl_XORed_partd_c[row][col] = (cls)tl;
		cost_partd_c[row][col] = totcost;
	}

	upd_bv upd;
	upd.vidx = -1;
	upd_m2b.write(upd);

	std::cout << "init fin" << std::endl;

	mod_break_1();

	clock_t start = clock();

	int rand1, rand2;
	rand1 = psrandom(seed);
	rand2 = psrandom(seed);

	// for (int i = 1; i <= Ncls; i++) {
	// 	if (i % 25 == 0) std::cout << std::endl;
	// 	if (cost_partd_c[i / DSIZE][i % DSIZE] == 0) {
	// 		std::cout << posInUCB_c[i / DSIZE][i % DSIZE] << "! ";
	// 	}
	// 	else {
	// 		std::cout << tl_XORed_partd_c[i / DSIZE][i % DSIZE] << " ";
	// 	}
	// 	// std::cout << (int)cost_partd_c[i / DSIZE][i % DSIZE] << " ";
	// }

	flip: for (unsigned long long f = 0; f < maxFlip; f++) {
		if (f % 100000 == 0) {
			std::cout << f << " " << numOfUCs << std::endl;
		}

		if (numOfUCs == 0) {
			flips = f;
			break;
		}

		ap_uint<4> uccol = rand1 % DSIZE;
		while (UCB_partd_len_c[uccol] == 0) {
			uccol++;
		}

		int ucrow = sliceRand(rand1, rand2, (int)UCB_partd_len_c[uccol]);
		int ucnum = UCB_partd_c[ucrow][uccol];

		if (ucrow >= UCB_partd_len_c[uccol]) {
			std::cout << f << " ucrow " << ucrow << " error" << std::endl;
			exit(0);
		}

		if (ucnum <= 0) {
			std::cout << f << " ucnum " << ucnum << " < 0" << std::endl;
			exit(0);
		}

		req_m2c.write(ucnum);

		rand1 = psrandom(seed);
		rand2 = psrandom(seed);

		if (rand1 < 0 || rand2 < 0) {
			std::cout << f << " " << rand1 << " " << rand2 << " rand12 error" << std::endl;
			exit(0);
		}

		mod_cls_2();
		mod_break_2();

		int var_flip = rsp_b2m.read();
		int abs_var_flip = ABS(var_flip);

		if (ABS(var_flip) > numVars) {
			std::cout << f << " " << var_flip << " > numvars" << std::endl;
			exit(0);
		}

		/////////////////////// 2. IO ol req //////////////////////////

		int b1st = ol_len_c[GETPOS(var_flip)];
		int b1len = ol_len_c[GETPOS(var_flip) + 1] - b1st;
		int b2st = ol_len_c[GETPOS(-var_flip)];
		int b2len = ol_len_c[GETPOS(-var_flip) + 1] - b2st;

		num_line ltemp;
		ltemp.vidx = var_flip;
		ltemp.l1start = b1st;
		ltemp.l1size = b1len;
		ltemp.l2start = b2st;
		ltemp.l2size = b2len;
		req_m2l.write(ltemp);

		mod_loc();

		upd_bv temp_size;
		temp_size.val = b1len;
		upd_m2b.write(temp_size);
		temp_size.val = b2len;
		upd_m2b.write(temp_size);

		int ucbdec_tot = 0;
		int bvinc_tot = 0;

		int ucbinc_tot = 0;
		int bvdec_tot = 0;

		if (var_flip < 0) {
			upd_l2m_arr(var_flip, b1len, ucbdec_tot, bvinc_tot); 
			upd_l2m_arr_f(var_flip, b2len, ucbinc_tot, bvdec_tot); 
		}
		else {
			upd_l2m_arr_f(var_flip, b2len, ucbinc_tot, bvdec_tot); 
			upd_l2m_arr(var_flip, b1len, ucbdec_tot, bvinc_tot); 
		}

		upd_bv bv_last;
		bv_last.vidx = ABS(var_flip);
		bv_last.val = bvinc_tot - bvdec_tot;
		upd_m2b.write(bv_last);

		numOfUCs += ucbinc_tot;
		numOfUCs -= ucbdec_tot;

		vaArr_c[abs_var_flip] = 1 - vaArr_c[abs_var_flip];

		if (numOfUCs < 0) {
			std::cout << f << " " << numOfUCs << " < 0" << std::endl;
			exit(0);
		}

		mod_break_3();
	}

	req_m2c.write(-1);
	mod_cls_2();
	mod_break_2();

	num_line templ;
	templ.vidx = 0;
	req_m2l.write(templ);
	mod_loc();

	if (flips) {
		bool v = verify();
		if (v) { std::cout << "Solver found a solution. Verified. | seed: " << s << " | " << flips << " "; }
		else {
			std::cout << "Wrong solution. UCB count : " << numOfUCs << " | ";
		}
	}
	else {
		// std::cout << "Solver could not find a solution. | " << flips << " ";
	}

	clock_t end = clock();
	// std::cout << "Solver completed in: " << (double)(end - start)/CLOCKS_PER_SEC << " seconds | flip: " << flipcnt[0] << std::endl;
	// std::cout << upd_m2b.size() << upd_m2b_arr.size() << clen_c2m.size() << vars_c2m.size() << req_m2c.size() << rsp_c2b.size() << rsp_b2m.size() << req_m2l.size() << rsp_l2m_arr.size() << rsp_l2m_arr_f.size() << "\n";
	if (flips) {
		flipcnt[0] = flips;
	}
	else {
		flipcnt[0] = 0;
	}
	
}


void fysat(hls::vector<length, SDSIZE> ol_len_off[], hls::vector<clength, CDSIZE> cls_len_off[],
		hls::vector<int, DSIZE> ClauseList[], hls::vector<int, DSIZE> VarsOccList[],
		int numVars, int numClauses, int s, int k, unsigned long long maxFlip, unsigned long long flipcnt[]) {

	// ClauseList_c = new hls::vector<int, DSIZE> [MAXNCLS * (MAXK / DSIZE)];

	int sss = s;
	K = k;
	unsigned long long mmaxFlip = maxFlip;
	forcsim(ol_len_off, cls_len_off, ClauseList, VarsOccList, numClauses, numVars, mmaxFlip);
	mod_main(numVars, numClauses, s, flipcnt);
	clean();

	// delete[] ClauseList_c;
}
}