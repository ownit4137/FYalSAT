#include "ap_utils.h"
//#include "etc/ap_utils.h"
#include <cstdint>

#include "fysat.h"

using tapa::istream;
using tapa::mmap;
using tapa::ostream;
using tapa::stream;
using tapa::task;

inline int psrandom(int & rand_val) {
	int old = rand_val;
	bool b_32 = old >> 31;
	bool b_22 = (old << 10) >> 31;
	bool b_2 = (old << 30) >> 31;
	bool b_1 = (old << 31) >> 31;
	int new_bit = b_32 ^ b_22 ^ b_2 ^ b_1;
	int new_bit31 = new_bit << 30;
	rand_val = new_bit31 | (old>>1);
	return old;
}

inline int sliceRand(int r1, int r2, int num) {
	if (num == 1) return 0;

	int two_iplus1 = 2;
	int prev_i = 0;
	rand_loop: for (int i = 0; i < 20; i++) {
#pragma HLS PIPELINE II=1
		if (num < two_iplus1) {
			prev_i = i;
			break;
		}
		two_iplus1 *= 2;
	}

	int p = two_iplus1 / 2 - 1;
	int ret = (r2 % 2) ? ((num - 1) - (r1 & p)) : (r1 & p);
	return ret;
}

struct break_struct {
	bool end_try;
	bool update;
	var vidx;
	bscore bval;
};

struct cls_struct {
	nlit numtruelit_data;
	var xor_data;
};

struct ucb_struct{
	bool end_try;
	bool update;
	bool isIncLoop;
	cls cidx;
};

const int bsprob[8] = {32767, 6913, 1458, 308, 65, 14, 3, 1};
const int bsmask[8] = {32767, 8191, 2047, 511, 127, 15, 3, 1};

void ExecCtrl(
	unsigned long long target_cycles,
	tapa::istreams<bool,TPE_NUM> & ended_fifo,
	tapa::ostreams<bool,TPE_NUM> & stop_fifo
)
{
	bool ended_data[TPE_NUM];
#pragma HLS ARRAY_PARTITION variable=ended_data complete dim=1
	for(int t = 0; t < TPE_NUM; t++ ){
#pragma HLS unroll
		ended_data[t] = false;
		stop_fifo[t].write(false);
	}

	bool stop = false;
	unsigned long long i = 0;

	while(i < target_cycles && stop == false){
#pragma HLS pipeline II=1
		for(int t = 0; t < TPE_NUM; t++ ){
#pragma HLS unroll
			bool found_temp;
			if( ended_fifo[t].try_read(found_temp) ){
				if(found_temp == true){
					ended_data[t] = true;
					stop = true;
				}
			}
		}
		i++;
	}

	for(int t = 0; t < TPE_NUM; t++ ){
#pragma HLS unroll
		stop_fifo[t].write(true);
	}
	ap_wait();

	for(int t = 0; t < TPE_NUM; t++ ){
#pragma HLS unroll
		if( ended_data[t] == false ){
			ended_fifo[t].read();
		}
	}
}

