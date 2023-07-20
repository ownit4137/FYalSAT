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


void mod_cls (	hls::stream<int>& clen_c2m, hls::stream<int>& vars_c2m,
				hls::stream<int>& req_m2c, hls::stream<int>& rsp_c2b,
				hls::vector<int, DSIZE> ClauseList[], hls::vector<clength, CDSIZE> cls_len_off[], int numClauses, int k, unsigned long long maxFlip) {

	clength cls_len[MAXNCLS + 1];	//	 ~KMAX
#pragma HLS bind_storage variable=cls_len impl=uram type=RAM_2P

	int Ncls = numClauses;
	int maxcnt = (Ncls - 1) / CDSIZE + 1;

	copy_cls_len: for (int i = 0; i <= maxcnt; i++) {
#pragma HLS PIPELINE II = 1

		hls::vector<clength, CDSIZE> t = cls_len_off[i];
		for (int j = 0; j < CDSIZE; j++) {
			cls_len[i * CDSIZE + j] = t[j];
		}
	}

	for (int c = 1; c <= Ncls; c++) {		//// tot ch fin
		int clen = cls_len[c];
		int num_blk = ((clen - 1) / DSIZE) + 1;
		clen_c2m.write(clen);

		c_1line: for (int b = 0; b < num_blk; b++) {
			hls::vector<int, DSIZE> t = ClauseList[c * (k / DSIZE) + b];
			int size = (b == num_blk - 1) ? ((clen - 1) % DSIZE) + 1 : DSIZE;

			c_send: for (int i = 0; i < size; i++) {
				vars_c2m.write(t[i]);
			}
		}
	}

	flip_cls: for (unsigned long long f = 0; f < maxFlip; f++) {

		wait1: while (req_m2c.empty()) {}
		int cidx = req_m2c.read();
		if (cidx == -1) {
			rsp_c2b.write(-1);
			break;
		}

		int clen = cls_len[cidx];
		int num_blk = ((clen - 1) / DSIZE) + 1;

		wclen: while (rsp_c2b.full()) {}
		rsp_c2b.write(clen);

		wc: while (rsp_c2b.full()) {}
		uc_send: for (int b = 0; b < num_blk; b++) {
			hls::vector<int, DSIZE> t = ClauseList[cidx * (k / DSIZE) + b];
			int size = (b == num_blk - 1) ? ((clen - 1) % DSIZE) + 1 : DSIZE;						// mod 2

			uc_req_1line: for (int i = 0; i < size; i++) {
#pragma HLS PIPELINE II = 1
				rsp_c2b.write(t[i]);
			}
		}
	}
}

void mod_loc( hls::stream<num_line>& req_m2l, hls::stream<rsp_ol>& rsp_l2m_arr, hls::stream<rsp_ol>& rsp_l2m_arr_f,
			  hls::vector<int, DSIZE> VarsOccList[], unsigned long long maxFlip) {
		
	flip_loc: for (unsigned long long f = 0; f < maxFlip; f++) {

		num_line_wait: while (req_m2l.empty()) {}
		num_line ll = req_m2l.read();
		if (ll.vidx == 0) {
			break;
		}

		bool dir = ll.vidx < 0;
		int idx = ll.vidx < 0 ? ll.vidx : -ll.vidx;
		int stadd = ll.l1start < ll.l2start ? ll.l1start : ll.l2start;

		loc_req: for (int i = 0; i < ll.l1size + ll.l2size; i++) {
			hls::vector<int, DSIZE> rspol = VarsOccList[stadd + i];		// test
			rsp_ol temp;
			loc_1line1: for (int j = 0; j < DSIZE; j++) {
#pragma HLS UNROLL
				temp.oidx[j] = rspol[j];
			}

			wloc_wait: while (rsp_l2m_arr.full() || rsp_l2m_arr_f.full()) {}

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
		}
	}
}

