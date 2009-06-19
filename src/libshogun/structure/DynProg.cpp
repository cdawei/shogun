/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Written (W) 1999-2007 Soeren Sonnenburg
 * Written (W) 1999-2007 Gunnar Raetsch
 * Written (W) 2008-2009 Jonas Behr
 * Copyright (C) 1999-2009 Fraunhofer Institute FIRST and Max-Planck-Society
 */

#include "structure/DynProg.h"
#include "lib/Mathematics.h"
#include "lib/io.h"
#include "lib/config.h"
#include "features/StringFeatures.h"
#include "features/Alphabet.h"
#include "structure/Plif.h"
#include "structure/IntronList.h"
#include "lib/Array.h"
#include "lib/Array2.h"
#include "lib/Array3.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

//#define USE_TMP_ARRAYCLASS
//#define DYNPROG_DEBUG

static int32_t word_degree_default[4]={3,4,5,6} ;
static int32_t cum_num_words_default[5]={0,64,320,1344,5440} ;
static int32_t frame_plifs[3]={4,5,6};
static int32_t num_words_default[4]=   {64,256,1024,4096} ;
static int32_t mod_words_default[32] = {1,1,1,1,1,1,1,1,
									1,1,1,1,1,1,1,1,
									0,0,0,0,0,0,0,0,
									0,0,0,0,0,0,0,0} ;  
static bool sign_words_default[16] = {true,true,true,true,true,true,true,true,
									  false,false,false,false,false,false,false,false} ; // whether to use counts or signum of counts
static int32_t string_words_default[16] = {0,0,0,0,0,0,0,0,
									   1,1,1,1,1,1,1,1} ; // which string should be used

CDynProg::CDynProg(int32_t num_svms /*= 8 */)
: CSGObject(), m_transition_matrix_a_id(1,1), m_transition_matrix_a(1,1),
	m_transition_matrix_a_deriv(1,1), m_initial_state_distribution_p(1),
	m_initial_state_distribution_p_deriv(1), m_end_state_distribution_q(1),
	m_end_state_distribution_q_deriv(1),

	  // multi svm
	  m_num_degrees(4), 
	  m_num_svms(num_svms), 
	  m_word_degree(word_degree_default, m_num_degrees, true, true),
	  m_cum_num_words(cum_num_words_default, m_num_degrees+1, true, true),
	  m_cum_num_words_array(m_cum_num_words.get_array()),
	  m_num_words(num_words_default, m_num_degrees, true, true),
	  m_num_words_array(m_num_words.get_array()),
	  m_mod_words(mod_words_default, m_num_svms, 2, true, true),
	  m_mod_words_array(m_mod_words.get_array()),
	  m_sign_words(sign_words_default, m_num_svms, true, true),
	  m_sign_words_array(m_sign_words.get_array()),
	  m_string_words(string_words_default, m_num_svms, true, true),
	  m_string_words_array(m_string_words.get_array()),
	  //m_svm_pos_start(m_num_degrees),
	  m_num_unique_words(m_num_degrees),
	  m_svm_arrays_clean(true),

	  m_max_a_id(0), m_observation_matrix(1,1,1), 
	  m_pos(1), 
	  m_seq_len(0),
	  m_orf_info(1,2), 
	  m_plif_list(1), 
	  m_PEN(1,1),
	  m_genestr(1), m_wordstr(NULL), m_dict_weights(1,1), m_segment_loss(1,1,2), 
	  m_segment_ids(1),
	  m_segment_mask(1),
	  m_my_state_seq(1),
	  m_my_pos_seq(1),
	  m_my_scores(1),
	  m_my_losses(1),
	  m_scores(1),
	  m_states(1,1),
	  m_positions(1,1),

	  m_seq_sparse1(NULL),
	  m_seq_sparse2(NULL),
	  m_plif_matrices(NULL),
	  
	  m_genestr_stop(1),
	  m_intron_list(NULL),
	  m_num_intron_plifs(0),
	  m_lin_feat(1,1), //by Jonas
	  m_raw_intensities(NULL),
	  m_probe_pos(NULL),
	  m_num_probes_cum(NULL),
	  m_num_lin_feat_plifs_cum(NULL),
	  m_num_raw_data(0),
	  
	  m_long_transitions(false),
	  m_long_transition_threshold(100000),
	  m_long_transition_max(100000)
{
	trans_list_forward = NULL ;
	trans_list_forward_cnt = NULL ;
	trans_list_forward_val = NULL ;
	trans_list_forward_id = NULL ;
	trans_list_len = 0 ;

	mem_initialized = true ;

	m_N=1;

	m_raw_intensities = NULL;
	m_probe_pos = NULL;
	m_num_probes_cum = new int32_t[100];
	m_num_probes_cum[0] = 0;
	//m_use_tiling=false;
	m_num_lin_feat_plifs_cum = new int32_t[100];
	m_num_lin_feat_plifs_cum[0] = m_num_svms;
	m_num_raw_data = 0;
#ifdef ARRAY_STATISTICS
	m_word_degree.set_name("word_degree");
#endif
}

CDynProg::~CDynProg()
{
	if (trans_list_forward_cnt)
		delete[] trans_list_forward_cnt;
	if (trans_list_forward)
	{
		for (int32_t i=0; i<trans_list_len; i++)
		{
			if (trans_list_forward[i])
				delete[] trans_list_forward[i];
		}
		delete[] trans_list_forward;
	}
	if (trans_list_forward_val)
	{
		for (int32_t i=0; i<trans_list_len; i++)
		{
			if (trans_list_forward_val[i])
				delete[] trans_list_forward_val[i];
		}
		delete[] trans_list_forward_val;
	}
	if (trans_list_forward_id)
	{
		for (int32_t i=0; i<trans_list_len; i++)
		{
			if (trans_list_forward_id[i])
				delete[] trans_list_forward_id[i];
		}
		delete[] trans_list_forward_id;
	}
	if (m_raw_intensities)
		delete[] m_raw_intensities;
	if (m_probe_pos)
		delete[] m_probe_pos;
	if (m_num_probes_cum)
	  delete[] m_num_probes_cum ;
	if (m_num_lin_feat_plifs_cum)
	  delete[] m_num_lin_feat_plifs_cum ;

	delete m_intron_list;

	SG_UNREF(m_seq_sparse1);
	SG_UNREF(m_seq_sparse2);
	SG_UNREF(m_plif_matrices);
}

////////////////////////////////////////////////////////////////////////////////
int32_t CDynProg::get_num_svms()
{
	return m_num_svms;
}

void CDynProg::precompute_stop_codons()
{
	int32_t length=m_genestr.get_dim1();

	m_genestr_stop.resize_array(length) ;
	m_genestr_stop.zero() ;
	m_genestr_stop.set_name("genestr_stop") ;
	{
		for (int32_t i=0; i<length-2; i++)
			if ((m_genestr[i]=='t' || m_genestr[i]=='T') && 
					(((m_genestr[i+1]=='a' || m_genestr[i+1]=='A') && 
					  (m_genestr[i+2]=='a' || m_genestr[i+2]=='g' || m_genestr[i+2]=='A' || m_genestr[i+2]=='G')) ||
					 ((m_genestr[i+1]=='g'||m_genestr[i+1]=='G') && (m_genestr[i+2]=='a' || m_genestr[i+2]=='A') )))
			{
				m_genestr_stop.element(i)=true ;
			}
			else
				m_genestr_stop.element(i)=false ;
		m_genestr_stop.element(length-2)=false ;
		m_genestr_stop.element(length-1)=false ;
	}
}

void CDynProg::set_num_states(int32_t p_N)
{
	m_N=p_N ;

	m_transition_matrix_a_id.resize_array(m_N,m_N) ;
	m_transition_matrix_a.resize_array(m_N,m_N) ;
	m_transition_matrix_a_deriv.resize_array(m_N,m_N) ;
	m_initial_state_distribution_p.resize_array(m_N) ;
	m_initial_state_distribution_p_deriv.resize_array(m_N) ;
	m_end_state_distribution_q.resize_array(m_N);
	m_end_state_distribution_q_deriv.resize_array(m_N) ;

	m_orf_info.resize_array(m_N,2) ;
	m_PEN.resize_array(m_N,m_N) ;
}

int32_t CDynProg::get_num_states()
{
	return m_N;
}

void CDynProg::init_tiling_data(
	int32_t* probe_pos, float64_t* intensities, const int32_t num_probes)
{
	m_num_raw_data++;
	m_num_probes_cum[m_num_raw_data] = m_num_probes_cum[m_num_raw_data-1]+num_probes;

	int32_t* tmp_probe_pos = new int32_t[m_num_probes_cum[m_num_raw_data]];
	float64_t* tmp_raw_intensities = new float64_t[m_num_probes_cum[m_num_raw_data]];


	if (m_num_raw_data==1){
		memcpy(tmp_probe_pos, probe_pos, num_probes*sizeof(int32_t));
		memcpy(tmp_raw_intensities, intensities, num_probes*sizeof(float64_t));
		//SG_PRINT("raw_intens:%f \n",*tmp_raw_intensities+2);	
	}else{
		memcpy(tmp_probe_pos, m_probe_pos, m_num_probes_cum[m_num_raw_data-1]*sizeof(int32_t));	
		memcpy(tmp_raw_intensities, m_raw_intensities, m_num_probes_cum[m_num_raw_data-1]*sizeof(float64_t));	
		memcpy(tmp_probe_pos+m_num_probes_cum[m_num_raw_data-1], probe_pos, num_probes*sizeof(int32_t));	
		memcpy(tmp_raw_intensities+m_num_probes_cum[m_num_raw_data-1], intensities, num_probes*sizeof(float64_t));	
	}
	delete[] m_probe_pos;
	delete[] m_raw_intensities;
	m_probe_pos = tmp_probe_pos; //new int32_t[num_probes];
	m_raw_intensities = tmp_raw_intensities;//new float64_t[num_probes];

	//memcpy(m_probe_pos, probe_pos, num_probes*sizeof(int32_t));
	//memcpy(m_raw_intensities, intensities, num_probes*sizeof(float64_t));

}

void CDynProg::init_content_svm_value_array(const int32_t p_num_svms)
{
	m_lin_feat.resize_array(p_num_svms, m_seq_len);

	// initialize array
	for (int s=0; s<p_num_svms; s++)
	  for (int p=0; p<m_seq_len; p++)
	    m_lin_feat.set_element(s, p, 0.0) ;
}
void CDynProg::resize_lin_feat(const int32_t num_new_feat)
{
	int32_t dim1, dim2;
	m_lin_feat.get_array_size(dim1, dim2);
	ASSERT(dim1==m_num_lin_feat_plifs_cum[m_num_raw_data-1]);
	ASSERT(dim2==m_seq_len); // == number of candidate positions


	float64_t* arr = m_lin_feat.get_array();
	float64_t* tmp = new float64_t[(dim1+num_new_feat)*dim2];	
	memset(tmp, 0, (dim1+num_new_feat)*dim2*sizeof(float64_t)) ;
	for(int32_t j=0;j<m_seq_len;j++)
                for(int32_t k=0;k<m_num_lin_feat_plifs_cum[m_num_raw_data-1];k++)
			tmp[j*(dim1+num_new_feat)+k] = arr[j*dim1+k];

	m_lin_feat.set_array(tmp, dim1+num_new_feat,dim2, true, true);// copy array and free it later 
	delete[] tmp;

	/*for(int32_t j=0;j<5;j++)
	{
		for(int32_t k=0;k<m_num_lin_feat_plifs_cum[m_num_raw_data];k++)
		{
			SG_PRINT("(%i,%i)%f ",k,j,m_lin_feat.get_element(k,j));
		}
		SG_PRINT("\n");
	}
	m_lin_feat.get_array_size(dim1,dim2);
	SG_PRINT("resize_lin_feat: dim1:%i, dim2:%i\n",dim1,dim2);*/

	//SG_PRINT("resize_lin_feat: done\n");
}

void CDynProg::precompute_tiling_plifs(
	CPlif** PEN, const int32_t* tiling_plif_ids, const int32_t num_tiling_plifs)
{
	m_num_lin_feat_plifs_cum[m_num_raw_data] = m_num_lin_feat_plifs_cum[m_num_raw_data-1]+ num_tiling_plifs;
	float64_t* tiling_plif = new float64_t[num_tiling_plifs];
	float64_t* svm_value = new float64_t[m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs];
	for (int32_t i=0; i<m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs; i++)
		svm_value[i]=0.0;
	int32_t tiling_rows[num_tiling_plifs];
	for (int32_t i=0; i<num_tiling_plifs; i++)
	{
		tiling_plif[i]=0.0;
		CPlif * plif = PEN[tiling_plif_ids[i]];
		tiling_rows[i] = plif->get_use_svm();

		ASSERT(tiling_rows[i]-1==m_num_lin_feat_plifs_cum[m_num_raw_data-1]+i)
	}
	resize_lin_feat(num_tiling_plifs);


	int32_t* p_tiling_pos  = &m_probe_pos[m_num_probes_cum[m_num_raw_data-1]];
	float64_t* p_tiling_data = &m_raw_intensities[m_num_probes_cum[m_num_raw_data-1]];
	int32_t num=m_num_probes_cum[m_num_raw_data-1];

	for (int32_t pos_idx=0;pos_idx<m_seq_len;pos_idx++)
	{
		while (num<m_num_probes_cum[m_num_raw_data]&&*p_tiling_pos<m_pos[pos_idx])
		{
			for (int32_t i=0; i<num_tiling_plifs; i++)
			{
				svm_value[m_num_lin_feat_plifs_cum[m_num_raw_data-1]+i]=*p_tiling_data;
				CPlif * plif = PEN[tiling_plif_ids[i]];
				ASSERT(m_num_lin_feat_plifs_cum[m_num_raw_data-1]+i==plif->get_use_svm()-1);
				plif->set_do_calc(true);
				tiling_plif[i]+=plif->lookup_penalty(0,svm_value);
				plif->set_do_calc(false);
			}
			p_tiling_data++;
			p_tiling_pos++;
			num++;
		}
		for (int32_t i=0; i<num_tiling_plifs; i++)
			m_lin_feat.set_element(tiling_plif[i],tiling_rows[i]-1,pos_idx);
	}
	delete[] svm_value;
	delete[] tiling_plif;
}