void MainCtrl(
	tapa::mmap<bits<tapa::vec_t<length, SDSIZE>>> ol_len_off, 
	tapa::mmap<int> Result,
	tapa::ostream<bool> & ended_fifo, tapa::istream<bool> & stop_fifo,
	tapa::ostream<int> & m2c_fifo, 
	tapa::istream<tapa::vec_t<short, DSIZE+1>> & b2m_fifo,
	tapa::ostream<ap_uint<32>> & m2o_fifo, 
#ifdef ENABLE_DBG_UCBNUM
	tapa::istreams<int,DSIZE> & dbg_u2m_fifo,
#endif
	int numVars, int numClauses, int seed, int k, int maxFlip 
){

	int rand_val = seed;

	length ol_len[MAXNLIT];		// char
	bool vaArr[MAXNVAR + 1];	// variable assignment	-> ap_uint<1>
	bscore bsArr[MAXNVAR + 1];

	copy_ol_len: for (int i = 0; i <= numVars * 2 / SDSIZE + 1; i++) {
#pragma HLS PIPELINE II=1
		tapa::vec_t<length, SDSIZE> t = tapa::bit_cast<tapa::vec_t<length, SDSIZE>>(ol_len_off[i]);
		for (int j = 0; j < SDSIZE; j++) {
			ol_len[i * SDSIZE + j] = t[j];
		}
	}

	init_var: for (int i = 0; i <= numVars; i++) {
#pragma HLS PIPELINE II=1
		int r = psrandom(rand_val);
		vaArr[i] = r % 2 == 0 ? true : false;
		bsArr[i] = 0;
	}
	
	bool ifound_sol = false;
	bool ofound_sol = false;
	int try_num = 0;

	main_tryloop: while(1){
		m2c_fifo.write(numClauses);
		m2c_fifo.write(numVars);
		m2c_fifo.write(k);

		m2o_fifo.write(numClauses);

		init_pe_send: for (cls c = 1; c <= numClauses; c++) {		//// tot ch fin
			tapa::vec_t<short, DSIZE+1> cidx_dummy = b2m_fifo.read();
			tapa::vec_t<short, DSIZE+1> vlen_temp = b2m_fifo.read();
			int vlen = vlen_temp[DSIZE];
	
			nlit truelitnum = 0;
			var xor_data = 0;
	
			for (int i = 0; i < vlen; i++) {
#pragma HLS PIPELINE II=1
				tapa::vec_t<short, DSIZE+1> lit_temp = b2m_fifo.read();
				var lit = lit_temp[DSIZE];
				if (vaArr[ABS(lit)] == (lit > 0)) {
					truelitnum++;
					xor_data = xor_data ^ ABS(lit);
				}
			}

			ap_uint<32> m2o_temp = 0;
			m2o_temp(15,0) = xor_data;
			m2o_temp(31,16) = truelitnum;
	
			m2o_fifo.write(m2o_temp); 
	
			if (truelitnum == 1) {
				bsArr[xor_data]++;
			}
		}
		
		int next_rand_val = rand_val;

		bool wait_for_end = false;	
		int flip_num = 0;
		main_fliploop: while(1){
			var var_flip = 0;
			bool isUnsatCls = true;

			tapa::vec_t<short, DSIZE+1> cidx = b2m_fifo.read();
			if(cidx[DSIZE] == -1){
				break;
			}
			else if(cidx[DSIZE] == 0){
				ifound_sol = true;
			}
			else{
				tapa::vec_t<short, DSIZE+1> vlen_temp = b2m_fifo.read();
				int vlen = vlen_temp[DSIZE];
			
				rand_val = next_rand_val;
				int mask = 0;
				int sumProb = 0;
		
				read_break: for (int i = 0; i < vlen; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS dependence variable=bsArr type=inter false
					tapa::vec_t<short, DSIZE+1> b2m_temp = b2m_fifo.read();
					var vidx = b2m_temp[DSIZE];

					//check if it is really an unsat clause
					if (vaArr[ABS(vidx)] == (vidx > 0)) {
						isUnsatCls = false;
					}
	
					//update break score
					bscore bv_old = bsArr[ABS(vidx)];
					int sum = 0;
					for (int s = 0; s < DSIZE; s++) {
#pragma HLS UNROLL
						sum += b2m_temp[s];
					}
					bscore bv = sum + bv_old;
					bsArr[ABS(vidx)] = bv;

					int bv_prob = (bv >= 8) ? 1 : bsprob[bv];
					int prev_sumProb = sumProb;
					sumProb = bv_prob + prev_sumProb;

					int list_mask = (bv >= 8) ? 1 : bsmask[bv];
					int list_mask_1 = (list_mask<<1) | 1;
					int mask_1 = (mask<<1) | 1;

					if( list_mask > mask ){
						mask = (list_mask < sumProb) ? list_mask_1 : list_mask;
					}
					else{
						mask = (mask < sumProb) ? mask_1 : mask;
					}
					int masked_rand_val = rand_val & mask;

					if( (prev_sumProb <= masked_rand_val && masked_rand_val < sumProb) || var_flip == 0 ){
						var_flip = vidx;
					}

					psrandom(next_rand_val);
				}

#ifdef DO_UCB_PREFETCH
				m2c_fifo.write(0); //one clause processed
#endif

				bool stop_temp;
				if( stop_fifo.try_read(stop_temp) ){
					if(stop_temp == true){
						ofound_sol = true;
					}
				}
			}

			if( wait_for_end == false ){
				if(ifound_sol == true || ofound_sol == true || flip_num == maxFlip){
					m2o_fifo.write(0);
					m2o_fifo.write(0);
					m2o_fifo.write(0);
					m2o_fifo.write(0);
					wait_for_end = true;	
				}
				else if( isUnsatCls == true ){
					var abs_var_flip = ABS(var_flip);
					int pos_var_pos = GETPOS(var_flip);
					int neg_var_pos = GETPOS(-var_flip);

					int var_flip_temp = var_flip;
					int b1st = ol_len[pos_var_pos];
					int b1len = ol_len[pos_var_pos + 1] - b1st;
					int b2st = ol_len[neg_var_pos];
					int b2len = ol_len[neg_var_pos + 1] - b2st;

					int start_addr = (b1st < b2st) ? b1st : b2st;

					m2o_fifo.write(var_flip_temp);
					m2o_fifo.write(start_addr);
					m2o_fifo.write(b1len);
					m2o_fifo.write(b2len);

					vaArr[abs_var_flip] = 1 - vaArr[abs_var_flip];

					flip_num++;
				}
			}
		}

#ifdef ENABLE_DBG_UCBNUM
		int dbg_ucb_maxnum = 0;
		dbg_uc_sel: for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
			int dbg_temp = dbg_u2m_fifo[i].read();
			if( dbg_temp > dbg_ucb_maxnum ){
				dbg_ucb_maxnum = dbg_temp;
			}
		}
#endif

		//end flip
	
		try_num++;
		Result[0] = try_num;
		Result[1] = flip_num;

		for (int i = 1; i <= numVars; i++) {
#pragma HLS pipeline II=1	
			int tval = (vaArr[i] == true ? 1 : 0);
			Result[i+1] = tval;

			//init for next try
			int r = psrandom(rand_val);
			vaArr[i] = r % 2 == 0 ? true : false;
			bsArr[i] = 0;
		}
#ifdef ENABLE_DBG_UCBNUM
		Result[numVars+2] = dbg_ucb_maxnum;	
#endif

		if( ifound_sol == true || ofound_sol == true ){
			break;
		}
	}

	ended_fifo.write(true);

	while(ofound_sol == false){
#pragma HLS pipeline II=1	
		bool stop_temp;
		if( stop_fifo.try_read(stop_temp) ){
			if(stop_temp == true){
				ofound_sol = true;
			}
		}
	}
}