void mod_break(	hls::stream<upd_bv>& upd_m2b, hls::stream<upd_bv_arr>& upd_m2b_arr,
				hls::stream<int>& rsp_c2b, hls::stream<int>& rsp_b2m, int s, int numVars, unsigned long long maxFlip) {
	
	bscore bsArr[MAXNVAR + 1];
#pragma HLS bind_storage variable=bsArr impl=uram type=RAM_2P
	bscore bs_dif_partd[MAXNVAR + 1][DSIZE];
#pragma HLS bind_storage variable=bs_dif_partd impl=uram type=RAM_2P
	int seed = s;

	int bs2probs[100] = {40710, 8734, 3655, 1985, 1240, 846, 612, 463, 362, 291, 238, 199, 168, 144, 125, 109, 96, 86, 76, 69,
				62, 56, 51, 47, 43, 40, 37, 34, 32, 29, 27, 26, 24, 23, 21, 20, 19, 18, 17, 16,
				15, 14, 14, 13, 12, 12, 11, 11, 10, 10, 9, 9, 9, 8, 8, 8, 7, 7, 7, 7,
				6, 6, 6, 6, 6, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3,
				3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2};


	/////////////////////// [[  INIT  ]] //////////////////////////

	init_brv_0: for (int i = 0; i <= numVars; i++) {
#pragma HLS PIPELINE II = 1
		bsArr[i] = 0;
		for (int j = 0; j < DSIZE; j++) {
			bs_dif_partd[i][j] = 0;
		}
	}

	init_brv: while (true) {
		upd_bv init = upd_m2b.read();

		if (init.vidx == -1) { break; }
		bscore prev = bsArr[init.vidx];
		bsArr[init.vidx] = (bscore)init.val + prev;		// +1 or -1 // II = 3
	}

	/////////////////////// [[  FLIP  ]] //////////////////////////
	flip_brk: for (unsigned long long f = 0; f < maxFlip; f++) {

		/////////////////////// [[  BV RSP  ]] //////////////////////////

		int var_flip;

		int tempcls[MAXK];
		int tempbrk[MAXK];

		int probs[MAXK];
		int sumProb = 0;

		rsp_c2b_wait: while(rsp_c2b.empty()) {}
		int clen = rsp_c2b.read();
		if (clen == -1) break;

		lookup_break: for (int i = 0; i < clen; i++) {
#pragma HLS PIPELINE II = 1
			int vidx = rsp_c2b.read();
			int bv = bsArr[ABS(vidx)];

			int sum = 0;
			for (int s = 0; s < DSIZE; s++) {
#pragma HLS UNROLL
				sum += bs_dif_partd[ABS(vidx)][s];
			}
			bv += sum;

			int p = bv >= 100 ? 1 : bs2probs[bv];
			sumProb += p;
			probs[i] = sumProb;
			tempcls[i] = vidx;
		}

		int r8b = psrandom(seed) & 255;
		int randPosition = r8b * sumProb / 256;

		choose_var: for (int i = 0; i < clen; i++) {
#pragma HLS PIPELINE II = 1
			if (probs[i] >= randPosition) {
				var_flip = tempcls[i];
				break;
			}
		}

		wvflip_wait: while (rsp_b2m.full()) {}
		rsp_b2m.write(var_flip);

		upd_brk: for (int i = 0; i < clen; i++) {
			int var = tempcls[i];
			int bv = bsArr[ABS(var)];

			int sum = 0;
			upd_sum: for (int s = 0; s < DSIZE; s++) {
#pragma HLS UNROLL
				sum += bs_dif_partd[ABS(var)][s];
				bs_dif_partd[ABS(var)][s] = 0;
			}

			bsArr[ABS(var)] = bv + sum;
		}

		/////////////////////// [[  BV UPD  ]] //////////////////////////

		upd_m2b_wait: while (upd_m2b.empty()) {}
		upd_bv temp;
		int b1len, b2len;
		temp = upd_m2b.read();
		b1len = temp.val;
		temp = upd_m2b.read();
		b2len = temp.val;

		upd_line_wait: while (upd_m2b_arr.empty()) {}
		bv_upd_d1: for (int b = 0; b < b1len + b2len; b++) {
#pragma HLS PIPELINE II = 1
			upd_bv_arr updline = upd_m2b_arr.read();

			d1_1line: for (int i = 0; i < DSIZE; i++) {
#pragma HLS UNROLL
				bscore upd = bs_dif_partd[updline.vidx[i]][i];
				if (updline.vidx[i] != -1) {
					upd = upd + updline.val[i];
				}
				bs_dif_partd[updline.vidx[i]][i] = upd;
			}
		}
		
		upd_1_wait: while (upd_m2b.empty()) {}
		temp = upd_m2b.read();
		bscore prev = bsArr[temp.vidx];
		bsArr[temp.vidx] = prev + (bscore)temp.val;
	}

}