void CDynProg::create_word_string()
{
	delete[] m_wordstr;
	m_wordstr=new uint16_t**[5440];
	int32_t k=0;
	int32_t genestr_len=m_genestr.get_dim1();

	m_wordstr[k]=new uint16_t*[m_num_degrees] ;
	for (int32_t j=0; j<m_num_degrees; j++)
	{
		m_wordstr[k][j]=NULL ;
		{
			m_wordstr[k][j]=new uint16_t[genestr_len] ;
			for (int32_t i=0; i<genestr_len; i++)
				switch (m_genestr[i])
				{
					case 'A':
					case 'a': m_wordstr[k][j][i]=0 ; break ;
					case 'C':
					case 'c': m_wordstr[k][j][i]=1 ; break ;
					case 'G':
					case 'g': m_wordstr[k][j][i]=2 ; break ;
					case 'T':
					case 't': m_wordstr[k][j][i]=3 ; break ;
					default: ASSERT(0) ;
				}
			CAlphabet::translate_from_single_order(m_wordstr[k][j], genestr_len, m_word_degree[j]-1, m_word_degree[j], 2) ;
		}
	}
}

void CDynProg::precompute_content_values()
{
	for (int32_t s=0; s<m_num_svms; s++)
	  m_lin_feat.set_element(s, 0, 0.0);

	for (int32_t p=0 ; p<m_seq_len-1 ; p++)
	{
		int32_t from_pos = m_pos[p];
		int32_t to_pos = m_pos[p+1];
		float64_t my_svm_values_unnormalized[m_num_svms] ;
		//SG_PRINT("%i(%i->%i) ",p,from_pos, to_pos);
		
	    ASSERT(from_pos<=m_genestr.get_dim1());
	    ASSERT(to_pos<=m_genestr.get_dim1());
	    
	    for (int32_t s=0; s<m_num_svms; s++)
			my_svm_values_unnormalized[s]=0.0;//precomputed_svm_values.element(s,p);

	    for (int32_t i=from_pos; i<to_pos; i++)
		{
			for (int32_t j=0; j<m_num_degrees; j++)
			{
				uint16_t word = m_wordstr[0][j][i] ;
				for (int32_t s=0; s<m_num_svms; s++)
				{
					// check if this k-mer should be considered for this SVM
					if (m_mod_words.get_element(s,0)==3 && i%3!=m_mod_words.get_element(s,1))
						continue;
					my_svm_values_unnormalized[s] += m_dict_weights[(word+m_cum_num_words_array[j])+s*m_cum_num_words_array[m_num_degrees]] ;
				}
			}
		}
	    for (int32_t s=0; s<m_num_svms; s++)
		{
			float64_t prev = m_lin_feat.get_element(s, p);
			//SG_PRINT("elem (%i, %i, %f)\n", s, p, prev) ;
			if (prev<-1e20 || prev>1e20)
			{
				SG_PRINT("initialization missing (%i, %i, %f)\n", s, p, prev) ;
				prev=0 ;
			}
			m_lin_feat.set_element(prev + my_svm_values_unnormalized[s], s, p+1);
		}
	}
	for (int32_t j=0; j<m_num_degrees; j++)
		delete[] m_wordstr[0][j] ;
	delete[] m_wordstr[0] ;
}

void CDynProg::set_p_vector(float64_t *p, int32_t N)
{
	if (!(N==m_N))
		SG_ERROR("length of start prob vector p (%i) is not equal to the number of states (%i), N: %i\n",N, m_N);

	m_initial_state_distribution_p.set_array(p, N, true, true);
}

void CDynProg::set_q_vector(float64_t *q, int32_t N)
{
	if (!(N==m_N))
		SG_ERROR("length of end prob vector q (%i) is not equal to the number of states (%i), N: %i\n",N, m_N);
	m_end_state_distribution_q.set_array(q, N, true, true);
}

void CDynProg::set_a(float64_t *a, int32_t M, int32_t N)
{
	ASSERT(N==m_N);
	ASSERT(M==N);
	m_transition_matrix_a.set_array(a, N, N, true, true);
	m_transition_matrix_a_deriv.resize_array(N, N);
}

void CDynProg::set_a_id(int32_t *a, int32_t M, int32_t N)
{
	ASSERT(N==m_N);
	ASSERT(M==N);
	m_transition_matrix_a_id.set_array(a, N, N, true, true);
	m_max_a_id = 0;
	for (int32_t i=0; i<N; i++)
	{
		for (int32_t j=0; j<N; j++)
			m_max_a_id=CMath::max(m_max_a_id, m_transition_matrix_a_id.element(i,j));
	}
}

void CDynProg::set_a_trans_matrix(
	float64_t *a_trans, int32_t num_trans, int32_t num_cols)
{
	if (!((num_cols==3) || (num_cols==4)))
		SG_ERROR("!((num_cols==3) || (num_cols==4)), num_cols: %i\n",num_cols);

	delete[] trans_list_forward ;
	delete[] trans_list_forward_cnt ;
	delete[] trans_list_forward_val ;
	delete[] trans_list_forward_id ;

	trans_list_forward = NULL ;
	trans_list_forward_cnt = NULL ;
	trans_list_forward_val = NULL ;
	trans_list_len = 0 ;

	m_transition_matrix_a.zero() ;
	m_transition_matrix_a_id.zero() ;

	mem_initialized = true ;

	trans_list_forward_cnt=NULL ;
	trans_list_len = m_N ;
	trans_list_forward = new T_STATES*[m_N] ;
	trans_list_forward_cnt = new T_STATES[m_N] ;
	trans_list_forward_val = new float64_t*[m_N] ;
	trans_list_forward_id = new int32_t*[m_N] ;
	
	int32_t start_idx=0;
	for (int32_t j=0; j<m_N; j++)
	{
		int32_t old_start_idx=start_idx;

		while (start_idx<num_trans && a_trans[start_idx+num_trans]==j)
		{
			start_idx++;
			
			if (start_idx>1 && start_idx<num_trans)
				ASSERT(a_trans[start_idx+num_trans-1] <= a_trans[start_idx+num_trans]);
		}
		
		if (start_idx>1 && start_idx<num_trans)
			ASSERT(a_trans[start_idx+num_trans-1] <= a_trans[start_idx+num_trans]);
		
		int32_t len=start_idx-old_start_idx;
		ASSERT(len>=0);
		
		trans_list_forward_cnt[j] = 0 ;
		
		if (len>0)
		{
			trans_list_forward[j]     = new T_STATES[len] ;
			trans_list_forward_val[j] = new float64_t[len] ;
			trans_list_forward_id[j] = new int32_t[len] ;
		}
		else
		{
			trans_list_forward[j]     = NULL;
			trans_list_forward_val[j] = NULL;
			trans_list_forward_id[j]  = NULL;
		}
	}
	
	for (int32_t i=0; i<num_trans; i++)
	{
		int32_t from_state   = (int32_t)a_trans[i] ;
		int32_t to_state = (int32_t)a_trans[i+num_trans] ;
		float64_t val = a_trans[i+num_trans*2] ;
		int32_t id = 0 ;
		if (num_cols==4)
			id = (int32_t)a_trans[i+num_trans*3] ;
		//SG_DEBUG( "id=%i\n", id) ;
			
		ASSERT(to_state>=0 && to_state<m_N);
		ASSERT(from_state>=0 && from_state<m_N);
		
		trans_list_forward[to_state][trans_list_forward_cnt[to_state]]=from_state ;
		trans_list_forward_val[to_state][trans_list_forward_cnt[to_state]]=val ;
		trans_list_forward_id[to_state][trans_list_forward_cnt[to_state]]=id ;
		trans_list_forward_cnt[to_state]++ ;
		m_transition_matrix_a.element(from_state, to_state) = val ;
		m_transition_matrix_a_id.element(from_state, to_state) = id ;
		//SG_PRINT("from_state:%i to_state:%i trans_matrix_a_id:%i \n",from_state, to_state,m_transition_matrix_a_id.element(from_state, to_state));
	} ;

	m_max_a_id = 0 ;
	for (int32_t i=0; i<m_N; i++)
		for (int32_t j=0; j<m_N; j++)
		{
			//if (m_transition_matrix_a_id.element(i,j))
			//SG_DEBUG( "(%i,%i)=%i\n", i,j, m_transition_matrix_a_id.element(i,j)) ;
			m_max_a_id = CMath::max(m_max_a_id, m_transition_matrix_a_id.element(i,j)) ;
		}
	//SG_DEBUG( "m_max_a_id=%i\n", m_max_a_id) ;
}


void CDynProg::init_mod_words_array(
	int32_t * mod_words_input, int32_t num_elem, int32_t num_columns)
{
	//for (int32_t i=0; i<num_columns; i++)
	//{
	//	for (int32_t j=0; j<num_elem; j++)
	//		SG_PRINT("%i ",mod_words_input[i*num_elem+j]);
	//	SG_PRINT("\n");
	//}
	m_svm_arrays_clean=false ;

	ASSERT(m_num_svms==num_elem);
	ASSERT(num_columns==2);

	m_mod_words.set_array(mod_words_input, num_elem, 2, true, true) ;
	m_mod_words_array = m_mod_words.get_array() ;
	
	/*SG_DEBUG( "m_mod_words=[") ;
	for (int32_t i=0; i<num_elem; i++)
		SG_DEBUG( "%i, ", p_mod_words_array[i]) ;
		SG_DEBUG( "]\n") ;*/
} 

bool CDynProg::check_svm_arrays()
{
	//SG_DEBUG( "wd_dim1=%d, m_cum_num_words=%d, m_num_words=%d, m_svm_pos_start=%d, num_uniq_w=%d, mod_words_dims=(%d,%d), sign_w=%d,string_w=%d\n m_num_degrees=%d, m_num_svms=%d, m_num_strings=%d", m_word_degree.get_dim1(), m_cum_num_words.get_dim1(), m_num_words.get_dim1(), m_svm_pos_start.get_dim1(), m_num_unique_words.get_dim1(), m_mod_words.get_dim1(), m_mod_words.get_dim2(), m_sign_words.get_dim1(), m_string_words.get_dim1(), m_num_degrees, m_num_svms, m_num_strings);
	if ((m_word_degree.get_dim1()==m_num_degrees) &&
			(m_cum_num_words.get_dim1()==m_num_degrees+1) &&
			(m_num_words.get_dim1()==m_num_degrees) &&
			//(word_used.get_dim1()==m_num_degrees) &&
			//(word_used.get_dim2()==m_num_words[m_num_degrees-1]) &&
			//(word_used.get_dim3()==m_num_strings) &&
			//		(svm_values_unnormalized.get_dim1()==m_num_degrees) &&
			//		(svm_values_unnormalized.get_dim2()==m_num_svms) &&
			//(m_svm_pos_start.get_dim1()==m_num_degrees) &&
			(m_num_unique_words.get_dim1()==m_num_degrees) &&
			(m_mod_words.get_dim1()==m_num_svms) &&
			(m_mod_words.get_dim2()==2) && 
			(m_sign_words.get_dim1()==m_num_svms) &&
			(m_string_words.get_dim1()==m_num_svms))
	{
		m_svm_arrays_clean=true ;
		return true ;
	}
	else
	{
		if ((m_num_unique_words.get_dim1()==m_num_degrees) &&
            (m_mod_words.get_dim1()==m_num_svms) &&
			(m_mod_words.get_dim2()==2) &&
			(m_sign_words.get_dim1()==m_num_svms) &&
            (m_string_words.get_dim1()==m_num_svms))
			SG_PRINT("OK\n") ;
		else
			SG_PRINT("not OK\n") ;

		if (!(m_word_degree.get_dim1()==m_num_degrees))
			SG_WARNING("SVM array: word_degree.get_dim1()!=m_num_degrees") ;
		if (!(m_cum_num_words.get_dim1()==m_num_degrees+1))
			SG_WARNING("SVM array: m_cum_num_words.get_dim1()!=m_num_degrees+1") ;
		if (!(m_num_words.get_dim1()==m_num_degrees))
			SG_WARNING("SVM array: m_num_words.get_dim1()==m_num_degrees") ;
		//if (!(m_svm_pos_start.get_dim1()==m_num_degrees))
		//	SG_WARNING("SVM array: m_svm_pos_start.get_dim1()!=m_num_degrees") ;
		if (!(m_num_unique_words.get_dim1()==m_num_degrees))
			SG_WARNING("SVM array: m_num_unique_words.get_dim1()!=m_num_degrees") ;
		if (!(m_mod_words.get_dim1()==m_num_svms))
			SG_WARNING("SVM array: m_mod_words.get_dim1()!=num_svms") ;
		if (!(m_mod_words.get_dim2()==2))
			SG_WARNING("SVM array: m_mod_words.get_dim2()!=2") ;
		if (!(m_sign_words.get_dim1()==m_num_svms))
			SG_WARNING("SVM array: m_sign_words.get_dim1()!=num_svms") ;
		if (!(m_string_words.get_dim1()==m_num_svms))
			SG_WARNING("SVM array: m_string_words.get_dim1()!=num_svms") ;

		m_svm_arrays_clean=false ;
		return false ;	
	}
}

void CDynProg::set_observation_matrix(float64_t* seq, int32_t* dims, int32_t ndims)
{
	if (ndims!=3)
		SG_ERROR("Expected 3-dimensional Matrix\n");

	int32_t N=dims[0];
	int32_t cand_pos=dims[1];
	int32_t max_num_features=dims[2];

	if (!m_svm_arrays_clean)
	{
		SG_ERROR( "SVM arrays not clean") ;
		return ;
	} ;

	ASSERT(N==m_N);
	ASSERT(cand_pos==m_seq_len);
	ASSERT(m_initial_state_distribution_p.get_dim1()==N);
	ASSERT(m_end_state_distribution_q.get_dim1()==N);
	
	m_observation_matrix.set_array(seq, N, m_seq_len, max_num_features, true, true) ;
}
int32_t CDynProg::get_num_positions()
{
	return m_seq_len;
}