void OccRead(
		//tapa::mmap<bits<tapa::vec_t<int, DSIZE>>> VarsOccList,
		tapa::async_mmap<bits<tapa::vec_t<int, DSIZE>>> & VarsOccList,
		tapa::istream<ap_uint<32>> & m2o_fifo,
		tapa::ostreams<ap_uint<32>,DSIZE> & o2p_fifo
) {

	occ_tryloop: while(1){
		int numClauses = m2o_fifo.read();
 		for (int p = 0; p < DSIZE; p++) {
#pragma HLS UNROLL
			int init_num_temp = (numClauses/DSIZE) + (p <= (numClauses%DSIZE));
			int init_num = (p==0) ? init_num_temp - 1 : init_num_temp;
			o2p_fifo[p].write(init_num); 
		}

		pe_relayloop: for (cls c = 1; c <= numClauses; c++) {
#pragma HLS PIPELINE II=1
			ap_uint<32> m2o_temp = m2o_fifo.read();
			o2p_fifo[c%DSIZE].write(m2o_temp);
		}
					
		int vidx;
		int start_addr;
		int l1size;
		int l2size;

		ap_uint<3> m2o_state = 0;
		bool flipdata_valid = false;
		int req_addr = 0;
		int target_req_addr = 0;
		ap_uint<32> m2o_data;
		bool m2o_data_valid = false;

		ap_uint<2> o2p_state = 0;
		bool flipdata_read = false;
		int rsp_cnt = 0;
		int target_rsp_cnt = 0;
		tapa::vec_t<int, DSIZE> o2p_data;
		bool o2p_data_valid = false;

		bool end_try = false;
		bool end_try_req = false;
		occ_fliploop: while(end_try == false) {	
#pragma HLS PIPELINE II=1
			if( end_try_req == true && o2p_data_valid == false ){
				end_try = true;
			}

			if( flipdata_read == true ){
				flipdata_valid = false;
				flipdata_read = false;
			}

			if( o2p_data_valid == true ){
				for (int p = 0; p < DSIZE; p++) {
#pragma HLS UNROLL	
					o2p_fifo[p].write(o2p_data[p]);
				}
				o2p_data_valid = false;
			}

			if( o2p_data_valid == false ){
				if( o2p_state == 0 && flipdata_valid == true ){
					for (int p = 0; p < DSIZE; p++) {
#pragma HLS UNROLL
						o2p_data[p] = vidx;
					}
					o2p_data_valid = true;
					o2p_state = 1;
				}
				else if( o2p_state == 1 ){
					for (int p = 0; p < DSIZE; p++) {
#pragma HLS UNROLL
						o2p_data[p] = l1size;
					}
					o2p_data_valid = true;
					o2p_state = 2;
				}
				else if( o2p_state == 2 ){
					rsp_cnt = 0;
					target_rsp_cnt = l1size + l2size;
					flipdata_read = true;

					for (int p = 0; p < DSIZE; p++) {
#pragma HLS UNROLL
						o2p_data[p] = l2size;
					}					
					o2p_data_valid = true;
					if( vidx == 0 ){
						end_try_req = true;
						o2p_state = 0;
					}
					else{
						o2p_state = 3;
					}
				}
				else if( o2p_state == 3 ){
					bits<tapa::vec_t<int, DSIZE>> occ_temp;

					o2p_data_valid = VarsOccList.read_data.try_read(occ_temp);
					o2p_data = tapa::bit_cast<tapa::vec_t<int, DSIZE>>(occ_temp);

					if( o2p_data_valid == true ){
						rsp_cnt++;
						if( rsp_cnt == target_rsp_cnt ){
							o2p_state = 0;
						}
					}
				}
			}


			if( m2o_state == 4 ){
				if( VarsOccList.read_addr.try_write(req_addr) ){
					req_addr++;
					if( req_addr == target_req_addr ){
						m2o_state = 0;
					}
				}
			}	
			else if( m2o_data_valid == true ){
				if( m2o_state == 0 && flipdata_valid == false ){
					vidx = m2o_data;
					m2o_state = 1;
					m2o_data_valid = false;
				}
				else if( m2o_state == 1 ){
					start_addr = m2o_data;
					m2o_state = 2;
					m2o_data_valid = false;
				}
				else if( m2o_state == 2 ){
					l1size = m2o_data;
					m2o_state = 3;
					m2o_data_valid = false;
				}
				else if( m2o_state == 3 ){
					l2size = m2o_data;
					req_addr = start_addr;
					target_req_addr = start_addr + l1size + l2size;
					flipdata_valid = true;
					if( l1size + l2size == 0 ){
						m2o_state = 0;
					}
					else{	
						m2o_state = 4;
					}
					m2o_data_valid = false;
				}
			}

			if( m2o_data_valid == false ){
				m2o_data_valid = m2o_fifo.try_read(m2o_data);
			}
		}
	}
}

