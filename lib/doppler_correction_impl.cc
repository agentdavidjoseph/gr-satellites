/* -*- c++ -*- */
/*
 * Copyright 2022 Daniel Estevez <daniel@destevez.net>.
 *
 * This file is part of gr-satellites
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "doppler_correction_impl.h"
#include <gnuradio/expj.h>
#include <gnuradio/io_signature.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace gr {
namespace satellites {

doppler_correction::sptr
doppler_correction::make(std::string& filename, double samp_rate, double t0)
{
    return gnuradio::make_block_sptr<doppler_correction_impl>(filename, samp_rate, t0);
}


doppler_correction_impl::doppler_correction_impl(std::string& filename,
                                                 double samp_rate,
                                                 double t0)
    : gr::sync_block("doppler_correction",
                     gr::io_signature::make(1, 1, sizeof(gr_complex)),
                     gr::io_signature::make(1, 1, sizeof(gr_complex))),
      d_phase(0.0),
      d_samp_rate(samp_rate),
      d_current_index(0),
      d_t0(t0),
      d_sample_t0(0),
      d_rx_time_key(pmt::mp("rx_time"))
{
    read_doppler_file(filename);
}

doppler_correction_impl::~doppler_correction_impl() {}

void doppler_correction_impl::read_doppler_file(std::string& filename)
{
    std::ifstream input_file(filename);
    double time;
    double frequency;

    while (!input_file.eof()) {
        if (!input_file.good()) {
            throw std::runtime_error("format error in Doppler file");
        }
        input_file >> time >> frequency;
        times.push_back(time);
        freqs_rad_per_sample.push_back(2.0 * GR_M_PI * frequency / d_samp_rate);
    }
}

int doppler_correction_impl::work(int noutput_items,
                                  gr_vector_const_void_star& input_items,
                                  gr_vector_void_star& output_items)
{
    auto in = static_cast<const gr_complex*>(input_items[0]);
    auto out = static_cast<gr_complex*>(output_items[0]);

    get_tags_in_window(d_tags, 0, 0, noutput_items, d_rx_time_key);
    for (auto tag : d_tags) {
        if (pmt::is_tuple(tag.value)) {
            d_sample_t0 = tag.offset;
            d_t0 = static_cast<double>(pmt::to_uint64(pmt::tuple_ref(tag.value, 0))) +
                   pmt::to_double(pmt::tuple_ref(tag.value, 1));
            d_logger->info("set time {} at sample {}", d_t0, d_sample_t0);
        }
    }

    for (int j = 0; j < noutput_items; ++j) {
        double time = d_t0 + (nitems_written(0) - d_sample_t0 + j) / d_samp_rate;
        // Advance d_current_index so that the next time is greater than the
        // current.
        while (d_current_index + 1 < times.size() && times[d_current_index + 1] <= time) {
            ++d_current_index;
        }
        double freq;
        if ((time < times[d_current_index]) || (d_current_index + 1 == times.size())) {
            // We are before the beginning or past the end of the file, so we
            // maintain a constant frequency.
            freq = freqs_rad_per_sample[d_current_index];
        } else {
            // Linearly interpolate frequency
            double alpha = (time - times[d_current_index]) /
                           (times[d_current_index + 1] - times[d_current_index]);
            freq = (1.0 - alpha) * freqs_rad_per_sample[d_current_index] +
                   alpha * freqs_rad_per_sample[d_current_index + 1];
        }
        d_phase += freq;
        phase_wrap();
        const gr_complex nco = gr_expj(-d_phase);
        gr::fast_cc_multiply(out[j], in[j], nco);
    }

    return noutput_items;
}

} /* namespace satellites */
} /* namespace gr */
