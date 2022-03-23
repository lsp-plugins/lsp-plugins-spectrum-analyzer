/*
 * Copyright (C) 2021 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2021 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-spectrum-analyzer
 * Created on: 22 июн. 2021 г.
 *
 * lsp-plugins-spectrum-analyzer is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-plugins-spectrum-analyzer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-plugins-spectrum-analyzer. If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef PRIVATE_META_SPECTRUM_ANALYZER_H_
#define PRIVATE_META_SPECTRUM_ANALYZER_H_

#include <lsp-plug.in/plug-fw/meta/types.h>
#include <lsp-plug.in/plug-fw/const.h>
#include <lsp-plug.in/dsp-units/misc/windows.h>
#include <lsp-plug.in/dsp-units/misc/envelope.h>

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Spectrum analyzer metadata
        struct spectrum_analyzer
        {
            static const float          FREQ_MIN            = SPEC_FREQ_MIN;
            static const float          FREQ_DFL            = 1000.0f;
            static const float          FREQ_MAX            = SPEC_FREQ_MAX;

            static const size_t         PORTS_PER_INPUT     = 6;
            static const size_t         RANK_MIN            = 10;
            static const size_t         RANK_DFL            = 12;
            static const size_t         RANK_MAX            = 14;
            static const size_t         MESH_POINTS         = 640;

            static const float          THRESH_HI_DB        = 0.0f;
            static const float          THRESH_LO_DB        = -48.0f;
            static const float          SPECTRALIZER_BOOST  = 16.0f;
            static const size_t         MMESH_STEP          = 16;
            static const size_t         WND_DFL             = dspu::windows::HANN;
            static const size_t         ENV_DFL             = dspu::envelope::PINK_NOISE;
            static const size_t         FB_ROWS             = 360;
            static const float          FB_TIME             = 8.0f;

            static const float          REACT_TIME_MIN      = 0.000;
            static const float          REACT_TIME_MAX      = 1.000;
            static const float          REACT_TIME_DFL      = 0.200;
            static const float          REACT_TIME_STEP     = 0.001;

            static const float          SELECTOR_MIN        = 0.01f;
            static const float          SELECTOR_DFL        = 0.01f;
            static const float          SELECTOR_MAX        = 100.0f;
            static const float          SELECTOR_STEP       = 0.005f;

            static const float          ZOOM_MIN            = GAIN_AMP_M_36_DB;
            static const float          ZOOM_MAX            = GAIN_AMP_0_DB;
            static const float          ZOOM_DFL            = GAIN_AMP_0_DB;
            static const float          ZOOM_STEP           = 0.025f;

            static const float          PREAMP_DFL          = 1.0;

            static const size_t         REFRESH_RATE        = 20;
        };

        extern const plugin_t spectrum_analyzer_x1;
        extern const plugin_t spectrum_analyzer_x2;
        extern const plugin_t spectrum_analyzer_x4;
        extern const plugin_t spectrum_analyzer_x8;
        extern const plugin_t spectrum_analyzer_x12;
        extern const plugin_t spectrum_analyzer_x16;
    }
}



#endif /* PRIVATE_META_SPECTRUM_ANALYZER_H_ */