void PEModule(
	int pe_id,
	tapa::istream<ap_uint<32>> & o2p_fifo,
	tapa::ostream<break_struct> & p2b_fifo,
	tapa::ostream<ucb_struct> & ucb_fifo 
){
	//cls_struct cls_storage[MAXNCLS / DSIZE + 1];
	ap_uint<32> cls_storage[MAXNCLS / DSIZE];
#pragma HLS bind_storage variable=cls_storage impl=uram type=RAM_S2P

	pe_tryloop: while(1){
		int init_num = o2p_fifo.read();
		int offset = ((pe_id%DSIZE) == 0) ? 1 : 0;
		pe_initrec: for(int i = 0; i < init_num; i++){
#pragma HLS pipeline II=1
			ap_uint<32> o2p_temp = o2p_fifo.read();

			//cls_struct cls_data;
			//cls_data.xor_data = o2p_temp(15,0);
			//cls_data.numtruelit_data = o2p_temp(31,16);
			nlit numtruelit = o2p_temp(31,16);
			cls_storage[(i+offset)] = o2p_temp;	
			
			if (numtruelit == 0) {
				ucb_struct ucb_info;
				ucb_info.end_try = false;
				ucb_info.isIncLoop = false;
				ucb_info.update = true;
				ucb_info.cidx = (i+offset)*DSIZE + (pe_id%DSIZE);
				ucb_fifo.write(ucb_info); 
			}
		}		

		{
			ucb_struct ucb_info;
			ucb_info.end_try = false;
			ucb_info.isIncLoop = false;
			ucb_info.update = false;
			ucb_info.cidx = 0;
			ucb_fifo.write(ucb_info);
		}

		pe_fliploop: while(1) {
			ap_int<32> o2p_temp = o2p_fifo.read();
			var var_flip = o2p_temp;
			int b1len = o2p_fifo.read();
			int b2len = o2p_fifo.read();
			ap_wait();

			if( var_flip == 0 ){
				break;
			}

			var abs_var_flip = ABS(var_flip);

			bscore bvinc = 0;
			inc_dec_loop: for (int t = 0; t < b1len+b2len; t++) {
#pragma HLS pipeline II=1
#pragma HLS dependence variable=cls_storage type=inter false
				bool isIncLoop = (var_flip < 0 && t < b1len) || (var_flip > 0 && t >= b2len); 

				ap_int<32> o2p_temp = o2p_fifo.read();
				cls cidx = o2p_temp;
				//cls_struct cls_data = cls_storage[cidx/DSIZE];
				ap_uint<32> cls_data = cls_storage[cidx/DSIZE];
			
				if (cidx > 0) {
					//nlit numtruelit = cls_data.numtruelit_data;
					//var old_critv = cls_data.xor_data;
					var old_critv = cls_data(15,0);
					nlit numtruelit = cls_data(31,16);

					var new_critv = (isIncLoop == true && numtruelit == 0) ? abs_var_flip : (old_critv ^ abs_var_flip); 
					nlit new_numtruelit = (isIncLoop == true) ? numtruelit + 1 : numtruelit - 1;
						
					if( (isIncLoop == true && numtruelit == 0) || (isIncLoop == false && numtruelit == 1) ){
						ucb_struct ucb_info;
						ucb_info.end_try = false;
						ucb_info.isIncLoop = isIncLoop;
						ucb_info.update = true;
						ucb_info.cidx = cidx;
						ucb_fifo.write(ucb_info);
	
						bvinc = (isIncLoop == true) ? bvinc + 1 : bvinc - 1; // bv++ or bv--
					}
					else if( (isIncLoop == true && numtruelit == 1) || (isIncLoop == false && numtruelit == 2) ){
						break_struct break_data;
						break_data.end_try = false;
						break_data.update = true;
						break_data.vidx = (isIncLoop == true) ? old_critv : new_critv;
						break_data.bval = (isIncLoop == true) ? -1 : 1;
						p2b_fifo.write(break_data);	
					} 					
	
					//cls_struct new_cls_data;
					//new_cls_data.numtruelit_data = (isIncLoop == true) ? numtruelit + 1 : numtruelit - 1;
					//new_cls_data.xor_data = new_critv;
					ap_uint<32> new_cls_data;
					new_cls_data(15,0) = new_critv;
					new_cls_data(31,16) = new_numtruelit;
					cls_storage[cidx/DSIZE] = new_cls_data;
				}
			}
		
			{
				break_struct break_data;
				break_data.end_try = false;
				break_data.update = true;
				break_data.vidx = abs_var_flip;
				break_data.bval = bvinc;
				p2b_fifo.write(break_data);	

				break_data.end_try = false;
				break_data.update = false;
				break_data.vidx = 0;
				break_data.bval = 0;
				p2b_fifo.write(break_data);	

				ucb_struct ucb_info;
				ucb_info.end_try = false;
				ucb_info.isIncLoop = false;
				ucb_info.update = false;
				ucb_info.cidx = 0;
				ucb_fifo.write(ucb_info);
			}	
		}	

		break_struct break_data;
		break_data.end_try = true;
		break_data.update = false;
		break_data.vidx = 0;
		break_data.bval = 0;
		p2b_fifo.write(break_data);	

		ucb_struct ucb_info;
		ucb_info.end_try = true;
		ucb_info.isIncLoop = false;
		ucb_info.update = false;
		ucb_info.cidx = 0;
		ucb_fifo.write(ucb_info);
	}
}

