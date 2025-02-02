//
// Copyright (C) 2023, Advanced Micro Devices, Inc. All rights reserved.
// SPDX-License-Identifier: MIT
//
// Author: Mark Rollins

#include "pfa1008_permute_o.h"
#include "pfa1008_permute_o_luts.h"

using namespace std;
using namespace pfa1008_permute_o;

// ------------------------------------------------------------
// Multiplex Buffer Index
// ------------------------------------------------------------

void mux_index( TT_INDEX (&wr_idx)[8], ap_uint<2> (&wr_off)[4], int val,
                TT_INDEX& wr_idx0, TT_INDEX& wr_idx1 )
{
  if      ( wr_off[0] == ap_uint<2>(val) ) { wr_idx0 = wr_idx[0];  wr_idx1 = wr_idx[4]; }
  else if ( wr_off[1] == ap_uint<2>(val) ) { wr_idx0 = wr_idx[1];  wr_idx1 = wr_idx[5]; }
  else if ( wr_off[2] == ap_uint<2>(val) ) { wr_idx0 = wr_idx[2];  wr_idx1 = wr_idx[6]; }
  else                                     { wr_idx0 = wr_idx[3];  wr_idx1 = wr_idx[7]; }
}

// ------------------------------------------------------------
// Multiplex Buffer Write Data
// ------------------------------------------------------------

void mux_wrdata( TT_DATA (&wr_data)[8], ap_uint<2> (&wr_off)[4], int val,
                 TT_DATA& wr_data0, TT_DATA& wr_data1 )
{
  if      ( wr_off[0] == ap_uint<2>(val) ) { wr_data0 = wr_data[0];  wr_data1 = wr_data[4]; }
  else if ( wr_off[1] == ap_uint<2>(val) ) { wr_data0 = wr_data[1];  wr_data1 = wr_data[5]; }
  else if ( wr_off[2] == ap_uint<2>(val) ) { wr_data0 = wr_data[2];  wr_data1 = wr_data[6]; }
  else                                     { wr_data0 = wr_data[3];  wr_data1 = wr_data[7]; }
}

// ------------------------------------------------------------
// Unpack & store samples
// ------------------------------------------------------------

void pfa1008_unpack( TT_STREAM sig_i[SSR_IO], TT_DATA (&permute_i)[8] )
{
  // Unpack samples from input streams (incoming alternating four samples each on ss0 then ss1):
  ( permute_i[3], permute_i[2], permute_i[1], permute_i[0] ) = sig_i[0].read();
  ( permute_i[7], permute_i[6], permute_i[5], permute_i[4] ) = sig_i[1].read();
}

// ------------------------------------------------------------
// Apply Output Permutation
// ------------------------------------------------------------