void CDynProg::set_content_type_array(float64_t* seg_path)
{
	if (seg_path!=NULL)
	{
		int32_t *segment_ids = new int32_t[m_seq_len] ;
		float64_t *segment_mask = new float64_t[m_seq_len] ;
		for (int32_t i=0; i<m_seq_len; i++)
		{
		        segment_ids[i] = (int32_t)seg_path[2*i] ;
		        segment_mask[i] = seg_path[2*i+1] ;
		}
		best_path_set_segment_ids_mask(segment_ids, segment_mask, m_seq_len) ;
		delete[] segment_ids;
		delete[] segment_mask;
	}
	else
	{
		int32_t *izeros = new int32_t[m_seq_len] ;
		float64_t *dzeros = new float64_t[m_seq_len] ;
		for (int32_t i=0; i<m_seq_len; i++)
		{
			izeros[i]=0 ;
			dzeros[i]=0.0 ;
		}
		best_path_set_segment_ids_mask(izeros, dzeros, m_seq_len) ;
		delete[] izeros ;
		delete[] dzeros ;
	}
}

void CDynProg::set_pos(int32_t* pos, int32_t seq_len)  
{
	//if (seq_len!=m_observation_matrix.get_dim2())
	//	SG_ERROR( "pos size does not match previous info %i!=%i\n", seq_len, m_observation_matrix.get_dim2()) ;
	
	m_pos.set_array(pos, seq_len, true, true) ;
	m_seq_len = seq_len;
}

void CDynProg::set_orf_info(int32_t* orf_info, int32_t m, int32_t n) 
{
	if (n!=2)
		SG_ERROR( "orf_info size incorrect %i!=2\n", n) ;

	m_orf_info.set_array(orf_info, m, n, true, true) ;
	m_orf_info.set_name("orf_info") ;
}

void CDynProg::set_sparse_features(CSparseFeatures<float64_t>* seq_sparse1, CSparseFeatures<float64_t>* seq_sparse2)
{
	if ((!seq_sparse1 && seq_sparse2) || (seq_sparse1 && !seq_sparse2))
		SG_ERROR("Sparse features must either both be NULL or both NON-NULL\n");

	SG_UNREF(m_seq_sparse1);
	SG_UNREF(m_seq_sparse2);

	m_seq_sparse1=seq_sparse1;
	m_seq_sparse2=seq_sparse2;
	SG_REF(m_seq_sparse1);
	SG_REF(m_seq_sparse2);
}

void CDynProg::set_plif_matrices(CPlifMatrix* pm)
{
	SG_UNREF(m_plif_matrices);

	m_plif_matrices=pm;

	SG_REF(m_plif_matrices);
}

void CDynProg::set_gene_string(char* genestr, int32_t genestr_len)
{
	ASSERT(genestr);
	ASSERT(genestr_len>0);

	m_genestr.set_array(genestr, genestr_len, true, true) ;
}

void CDynProg::set_my_state_seq(int32_t* my_state_seq)
{
	ASSERT(my_state_seq && m_seq_len>0);
	m_my_state_seq.resize_array(m_seq_len);
	for (int32_t i=0; i<m_seq_len; i++)
		m_my_state_seq[i]=my_state_seq[i];
}

void CDynProg::set_my_pos_seq(int32_t* my_pos_seq)
{
	ASSERT(my_pos_seq && m_seq_len>0);
	m_my_pos_seq.resize_array(m_seq_len);
	for (int32_t i=0; i<m_seq_len; i++)
		m_my_pos_seq[i]=my_pos_seq[i];
}

void CDynProg::set_dict_weights(
	float64_t* dictionary_weights, int32_t dict_len, int32_t n)
{
	if (m_num_svms!=n)
		SG_ERROR( "m_dict_weights array does not match num_svms=%i!=%i\n", m_num_svms, n) ;

	m_dict_weights.set_array(dictionary_weights, dict_len, m_num_svms, true, true) ;

	// initialize, so it does not bother when not used
	m_segment_loss.resize_array(m_max_a_id+1, m_max_a_id+1, 2) ;
	m_segment_loss.zero() ;
	m_segment_ids.resize_array(m_observation_matrix.get_dim2()) ;
	m_segment_mask.resize_array(m_observation_matrix.get_dim2()) ;
	m_segment_ids.zero() ;
	m_segment_mask.zero() ;
}

void CDynProg::best_path_set_segment_loss(
	float64_t* segment_loss, int32_t m, int32_t n)
{
	// here we need two matrices. Store it in one: 2N x N
	if (2*m!=n)
		SG_ERROR( "segment_loss should be 2 x quadratic matrix: %i!=%i\n", 2*m, n) ;

	if (m!=m_max_a_id+1)
		SG_ERROR( "segment_loss size should match m_max_a_id: %i!=%i\n", m, m_max_a_id+1) ;

	m_segment_loss.set_array(segment_loss, m, n/2, 2, true, true) ;
	/*for (int32_t i=0; i<n; i++)
		for (int32_t j=0; j<n; j++)
		SG_DEBUG( "loss(%i,%i)=%f\n", i,j, m_segment_loss.element(0,i,j)) ;*/
}

void CDynProg::best_path_set_segment_ids_mask(
	int32_t* segment_ids, float64_t* segment_mask, int32_t m)
{
	int32_t max_id = 0;
	for (int32_t i=1;i<m;i++)
		max_id = CMath::max(max_id,segment_ids[i]);
	//SG_PRINT("max_id: %i, m:%i\n",max_id, m); 	
	m_segment_ids.set_array(segment_ids, m, true, true) ;
	m_segment_ids.set_name("m_segment_ids");
	m_segment_mask.set_array(segment_mask, m, true, true) ;
	m_segment_mask.set_name("m_segment_mask");
}

void CDynProg::get_scores(float64_t **scores, int32_t *m) 
{
	*scores=m_scores.get_array();
	*m=m_scores.get_dim1();
}

void CDynProg::get_states(int32_t **states, int32_t *m, int32_t *n) 
{
	*states=m_states.get_array() ;
	*m=m_states.get_dim1() ;
	*n=m_states.get_dim2() ;
}

void CDynProg::get_positions(
	int32_t **positions, int32_t *m, int32_t *n)
{
	*positions=m_positions.get_array() ;
	*m=m_positions.get_dim1() ;
	*n=m_positions.get_dim2() ;
}

void CDynProg::get_path_scores(float64_t** scores, int32_t* seq_len)
{
	ASSERT(scores && seq_len);
	*scores=m_my_scores.get_array();
	*seq_len=m_my_scores.get_dim1();
}

void CDynProg::get_path_losses(float64_t** losses, int32_t* seq_len)
{
	ASSERT(losses && seq_len);
	*losses=m_my_losses.get_array();
	*seq_len=m_my_losses.get_dim1();
}


////////////////////////////////////////////////////////////////////////////////

float64_t CDynProg::best_path_no_b(
	int32_t max_iter, int32_t &best_iter, int32_t *my_path)
{
	CArray2<T_STATES> psi(max_iter, m_N) ;
	CArray<float64_t>* delta = new CArray<float64_t>(m_N) ;
	CArray<float64_t>* delta_new = new CArray<float64_t>(m_N) ;
	
	{ // initialization
		for (int32_t i=0; i<m_N; i++)
		{
			delta->element(i) = get_p(i) ;
			psi.element(0, i)= 0 ;
		}
	} 
	
	float64_t best_iter_prob = CMath::ALMOST_NEG_INFTY ;
	best_iter = 0 ;
	
	// recursion
	for (int32_t t=1; t<max_iter; t++)
	{
		CArray<float64_t>* dummy;
		int32_t NN=m_N ;
		for (int32_t j=0; j<NN; j++)
		{
			float64_t maxj = delta->element(0) + m_transition_matrix_a.element(0,j);
			int32_t argmax=0;
			
			for (int32_t i=1; i<NN; i++)
			{
				float64_t temp = delta->element(i) + m_transition_matrix_a.element(i,j);
				
				if (temp>maxj)
				{
					maxj=temp;
					argmax=i;
				}
			}
			delta_new->element(j)=maxj ;
			psi.element(t, j)=argmax ;
		}
		
		dummy=delta;
		delta=delta_new;
		delta_new=dummy;	//switch delta/delta_new
		
		{ //termination
			float64_t maxj=delta->element(0)+get_q(0);
			int32_t argmax=0;
			
			for (int32_t i=1; i<m_N; i++)
			{
				float64_t temp=delta->element(i)+get_q(i);
				
				if (temp>maxj)
				{
					maxj=temp;
					argmax=i;
				}
			}
			//pat_prob=maxj;
			
			if (maxj>best_iter_prob)
			{
				my_path[t]=argmax;
				best_iter=t ;
				best_iter_prob = maxj ;
			} ;
		} ;
	}

	
	{ //state sequence backtracking
		for (int32_t t = best_iter; t>0; t--)
		{
			my_path[t-1]=psi.element(t, my_path[t]);
		}
	}

	delete delta ;
	delete delta_new ;
	
	return best_iter_prob ;
}

void CDynProg::best_path_no_b_trans(
	int32_t max_iter, int32_t &max_best_iter, int16_t nbest,
	float64_t *prob_nbest, int32_t *my_paths)
{
	//T_STATES *psi=new T_STATES[max_iter*m_N*nbest] ;
	CArray3<T_STATES> psi(max_iter, m_N, nbest) ;
	CArray3<int16_t> ktable(max_iter, m_N, nbest) ;
	CArray2<int16_t> ktable_ends(max_iter, nbest) ;

	CArray<float64_t> tempvv(nbest*m_N) ;
	CArray<int32_t> tempii(nbest*m_N) ;

	CArray2<T_STATES> path_ends(max_iter, nbest) ;
	CArray2<float64_t> *delta=new CArray2<float64_t>(m_N, nbest) ;
	CArray2<float64_t> *delta_new=new CArray2<float64_t>(m_N, nbest) ;
	CArray2<float64_t> delta_end(max_iter, nbest) ;

	CArray2<int32_t> paths(max_iter, nbest) ;
	paths.set_array(my_paths, max_iter, nbest, false, false) ;

	{ // initialization
		for (T_STATES i=0; i<m_N; i++)
		{
			delta->element(i,0) = get_p(i) ;
			for (int16_t k=1; k<nbest; k++)
			{
				delta->element(i,k)=-CMath::INFTY ;
				ktable.element(0,i,k)=0 ;
			}
		}
	}
	
	// recursion
	for (int32_t t=1; t<max_iter; t++)
	{
		CArray2<float64_t>* dummy=NULL;

		for (T_STATES j=0; j<m_N; j++)
		{
			const T_STATES num_elem   = trans_list_forward_cnt[j] ;
			const T_STATES *elem_list = trans_list_forward[j] ;
			const float64_t *elem_val = trans_list_forward_val[j] ;
			
			int32_t list_len=0 ;
			for (int16_t diff=0; diff<nbest; diff++)
			{
				for (int32_t i=0; i<num_elem; i++)
				{
					T_STATES ii = elem_list[i] ;
					
					tempvv.element(list_len) = -(delta->element(ii,diff) + elem_val[i]) ;
					tempii.element(list_len) = diff*m_N + ii ;
					list_len++ ;
				}
			}
			CMath::qsort_index(tempvv.get_array(), tempii.get_array(), list_len) ;
			
			for (int16_t k=0; k<nbest; k++)
			{
				if (k<list_len)
				{
					delta_new->element(j,k)  = -tempvv[k] ;
					psi.element(t,j,k)      = (tempii[k]%m_N) ;
					ktable.element(t,j,k)   = (tempii[k]-(tempii[k]%m_N))/m_N ;
				}
				else
				{
					delta_new->element(j,k)  = -CMath::INFTY ;
					psi.element(t,j,k)      = 0 ;
					ktable.element(t,j,k)   = 0 ;
				}
			}
		}
		
		dummy=delta;
		delta=delta_new;
		delta_new=dummy;	//switch delta/delta_new
		
		{ //termination
			int32_t list_len = 0 ;
			for (int16_t diff=0; diff<nbest; diff++)
			{
				for (T_STATES i=0; i<m_N; i++)
				{
					tempvv.element(list_len) = -(delta->element(i,diff)+get_q(i));
					tempii.element(list_len) = diff*m_N + i ;
					list_len++ ;
				}
			}
			CMath::qsort_index(tempvv.get_array(), tempii.get_array(), list_len) ;
			
			for (int16_t k=0; k<nbest; k++)
			{
				delta_end.element(t-1,k) = -tempvv[k] ;
				path_ends.element(t-1,k) = (tempii[k]%m_N) ;
				ktable_ends.element(t-1,k) = (tempii[k]-(tempii[k]%m_N))/m_N ;
			}
		}
	}
	
	{ //state sequence backtracking
		max_best_iter=0 ;
		
		CArray<float64_t> sort_delta_end(max_iter*nbest) ;
		CArray<int16_t> sort_k(max_iter*nbest) ;
		CArray<int32_t> sort_t(max_iter*nbest) ;
		CArray<int32_t> sort_idx(max_iter*nbest) ;
		
		int32_t i=0 ;
		for (int32_t iter=0; iter<max_iter-1; iter++)
			for (int16_t k=0; k<nbest; k++)
			{
				sort_delta_end[i]=-delta_end.element(iter,k) ;
				sort_k[i]=k ;
				sort_t[i]=iter+1 ;
				sort_idx[i]=i ;
				i++ ;
			}
		
		CMath::qsort_index(sort_delta_end.get_array(), sort_idx.get_array(), (max_iter-1)*nbest) ;

		for (int16_t n=0; n<nbest; n++)
		{
			int16_t k=sort_k[sort_idx[n]] ;
			int32_t iter=sort_t[sort_idx[n]] ;
			prob_nbest[n]=-sort_delta_end[n] ;

			if (iter>max_best_iter)
				max_best_iter=iter ;
			
			ASSERT(k<nbest) ;
			ASSERT(iter<max_iter) ;
			
			paths.element(iter,n) = path_ends.element(iter-1, k) ;
			int16_t q   = ktable_ends.element(iter-1, k) ;
			
			for (int32_t t = iter; t>0; t--)
			{
				paths.element(t-1,n)=psi.element(t, paths.element(t,n), q);
				q = ktable.element(t, paths.element(t,n), q) ;
			}
		}
	}

	delete delta ;
	delete delta_new ;
}