void BreakLocal(
	tapa::istream<int> & c2b_fifo,
	tapa::ostream<tapa::vec_t<short, DSIZE+1>> & b2m_fifo,
	tapa::istreams<break_struct,DSIZE> & p2b_fifo
){
	bscore bs_storage[MAXNVAR + 1][DSIZE];
#pragma HLS ARRAY_PARTITION variable=bs_storage complete dim=2

	break_tryloop: while(1){
		int numClauses = c2b_fifo.read();
		int numVars = c2b_fifo.read();

		//passes vidx of all clauses to main
		{
			bool send_end = false;
			bool cidx_avail = false;
			bool vlen_avail = false;
			int cidx = 0;
			int vlen = 0;
			int vcnt = 0;
			int vinit = 0;

			send_idx: while(send_end==false){
#pragma HLS PIPELINE II=1
				if( !c2b_fifo.empty() ){
					int c2b_temp = c2b_fifo.read();
					tapa::vec_t<short, DSIZE+1> b2m_temp;
					b2m_temp[DSIZE] = c2b_temp;

					if( cidx_avail == false ){
						cidx_avail = true;
						cidx = c2b_temp;
					}
					else{
						if( vlen_avail == false ){
							vlen_avail = true;
							vlen = c2b_temp;
							vcnt = 0;	
						}
						else{					
							vcnt++;
							if( vcnt == vlen ){
								cidx_avail = false;
								vlen_avail = false;
								if( cidx == numClauses ){
									send_end = true;
								}
							}
						}
					}
					b2m_fifo.write(b2m_temp);
				}
	
				//break score init
				for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
					bs_storage[vinit][i] = 0;
				}
				if( vinit == numVars ){
					vinit = 0;
				}
				else{
					vinit++;
				}
			}
		}

		int end_try = false;
		break_flip_loop: while(end_try == false) {

			if( !c2b_fifo.empty() ){
				bool send_end = false;
				bool cidx_avail = false;
				bool vlen_avail = false;
				int cidx = 0;
				int vlen = 0;
				int vcnt = 0;
		
				send_break: while(send_end==false){
#pragma HLS PIPELINE II=1
#pragma HLS dependence variable=bs_storage type=inter false
					if( !c2b_fifo.empty() ){
						int c2b_temp = c2b_fifo.read();
						tapa::vec_t<short, DSIZE+1> b2m_temp;
						b2m_temp[DSIZE] = c2b_temp;
		
						if( cidx_avail == false ){
							cidx_avail = true;
							cidx = c2b_temp;
							if( cidx == 0 || cidx == -1 ){
								send_end = true;
							}
							else{
								b2m_temp[DSIZE] = 1;
							}
							if( cidx == -1 ){
								end_try = true;
							}
						}
						else{
							if( vlen_avail == false ){
								vlen_avail = true;
								vlen = c2b_temp;
								vcnt = 0;	
							}
							else{
								var vidx = c2b_temp;
								var abs_vidx = ABS(vidx);
								for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
									b2m_temp[i] = bs_storage[abs_vidx][i];
									bs_storage[abs_vidx][i] = 0;
								}
		
								vcnt++;
								if( vcnt == vlen ){
									send_end = true;
								}
							}
						}
						b2m_fifo.write(b2m_temp);
					}
				}
			}

			bool end_flip_pe[DSIZE];
#pragma HLS ARRAY_PARTITION variable=end_flip_pe complete dim=1

			bool p2b_fifo_empty = true;
			for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
				end_flip_pe[i] = false;
				if(!p2b_fifo[i].empty()){
					p2b_fifo_empty = false;
				}
			}
			if( p2b_fifo_empty == false ){	
				bool end_flip = false;
				break_loop: while( end_flip == false ) {
#pragma HLS pipeline II=1
					end_flip = true;
					for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
						if( end_flip_pe[i] == false ){
							end_flip = false;
						}
						if( !p2b_fifo[i].empty() && end_flip_pe[i] == false ){
							break_struct break_data = p2b_fifo[i].read();
							bool update = break_data.update;
							var vidx = break_data.vidx;
							bscore bval = break_data.bval;
			
							if( update == true ){
								bs_storage[vidx][i] += bval;
							}
							else{
								end_flip_pe[i] = true;
							}
						}	
					}	
				}
			}
		}
	}
}