inline void upd_l2m_arr(int var_flip, int b1len,
						hls::stream<rsp_ol>& rsp_l2m_arr, hls::stream<rsp_ol>& rsp_l2m_arr_f, hls::stream<upd_bv_arr>& upd_m2b_arr,
						cost cost_partd[][DSIZE], cls tl_XORed_partd[][DSIZE], 
 						cls UCB_partd_len[DSIZE], ucbidx posInUCB[][DSIZE], cls UCB_partd[][DSIZE],
						int& ucbdec_tot, int& bvinc_tot) {
	
	if (b1len > 0) {
		rinc_wait: while (rsp_l2m_arr.empty()) {}
	}
	cost_inc_loop: for (int t = 0; t < b1len; t++) {
#pragma HLS pipeline II=1
		rsp_ol ol_elem = rsp_l2m_arr.read();
		upd_bv_arr bvdec_1line;
		int ucbdec_1line = 0;
		int bvinc_1line = 0;

		for (int i = 0; i < DSIZE; i++) {
#pragma HLS UNROLL
			int cn = ol_elem.oidx[i];
			int ucbdec = 0;
			int bvinc = 0;
			int tbvidx = -1;
			bscore tbvval = 0;

			if (cn > 0) {
				int row = cn / DSIZE;
				cost cost = cost_partd[row][i];
				int critv = tl_XORed_partd[row][i];

				if (cost == 0) {	// ucbdelete
					int row_ucb = UCB_partd_len[i];
					int outIdx = posInUCB[row][i];
					int replaceElem = UCB_partd[row_ucb - 1][i];
					UCB_partd[outIdx][i] = replaceElem;
					posInUCB[replaceElem / DSIZE][i] = outIdx;

					ucbdec = 1;
					bvinc = 1;								// bv++
					critv = ABS(var_flip);

					UCB_partd_len[i] = row_ucb - 1;

				} else if (cost == 1) {
					tbvidx = critv;
					tbvval = -1;
					critv = critv ^ ABS(var_flip);
				} else {
					critv = critv ^ ABS(var_flip);
				}

				tl_XORed_partd[row][i] = (cls)critv;
				cost_partd[row][i] = cost + 1;
			}

			ucbdec_1line += ucbdec;
			bvinc_1line += bvinc;
			bvdec_1line.vidx[i] = tbvidx;
			bvdec_1line.val[i] = tbvval;
		}

		upd_wait: while (upd_m2b_arr.full()) {}
		upd_m2b_arr.write(bvdec_1line);

		ucbdec_tot += ucbdec_1line;
		bvinc_tot += bvinc_1line;
	}
}