// Permutation requires reading of several consecutive samples from the same polyphase, so we need to provision
// four copies of the data, using true dual port memories to support reading and writing 8 samples per PL cycle
// We write samples to buffer in DFT-16 order (ie. 3rd dimension of FFT cube, with 63 stride)
// We read samples from buffer using LUT-based output permutation
void pfa1008_permute( TT_DATA (&permute_i)[8], TT_DATA (&permute_o)[8] )
{
  static TT_DATA buff[2][4][4][NFFT/4];
#pragma HLS array_partition variable=buff dim=1
#pragma HLS bind_storage variable=buff type=RAM_T2P impl=bram
#pragma HLS dependence variable=buff type=intra false

  static TT_ADDR    permute_lut[4][NFFT] = { PERM_O_ADDR, PERM_O_ADDR, PERM_O_ADDR, PERM_O_ADDR };
#pragma HLS array_partition variable=permute_lut dim=1
#pragma HLS bind_storage variable=permute_lut type=RAM_T2P impl=bram

  static TT_ADDR    wr_addr  = 0;
  static ap_uint<3> wr_cnt7  = 0;
  static ap_uint<4> wr_cnt9  = 0;
  static ap_uint<4> wr_cnt16 = 0;
#pragma HLS reset variable=wr_addr
#pragma HLS reset variable=wr_cnt7
#pragma HLS reset variable=wr_cnt9
#pragma HLS reset variable=wr_cnt16

  static bool      ping = 0;
  static TT_ADDR   rd_addr = 0;
#pragma HLS reset variable=ping
#pragma HLS reset variable=rd_addr

  // ==================== READ & WRITE BUFFER ====================

  // cout << "cnt7: " << wr_cnt7 << " cnt9: " << wr_cnt9 << " cnt16: " << wr_cnt16 << " " << "wr_addr: ";

  TT_INDEX   wr_idx[8];
  ap_uint<2> wr_off[4];
  TT_INDEX   wr_idx0, wr_idx1, wr_idx2, wr_idx3, wr_idx4, wr_idx5, wr_idx6, wr_idx7;
  TT_DATA    wr_data0, wr_data1, wr_data2, wr_data3, wr_data4, wr_data5, wr_data6, wr_data7;
  TT_INDEX   rd_idx[8];
  ap_uint<2> rd_off[8];

  TT_DATA rd_data0[8], rd_data1[8], rd_data2[8], rd_data3[8];
#pragma HLS array_partition variable=rd_data0 dim=1
#pragma HLS array_partition variable=rd_data1 dim=1
#pragma HLS array_partition variable=rd_data2 dim=1
#pragma HLS array_partition variable=rd_data3 dim=1

 READ_WRITE : for (int ss=0,offW=0; ss < 8; ss++,offW+=63) {
    TT_ADDR wr_addr_use = TT_ADDR(wr_addr + offW);
    TT_ADDR rd_addr_use = permute_lut[ss>>1][rd_addr+ss];
    wr_idx[ss].range(7,0) = wr_addr_use.range(9,2);
    rd_idx[ss].range(7,0) = rd_addr_use.range(9,2);
    if ( ss < 4 ) {
      wr_off[ss].range(1,0) = wr_addr_use.range(1,0);
    }
    rd_off[ss].range(1,0) = rd_addr_use.range(1,0);
    // cout << wr_addr_use << " ";
    // cout << rd_addr_use << " ";
  }
  // cout << endl;

  // Buffer Write Muxing:
  mux_index( wr_idx, wr_off, 0, wr_idx0, wr_idx1 );
  mux_index( wr_idx, wr_off, 1, wr_idx2, wr_idx3 );
  mux_index( wr_idx, wr_off, 2, wr_idx4, wr_idx5 );
  mux_index( wr_idx, wr_off, 3, wr_idx6, wr_idx7 );

  mux_wrdata( permute_i, wr_off, 0, wr_data0, wr_data1 );
  mux_wrdata( permute_i, wr_off, 1, wr_data2, wr_data3 );
  mux_wrdata( permute_i, wr_off, 2, wr_data4, wr_data5 );
  mux_wrdata( permute_i, wr_off, 3, wr_data6, wr_data7 );

  // Buffer Write:
 WRITE : for (int ss=0; ss < 4; ss++) {
    // Write four data copies (needed for permuted read-back):
    buff[ping][0][ss][wr_idx0] = wr_data0;
    buff[ping][0][ss][wr_idx1] = wr_data1;
    buff[ping][1][ss][wr_idx2] = wr_data2;
    buff[ping][1][ss][wr_idx3] = wr_data3;
    buff[ping][2][ss][wr_idx4] = wr_data4;
    buff[ping][2][ss][wr_idx5] = wr_data5;
    buff[ping][3][ss][wr_idx6] = wr_data6;
    buff[ping][3][ss][wr_idx7] = wr_data7;
  }

  // Buffer Read:
  rd_data0[0] = buff[!ping][0][0][rd_idx[0]];
  rd_data0[1] = buff[!ping][0][0][rd_idx[1]];
  rd_data0[2] = buff[!ping][0][1][rd_idx[2]];
  rd_data0[3] = buff[!ping][0][1][rd_idx[3]];
  rd_data0[4] = buff[!ping][0][2][rd_idx[4]];
  rd_data0[5] = buff[!ping][0][2][rd_idx[5]];
  rd_data0[6] = buff[!ping][0][3][rd_idx[6]];
  rd_data0[7] = buff[!ping][0][3][rd_idx[7]];

  rd_data1[0] = buff[!ping][1][0][rd_idx[0]];
  rd_data1[1] = buff[!ping][1][0][rd_idx[1]];
  rd_data1[2] = buff[!ping][1][1][rd_idx[2]];
  rd_data1[3] = buff[!ping][1][1][rd_idx[3]];
  rd_data1[4] = buff[!ping][1][2][rd_idx[4]];
  rd_data1[5] = buff[!ping][1][2][rd_idx[5]];
  rd_data1[6] = buff[!ping][1][3][rd_idx[6]];
  rd_data1[7] = buff[!ping][1][3][rd_idx[7]];

  rd_data2[0] = buff[!ping][2][0][rd_idx[0]];
  rd_data2[1] = buff[!ping][2][0][rd_idx[1]];
  rd_data2[2] = buff[!ping][2][1][rd_idx[2]];
  rd_data2[3] = buff[!ping][2][1][rd_idx[3]];
  rd_data2[4] = buff[!ping][2][2][rd_idx[4]];
  rd_data2[5] = buff[!ping][2][2][rd_idx[5]];
  rd_data2[6] = buff[!ping][2][3][rd_idx[6]];
  rd_data2[7] = buff[!ping][2][3][rd_idx[7]];

  rd_data3[0] = buff[!ping][3][0][rd_idx[0]];
  rd_data3[1] = buff[!ping][3][0][rd_idx[1]];
  rd_data3[2] = buff[!ping][3][1][rd_idx[2]];
  rd_data3[3] = buff[!ping][3][1][rd_idx[3]];
  rd_data3[4] = buff[!ping][3][2][rd_idx[4]];
  rd_data3[5] = buff[!ping][3][2][rd_idx[5]];
  rd_data3[6] = buff[!ping][3][3][rd_idx[6]];
  rd_data3[7] = buff[!ping][3][3][rd_idx[7]];

  // Apply polyphase muxes:
 MUX_ALL : for (int ss=0; ss < 8; ss++) {
    switch ( rd_off[ss] ) {
    case 0 : permute_o[ss] = rd_data0[ss]; break;
    case 1 : permute_o[ss] = rd_data1[ss]; break;
    case 2 : permute_o[ss] = rd_data2[ss]; break;
    default: permute_o[ss] = rd_data3[ss]; break;
    }
    // cout << permute_o[ss] << " ";
  }
  // cout << endl;

  // Update write address:
  if ( wr_cnt16 == ap_uint<4>(8) ) { // Check last iteration of inner loop
    if ( wr_cnt9 == ap_uint<4>(8) ) { // Check last iteration of middle loop
      if ( wr_cnt7 == ap_uint<3>(6) ) { // Check last iteration of outer loop
        ping = !ping;
        wr_addr = 0;
        wr_cnt7 = 0;
        wr_cnt9 = 0;
        wr_cnt16 = 0;
      }
      else { // Outer loop body
        wr_cnt7++;
        wr_cnt9 = 0;
        wr_cnt16 = 0;
        wr_addr = TT_ADDR(wr_cnt7);
      }
    }
    else { // Middle loop body
      wr_cnt16 = 0;
      wr_cnt9++;
      wr_addr = TT_ADDR(7*wr_cnt9 + wr_cnt7);
    }
  }
  else { // Inner loop body
    wr_cnt16 += 8;
    wr_addr = TT_ADDR(wr_addr + 8*63);
  }
  // Update read address:
  if ( rd_addr == TT_ADDR(NFFT-8) ) {
    rd_addr = 0;
  }
  else {
    rd_addr = TT_ADDR(rd_addr + 8);
  }
}