void UCBLocal(
	tapa::istream<ucb_struct> & ucb_fifo,
	tapa::ostream<int> & ucb_sel_fifo
#ifdef ENABLE_DBG_UCBNUM
	, tapa::ostream<int> & dbg_u2m_fifo
#endif
){
	cls UCBQueue[UCBSIZE / DSIZE];
	ucbidx UCBQueuePos[MAXNCLS / DSIZE]; 

	//int rand_val = seed;
	//int rand_val = 0;
	//unsigned int ucb_num_mask = 1;
	ushort ucbsel_idx = 0;

	ushort ucb_num = 0;
#ifdef ENABLE_DBG_UCBNUM
	ushort dbg_ucb_maxnum = 0;
#endif

	ucblocal_loop: while(1){
		bool end_try = false;
		bool end_flip = false;
//#ifdef DO_UCB_PREFETCH
//		cls UCB_tailcls = UCBQueue[ucb_num - 1];
//#endif

		ucb_loop: while( end_flip == false ) {
//#ifdef DO_UCB_PREFETCH
//	#pragma HLS pipeline II=1
//#else
	#pragma HLS pipeline II=3
//#endif

#ifdef ENABLE_DBG_UCBNUM
			if( ucb_num > dbg_ucb_maxnum ){
				dbg_ucb_maxnum = ucb_num;
			}
#endif
			if( !ucb_fifo.empty() ){
				ucb_struct ucb_info = ucb_fifo.read();
				bool update = ucb_info.update;
				bool isIncLoop = ucb_info.isIncLoop;
				cls cidx = ucb_info.cidx;

				int caddr = cidx / DSIZE;
				ushort QueuePos = UCBQueuePos[caddr];
//#ifndef DO_UCB_PREFETCH
				cls UCB_tailcls = UCBQueue[(ucb_num - 1)];
//#endif
				ushort UCBQueue_widx = (isIncLoop == true) ? QueuePos : ucb_num;
				int UCBQueuePos_widx = (isIncLoop == true) ? UCB_tailcls / DSIZE : caddr;

				if( update == true ){
					UCBQueue[UCBQueue_widx] = (isIncLoop == true) ? UCB_tailcls : cidx;
					UCBQueuePos[UCBQueuePos_widx] = (isIncLoop == true) ? (ucbidx)QueuePos : (ucbidx)ucb_num;
//#ifdef DO_UCB_PREFETCH
//					UCB_tailcls = (isIncLoop == true) ? UCBQueue[ucb_num - 2] : cidx;
//#endif
					ucb_num = (isIncLoop == true) ? ((ucb_num==0) ? 0 : ucb_num - 1) : ((ucb_num == (UCBSIZE/DSIZE-1)) ? ucb_num : ucb_num + 1);
				}
				else{
					end_flip = true;
					if( ucb_info.end_try == true ){
						end_try = true;	
					}
				}
			}

			//psrandom(rand_val);
			//unsigned int ucb_num_maskL = ((ucb_num_mask << 1) | 1);
			//unsigned int ucb_num_maskS = ((ucb_num_mask >> 1) | 1);
			//if( ucb_num_maskL < ucb_num ){
			//	ucb_num_mask = ucb_num_maskL;
			//}
			//else if( ucb_num_mask >= ucb_num ){
			//	ucb_num_mask = ucb_num_maskS;
			//}
		}

		//int ucbsel_idx = rand_val & ucb_num_mask;
		if( ucbsel_idx >= ucb_num ){
			ucbsel_idx = 0;
		}

		cls unsat_cidx;
		if (end_try == true) {
			unsat_cidx = -1;
		}
		else if (ucb_num == 0) {
			unsat_cidx = 0;
		}
		//else if (ucb_num == 1) {
		//	unsat_cidx = UCBQueue[0];
		//}
		else{
			unsat_cidx = UCBQueue[ucbsel_idx];
		}
		ucb_sel_fifo.write(unsat_cidx);


		ucbsel_idx++;
		if( end_try == true ){
			ucbsel_idx = 0;
			//ucb_num_mask = 1;
			ucb_num = 0;
#ifdef ENABLE_DBG_UCBNUM
			dbg_u2m_fifo.write(dbg_ucb_maxnum);
#endif
		}
	}
}


