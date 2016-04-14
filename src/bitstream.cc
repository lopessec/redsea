#include "bitstream.h"

#include <complex>

#include "wdsp/wdsp.h"

#include "filters.h"

#define FS        250000.0
#define FC_0      57000.0
#define IBUFLEN   4096
#define OBUFLEN   128
#define BITBUFLEN 1024

namespace redsea {

int sign(double a) {
  return (a >= 0 ? 1 : 0);
}

BitStream::BitStream() : tot_errs_(2), reading_frame_(0), counter_(0), fsc_(FC_0), bit_buffer_(BITBUFLEN), mixer_phi_(0), clock_offset_(0), is_eof_(false), subcarr_lopass_fir_(wdsp::FIR(4000.0 / FS, 127)), subcarr_baseband_(IBUFLEN), mixer_lagged_(IBUFLEN) {

}

void BitStream::bit(int b) {
  bit_buffer_.append(b);
  /*if (nbit % 4 == 0) {
    printf("%x", nybble & 0xf);
    if ((nbit/4) % OBUFLEN == 0) {
      fflush(0);
    }
  }*/
}

void BitStream::deltaBit(int b) {
  bit(b ^ dbit_);
  dbit_ = b;
}

void BitStream::biphase(double acc) {

  if (sign(acc) != sign(prev_acc_)) {
    tot_errs_[counter_ % 2] ++;
  }

  if (counter_ % 2 == reading_frame_) {
    deltaBit(sign(acc + prev_acc_));
  }
  if (counter_ == 0) {
    if (tot_errs_[1 - reading_frame_] < tot_errs_[reading_frame_]) {
      reading_frame_ = 1 - reading_frame_;
    }
    tot_errs_[0] = 0;
    tot_errs_[1] = 0;
  }

  prev_acc_ = acc;
  counter_ = (counter_ + 1) % 800;
}


void BitStream::demodulateMoreBits() {

  int16_t sample[IBUFLEN];
  int bytesread = fread(sample, sizeof(int16_t), IBUFLEN, stdin);
  if (bytesread < IBUFLEN) {
    is_eof_ = true;
    return;
  }

  for (int i = 0; i < bytesread; i++) {

    /* Subcarrier downmix & phase recovery */

    mixer_phi_ += 2 * M_PI * fsc_ * (1.0/FS);
    //std::complex<double> subcarr_bb = wdsp::mix(sample[i] / 32768.0, subcarr_phi_);
    subcarr_baseband_.appendOverlapFiltered(wdsp::mix(sample[i] / 32768.0, mixer_phi_),
        subcarr_lopass_fir_);
//      filter_lp_2400_iq(sample[i] / 32768.0 * std::polar(1.0, subcarr_phi_));

    double pll_beta = 2e-3;

    mixer_lagged_.append(std::polar(1.0,mixer_phi_));

    std::complex<double> sc_sample = subcarr_baseband_.getNext();
    std::complex<double> mi = mixer_lagged_.getNext();

    //double delta_phi = arg(sc_sample * conj(mi));

    //double delta_phi = 2.0*filter_lp_pll(real(sc_sample) * imag(sc_sample));
    //double delta_phi = imag(sc_sample);

    double phi1 = arg(sc_sample);
    if (phi1 >= M_PI_2) {
      phi1 -= M_PI;
    } else if (phi1 <= -M_PI_2) {
      phi1 += M_PI;
    }
    double phi2 = arg(mi);

    mixer_phi_ -= pll_beta * phi1;
    fsc_            -= .5 * pll_beta * phi1;

    //printf("dd %f\ndd %f\n",phi1, phi2);
    //printf("dd %f\ndd %f\n",mi.real(), mi.imag());
    //printf("dd %f\ndd %f\n",sc_sample.real(), sc_sample.imag());
    //printf("dd %f\n", phi1*0.02);
    /* 1187.5 Hz clock */

    double clock_phi = mixer_phi_ / 48.0 + clock_offset_;
    double lo_clock  = (fmod(clock_phi, 2*M_PI) < M_PI ? 1 : -1);

    /* Clock phase recovery */

    if (sign(prev_bb_) != sign(real(sc_sample))) {
      double d_cphi = fmod(clock_phi, M_PI);
      if (d_cphi >= M_PI_2) d_cphi -= M_PI;
      clock_offset_ -= 0.005 * d_cphi;
    }

    /* Decimate band-limited signal */
    if (numsamples_ % 8 == 0) {

      /* biphase symbol integrate & dump */
      acc_ += real(sc_sample) * lo_clock;

      if (sign(lo_clock) != sign(prevclock_)) {
        biphase(acc_);
        acc_ = 0;
      }

      prevclock_ = lo_clock;
    }

    numsamples_ ++;

    prev_bb_ = real(sc_sample);

  }

}

int BitStream::getNextBit() {
  while (bit_buffer_.getFillCount() < 1)
    demodulateMoreBits();

  int result = bit_buffer_.getNext();
  //printf("read %d, write %d, fill count %d\n",bit_buffer_read_ptr_, bit_buffer_write_ptr_, bit_buffer_fill_count_);
  return result;
}

bool BitStream::isEOF() const {
  return is_eof_;
}

} // namespace redsea