inline void upd_l2m_arr_f(	int var_flip, int b2len,
							hls::stream<rsp_ol>& rsp_l2m_arr, hls::stream<rsp_ol>& rsp_l2m_arr_f, hls::stream<upd_bv_arr>& upd_m2b_arr,
							cost cost_partd[][DSIZE], cls tl_XORed_partd[][DSIZE], 
 							cls UCB_partd_len[DSIZE], ucbidx posInUCB[][DSIZE], cls UCB_partd[][DSIZE],
							int& ucbinc_tot, int& bvdec_tot) {
	
	if (b2len > 0) {
		rdec_wait: while (rsp_l2m_arr_f.empty()) {}
	}
	cost_dec_loop: for (int t = 0; t < b2len; t++) {
#pragma HLS pipeline II=1
		rsp_ol ol_elem = rsp_l2m_arr_f.read();
		upd_bv_arr bvinc_1line;
		int ucbinc_1line = 0;
		int bvdec_1line = 0;

		for (int i = 0; i < DSIZE; i++) {
#pragma HLS UNROLL
			int cn = ol_elem.oidx[i];
			int ucbinc = 0;
			int bvdec = 0;
			int tbvidx = -1;
			int tbvval = 0;

			if (cn > 0) {
				int row = cn / DSIZE;
				int critv = tl_XORed_partd[row][i];
				cost cost = cost_partd[row][i];
				
				tl_XORed_partd[row][i] = (cls)(critv ^ ABS(var_flip));

				if (cost == 1) {
					ucbinc = 1;
					bvdec = 1;												// bv--

					cls row_ucb = UCB_partd_len[i];
					UCB_partd[row_ucb][i] = cn;
					posInUCB[row][i] = row_ucb;
					UCB_partd_len[i] = row_ucb + 1; // row_aft;
				}
				else if (cost == 2) {
					tbvidx = critv ^ ABS(var_flip);
					tbvval = +1;
				}
				cost_partd[row][i] = cost - 1;		// can be moved
			}
			ucbinc_1line += ucbinc;
			bvdec_1line += bvdec;
			bvinc_1line.vidx[i] = tbvidx;
			bvinc_1line.val[i] = tbvval;
		}

		upd_f_wait: while (upd_m2b_arr.full()) {}
		upd_m2b_arr.write(bvinc_1line);

		ucbinc_tot += ucbinc_1line;
		bvdec_tot += bvdec_1line;
	}
}