void ClsRead (
	tapa::mmap<bits<tapa::vec_t<int, 16>>> ClauseList, 
	tapa::istream<int> & m2c_fifo, 
	tapa::istreams<int, DSIZE> & ucb_sel_fifo,
	tapa::ostream<tapa::vec_t<int, 16>> & cvec_fifo
) {
	tapa::vec_t<int, 16> cvec_zero; 
	cvec_zero[0] = 0; cvec_zero[1] = 0; cvec_zero[2] = 0; cvec_zero[3] = 0; 
	cvec_zero[4] = 0; cvec_zero[5] = 0; cvec_zero[6] = 0; cvec_zero[7] = 0; 
	cvec_zero[8] = 0; cvec_zero[9] = 0; cvec_zero[10] = 0; cvec_zero[11] = 0; 
	cvec_zero[12] = 0; cvec_zero[13] = 0; cvec_zero[14] = 0; cvec_zero[15] = 0; 

	tapa::vec_t<int, 16> cvec_end = cvec_zero; 
	cvec_end[0] = -1;

	cls_tryloop: while(1){
		int numClauses = m2c_fifo.read();
		int numVars = m2c_fifo.read();
		int k = m2c_fifo.read();
		
		tapa::vec_t<int, 16> cvec_init = cvec_zero; 
		cvec_init[0] = numClauses; cvec_init[1] = numVars;
		cvec_fifo.write(cvec_init);

		init_cls_read: for (int c = 1; c <= numClauses; c++) {		//// tot ch fin
			init_cls_lit: for (int b = 0; b < k / 16; b++) {
#pragma HLS PIPELINE II=1
				tapa::vec_t<int, 16> cvec_temp = tapa::bit_cast<tapa::vec_t<int, 16>>(ClauseList[c * (k / 16) + b]);
				if( cvec_temp[0] != 0 ){
					cvec_fifo.write(cvec_temp);
				}
			}
		}

		int num_in_fetch = 0;

#if DSIZE_LOG == 0
		ap_uint<1> sel_addr = 0;
#else
		ap_uint<DSIZE_LOG> sel_addr = 0;
#endif
		int ucb_sel_data[DSIZE];
#pragma HLS ARRAY_PARTITION variable=ucb_sel_data complete dim=1
		for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
			ucb_sel_data[i] = -2; //empty
		}
	
		cls_fliploop: while(1){
#ifdef DO_UCB_PREFETCH
			bool data_exist = true;
			receive_uc: while( data_exist == true ){
#pragma HLS PIPELINE II=1
				data_exist = false;
				bool all_exist = true;
				for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
					if(ucb_sel_fifo[i].empty()){
						all_exist = false;
					}
					else{
						data_exist = true;
					}
				}
				if( all_exist == true ){
					for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
						ucb_sel_data[i] = ucb_sel_fifo[i].read();
					}				
				}
			}
#else
			for (int i = 0; i < DSIZE; i++) {
#pragma HLS unroll
				ucb_sel_data[i] = ucb_sel_fifo[i].read();
			}				
#endif		
	
			int zero_cnt = 0;	
			int ucb_sel = -2;

			pick_uc: for (int i = 0; i < DSIZE; i++) {
#pragma HLS pipeline II=1
#pragma HLS dependence variable=ucb_sel_data type=inter false
				int ucb_temp = ucb_sel_data[sel_addr];
				ucb_sel_data[sel_addr] = -2;
				if( ucb_temp == 0 ){
					zero_cnt++;
				}
#if DSIZE_LOG != 0
				sel_addr++;
#endif
				if( ucb_temp == -1 || ucb_temp > 0 ){
					ucb_sel = ucb_temp;
					break;
				}
			}
			
			if( ucb_sel == -2 ){ 
				if( zero_cnt == DSIZE ){ // solution found
					cvec_fifo.write(cvec_zero);
				}
				//else do nothing
			}
			else if( ucb_sel == -1 ){ // end try
				cvec_fifo.write(cvec_end);
				break;
			}
			else{ //unsat cls found
				uc_send: for (int b = 0; b < k / 16; b++) {
#pragma HLS PIPELINE II=1
					tapa::vec_t<int, 16> cvec_temp = tapa::bit_cast<tapa::vec_t<int, 16>>(ClauseList[ucb_sel * (k / 16) + b]);
					if( cvec_temp[0] != 0 ){
						cvec_fifo.write(cvec_temp);
					}
				}
				num_in_fetch++;
			}