void CDynProg::init_segment_loss(
	struct segment_loss_struct & loss, int32_t seqlen, int32_t howmuchlookback)
{
#ifdef DYNPROG_TIMING
	MyTime.start() ;
#endif
	int32_t clear_size = CMath::min(howmuchlookback,seqlen) ;
	
	if (!loss.num_segment_id)
	{
		loss.segments_changed       = new int32_t[seqlen] ;
		loss.num_segment_id         = new float64_t[(m_max_a_id+1)*seqlen] ;
		loss.length_segment_id      = new int32_t[(m_max_a_id+1)*seqlen] ;

		clear_size = seqlen ;
	}
	
	for (int32_t j=0; j<clear_size; j++)
	{
		loss.segments_changed[j]=0 ;
		for (int32_t i=0; i<m_max_a_id+1; i++)       
		{
			loss.num_segment_id[i*seqlen+j] = 0;
			loss.length_segment_id[i*seqlen+j] = 0;
		}
	}

	loss.maxlookback = howmuchlookback ;
	loss.seqlen = seqlen;

#ifdef DYNPROG_TIMING
	MyTime.stop() ;
	segment_init_time += MyTime.time_diff_sec() ;
#endif
}

void CDynProg::clear_segment_loss(struct segment_loss_struct & loss) 
{
#ifdef DYNPROG_TIMING
	MyTime.start() ;
#endif
	
	if (loss.num_segment_id != NULL)
	{
		delete[] loss.segments_changed ;
		delete[] loss.num_segment_id ;
		delete[] loss.length_segment_id ;
		loss.segments_changed = NULL ;
		loss.num_segment_id = NULL ;
		loss.length_segment_id = NULL ;
	}
#ifdef DYNPROG_TIMING
	MyTime.stop() ;
	segment_clean_time += MyTime.time_diff_sec() ;
#endif
}

float64_t CDynProg::extend_segment_loss(
	struct segment_loss_struct & loss,
	int32_t segment_id, int32_t pos, int32_t & last_pos, float64_t &last_value)
{
#ifdef DYNPROG_TIMING_DETAIL
	MyTime.start() ;
#endif
	
	if (pos==last_pos)
	{		
#ifdef DYNPROG_TIMING_DETAIL
		MyTime.stop() ;
		segment_extend_time += MyTime.time_diff_sec() ;
#endif
		return last_value ;
	}
	
	ASSERT(pos<last_pos) ;

	last_pos-- ;
	bool changed = false ;
	while (last_pos>=pos)
	{
		if (loss.segments_changed[last_pos])
		{
			changed=true ;
			break ;
		}
		last_pos-- ;
	}
	if (last_pos<pos)
		last_pos = pos ;
	
	if (!changed)
	{
		ASSERT(last_pos>=0) ;
		ASSERT(last_pos<loss.seqlen) ;
		float64_t length_contrib = (m_pos[last_pos]-m_pos[pos])*m_segment_loss.element(m_segment_ids.element(pos), segment_id, 1) ;
		float64_t ret = last_value + length_contrib ;
		last_pos = pos ;
		
#ifdef DYNPROG_TIMING_DETAIL
		MyTime.stop() ;
		segment_extend_time += MyTime.time_diff_sec() ;
#endif
		return ret ;
	}
	
	CArray2<float64_t> num_segment_id(loss.num_segment_id, loss.seqlen, m_max_a_id+1, false, false) ;
	CArray2<int32_t> length_segment_id(loss.length_segment_id, loss.seqlen, m_max_a_id+1, false, false) ;
	float64_t ret = 0.0 ;
	for (int32_t i=0; i<m_max_a_id+1; i++)
	{
		//SG_DEBUG( "%i: %i, %i, %f (%f), %f (%f)\n", pos, num_segment_id.element(pos, i), length_segment_id.element(pos, i), num_segment_id.element(pos, i)*m_segment_loss.element(i, segment_id,0), m_segment_loss.element(i, segment_id, 0), length_segment_id.element(pos, i)*m_segment_loss.element(i, segment_id, 1), m_segment_loss.element(i, segment_id,1)) ;

		if (num_segment_id.element(pos, i)!=0)
		{
			ret += num_segment_id.element(pos, i)*m_segment_loss.element(i, segment_id, 0) ;
		//	SG_PRINT("ret:%f pos:%i i:%i segment_id:%i \n",ret,pos,i,segment_id);
		//	if (ret>0)
		//	{
		//		for (int32_t g=0; g<m_max_a_id+1; g++)
		//			SG_PRINT("g:%i sid(pos, g):%i    ",g,num_segment_id.element(pos, g));
		//		SG_PRINT("\n");
		//	}
		}
		if (length_segment_id.element(pos, i)!=0)
			ret += length_segment_id.element(pos, i)*m_segment_loss.element(i, segment_id, 1) ;
	}
	last_pos = pos ;
	last_value = ret ;

#ifdef DYNPROG_TIMING_DETAIL
	MyTime.stop() ;
	segment_extend_time += MyTime.time_diff_sec() ;
#endif
	return ret ;
}

void CDynProg::find_segment_loss_till_pos(
	int32_t t_end, CArray<int32_t>& segment_ids,
	CArray<float64_t>& segment_mask, struct segment_loss_struct & loss)
{
#ifdef DYNPROG_TIMING
	MyTime.start() ;
#endif
	CArray2<float64_t> num_segment_id(loss.num_segment_id, loss.seqlen, m_max_a_id+1, false, false) ;
	CArray2<int32_t> length_segment_id(loss.length_segment_id, loss.seqlen, m_max_a_id+1, false, false) ;
	
	for (int32_t i=0; i<m_max_a_id+1; i++)
	{
		num_segment_id.element(t_end, i) = 0 ;
		length_segment_id.element(t_end, i) = 0 ;
	}
	int32_t wobble_pos_segment_id_switch = 0 ;
	int32_t last_segment_id = -1 ;
	int32_t ts = t_end-1 ;       
	while ((ts>=0) && (m_pos[t_end] - m_pos[ts] <= loss.maxlookback))
	{
		int32_t cur_segment_id = segment_ids.element(ts) ;
		// allow at most one wobble
		bool wobble_pos = (CMath::abs(segment_mask.element(ts))<1e-7) && (wobble_pos_segment_id_switch==0) ;
		if (!(cur_segment_id<=m_max_a_id))
			SG_ERROR("(cur_segment_id<=m_max_a_id), cur_segment_id:%i m_max_a_id:%i\n",cur_segment_id,m_max_a_id);
		if (!(cur_segment_id>=0))
			 SG_ERROR("cur_segment_id<0, cur_segment_id:%i m_max_a_id:%i ts:%i t_end:%i\n",cur_segment_id,m_max_a_id, ts, t_end);
		
		for (int32_t i=0; i<m_max_a_id+1; i++)
		{
			num_segment_id.element(ts, i) = num_segment_id.element(ts+1, i) ;
			length_segment_id.element(ts, i) = length_segment_id.element(ts+1, i) ;
		}
		
		if (cur_segment_id!=last_segment_id)
		{
			if (wobble_pos)
			{
				//SG_DEBUG( "no change at %i: %i, %i\n", ts, last_segment_id, cur_segment_id) ;
				wobble_pos_segment_id_switch++ ;
				//ASSERT(wobble_pos_segment_id_switch<=1) ;
			}
			else
			{
				//SG_DEBUG( "change at %i: %i, %i\n", ts, last_segment_id, cur_segment_id) ;
				loss.segments_changed[ts] = true ;
				num_segment_id.element(ts, cur_segment_id) += segment_mask.element(ts);
				length_segment_id.element(ts, cur_segment_id) += (int32_t)((m_pos[ts+1]-m_pos[ts])*segment_mask.element(ts));
				wobble_pos_segment_id_switch = 0 ;
			}
			last_segment_id = cur_segment_id ;
		} 
		else
			if (!wobble_pos)
				length_segment_id.element(ts, cur_segment_id) += m_pos[ts+1] - m_pos[ts] ;

		ts--;
	}
#ifdef DYNPROG_TIMING
	MyTime.stop() ;
	segment_pos_time += MyTime.time_diff_sec() ;
#endif
}

bool CDynProg::extend_orf(
	int32_t orf_from, int32_t orf_to, int32_t start, int32_t &last_pos,
	int32_t to)
{
#ifdef DYNPROG_TIMING_DETAIL
	MyTime.start() ;
#endif
	
	if (start<0) 
		start=0 ;
	if (to<0)
		to=0 ;
	
	int32_t orf_target = orf_to-orf_from ;
	if (orf_target<0) orf_target+=3 ;
	
	int32_t pos ;
	if (last_pos==to)
		pos = to-orf_to-3 ;
	else
		pos=last_pos ;

	if (pos<0)
	{
#ifdef DYNPROG_TIMING_DETAIL
		MyTime.stop() ;
		orf_time += MyTime.time_diff_sec() ;
#endif
		return true ;
	}
	
	for (; pos>=start; pos-=3)
		if (m_genestr_stop[pos])
		{
#ifdef DYNPROG_TIMING_DETAIL
			MyTime.stop() ;
			orf_time += MyTime.time_diff_sec() ;
#endif
			return false ;
		}
	
	
	last_pos = CMath::min(pos+3,to-orf_to-3) ;

#ifdef DYNPROG_TIMING_DETAIL
	MyTime.stop() ;
	orf_time += MyTime.time_diff_sec() ;
#endif
	return true ;
}