void mod_main(	hls::stream<upd_bv>& upd_m2b, hls::stream<upd_bv_arr>& upd_m2b_arr,
				hls::stream<int>& clen_c2m, hls::stream<int>& vars_c2m,
				hls::stream<int>& req_m2c, hls::stream<int>& rsp_b2m,
				hls::stream<num_line>& req_m2l, hls::stream<rsp_ol>& rsp_l2m_arr, hls::stream<rsp_ol>& rsp_l2m_arr_f,

				hls::vector<length, SDSIZE> ol_len_off[],
				int numVars, int numClauses, int s, unsigned long long maxFlip, unsigned long long flipcnt[], hls::vector<char, 1> answer[]) {
	unsigned long long flips = 0;

	int Ncls = numClauses;
	int Nvar = numVars;
	int seed = s;

	cost cost_partd[MAXNCLS / DSIZE + 1][DSIZE];
	cls tl_XORed_partd[MAXNCLS / DSIZE + 1][DSIZE];
#pragma HLS ARRAY_PARTITION variable=tl_XORed_partd complete dim=2
#pragma HLS bind_storage variable=tl_XORed_partd impl=uram type=RAM_2P
#pragma HLS ARRAY_PARTITION variable=cost_partd complete dim=2
#pragma HLS bind_storage variable=cost_partd impl=uram type=RAM_2P


	int numOfUCs = 0;
	cls UCB_partd[UCBSIZE / DSIZE + 1][DSIZE];
	ucbidx posInUCB[MAXNCLS / DSIZE + 1][DSIZE];	// last line garbage @@@@@@@@
	cls UCB_partd_len[DSIZE];

#pragma HLS ARRAY_PARTITION variable=UCB_partd complete dim=2
#pragma HLS bind_storage variable=UCB_partd impl=uram type=RAM_2P
#pragma HLS ARRAY_PARTITION variable=posInUCB complete dim=2
#pragma HLS bind_storage variable=posInUCB impl=uram type=RAM_2P
#pragma HLS ARRAY_PARTITION variable=UCB_partd_len complete dim=1

	length ol_len[MAXNLIT];		// char
#pragma HLS bind_storage variable=ol_len impl=uram type=RAM_2P
	bool vaArr[MAXNVAR + 1];	// variable assignment	-> ap_uint<1>

	/////////////////////// [[  INIT  ]] //////////////////////////

	copy_ol_len: for (int i = 0; i <= Nvar * 2 / SDSIZE + 1; i++) {
#pragma HLS PIPELINE II = 1
		hls::vector<length, SDSIZE> t = ol_len_off[i];
		for (int j = 0; j < SDSIZE; j++) {
			ol_len[i * SDSIZE + j] = t[j];
		}
	}

	// for (int i = 0; i <= Nvar * 2; i++) {
	// 	if (i % 20 == 0) std::cout << "\n";
	// 	std::cout << ol_len[i] << " ";
	// }
	// std::cout << std::endl;

	init_var: for (int i = 0; i <= Nvar; i++) {
#pragma HLS PIPELINE II = 1
		vaArr[i] = psrandom(seed) % 2 == 0 ? true : false;
		// vaArr_c[i] = i % 2 == 0 ? true : false;
	}

	init_UCB_partd_len: for (int i = 0; i < DSIZE; i++) {
#pragma HLS UNROLL
		UCB_partd_len[i] = 0;
	}

	for (cls c = 1; c <= Ncls; c++) {		//// tot ch fin
		int num_blk = clen_c2m.read();
		cost totcost = 0;
		int tl = 0;

		for (int i = 0; i < num_blk; i++) {
#pragma HLS PIPELINE II = 1
			int lit = vars_c2m.read();
			if (vaArr[ABS(lit)] == (lit > 0)) {
				totcost++;
				tl = tl ^ ABS(lit);
			}
		}

		if (totcost == 0) {
			int col_ucb = c % DSIZE;
			cls row_ucb = UCB_partd_len[col_ucb];
			UCB_partd[row_ucb][col_ucb] = c;
			posInUCB[c / DSIZE][col_ucb] = row_ucb;

			numOfUCs++;
			UCB_partd_len[col_ucb]++;
		}

		else if (totcost == 1) {
			upd_bv upd;
			upd.vidx = tl;
			upd.val = +1;
			upd_m2b.write(upd);
		}

		int row = c / DSIZE;
		int col = c % DSIZE;
		tl_XORed_partd[row][col] = (cls)tl;
		cost_partd[row][col] = totcost;
	}

	upd_bv upd;
	upd.vidx = -1;
	upd_m2b.write(upd);

	int rand1, rand2;
	rand1 = psrandom(seed);
	rand2 = psrandom(seed);

	flip: for (unsigned long long f = 0; f < maxFlip; f++) {

		if (numOfUCs == 0) {
			flips = f;
			break;
		}

		ap_uint<4> uccol = rand1 % DSIZE;
		pick_uc: while (UCB_partd_len[uccol] == 0) {
			uccol++;
		}

		int ucrow = sliceRand(rand1, rand2, (int)UCB_partd_len[uccol]);
		int ucnum = UCB_partd[ucrow][uccol];

		wucnum_wait: while (req_m2c.full()) {}
		req_m2c.write(ucnum);

		rand1 = psrandom(seed);
		rand2 = psrandom(seed);

		var_flip_wait: while (rsp_b2m.empty()) {}
		int var_flip = rsp_b2m.read();
		vaArr[ABS(var_flip)] = 1 - vaArr[ABS(var_flip)];

		/////////////////////// 2. IO loc //////////////////////////

		int b1st = ol_len[GETPOS(var_flip)];
		int b1len = ol_len[GETPOS(var_flip) + 1] - b1st;
		int b2st = ol_len[GETPOS(-var_flip)];
		int b2len = ol_len[GETPOS(-var_flip) + 1] - b2st;

		num_line ltemp;
		ltemp.vidx = var_flip;
		ltemp.l1start = b1st;
		ltemp.l1size = b1len;
		ltemp.l2start = b2st;
		ltemp.l2size = b2len;
		wlocinfo_wait: while (req_m2l.full()) {}
		req_m2l.write(ltemp);

		wblkinfo_wait: while (upd_m2b.full()) {}
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
			upd_l2m_arr(var_flip, b1len, rsp_l2m_arr, rsp_l2m_arr_f, upd_m2b_arr, cost_partd, tl_XORed_partd, UCB_partd_len, posInUCB, UCB_partd, ucbdec_tot, bvinc_tot); 
			upd_l2m_arr_f(var_flip, b2len, rsp_l2m_arr, rsp_l2m_arr_f, upd_m2b_arr, cost_partd, tl_XORed_partd, UCB_partd_len, posInUCB, UCB_partd, ucbinc_tot, bvdec_tot); 
		}
		else {
			upd_l2m_arr_f(var_flip, b2len, rsp_l2m_arr, rsp_l2m_arr_f, upd_m2b_arr, cost_partd, tl_XORed_partd, UCB_partd_len, posInUCB, UCB_partd, ucbinc_tot, bvdec_tot); 
			upd_l2m_arr(var_flip, b1len, rsp_l2m_arr, rsp_l2m_arr_f, upd_m2b_arr, cost_partd, tl_XORed_partd, UCB_partd_len, posInUCB, UCB_partd, ucbdec_tot, bvinc_tot); 
		}

		wbv_wait: while (upd_m2b.full()) {}
		upd_bv bv_last;
		bv_last.vidx = ABS(var_flip);
		bv_last.val = bvinc_tot - bvdec_tot;
		upd_m2b.write(bv_last);

		numOfUCs += ucbinc_tot;
		numOfUCs -= ucbdec_tot;
	}

	req_m2c.write(-1);

	num_line templ;
	templ.vidx = 0;
	req_m2l.write(templ);

	flipcnt[0] = flips;

	for (int i = 1; i <= Nvar; i++) {
		char tval = (vaArr[i] == true ? 1 : 0);
		answer[i] = tval;
	}
}