#ifdef DO_UCB_PREFETCH
			//avoids backpressure
			ap_wait();
			bool just_read = false;
			m2c_readloop: do {
#pragma HLS pipeline II=2
				int dummy;
				if( m2c_fifo.try_read(dummy) ){
					num_in_fetch--;
					just_read = true;
				}
				else{
					just_read = false;
				}
			} while( just_read == true || num_in_fetch >= PREFETCH_NUM || (zero_cnt == DSIZE && num_in_fetch > 0) );
#endif
		}

#ifdef DO_UCB_PREFETCH
		m2c_endloop: while( num_in_fetch > 0 ){
#pragma HLS pipeline II=2
			int dummy;
			if( m2c_fifo.try_read(dummy) ){
				num_in_fetch--;
			}
		}
#endif
	}
}

void ClsRateChange (
	tapa::istream< tapa::vec_t<int, 16> > & cvec_fifo,
	tapa::ostream<int> & c2b_fifo
){
	ap_uint<4> cnt = 0;
	tapa::vec_t<int, 16> cvec1, cvec2;
	bool cvec1_exist = false, cvec2_exist = false;

	while(1){
#pragma HLS pipeline II=1
		if( cvec2_exist == true ){
			int c2b_temp = cvec2[cnt];
			if( cnt == 0 || c2b_temp != 0 ){
				c2b_fifo.write(c2b_temp);
			}
			if(cnt == 15){
				cvec2_exist = false;
			}
			cnt++;
		}		
		if( cvec2_exist == false && cvec1_exist == true ){
			cvec2 = cvec1;
			cvec2_exist = true;
			cvec1_exist = false;
		}
		if( cvec1_exist == false ){
			cvec1_exist = cvec_fifo.try_read(cvec1);
		}
	}
}

void fysat(
	tapa::mmaps<bits<tapa::vec_t<length, SDSIZE>>,TPE_NUM> ol_len_off, 
	tapa::mmaps<bits<tapa::vec_t<int, 16>>,TPE_NUM> ClauseList, 
	tapa::mmaps<bits<tapa::vec_t<int, DSIZE>>,TPE_NUM> VarsOccList,
	tapa::mmaps<int,TPE_NUM> Result,
	int numVars, int numClauses, int s, int k, 
	int maxFlip, unsigned long long target_cycles
	) {

	tapa::streams<bool,TPE_NUM,2> stop_fifo;
	tapa::streams<bool,TPE_NUM,2> ended_fifo;
	tapa::streams<int,TPE_NUM,4> m2c_fifo;
	tapa::streams<tapa::vec_t<int, 16>,TPE_NUM,8> cvec_fifo;
	tapa::streams<int,TPE_NUM,512> c2b_fifo;
	tapa::streams<tapa::vec_t<short,DSIZE+1>,TPE_NUM,8> b2m_fifo;
	tapa::streams<ap_uint<32>,TPE_NUM,16> m2o_fifo;
	tapa::streams<ap_uint<32>,DSIZE*TPE_NUM,512> o2p_fifo;
	tapa::streams<break_struct,DSIZE*TPE_NUM,512> p2b_fifo;
	tapa::streams<ucb_struct,DSIZE*TPE_NUM,512> ucb_fifo;
	tapa::streams<int,DSIZE*TPE_NUM,8> ucb_sel_fifo;
#ifdef ENABLE_DBG_UCBNUM
	tapa::streams<int,DSIZE*TPE_NUM,2> dbg_u2m_fifo;
#endif
	
	tapa::task()
		.invoke<tapa::join>(ExecCtrl, target_cycles, ended_fifo, stop_fifo )
		.invoke<tapa::join,TPE_NUM>(MainCtrl, ol_len_off, Result, ended_fifo, stop_fifo, m2c_fifo, b2m_fifo, m2o_fifo,
#ifdef ENABLE_DBG_UCBNUM
						 dbg_u2m_fifo,
#endif
						 numVars, numClauses, s, k, maxFlip)
		.invoke<tapa::detach,TPE_NUM>(OccRead, VarsOccList, m2o_fifo, o2p_fifo)
		.invoke<tapa::detach,DSIZE*TPE_NUM>(PEModule, tapa::seq(), o2p_fifo, p2b_fifo, ucb_fifo)  
		.invoke<tapa::detach,TPE_NUM>(BreakLocal, c2b_fifo, b2m_fifo, p2b_fifo)  
		.invoke<tapa::detach,DSIZE*TPE_NUM>(UCBLocal, ucb_fifo, ucb_sel_fifo
#ifdef ENABLE_DBG_UCBNUM
						, dbg_u2m_fifo
#endif
		)
		.invoke<tapa::detach,TPE_NUM>(ClsRead, ClauseList, m2c_fifo, ucb_sel_fifo, cvec_fifo)
		.invoke<tapa::detach,TPE_NUM>(ClsRateChange, cvec_fifo, c2b_fifo)
	;
}