void CDynProg::compute_nbest_paths(int32_t max_num_signals, bool use_orf,
		int16_t nbest, bool with_loss, bool with_multiple_sequences)
	{

	//FIXME we need checks here if all the fields are of right size
	//SG_PRINT("m_seq_len: %i\n", m_seq_len);
	//SG_PRINT("m_pos[0]: %i\n", m_pos[0]);
	//SG_PRINT("\n");

	//FIXME these variables can go away when compute_nbest_paths uses them
	//instead of the local pointers below
	const float64_t* seq_array = m_observation_matrix.get_array();
	m_scores.resize_array(nbest) ;
	m_states.resize_array(nbest, m_observation_matrix.get_dim2()) ;
	m_positions.resize_array(nbest, m_observation_matrix.get_dim2()) ;

	for (int32_t i=0; i<nbest; i++)
	{
		m_scores[i]=-1;
		for (int32_t j=0; j<m_observation_matrix.get_dim2(); j++)
		{
			m_states.element(i,j)=-1;
			m_positions.element(i,j)=-1;
		}
	}
	float64_t* prob_nbest=m_scores.get_array();
	int32_t* my_state_seq=m_states.get_array();
	int32_t* my_pos_seq=m_positions.get_array();

	CPlifBase** Plif_matrix=m_plif_matrices->get_plif_matrix();
	CPlifBase** Plif_state_signals=m_plif_matrices->get_state_signals();
	//END FIXME


#ifdef DYNPROG_TIMING
		segment_init_time = 0.0 ;
		segment_pos_time = 0.0 ;
		segment_extend_time = 0.0 ;
		segment_clean_time = 0.0 ;
		orf_time = 0.0 ;
		svm_init_time = 0.0 ;
		svm_pos_time = 0.0 ;
		svm_clean_time = 0.0 ;
		inner_loop_time = 0.0 ;
		content_svm_values_time = 0.0 ;
		content_plifs_time = 0.0 ;
		inner_loop_max_time = 0.0 ;
		long_transition_time = 0.0 ;

		MyTime2.start() ;
#endif

		if (!m_svm_arrays_clean)
		{
			SG_ERROR( "SVM arrays not clean") ;
			return ;
		}

#ifdef DYNPROG_DEBUG
		m_transition_matrix_a.set_name("transition_matrix");
		m_transition_matrix_a.display_array();
		m_mod_words.display_array() ;
		m_sign_words.display_array() ;
		m_string_words.display_array() ;
		//SG_PRINT("use_orf = %i\n", use_orf) ;
#endif

		int32_t max_look_back = 1000 ;
		bool use_svm = false ;

		SG_DEBUG("m_N:%i, m_seq_len:%i, max_num_signals:%i\n",m_N, m_seq_len, max_num_signals) ;

		//	for (int32_t i=0;i<m_N*m_seq_len*max_num_signals;i++)
		//		SG_PRINT("(%i)%0.2f ",i,seq_array[i]);

		//CArray2<CPlifBase*> PEN(Plif_matrix, m_N, m_N, false, false) ;
		CArray2<CPlifBase*> PEN(Plif_matrix, m_N, m_N, false, true) ;
		PEN.set_name("PEN");
		//CArray2<CPlifBase*> PEN_state_signals(Plif_state_signals, m_N, max_num_signals, false, false) ;
		CArray2<CPlifBase*> PEN_state_signals(Plif_state_signals, m_N, max_num_signals, false, true) ;
		PEN_state_signals.set_name("state_signals");

		CArray2<float64_t> seq(m_N, m_seq_len) ;
		seq.set_name("seq") ;
		seq.zero() ;

		float64_t svm_value[m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs];
		{ // initialize svm_svalue
			for (int32_t s=0; s<m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs; s++)
				svm_value[s]=0 ;
		}

		{ // convert seq_input to seq
			// this is independent of the svm values 

			//CArray3<float64_t> seq_input(seq_array, m_N, m_seq_len, max_num_signals) ;
			CArray3<float64_t> *seq_input=NULL ;
			if (seq_array!=NULL)
			{
				//SG_PRINT("using dense seq_array\n") ;

				seq_input=new CArray3<float64_t>(seq_array, m_N, m_seq_len, max_num_signals) ;
				seq_input->set_name("seq_input") ;
				//seq_input.display_array() ;

				ASSERT(m_seq_sparse1==NULL) ;
				ASSERT(m_seq_sparse2==NULL) ;
			} else
			{
				SG_PRINT("using sparse seq_array\n") ;

				ASSERT(m_seq_sparse1!=NULL) ;
				ASSERT(m_seq_sparse2!=NULL) ;
				ASSERT(max_num_signals==2) ;
			}

			for (int32_t i=0; i<m_N; i++)
				for (int32_t j=0; j<m_seq_len; j++)
					seq.element(i,j) = 0 ;

			for (int32_t i=0; i<m_N; i++)
				for (int32_t j=0; j<m_seq_len; j++)
					for (int32_t k=0; k<max_num_signals; k++)
					{
						if ((PEN_state_signals.element(i,k)==NULL) && (k==0))
						{
							// no plif
							if (seq_input!=NULL)
								seq.element(i,j) = seq_input->element(i,j,k) ;
							else
							{
								if (k==0)
									seq.element(i,j) = m_seq_sparse1->get_element(i,j) ;
								if (k==1)
									seq.element(i,j) = m_seq_sparse2->get_element(i,j) ;
							}
							break ;
						}
						if (PEN_state_signals.element(i,k)!=NULL)
						{
							if (seq_input!=NULL)
							{
								// just one plif
								if (CMath::is_finite(seq_input->element(i,j,k)))
									seq.element(i,j) += PEN_state_signals.element(i,k)->lookup_penalty(seq_input->element(i,j,k), svm_value) ;
								else
									// keep infinity values
									seq.element(i,j) = seq_input->element(i, j, k) ;
							}
							else
							{
								if (k==0)
								{
									// just one plif
									if (CMath::is_finite(m_seq_sparse1->get_element(i,j)))
										seq.element(i,j) += PEN_state_signals.element(i,k)->lookup_penalty(m_seq_sparse1->get_element(i,j), svm_value) ;
									else
										// keep infinity values
										seq.element(i,j) = m_seq_sparse1->get_element(i, j) ;
								}
								if (k==1)
								{
									// just one plif
									if (CMath::is_finite(m_seq_sparse2->get_element(i,j)))
										seq.element(i,j) += PEN_state_signals.element(i,k)->lookup_penalty(m_seq_sparse2->get_element(i,j), svm_value) ;
									else
										// keep infinity values
										seq.element(i,j) = m_seq_sparse2->get_element(i, j) ;
								}
							}
						} 
						else
							break ;
					}
			delete seq_input ;
		}

		// allow longer transitions than look_back
		bool long_transitions = false; //m_long_transitions ;
		CArray2<int32_t> long_transition_content_position(m_N,m_N) ;
		CArray2<int32_t> long_transition_content_start(m_N,m_N) ;
		CArray2<float64_t> long_transition_content_scores(m_N,m_N) ;
		CArray2<float64_t> long_transition_content_scores_pen(m_N,m_N) ;
		CArray2<float64_t> long_transition_content_scores_prev(m_N,m_N) ;
		CArray2<float64_t> long_transition_content_scores_elem(m_N,m_N) ;

		if (with_loss || nbest!=1)
		{
			SG_DEBUG("disabling long transitions\n") ;
			long_transitions = false ;
		}
		long_transition_content_scores.set_const(-CMath::INFTY);
		long_transition_content_scores_pen.set_const(0) ;
		long_transition_content_scores_elem.set_const(0) ;
		long_transition_content_scores_prev.set_const(0) ;
		long_transition_content_start.zero() ;
		long_transition_content_position.zero() ;

		CArray2<int32_t> look_back(m_N,m_N) ;

		{ // determine maximal length of look-back
			for (int32_t i=0; i<m_N; i++)
				for (int32_t j=0; j<m_N; j++)
					look_back.set_element(INT_MAX, i, j) ;

			for (int32_t j=0; j<m_N; j++)
			{
				// only consider transitions that are actually allowed
				const T_STATES num_elem   = trans_list_forward_cnt[j] ;
				const T_STATES *elem_list = trans_list_forward[j] ;

				for (int32_t i=0; i<num_elem; i++)
				{
					T_STATES ii = elem_list[i] ;

					CPlifBase *penij=PEN.element(j, ii) ;
					if (penij==NULL)
					{
						if (long_transitions)
							look_back.set_element(m_long_transition_threshold, j, ii) ;
						continue ;
					}

					/* if the transition is an ORF or we do computation with loss, we have to disable long transitions */
					if ((m_orf_info.element(ii,0)!=-1) || (m_orf_info.element(j,1)!=-1) || (!long_transitions))
					{
						look_back.set_element(CMath::ceil(penij->get_max_value()), j, ii) ;
						if (CMath::ceil(penij->get_max_value()) > max_look_back)
						{
							SG_DEBUG( "%d %d -> value: %f\n", ii,j,penij->get_max_value());
							max_look_back=(int32_t) (CMath::ceil(penij->get_max_value()));
						}
					}
					else
						look_back.set_element(CMath::min( (int32_t)CMath::ceil(penij->get_max_value()), m_long_transition_threshold ), j, ii) ;

					if (penij->uses_svm_values())
						use_svm=true ;
				}
			}
			/* make sure max_look_back is at least as long as a long transition */
			if (long_transitions)
				max_look_back = CMath::max(m_long_transition_threshold, max_look_back) ;

			/* make sure max_look_back is not longer than the whole string */
			max_look_back = CMath::min(m_genestr.get_dim1(), max_look_back) ;

			int32_t num_long_transitions = 0 ;
			for (int32_t i=0; i<m_N; i++)
				for (int32_t j=0; j<m_N; j++)
				{
					if (look_back.get_element(i,j)==m_long_transition_threshold)
						num_long_transitions++ ;
					if (look_back.get_element(i,j)==INT_MAX)
					{
						if (long_transitions)
							look_back.set_element(m_long_transition_threshold, i, j) ;
						else
							look_back.set_element(max_look_back, i, j) ;
					}
				}
			SG_DEBUG("Using %i long transitions\n", num_long_transitions) ;
		}

		//SG_PRINT("use_svm=%i, genestr_len: \n", use_svm, m_genestr.get_dim1()) ;
		SG_DEBUG("use_svm=%i\n", use_svm) ;

		SG_DEBUG("maxlook: %d m_N: %d nbest: %d \n", max_look_back, m_N, nbest);
		const int32_t look_back_buflen = (max_look_back*m_N+1)*nbest ;
		SG_DEBUG("look_back_buflen=%i\n", look_back_buflen) ;
		/*const float64_t mem_use = (float64_t)(m_seq_len*m_N*nbest*(sizeof(T_STATES)+sizeof(int16_t)+sizeof(int32_t))+
		  look_back_buflen*(2*sizeof(float64_t)+sizeof(int32_t))+
		  m_seq_len*(sizeof(T_STATES)+sizeof(int32_t))+
		  m_genestr.get_dim1()*sizeof(bool))/(1024*1024);*/

		//bool is_big = (mem_use>200) || (m_seq_len>5000) ;

		/*if (is_big)
		  {
		  SG_DEBUG("calling compute_nbest_paths: m_seq_len=%i, m_N=%i, lookback=%i nbest=%i\n", 
		  m_seq_len, m_N, max_look_back, nbest) ;
		  SG_DEBUG("allocating %1.2fMB of memory\n", 
		  mem_use) ;
		  }*/
		ASSERT(nbest<32000) ;



		CArray3<float64_t> delta(m_seq_len, m_N, nbest) ;
		delta.set_name("delta");
		float64_t* delta_array = delta.get_array() ;
		//delta.zero() ;

		CArray3<T_STATES> psi(m_seq_len, m_N, nbest) ;
		psi.set_name("psi");
		//psi.zero() ;

		CArray3<int16_t> ktable(m_seq_len, m_N, nbest) ;
		ktable.set_name("ktable");
		//ktable.zero() ;

		CArray3<int32_t> ptable(m_seq_len, m_N, nbest) ;	
		ptable.set_name("ptable");
		//ptable.zero() ;

		CArray<float64_t> delta_end(nbest) ;
		delta_end.set_name("delta_end");
		//delta_end.zero() ;

		CArray<T_STATES> path_ends(nbest) ;
		path_ends.set_name("path_ends");
		//path_ends.zero() ;

		CArray<int16_t> ktable_end(nbest) ;
		ktable_end.set_name("ktable_end");
		//ktable_end.zero() ;

		float64_t * fixedtempvv=new float64_t[look_back_buflen] ;
		memset(fixedtempvv, 0, look_back_buflen*sizeof(float64_t)) ;
		int32_t * fixedtempii=new int32_t[look_back_buflen] ;
		memset(fixedtempii, 0, look_back_buflen*sizeof(int32_t)) ;

		CArray<float64_t> oldtempvv(look_back_buflen) ;
		oldtempvv.set_name("oldtempvv");
		CArray<float64_t> oldtempvv2(look_back_buflen) ;
		oldtempvv2.set_name("oldtempvv2");
		//oldtempvv.zero() ;
		//oldtempvv.display_size() ;

		CArray<int32_t> oldtempii(look_back_buflen) ;
		oldtempii.set_name("oldtempii");
		CArray<int32_t> oldtempii2(look_back_buflen) ;
		oldtempii2.set_name("oldtempii2");
		//oldtempii.zero() ;

		CArray<T_STATES> state_seq(m_seq_len) ;
		state_seq.set_name("state_seq");
		//state_seq.zero() ;

		CArray<int32_t> pos_seq(m_seq_len) ;
		pos_seq.set_name("pos_seq");
		//pos_seq.zero() ;


		m_dict_weights.set_name("dict_weights") ;
		m_word_degree.set_name("word_degree") ;
		m_cum_num_words.set_name("cum_num_words") ;
		m_num_words.set_name("num_words") ;
		//word_used.set_name("word_used") ;
		//svm_values_unnormalized.set_name("svm_values_unnormalized") ;
		//m_svm_pos_start.set_name("svm_pos_start") ;
		m_num_unique_words.set_name("num_unique_words") ;

		PEN.set_name("PEN") ;
		seq.set_name("seq") ;

		delta.set_name("delta") ;
		psi.set_name("psi") ;
		ktable.set_name("ktable") ;
		ptable.set_name("ptable") ;
		delta_end.set_name("delta_end") ;
		path_ends.set_name("path_ends") ;
		ktable_end.set_name("ktable_end") ;

#ifdef USE_TMP_ARRAYCLASS
		fixedtempvv.set_name("fixedtempvv") ;
		fixedtempii.set_name("fixedtempvv") ;
#endif

		oldtempvv.set_name("oldtempvv") ;
		oldtempvv2.set_name("oldtempvv2") ;
		oldtempii.set_name("oldtempii") ;
		oldtempii2.set_name("oldtempii2") ;


		//////////////////////////////////////////////////////////////////////////////// 

#ifdef DYNPROG_DEBUG
		state_seq.display_size() ;
		pos_seq.display_size() ;

		m_dict_weights.display_size() ;
		m_word_degree.display_array() ;
		m_cum_num_words.display_array() ;
		m_num_words.display_array() ;
		//word_used.display_size() ;
		//svm_values_unnormalized.display_size() ;
		//m_svm_pos_start.display_array() ;
		m_num_unique_words.display_array() ;

		PEN.display_size() ;
		PEN_state_signals.display_size() ;
		seq.display_size() ;
		m_orf_info.display_size() ;

		//m_genestr_stop.display_size() ;
		delta.display_size() ;
		psi.display_size() ;
		ktable.display_size() ;
		ptable.display_size() ;
		delta_end.display_size() ;
		path_ends.display_size() ;
		ktable_end.display_size() ;

#ifdef USE_TMP_ARRAYCLASS
		fixedtempvv.display_size() ;
		fixedtempii.display_size() ;
#endif

		//oldtempvv.display_size() ;
		//oldtempii.display_size() ;

		state_seq.display_size() ;
		pos_seq.display_size() ;

		//seq.zero() ;

#endif //DYNPROG_DEBUG

		////////////////////////////////////////////////////////////////////////////////



		{
			for (int32_t s=0; s<m_num_svms; s++)
				ASSERT(m_string_words_array[s]<1)  ;
		}


		//CArray2<int32_t*> trans_matrix_svms(m_N,m_N);
		//CArray2<int32_t> trans_matrix_num_svms(m_N,m_N);

		{ // initialization

			for (T_STATES i=0; i<m_N; i++)
			{
				//delta.element(0, i, 0) = get_p(i) + seq.element(i,0) ;        // get_p defined in HMM.h to be equiv to initial_state_distribution
				delta.element(delta_array, 0, i, 0, m_seq_len, m_N) = get_p(i) + seq.element(i,0) ;        // get_p defined in HMM.h to be equiv to initial_state_distribution
				psi.element(0,i,0)   = 0 ;
				if (nbest>1)
					ktable.element(0,i,0)  = 0 ;
				ptable.element(0,i,0)  = 0 ;
				for (int16_t k=1; k<nbest; k++)
				{
					int32_t dim1, dim2, dim3 ;
					delta.get_array_size(dim1, dim2, dim3) ;
					//SG_DEBUG("i=%i, k=%i -- %i, %i, %i\n", i, k, dim1, dim2, dim3) ;
					//delta.element(0, i, k)    = -CMath::INFTY ;
					delta.element(delta_array, 0, i, k, m_seq_len, m_N)    = -CMath::INFTY ;
					psi.element(0,i,0)      = 0 ;                  // <--- what's this for?
					if (nbest>1)
						ktable.element(0,i,k)     = 0 ;
					ptable.element(0,i,k)     = 0 ;
				}
				/*
				   for (T_STATES j=0; j<m_N; j++)
				   {
				   CPlifBase * penalty = PEN.element(i,j) ;
				   int32_t num_current_svms=0;
				   int32_t svm_ids[] = {-8, -7, -6, -5, -4, -3, -2, -1};
				   if (penalty)
				   {
				   SG_PRINT("trans %i -> %i \n",i,j);
				   penalty->get_used_svms(&num_current_svms, svm_ids);
				   trans_matrix_svms.set_element(svm_ids,i,j);
				   for (int32_t l=0;l<num_current_svms;l++)
				   SG_PRINT("svm_ids[%i]: %i \n",l,svm_ids[l]);
				   trans_matrix_num_svms.set_element(num_current_svms,i,j);
				   }
				   }
				   */

			}
		}

		/*struct svm_values_struct svs;
		  svs.num_unique_words = NULL;
		  svs.svm_values = NULL;
		  svs.svm_values_unnormalized = NULL;
		  svs.word_used = NULL;*/

		struct segment_loss_struct loss;
		loss.segments_changed = NULL;
		loss.num_segment_id = NULL;

		SG_DEBUG("START_RECURSION \n\n");

		// recursion
		for (int32_t t=1; t<m_seq_len; t++)
		{
			//if (is_big && t%(1+(m_seq_len/1000))==1)
			//	SG_PROGRESS(t, 0, m_seq_len);
			//SG_PRINT("%i\n", t) ;

			if (with_loss)
			{
				init_segment_loss(loss, m_seq_len, max_look_back);
				find_segment_loss_till_pos(t, m_segment_ids, m_segment_mask, loss);  
			}

			for (T_STATES j=0; j<m_N; j++)
			{
				if (seq.element(j,t)<=-1e20)
				{ // if we cannot observe the symbol here, then we can omit the rest
					for (int16_t k=0; k<nbest; k++)
					{
						delta.element(delta_array, t, j, k, m_seq_len, m_N)    = seq.element(j,t) ;
						psi.element(t,j,k)         = 0 ;
						if (nbest>1)
							ktable.element(t,j,k)  = 0 ;
						ptable.element(t,j,k)      = 0 ;
					}
				}
				else
				{
					const T_STATES num_elem   = trans_list_forward_cnt[j] ;
					const T_STATES *elem_list = trans_list_forward[j] ;
					const float64_t *elem_val      = trans_list_forward_val[j] ;
					const int32_t *elem_id      = trans_list_forward_id[j] ;

					int32_t fixed_list_len = 0 ;
					float64_t fixedtempvv_ = CMath::INFTY ;
					int32_t fixedtempii_ = 0 ;
					bool fixedtemplong = false ;

					for (int32_t i=0; i<num_elem; i++)
					{
						T_STATES ii = elem_list[i] ;

						const CPlifBase * penalty = PEN.element(j,ii) ;

						/*int32_t look_back = max_look_back ;
						  if (0)
						  { // find lookback length
						  CPlifBase *pen = (CPlifBase*) penalty ;
						  if (pen!=NULL)
						  look_back=(int32_t) (CMath::ceil(pen->get_max_value()));
						  if (look_back>=1e6)
						  SG_PRINT("%i,%i -> %d from %ld\n", j, ii, look_back, (long)pen) ;
						  ASSERT(look_back<1e6);
						  } */

						int32_t look_back_ = look_back.element(j, ii) ;

						int32_t orf_from = m_orf_info.element(ii,0) ;
						int32_t orf_to   = m_orf_info.element(j,1) ;
						if((orf_from!=-1)!=(orf_to!=-1))
							SG_DEBUG("j=%i  ii=%i  orf_from=%i orf_to=%i p=%1.2f\n", j, ii, orf_from, orf_to, elem_val[i]) ;
						ASSERT((orf_from!=-1)==(orf_to!=-1)) ;

						int32_t orf_target = -1 ;
						if (orf_from!=-1)
						{
							orf_target=orf_to-orf_from ;
							if (orf_target<0) 
								orf_target+=3 ;
							ASSERT(orf_target>=0 && orf_target<3) ;
						}

						int32_t orf_last_pos = m_pos[t] ;
						int32_t loss_last_pos = t ;
						float64_t last_loss = 0.0 ;

#ifdef DYNPROG_TIMING
						MyTime3.start() ;
#endif				
						int32_t num_ok_pos = 0 ;
						float64_t last_mval=0 ;
						int32_t last_ts = 0 ;

						for (int32_t ts=t-1; ts>=0 && m_pos[t]-m_pos[ts]<=look_back_; ts--)
						{
							bool ok ;
							//int32_t plen=t-ts;

							/*for (int32_t s=0; s<m_num_svms; s++)
							  if ((fabs(svs.svm_values[s*svs.seqlen+plen]-svs2.svm_values[s*svs.seqlen+plen])>1e-6) ||
							  (fabs(svs.svm_values[s*svs.seqlen+plen]-svs3.svm_values[s*svs.seqlen+plen])>1e-6))
							  {
							  SG_DEBUG( "s=%i, t=%i, ts=%i, %1.5e, %1.5e, %1.5e\n", s, t, ts, svs.svm_values[s*svs.seqlen+plen], svs2.svm_values[s*svs.seqlen+plen], svs3.svm_values[s*svs.seqlen+plen]);
							  }*/

							if (orf_target==-1)
								ok=true ;
							else if (m_pos[ts]!=-1 && (m_pos[t]-m_pos[ts])%3==orf_target)
								ok=(!use_orf) || extend_orf(orf_from, orf_to, m_pos[ts], orf_last_pos, m_pos[t]) ;
							else
								ok=false ;

							if (ok)
							{

								float64_t segment_loss = 0.0 ;
								if (with_loss)
									segment_loss = extend_segment_loss(loss, elem_id[i], ts, loss_last_pos, last_loss) ;

								////////////////////////////////////////////////////////
								// BEST_PATH_TRANS
								////////////////////////////////////////////////////////

								int32_t frame = m_orf_info.element(ii,0);
								lookup_content_svm_values(ts, t, m_pos[ts], m_pos[t], svm_value, frame);

								float64_t pen_val = 0.0 ;
								if (penalty)
								{
#ifdef DYNPROG_TIMING_DETAIL
									MyTime.start() ;
#endif								
									pen_val = penalty->lookup_penalty(m_pos[t]-m_pos[ts], svm_value) ;

#ifdef DYNPROG_TIMING_DETAIL
									MyTime.stop() ;
									content_plifs_time += MyTime.time_diff_sec() ;
#endif
								}

#ifdef DYNPROG_TIMING_DETAIL
								MyTime.start() ;
#endif								
								num_ok_pos++ ;

								if (nbest==1)
								{
									float64_t  val        = elem_val[i] + pen_val ;
									if (with_loss)
										val              += segment_loss ;

									float64_t mval = -(val + delta.element(delta_array, ts, ii, 0, m_seq_len, m_N)) ;

									if (mval<fixedtempvv_)
									{
										fixedtempvv_ = mval ;
										fixedtempii_ = ii + ts*m_N;
										fixed_list_len = 1 ;
										fixedtemplong = false ;
									}
									last_mval = mval ;
									last_ts = ts ;
								}
								else
								{
									for (int16_t diff=0; diff<nbest; diff++)
									{
										float64_t  val        = elem_val[i]  ;
										val                  += pen_val ;
										if (with_loss)
											val              += segment_loss ;

										float64_t mval = -(val + delta.element(delta_array, ts, ii, diff, m_seq_len, m_N)) ;

										/* only place -val in fixedtempvv if it is one of the nbest lowest values in there */
										/* fixedtempvv[i], i=0:nbest-1, is sorted so that fixedtempvv[0] <= fixedtempvv[1] <= ...*/
										/* fixed_list_len has the number of elements in fixedtempvv */

										if ((fixed_list_len < nbest) || ((0==fixed_list_len) || (mval < fixedtempvv[fixed_list_len-1])))
										{
											if ( (fixed_list_len<nbest) && ((0==fixed_list_len) || (mval>fixedtempvv[fixed_list_len-1])) )
											{
												fixedtempvv[fixed_list_len] = mval ;
												fixedtempii[fixed_list_len] = ii + diff*m_N + ts*m_N*nbest;
												fixed_list_len++ ;
											}
											else  // must have mval < fixedtempvv[fixed_list_len-1]
											{
												int32_t addhere = fixed_list_len;
												while ((addhere > 0) && (mval < fixedtempvv[addhere-1]))
													addhere--;

												// move everything from addhere+1 one forward 
												for (int32_t jj=fixed_list_len-1; jj>addhere; jj--)
												{
													fixedtempvv[jj] = fixedtempvv[jj-1];
													fixedtempii[jj] = fixedtempii[jj-1];
												}

												fixedtempvv[addhere] = mval;
												fixedtempii[addhere] = ii + diff*m_N + ts*m_N*nbest;

												if (fixed_list_len < nbest)
													fixed_list_len++;
											}
										}
									}
								}
#ifdef DYNPROG_TIMING_DETAIL
								MyTime.stop() ;
								inner_loop_max_time += MyTime.time_diff_sec() ;
#endif
							}
						}
#ifdef DYNPROG_TIMING
						MyTime3.stop() ;
						inner_loop_time += MyTime3.time_diff_sec() ;
#endif

						/* long transition stuff */
						/* only do this, if 
						 * this feature is enabled
						 * this is not a transition with ORF restrictions
						 * the loss is switched off
						 * nbest=1
						 */ 
#ifdef DYNPROG_TIMING
						MyTime3.start() ;
#endif
						if ( long_transitions && orf_target==-1 && look_back_ == m_long_transition_threshold )
						{
							int ts = t ;
							while (ts>0 && m_pos[t]-m_pos[ts-1] < m_long_transition_threshold)
								ts-- ;

							if (ts>0)
							{
								ASSERT((m_pos[t]-m_pos[ts-1] >= m_long_transition_threshold) && (m_pos[t]-m_pos[ts] < m_long_transition_threshold))

									/* only consider this transition, if the right position was found */
									float pen_val = 0.0 ;
								if (penalty)
								{
									int32_t frame = m_orf_info.element(ii,0);
									lookup_content_svm_values(ts, t, m_pos[ts], m_pos[t], svm_value, frame);
									pen_val = penalty->lookup_penalty(m_pos[t]-m_pos[ts], svm_value) ;
								}
								//if (m_pos[ts]==3812)
								//	SG_PRINT("%i,%i,%i: pen_val=%1.5f (t=%i, ts=%i, ts-1=%i, ts+1=%i)\n", m_pos[t], j, ii, pen_val, m_pos[t], m_pos[ts], m_pos[ts-1], m_pos[ts+1]) ;

								float64_t mval = -(long_transition_content_scores.get_element(ii, j) + pen_val*0.5) ;
								/* // incomplete extra check
								   float64_t mval2 = CMath::INFTY ;
								   if (long_transition_content_position.get_element(ii,j)>0)
								   mval2 = -( pen_val/2 + delta.element(delta_array, long_transition_content_position.get_element(ii,j), ii, 0, m_seq_len, m_N) + elem_val[i]  ) ;
								   if (fabs(mval-mval2)>1e-8)
								   SG_PRINT("!!!  mval=%1.2f  mval2=%1.2f\n", mval, mval2) ;

								   if (long_transition_content_position.get_element(ii,j)>0)
								   mval2 = -( pen_val/2 + delta.element(delta_array, long_transition_content_position.get_element(ii,j), ii, 0, m_seq_len, m_N) + elem_val[i]  ) ;
								   */


								if ((mval < fixedtempvv_) &&
										(m_pos[t] - m_pos[long_transition_content_position.get_element(ii, j)])<=m_long_transition_max)
								{
									/* then the long transition is better than the short one => replace it */ 
									int32_t fromtjk =  fixedtempii_ ;
									/*SG_PRINT("%i,%i: Long transition (%1.5f=-(%1.5f+%1.5f+%1.5f+%1.5f), %i) to m_pos %i better than short transition (%1.5f,%i) to m_pos %i \n", 
									  m_pos[t], j, 
									  mval, pen_val*0.5, long_transition_content_scores_pen.get_element(ii, j), long_transition_content_scores_elem.get_element(ii, j), long_transition_content_scores_prev.get_element(ii, j), ii, 
									  m_pos[long_transition_content_position.get_element(ii, j)], 
									  fixedtempvv_, (fromtjk%m_N), m_pos[(fromtjk-(fromtjk%(m_N*nbest)))/(m_N*nbest)]) ;*/
									ASSERT((fromtjk-(fromtjk%(m_N*nbest)))/(m_N*nbest)==0 || m_pos[(fromtjk-(fromtjk%(m_N*nbest)))/(m_N*nbest)]>=m_pos[long_transition_content_position.get_element(ii, j)] || fixedtemplong) ;

									fixedtempvv_ = mval ;
									fixedtempii_ = ii + m_N*long_transition_content_position.get_element(ii, j) ;
									fixed_list_len = 1 ;
									fixedtemplong = true ;
								}

								int32_t start = long_transition_content_start.get_element(ii, j) ;
								for (int32_t ts2=start; m_pos[t]-m_pos[ts2] > m_long_transition_threshold ; ts2++)
								{
									int32_t t2 = ts2 ;
									while (t2<=t && m_pos[t2+1]-m_pos[ts2]<m_long_transition_threshold)
										t2++ ;

									ASSERT(m_pos[t2+1]-m_pos[ts2] >= m_long_transition_threshold || t2==t) ;
									ASSERT(m_pos[t2]-m_pos[ts2] < m_long_transition_threshold) ;


									/* recompute penalty, if necessary */
									if (penalty)
									{
										int32_t frame = m_orf_info.element(ii,0);
										lookup_content_svm_values(ts2, t, m_pos[ts2], m_pos[t2], svm_value, frame);
										pen_val = penalty->lookup_penalty(m_pos[t2]-m_pos[ts2], svm_value) ;
									}

									//if (m_pos[ts2]==3812)
									//{
									//	SG_PRINT("%i - %i   vs  %i - %i\n", m_pos[t], m_pos[ts], m_pos[t2], m_pos[ts2]) ;
									//	SG_PRINT("ts=%i  t=%i  ts2=%i  m_seq_len=%i\n", m_pos[ts], m_pos[t], m_pos[ts2], m_seq_len) ;
									//}

									float64_t mval_trans = -( elem_val[i] + pen_val*0.5 + delta.element(delta_array, ts2, ii, 0, m_seq_len, m_N) ) ;
									//float64_t mval_trans = -( elem_val[i] + delta.element(delta_array, ts, ii, 0, m_seq_len, m_N) ) ; // enable this for the incomplete extra check

									if (m_pos[t2] - m_pos[long_transition_content_position.get_element(ii, j)] > m_long_transition_max)
									{
										long_transition_content_scores.set_element(-CMath::INFTY, ii, j) ;
										long_transition_content_scores_pen.set_element(0, ii, j) ;
										long_transition_content_scores_elem.set_element(0, ii, j) ;
										long_transition_content_scores_prev.set_element(0, ii, j) ;
										long_transition_content_position.set_element(0, ii, j) ;
									}

									if (-long_transition_content_scores.get_element(ii, j) > mval_trans )
									{
										/* then the old long transition is either too far away or worse than the current one */
										long_transition_content_scores.set_element(-mval_trans, ii, j) ;
										long_transition_content_scores_pen.set_element(pen_val*0.5, ii, j) ;
										long_transition_content_scores_elem.set_element(elem_val[i], ii, j) ;
										long_transition_content_scores_prev.set_element(delta.element(delta_array, ts2, ii, 0, m_seq_len, m_N), ii, j) ;
										long_transition_content_position.set_element(ts2, ii, j) ;
									}

									long_transition_content_start.set_element(ts2, ii, j) ;
								}

								/* // extra check
								   float64_t mval_trans2 = -( elem_val[i] + pen_val + delta.element(delta_array, ts, ii, 0, m_seq_len, m_N) ) ;
								   if (last_ts==ts && fabs(last_mval-mval_trans2)>1e-5)
								   SG_PRINT("last_mval=%1.2f at m_pos %i vs. mval_trans2=%1.2f at m_pos %i (diff=%f)\n", last_mval, m_pos[last_ts], mval_trans2, m_pos[ts], last_mval-mval_trans2) ;
								   */
							}
						}
					}
#ifdef DYNPROG_TIMING
					MyTime3.stop() ;
					long_transition_time += MyTime3.time_diff_sec() ;
#endif


					int32_t numEnt = fixed_list_len;

					float64_t minusscore;
					int64_t fromtjk;

					for (int16_t k=0; k<nbest; k++)
					{
						if (k<numEnt)
						{
							if (nbest==1)
							{
								minusscore = fixedtempvv_ ;
								fromtjk = fixedtempii_ ;
							}
							else
							{
								minusscore = fixedtempvv[k];
								fromtjk = fixedtempii[k];
							}

							delta.element(delta_array, t, j, k, m_seq_len, m_N)    = -minusscore + seq.element(j,t);
							psi.element(t,j,k)      = (fromtjk%m_N) ;
							if (nbest>1)
								ktable.element(t,j,k)   = (fromtjk%(m_N*nbest)-psi.element(t,j,k))/m_N ;
							ptable.element(t,j,k)   = (fromtjk-(fromtjk%(m_N*nbest)))/(m_N*nbest) ;
						}
						else
						{
							delta.element(delta_array, t, j, k, m_seq_len, m_N)    = -CMath::INFTY ;
							psi.element(t,j,k)      = 0 ;
							if (nbest>1)
								ktable.element(t,j,k)     = 0 ;
							ptable.element(t,j,k)     = 0 ;
						}
					}
				}
			}
		}

		if (with_loss)
			clear_segment_loss(loss);

		{ //termination
			int32_t list_len = 0 ;
			for (int16_t diff=0; diff<nbest; diff++)
			{
				for (T_STATES i=0; i<m_N; i++)
				{
					oldtempvv[list_len] = -(delta.element(delta_array, (m_seq_len-1), i, diff, m_seq_len, m_N)+get_q(i)) ;
					oldtempii[list_len] = i + diff*m_N ;
					list_len++ ;
				}
			}

			CMath::nmin(oldtempvv.get_array(), oldtempii.get_array(), list_len, nbest) ;

			for (int16_t k=0; k<nbest; k++)
			{
				delta_end.element(k) = -oldtempvv[k] ;
				path_ends.element(k) = (oldtempii[k]%m_N) ;
				if (nbest>1)
					ktable_end.element(k) = (oldtempii[k]-path_ends.element(k))/m_N ;
			}
		}

		{ //state sequence backtracking		
			for (int16_t k=0; k<nbest; k++)
			{
				prob_nbest[k]= delta_end.element(k) ;

				int32_t i         = 0 ;
				state_seq[i]  = path_ends.element(k) ;
				int16_t q   = 0 ;
				if (nbest>1)
					q=ktable_end.element(k) ;
				pos_seq[i]    = m_seq_len-1 ;

				while (pos_seq[i]>0)
				{
					//SG_DEBUG("s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
					state_seq[i+1] = psi.element(pos_seq[i], state_seq[i], q);
					pos_seq[i+1]   = ptable.element(pos_seq[i], state_seq[i], q) ;
					if (nbest>1)
						q              = ktable.element(pos_seq[i], state_seq[i], q) ;
					i++ ;
				}
				//SG_DEBUG("s=%i p=%i q=%i\n", state_seq[i], pos_seq[i], q) ;
				int32_t num_states = i+1 ;
				for (i=0; i<num_states;i++)
				{
					my_state_seq[i+k*m_seq_len] = state_seq[num_states-i-1] ;
					my_pos_seq[i+k*m_seq_len]   = pos_seq[num_states-i-1] ;
				}
				my_state_seq[num_states+k*m_seq_len]=-1 ;
				my_pos_seq[num_states+k*m_seq_len]=-1 ;
			}
		}

		//if (is_big)
		//	SG_PRINT( "DONE.     \n") ;


#ifdef DYNPROG_TIMING
		MyTime2.stop() ;

		//if (is_big)
		SG_PRINT("Timing:  orf=%1.2f s \n Segment_init=%1.2f s Segment_pos=%1.2f s  Segment_extend=%1.2f s Segment_clean=%1.2f s\nsvm_init=%1.2f s  svm_pos=%1.2f  svm_clean=%1.2f\n  content_svm_values_time=%1.2f  content_plifs_time=%1.2f\ninner_loop_max_time=%1.2f inner_loop=%1.2f long_transition_time=%1.2f\n total=%1.2f\n", orf_time, segment_init_time, segment_pos_time, segment_extend_time, segment_clean_time, svm_init_time, svm_pos_time, svm_clean_time, content_svm_values_time, content_plifs_time, inner_loop_max_time, inner_loop_time, long_transition_time, MyTime2.time_diff_sec()) ;
#endif

		delete[] fixedtempvv ;
		delete[] fixedtempii ;
	}