void fysat(hls::vector<length, SDSIZE> ol_len_off[], hls::vector<clength, CDSIZE> cls_len_off[],
		hls::vector<int, DSIZE> ClauseList[], hls::vector<int, DSIZE> VarsOccList[],
		int numVars, int numClauses, int s, int k, 
		unsigned long long maxFlip, unsigned long long flipcnt[], hls::vector<char, 1> answer[]) {

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

#pragma HLS INTERFACE mode=m_axi bundle=m0 port=ClauseList
#pragma HLS INTERFACE mode=m_axi bundle=m1 port=VarsOccList
#pragma HLS INTERFACE mode=m_axi bundle=m2 port=cls_len_off
#pragma HLS INTERFACE mode=m_axi bundle=m3 port=ol_len_off
#pragma HLS INTERFACE mode=m_axi bundle=m4 port=flipcnt
#pragma HLS INTERFACE mode=m_axi bundle=m5 port=answer


#pragma HLS DATAFLOW
#pragma HLS STREAM variable=upd_m2b depth=4096
#pragma HLS STREAM variable=upd_m2b_arr depth=4096
#pragma HLS STREAM variable=clen_c2m depth=4096
#pragma HLS STREAM variable=vars_c2m depth=4096
#pragma HLS STREAM variable=req_m2c depth=4096
#pragma HLS STREAM variable=rsp_c2b depth=4096
#pragma HLS STREAM variable=rsp_b2m depth=4096
#pragma HLS STREAM variable=req_m2l depth=4096
#pragma HLS STREAM variable=rsp_l2m_arr depth=4096
#pragma HLS STREAM variable=rsp_l2m_arr_f depth=4096

	mod_main(upd_m2b, upd_m2b_arr, clen_c2m, vars_c2m, req_m2c, rsp_b2m, req_m2l, rsp_l2m_arr, rsp_l2m_arr_f, ol_len_off, numVars, numClauses, s, maxFlip, flipcnt, answer);
	mod_loc(req_m2l, rsp_l2m_arr, rsp_l2m_arr_f, VarsOccList, maxFlip);
	mod_cls(clen_c2m, vars_c2m, req_m2c, rsp_c2b, ClauseList, cls_len_off, numClauses, k, maxFlip);
	mod_break(upd_m2b, upd_m2b_arr, rsp_c2b, rsp_b2m, s, numVars, maxFlip);

}
}