// ------------------------------------------------------------
// Write output streams
// ------------------------------------------------------------

void pfa1008_write_streams( TT_DATA (&permute_o)[8], TT_STREAM sig_o[SSR_IO] )
{
  static bool        running = 0;
  static ap_uint<10> startup = 0; // Count off first block of "don't cares" from buffer plus latency below:
#pragma HLS reset variable=running
#pragma HLS reset variable=startup

  // Need to write samples in polyphase order on ss0 and ss1:
  TT_AXI4 ss0_data = ( permute_o[6], permute_o[4], permute_o[2], permute_o[0] );
  TT_AXI4 ss1_data = ( permute_o[7], permute_o[5], permute_o[3], permute_o[1] );
  // Write streams:
  if ( running == 1 ) {
    sig_o[0].write( ss0_data );
    sig_o[1].write( ss1_data );
  }
  if ( startup == ap_uint<10>(NFFT/8-1) ) {
    running = 1;
  }
  else {
    startup = ap_uint<10>(startup + 1);
  }
}

// ------------------------------------------------------------
// Wrapper
// ------------------------------------------------------------

void pfa1008_permute_o_wrapper( TT_STREAM sig_i[SSR_IO], TT_STREAM sig_o[SSR_IO] )
{
#pragma HLS interface mode=ap_ctrl_none port=return
#pragma HLS pipeline II=1

  static TT_DATA permute_i[8], permute_o[8];
#pragma HLS array_partition variable=permute_i dim=1
#pragma HLS array_partition variable=permute_o dim=1

  // Unpack & store samples:
  pfa1008_unpack( sig_i, permute_i );

  // Permute:
  pfa1008_permute( permute_i, permute_o );

  // Format output streams:
  pfa1008_write_streams( permute_o, sig_o );
}