void CDynProg::best_path_trans_deriv(
	int32_t *my_state_seq, int32_t *my_pos_seq,
	int32_t my_seq_len, const float64_t *seq_array, int32_t max_num_signals)
{	
	m_initial_state_distribution_p_deriv.resize_array(m_N) ;
	m_end_state_distribution_q_deriv.resize_array(m_N) ;
	m_transition_matrix_a_deriv.resize_array(m_N,m_N) ;
	//m_my_scores.resize_array(m_my_state_seq.get_array_size()) ;
	//m_my_losses.resize_array(m_my_state_seq.get_array_size()) ;
	m_my_scores.resize_array(my_seq_len);
	m_my_losses.resize_array(my_seq_len);
	float64_t* my_scores=m_my_scores.get_array();
	float64_t* my_losses=m_my_losses.get_array();
	CPlifBase** Plif_matrix=m_plif_matrices->get_plif_matrix();
	CPlifBase** Plif_state_signals=m_plif_matrices->get_state_signals();

	if (!m_svm_arrays_clean)
	{
		SG_ERROR( "SVM arrays not clean") ;
		return ;
	} ;
	//SG_PRINT( "genestr_len=%i, genestr_num=%i\n", genestr_len, genestr_num) ;
	//m_mod_words.display() ;
	//m_sign_words.display() ;
	//m_string_words.display() ;

	bool use_svm = false ;

	CArray2<CPlifBase*> PEN(Plif_matrix, m_N, m_N, false, false) ;
	CArray2<CPlifBase*> PEN_state_signals(Plif_state_signals, m_N, max_num_signals, false, false) ;
	CArray3<float64_t> seq_input(seq_array, m_N, m_seq_len, max_num_signals) ;

	{ // determine whether to use svm outputs and clear derivatives
		for (int32_t i=0; i<m_N; i++)
			for (int32_t j=0; j<m_N; j++)
			{
				CPlifBase *penij=PEN.element(i,j) ;
				if (penij==NULL)
					continue ;

				if (penij->uses_svm_values())
					use_svm=true ;
				penij->penalty_clear_derivative() ;
			}
		for (int32_t i=0; i<m_N; i++)
			for (int32_t j=0; j<max_num_signals; j++)
			{
				CPlifBase *penij=PEN_state_signals.element(i,j) ;
				if (penij==NULL)
					continue ;
				if (penij->uses_svm_values())
					use_svm=true ;
				penij->penalty_clear_derivative() ;
			}
	}

	{ // set derivatives of p, q and a to zero

		for (int32_t i=0; i<m_N; i++)
		{
			m_initial_state_distribution_p_deriv.element(i)=0 ;
			m_end_state_distribution_q_deriv.element(i)=0 ;
			for (int32_t j=0; j<m_N; j++)
				m_transition_matrix_a_deriv.element(i,j)=0 ;
		}
	}

	{ // clear score vector
		for (int32_t i=0; i<my_seq_len; i++)
		{
			my_scores[i]=0.0 ;
			my_losses[i]=0.0 ;
		}
	}

	//int32_t total_len = 0 ;

	//m_transition_matrix_a.display_array() ;
	//m_transition_matrix_a_id.display_array() ;

	// compute derivatives for given path
	float64_t* svm_value = new float64_t[m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs];
	float64_t* svm_value_part1 = new float64_t[m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs];
	float64_t* svm_value_part2 = new float64_t[m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs];
	for (int32_t s=0; s<m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs; s++)
	{
		svm_value[s]=0 ;
		svm_value_part1[s]=0 ;
		svm_value_part2[s]=0 ;
	}

	//#ifdef DYNPROG_DEBUG
	float64_t total_score = 0.0 ;
	float64_t total_loss = 0.0 ;
	//#endif		

	ASSERT(my_state_seq[0]>=0) ;
	m_initial_state_distribution_p_deriv.element(my_state_seq[0])++ ;
	my_scores[0] += m_initial_state_distribution_p.element(my_state_seq[0]) ;

	ASSERT(my_state_seq[my_seq_len-1]>=0) ;
	m_end_state_distribution_q_deriv.element(my_state_seq[my_seq_len-1])++ ;
	my_scores[my_seq_len-1] += m_end_state_distribution_q.element(my_state_seq[my_seq_len-1]);

	//#ifdef DYNPROG_DEBUG
	total_score += my_scores[0] + my_scores[my_seq_len-1] ;
	//#endif 		

	struct segment_loss_struct loss;
	loss.segments_changed = NULL;
	loss.num_segment_id = NULL;
	//SG_DEBUG( "m_seq_len=%i\n", my_seq_len) ;
	for (int32_t i=0; i<my_seq_len-1; i++)
	{
		if (my_state_seq[i+1]==-1)
			break ;
		int32_t from_state = my_state_seq[i] ;
		int32_t to_state   = my_state_seq[i+1] ;
		int32_t from_pos   = my_pos_seq[i] ;
		int32_t to_pos     = my_pos_seq[i+1] ;

		// compute loss relative to another segmentation using the segment_loss function
		init_segment_loss(loss, m_seq_len, m_pos[to_pos]-m_pos[from_pos]+10);
		find_segment_loss_till_pos(to_pos,m_segment_ids, m_segment_mask, loss);  

		int32_t loss_last_pos = to_pos ;
		float64_t last_loss = 0.0 ;
		int32_t elem_id = m_transition_matrix_a_id.element(from_state, to_state) ;
		my_losses[i] = extend_segment_loss(loss, elem_id, from_pos, loss_last_pos, last_loss) ;
#ifdef DYNPROG_DEBUG
		io->set_loglevel(M_DEBUG) ;
		SG_DEBUG( "%i. segment loss %f (id=%i): from=%i(%i), to=%i(%i)\n", i, my_losses[i], elem_id, from_pos, from_state, to_pos, to_state) ;
#endif
		// increase usage of this transition
		m_transition_matrix_a_deriv.element(from_state, to_state)++ ;
		my_scores[i] += m_transition_matrix_a.element(from_state, to_state) ;
		//SG_PRINT("m_transition_matrix_a.element(%i, %i),%f \n",from_state, to_state, m_transition_matrix_a.element(from_state, to_state));
#ifdef DYNPROG_DEBUG
		SG_DEBUG( "%i. scores[i]=%f\n", i, my_scores[i]) ;
#endif

		/*int32_t last_svm_pos[m_num_degrees] ;
		  for (int32_t qq=0; qq<m_num_degrees; qq++)
		  last_svm_pos[qq]=-1 ;*/

		bool is_long_transition = false ;
		//if (m_pos[to_pos]-m_pos[from_pos]>m_long_transition_threshold)
		//	is_long_transition = true ;
		//if (m_orf_info.element(from_state,0)!=-1)
		//	is_long_transition = false ;

		int32_t from_pos_thresh = from_pos ;
		int32_t to_pos_thresh = to_pos ;

		if (use_svm)
		{
			if (is_long_transition)
			{
				while (from_pos_thresh<to_pos && m_pos[from_pos_thresh+1] - m_pos[from_pos] < m_long_transition_threshold)
					from_pos_thresh++ ;
				ASSERT(from_pos_thresh<to_pos) ;
				ASSERT(m_pos[from_pos_thresh] - m_pos[from_pos] < m_long_transition_threshold);
				ASSERT(m_pos[from_pos_thresh+1] - m_pos[from_pos] >= m_long_transition_threshold);

				SG_PRINT("pos1: %i  pos2: %i   pos3: %i\n", m_pos[from_pos], m_pos[from_pos_thresh], m_pos[from_pos_thresh+1]) ;

				int32_t frame = m_orf_info.element(from_state,0);
				lookup_content_svm_values(from_pos, from_pos_thresh, m_pos[from_pos], m_pos[from_pos_thresh], svm_value_part1, frame);

				while (to_pos_thresh>0 && m_pos[to_pos] - m_pos[to_pos_thresh-1] < m_long_transition_threshold)
					to_pos_thresh-- ;
				ASSERT(to_pos_thresh>0) ;
				ASSERT(m_pos[to_pos] - m_pos[to_pos_thresh] < m_long_transition_threshold) ;
				ASSERT(m_pos[to_pos] - m_pos[to_pos_thresh-1] >= m_long_transition_threshold) ;

				lookup_content_svm_values(to_pos_thresh, to_pos, m_pos[to_pos_thresh], m_pos[to_pos], svm_value_part2, frame);
			}
			else
			{
				/* normal case */

				//SG_PRINT("from_pos: %i; to_pos: %i; m_pos[to_pos]-m_pos[from_pos]: %i \n",from_pos, to_pos, m_pos[to_pos]-m_pos[from_pos]); 
				int32_t frame = m_orf_info.element(from_state,0);
				if (false)//(frame>=0)
				{
					int32_t num_current_svms=0;
					int32_t svm_ids[] = {-8, -7, -6, -5, -4, -3, -2, -1};
					SG_PRINT("penalties(%i, %i), frame:%i  ", from_state, to_state, frame);
					PEN.element(to_state, from_state)->get_used_svms(&num_current_svms, svm_ids);
					SG_PRINT("\n");
				}

				lookup_content_svm_values(from_pos, to_pos, m_pos[from_pos],m_pos[to_pos], svm_value, frame);
				if (false)//(frame>=0)
					SG_PRINT("svm_values: %f, %f, %f \n", svm_value[4], svm_value[5], svm_value[6]);
				//SG_PRINT("svm_values: %f, %f, %f, %f \n", svm_value[8], svm_value[9], svm_value[10], svm_value[11]);

			}
		}

		if (PEN.element(to_state, from_state)!=NULL)
		{
			float64_t nscore = 0 ;
			if (is_long_transition)
				nscore= 0.5*PEN.element(to_state, from_state)->lookup_penalty(m_pos[from_pos_thresh]-m_pos[from_pos], svm_value_part1) + 
					0.5*PEN.element(to_state, from_state)->lookup_penalty(m_pos[to_pos]-m_pos[to_pos_thresh], svm_value_part2) ;
			else
				nscore = PEN.element(to_state, from_state)->lookup_penalty(m_pos[to_pos]-m_pos[from_pos], svm_value) ;
			my_scores[i] += nscore ;

			for (int32_t s=m_num_svms;s<m_num_lin_feat_plifs_cum[m_num_raw_data]; s++)/*set tiling plif values to neutral values (that do not influence derivative calculation)*/
			{
				svm_value[s]=-CMath::INFTY;
				svm_value_part1[s]=-CMath::INFTY;
				svm_value_part2[s]=-CMath::INFTY;
			}

#ifdef DYNPROG_DEBUG
			//SG_DEBUG( "%i. transition penalty: from_state=%i to_state=%i from_pos=%i [%i] to_pos=%i [%i] value=%i\n", i, from_state, to_state, from_pos, m_pos[from_pos], to_pos, m_pos[to_pos], m_pos[to_pos]-m_pos[from_pos]) ;
#endif
			if (is_long_transition)
			{
				float64_t sum_score = 0.0 ;

				for (int kk=0; kk<i; kk++)
					sum_score += my_scores[i] ;

				SG_PRINT("is_long_transition=%i  (from_pos=%i (%i), to_pos=%i (%i)=> %1.5f, %1.5f --- 1: %1.5f (%i-%i)  2: %1.5f (%i-%i) \n", 
						is_long_transition, m_pos[from_pos], from_state, m_pos[to_pos], to_state, 
						nscore, sum_score, 
						PEN.element(to_state, from_state)->lookup_penalty(m_pos[from_pos_thresh]-m_pos[from_pos], svm_value_part1)*0.5, m_pos[from_pos], m_pos[from_pos_thresh], 
						PEN.element(to_state, from_state)->lookup_penalty(m_pos[to_pos]-m_pos[to_pos_thresh], svm_value_part2)*0.5, m_pos[to_pos_thresh], m_pos[to_pos]) ;
			}

			if (is_long_transition)
			{
				PEN.element(to_state, from_state)->penalty_add_derivative(m_pos[from_pos_thresh]-m_pos[from_pos], svm_value_part1, 0.5) ;
				PEN.element(to_state, from_state)->penalty_add_derivative(m_pos[to_pos]-m_pos[to_pos_thresh], svm_value_part2, 0.5) ;
			}
			else
				PEN.element(to_state, from_state)->penalty_add_derivative(m_pos[to_pos]-m_pos[from_pos], svm_value, 1) ;

			//SG_PRINT("m_num_raw_data = %i \n", m_num_raw_data) ;

			// for tiling array and rna-seq data every single measurement must be added to the derivative 
			// in contrast to the content svm predictions where we have a single value per transition;
			// content svm predictions have already been added to the derivative, thus we start with d=1 
			// instead of d=0
			for (int32_t d=1; d<=m_num_raw_data; d++) 
			{
				ASSERT(!is_long_transition) ; // not sure what has to be done here

				for (int32_t s=0;s<m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs;s++)
					svm_value[s]=-CMath::INFTY;
				float64_t* intensities = new float64_t[m_num_probes_cum[d]];
				int32_t num_intensities = raw_intensities_interval_query(m_pos[from_pos], m_pos[to_pos],intensities, d);
				//SG_PRINT("m_pos[from_pos]:%i, m_pos[to_pos]:%i, num_intensities:%i\n",m_pos[from_pos],m_pos[to_pos], num_intensities);
				for (int32_t k=0;k<num_intensities;k++)
				{
					for (int32_t j=m_num_lin_feat_plifs_cum[d-1];j<m_num_lin_feat_plifs_cum[d];j++)
						svm_value[j]=intensities[k];

					PEN.element(to_state, from_state)->penalty_add_derivative(-CMath::INFTY, svm_value, 1) ;	

				}
				delete[] intensities;
			}
		}
#ifdef DYNPROG_DEBUG
		SG_DEBUG( "%i. scores[i]=%f\n", i, my_scores[i]) ;
#endif

		//SG_DEBUG( "emmission penalty skipped: to_state=%i to_pos=%i value=%1.2f score=%1.2f\n", to_state, to_pos, seq_input.element(to_state, to_pos), 0.0) ;
		for (int32_t k=0; k<max_num_signals; k++)
		{
			if ((PEN_state_signals.element(to_state,k)==NULL)&&(k==0))
			{
#ifdef DYNPROG_DEBUG
				SG_DEBUG( "%i. emmission penalty: to_state=%i to_pos=%i score=%1.2f (no signal plif)\n", i, to_state, to_pos, seq_input.element(to_state, to_pos, k)) ;
#endif
				my_scores[i] += seq_input.element(to_state, to_pos, k) ;
				//if (seq_input.element(to_state, to_pos, k) !=0)
				//	SG_PRINT("features(%i,%i): %f\n",to_state,to_pos,seq_input.element(to_state, to_pos, k));
				break ;
			}
			if (PEN_state_signals.element(to_state, k)!=NULL)
			{
				float64_t nscore = PEN_state_signals.element(to_state,k)->lookup_penalty(seq_input.element(to_state, to_pos, k), svm_value) ; // this should be ok for long_transitions (svm_value does not matter)
				my_scores[i] += nscore ;
				//break ;
				//int32_t num_current_svms=0;
				//int32_t svm_ids[] = {-8, -7, -6, -5, -4, -3, -2, -1};
				//SG_PRINT("PEN_state_signals->id: ");
				//PEN_state_signals.element(to_state, k)->get_used_svms(&num_current_svms, svm_ids);
				//SG_PRINT("\n");
				//if (nscore != 0)
				//SG_PRINT( "%i. emmission penalty: to_state=%i to_pos=%i value=%1.2f score=%1.2f k=%i\n", i, to_state, to_pos, seq_input.element(to_state, to_pos, k), nscore, k) ;
#ifdef DYNPROG_DEBUG
				SG_DEBUG( "%i. emmission penalty: to_state=%i to_pos=%i value=%1.2f score=%1.2f k=%i\n", i, to_state, to_pos, seq_input.element(to_state, to_pos, k), nscore, k) ;
#endif
				PEN_state_signals.element(to_state,k)->penalty_add_derivative(seq_input.element(to_state, to_pos, k), svm_value, 1) ; // this should be ok for long_transitions (svm_value does not matter)
			} else
				break ;
		}

		//#ifdef DYNPROG_DEBUG
		//SG_PRINT( "scores[%i]=%f (final) \n", i, my_scores[i]) ;
		//SG_PRINT( "losses[%i]=%f (final) , total_loss: %f \n", i, my_losses[i], total_loss) ;
		total_score += my_scores[i] ;
		total_loss += my_losses[i] ;
		//#endif
	}
	//#ifdef DYNPROG_DEBUG
	//SG_PRINT( "total score = %f \n", total_score) ;
	//SG_PRINT( "total loss = %f \n", total_loss) ;
	//#endif
	clear_segment_loss(loss);
	delete[] svm_value;
	delete[] svm_value_part1 ;
	delete[] svm_value_part2 ;
}

int32_t CDynProg::raw_intensities_interval_query(const int32_t from_pos, const int32_t to_pos, float64_t* intensities, int32_t type)
{
	ASSERT(from_pos<to_pos);
	int32_t num_intensities = 0;
	int32_t* p_tiling_pos  = &m_probe_pos[m_num_probes_cum[type-1]];
	float64_t* p_tiling_data = &m_raw_intensities[m_num_probes_cum[type-1]];
	int32_t last_pos;
	int32_t num = m_num_probes_cum[type-1];
	while (*p_tiling_pos<to_pos)
	{
		if (*p_tiling_pos>=from_pos)
		{
			intensities[num_intensities] = *p_tiling_data;
			num_intensities++;
		}
		num++;
		if (num>=m_num_probes_cum[type])
			break;
		last_pos = *p_tiling_pos;
		p_tiling_pos++;
		p_tiling_data++;
		ASSERT(last_pos<*p_tiling_pos);
	}
	return num_intensities;
}

void CDynProg::lookup_content_svm_values(const int32_t from_state, const int32_t to_state, const int32_t from_pos, const int32_t to_pos, float64_t* svm_values, int32_t frame)
{
#ifdef DYNPROG_TIMING_DETAIL
	MyTime.start() ;
#endif
//	ASSERT(from_state<to_state);
//	if (!(from_pos<to_pos))
//		SG_ERROR("from_pos!<to_pos, from_pos: %i to_pos: %i \n",from_pos,to_pos);
	for (int32_t i=0;i<m_num_svms;i++)
	{
		float64_t to_val   = m_lin_feat.get_element(i, to_state);
		float64_t from_val = m_lin_feat.get_element(i, from_state);
		svm_values[i] = (to_val-from_val)/(to_pos-from_pos);
	}
	for (int32_t i=m_num_svms;i<m_num_lin_feat_plifs_cum[m_num_raw_data];i++)
	{
		float64_t to_val   = m_lin_feat.get_element(i, to_state);
		float64_t from_val = m_lin_feat.get_element(i, from_state);
		svm_values[i] = to_val-from_val ;
	}
	if (m_intron_list)
	{
		int32_t support[m_num_intron_plifs];
		m_intron_list->get_intron_support(support, from_state, to_state);
		int32_t intron_list_start = m_num_lin_feat_plifs_cum[m_num_raw_data];
		int32_t intron_list_end = m_num_lin_feat_plifs_cum[m_num_raw_data]+m_num_intron_plifs; 
		int32_t cnt = 0;
		for (int32_t i=intron_list_start; i<intron_list_end;i++)
		{
			svm_values[i] = (float64_t) (support[cnt]);
			cnt++;
		}
		//if (to_pos>3990 && to_pos<4010)
		//	SG_PRINT("from_state:%i to_state:%i support[0]:%i support[1]:%i\n",from_state, to_state, support[0], support[1]);
	}
	// find the correct row with precomputed frame predictions
	if (frame!=-1)
	{
		svm_values[frame_plifs[0]] = 1e10;
		svm_values[frame_plifs[1]] = 1e10;
		svm_values[frame_plifs[2]] = 1e10;
		int32_t global_frame = from_pos%3;
		int32_t row = ((global_frame+frame)%3)+4;
		float64_t to_val   = m_lin_feat.get_element(row, to_state);
		float64_t from_val = m_lin_feat.get_element(row, from_state);
		svm_values[frame+frame_plifs[0]] = (to_val-from_val)/(to_pos-from_pos);
	}
#ifdef DYNPROG_TIMING_DETAIL
	MyTime.stop() ;
	content_svm_values_time += MyTime.time_diff_sec() ;
#endif
}
void CDynProg::set_intron_list(CIntronList* intron_list, int32_t num_plifs)
{
	m_intron_list = intron_list;
	m_num_intron_plifs = num_plifs;
}

