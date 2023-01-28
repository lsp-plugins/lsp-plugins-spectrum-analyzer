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

#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/shared/meta/developers.h>
#include <private/meta/spectrum_analyzer.h>

#define LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION_MAJOR         1
#define LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION_MINOR         0
#define LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION_MICRO         112

#define LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION  \
    LSP_MODULE_VERSION( \
        LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION_MAJOR, \
        LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION_MINOR, \
        LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION_MICRO  \
    )

namespace lsp
{
    namespace meta
    {
        //-------------------------------------------------------------------------
        // Spectrum analyser: x1, x8, x12, x16, x24, x32
        static const int plugin_classes[]           = { C_ANALYSER, -1 };
        static const int clap_features[]            = { CF_ANALYZER, CF_UTILITY, -1 };

        #define R(x) { x, NULL }

        static const port_item_t fft_tolerance[] =
        {
            { "1024", NULL },
            { "2048", NULL },
            { "4096", NULL },
            { "8192", NULL },
            { "16384", NULL },
            { NULL, NULL }
        };

        static const port_item_t spectrum_analyzer_x1_modes[]=
        {
            { "Analyzer",       "spectrum.analyzer" },
            { "Mastering",      "spectrum.mastering" },
            { "Spectralizer",   "spectrum.spectralizer" },
            { NULL,             NULL }
        };

        static const port_item_t spectrum_analyzer_x2_channels[]=
        {
            R("0"),
            R("1"),
            { NULL, NULL }
        };

        static const port_item_t spectrum_analyzer_x2_modes[]=
        {
            { "Analyzer",           "spectrum.analyzer" },
            { "Mastering",          "spectrum.mastering" },
            { "Spectralizer",       "spectrum.spectralizer" },
            { "Spectralizer Stereo", "spectrum.spectralizer_s" },
            { NULL, NULL }
        };

        static const port_item_t spectrum_analyzer_x4_channels[]=
        {
            R("0"), R("1"), R("2"), R("3"),
            { NULL, NULL }
        };

        static const port_item_t spectrum_analyzer_x4_modes[]=
        {
            { "Analyzer",           "spectrum.analyzer" },
            { "Analyzer Stereo",    "spectrum.analyzer_s" },
            { "Mastering",          "spectrum.mastering" },
            { "Mastering Stereo",   "spectrum.mastering_s" },
            { "Spectralizer",       "spectrum.spectralizer" },
            { "Spectralizer Stereo", "spectrum.spectralizer_s" },
            { NULL, NULL }
        };

        static const port_item_t spectrum_analyzer_x8_channels[]=
        {
            R("0"), R("1"), R("2"), R("3"),
            R("4"), R("5"), R("6"), R("7"),
            { NULL, NULL }
        };

        static const port_item_t *spectrum_analyzer_x8_modes = spectrum_analyzer_x4_modes;

        static const port_item_t spectrum_analyzer_x12_channels[]=
        {
            R("0"), R("1"), R("2"), R("3"),
            R("4"), R("5"), R("6"), R("7"),
            R("8"), R("9"), R("10"), R("11"),
            { NULL, NULL }
        };

        static const port_item_t *spectrum_analyzer_x12_modes = spectrum_analyzer_x4_modes;

        static const port_item_t spectrum_analyzer_x16_channels[]=
        {
            R("0"), R("1"), R("2"), R("3"),
            R("4"), R("5"), R("6"), R("7"),
            R("8"), R("9"), R("10"), R("11"),
            R("12"), R("13"), R("14"), R("15"),
            { NULL, NULL }
        };

        static const port_item_t spectralizer_modes[] =
        {
            { "Rainbow",    "spectrum.spc.rainbow" },
            { "Fog",        "spectrum.spc.fog" },
            { "Color",      "spectrum.spc.color" },
            { "Lightning",  "spectrum.spc.lightning" },
            { "Lightness",  "spectrum.spc.lightness" },
            { NULL, NULL }
        };

        static const port_item_t *spectrum_analyzer_x16_modes = spectrum_analyzer_x4_modes;

        #define SA_INPUT(x, total) \
            AUDIO_INPUT_N(x), \
            AUDIO_OUTPUT_N(x), \
            { "on_" #x, "Analyse " #x, U_BOOL, R_CONTROL, F_IN, 0, 0, (x == 0) ? 1.0f : 0.0f, 0, NULL    }, \
            { "solo_" #x, "Solo " #x, U_BOOL, R_CONTROL, F_IN, 0, 0, 0, 0, NULL    }, \
            { "frz_" #x, "Freeze " #x, U_BOOL, R_CONTROL, F_IN, 0, 0, 0, 0, NULL    }, \
            { "hue_" #x, "Hue " #x, U_NONE, R_CONTROL, F_IN | F_UPPER | F_LOWER | F_STEP | F_CYCLIC, 0.0f, 1.0f, (float(x) / float(total)), 0.25f/360.0f, NULL     }, \
            AMP_GAIN("sh_" #x, "Shift gain " #x, 1.0f, 1000.0f)

        #define SA_COMMON(c, channel) \
            BYPASS, \
            COMBO("mode", "Analyzer mode", 0, spectrum_analyzer_x ## c ## _modes), \
            COMBO("spm", "Spectralizer mode", 1, spectralizer_modes), \
            SWITCH("splog", "Spectralizer logarithmic scale", 1), \
            SWITCH("freeze", "Analyzer freeze", 0), \
            { "tol", "FFT Tolerance", U_ENUM, R_CONTROL, F_IN, 0, 0, spectrum_analyzer::RANK_DFL - spectrum_analyzer::RANK_MIN, 0, fft_tolerance }, \
            { "wnd", "FFT Window", U_ENUM, R_CONTROL, F_IN, 0, 0, spectrum_analyzer::WND_DFL, 0, fft_windows }, \
            { "env", "FFT Envelope", U_ENUM, R_CONTROL, F_IN, 0, 0, spectrum_analyzer::ENV_DFL, 0, fft_envelopes }, \
            AMP_GAIN("pamp", "Preamp gain", spectrum_analyzer::PREAMP_DFL, 1000.0f), \
            LOG_CONTROL("zoom", "Graph zoom", U_GAIN_AMP, spectrum_analyzer::ZOOM), \
            { "react",          "Reactivity",       U_SEC,          R_CONTROL, F_IN | F_UPPER | F_LOWER | F_STEP | F_LOG, \
                 spectrum_analyzer::REACT_TIME_MIN, spectrum_analyzer::REACT_TIME_MAX, spectrum_analyzer::REACT_TIME_DFL, spectrum_analyzer::REACT_TIME_STEP, NULL }, \
            channel(c) \
            { "sel", "Selector", U_PERCENT, R_CONTROL, F_IN | F_UPPER | F_LOWER | F_STEP | F_LOG, \
                 spectrum_analyzer::SELECTOR_MIN, spectrum_analyzer::SELECTOR_MAX, spectrum_analyzer::SELECTOR_DFL, spectrum_analyzer::SELECTOR_STEP, NULL }, \
            { "freq", "Frequency", U_HZ, R_METER, F_OUT | F_UPPER | F_LOWER, \
                spectrum_analyzer::FREQ_MIN, spectrum_analyzer::FREQ_MAX, spectrum_analyzer::FREQ_DFL, 0, NULL }, \
            { "lvl", "Level", U_GAIN_AMP, R_METER, F_OUT | F_UPPER | F_LOWER, 0, 10000, 0, 0, NULL }, \
            MESH("spd", "Spectrum Data", c + 1, spectrum_analyzer::MESH_POINTS)

        #define SA_CHANNEL(c)   { "chn", "Channel", U_ENUM, R_CONTROL, F_IN, 0, 0, 0, 0, spectrum_analyzer_x ## c ## _channels },
        #define SA_SKIP(c)

        static const port_t spectrum_analyzer_x1_ports[] =
        {
            SA_INPUT(0, 1),
            SA_COMMON(1, SA_SKIP),
            FBUFFER("fb", "Spectralizer buffer", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x2_ports[] =
        {
            SA_INPUT(0, 2),
            SA_INPUT(1, 2),
            SA_COMMON(2, SA_CHANNEL),
            COMBO("spc", "Spectralizer channel", 0, spectrum_analyzer_x2_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x4_ports[] =
        {
            SA_INPUT(0, 4),
            SA_INPUT(1, 4),
            SA_INPUT(2, 4),
            SA_INPUT(3, 4),
            SA_COMMON(4, SA_CHANNEL),
            COMBO("spc0", "Spectralizer channel 0", 0, spectrum_analyzer_x4_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            COMBO("spc1", "Spectralizer channel 1", 1, spectrum_analyzer_x4_channels),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x8_ports[] =
        {
            SA_INPUT(0, 8),
            SA_INPUT(1, 8),
            SA_INPUT(2, 8),
            SA_INPUT(3, 8),
            SA_INPUT(4, 8),
            SA_INPUT(5, 8),
            SA_INPUT(6, 8),
            SA_INPUT(7, 8),
            SA_COMMON(8, SA_CHANNEL),
            COMBO("spc0", "Spectralizer channel 0", 0, spectrum_analyzer_x8_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            COMBO("spc1", "Spectralizer channel 1", 1, spectrum_analyzer_x8_channels),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x12_ports[] =
        {
            SA_INPUT(0, 12),
            SA_INPUT(1, 12),
            SA_INPUT(2, 12),
            SA_INPUT(3, 12),
            SA_INPUT(4, 12),
            SA_INPUT(5, 12),
            SA_INPUT(6, 12),
            SA_INPUT(7, 12),
            SA_INPUT(8, 12),
            SA_INPUT(9, 12),
            SA_INPUT(10, 12),
            SA_INPUT(11, 12),
            SA_COMMON(12, SA_CHANNEL),
            COMBO("spc0", "Spectralizer channel 0", 0, spectrum_analyzer_x12_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            COMBO("spc1", "Spectralizer channel 1", 1, spectrum_analyzer_x12_channels),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x16_ports[] =
        {
            SA_INPUT(0, 16),
            SA_INPUT(1, 16),
            SA_INPUT(2, 16),
            SA_INPUT(3, 16),
            SA_INPUT(4, 16),
            SA_INPUT(5, 16),
            SA_INPUT(6, 16),
            SA_INPUT(7, 16),
            SA_INPUT(8, 16),
            SA_INPUT(9, 16),
            SA_INPUT(10, 16),
            SA_INPUT(11, 16),
            SA_INPUT(12, 16),
            SA_INPUT(13, 16),
            SA_INPUT(14, 16),
            SA_INPUT(15, 16),
            SA_COMMON(16, SA_CHANNEL),
            COMBO("spc0", "Spectralizer channel 0", 0, spectrum_analyzer_x16_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            COMBO("spc1", "Spectralizer channel 1", 1, spectrum_analyzer_x16_channels),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        #undef SA_CHANNEL
        #undef SA_SKIP
        #undef SA_INPUT
        #undef SA_COMMON

        const meta::bundle_t spectrum_analyzer_bundle =
        {
            "spectrum_analyzer",
            "Spectrum Analyzer",
            B_ANALYZERS,
            "N4OjF2sJUHg",
            "This plugin performs spectral analysis of up to 16 channels. Stereo channels\nalso are possible to analyze by utilizing special modes and selecting\ncorresponding channels. It does not affect any changes to the input signal,\nso can be placed anywhere where the metering is needed."
        };

        const plugin_t spectrum_analyzer_x1 =
        {
            "Spektrumanalysator x1",
            "Spectrum Analyzer x1",
            "SA1",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x1",
            LSP_LV2_URI("spectrum_analyzer_x1"),
            LSP_LV2UI_URI("spectrum_analyzer_x1"),
            "qtez",
            LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 0,
            LSP_LADSPA_URI("spectrum_analyzer_x1"),
            LSP_CLAP_URI("spectrum_analyzer_x1"),
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x1_ports,
            "analyzer/spectrum/x1.xml",
            NULL,
            NULL,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x2 =
        {
            "Spektrumanalysator x2",
            "Spectrum Analyzer x2",
            "SA2",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x2",
            LSP_LV2_URI("spectrum_analyzer_x2"),
            LSP_LV2UI_URI("spectrum_analyzer_x2"),
            "aw7r",
            LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 1,
            LSP_LADSPA_URI("spectrum_analyzer_x2"),
            LSP_CLAP_URI("spectrum_analyzer_x2"),
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x2_ports,
            "analyzer/spectrum/x2.xml",
            NULL,
            NULL,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x4 =
        {
            "Spektrumanalysator x4",
            "Spectrum Analyzer x4",
            "SA4",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x4",
            LSP_LV2_URI("spectrum_analyzer_x4"),
            LSP_LV2UI_URI("spectrum_analyzer_x4"),
            "xzgo",
            LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 2,
            LSP_LADSPA_URI("spectrum_analyzer_x4"),
            LSP_CLAP_URI("spectrum_analyzer_x4"),
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x4_ports,
            "analyzer/spectrum/x4.xml",
            NULL,
            NULL,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x8 =
        {
            "Spektrumanalysator x8",
            "Spectrum Analyzer x8",
            "SA8",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x8",
            LSP_LV2_URI("spectrum_analyzer_x8"),
            LSP_LV2UI_URI("spectrum_analyzer_x8"),
            "e5hb",
            LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 3,
            LSP_LADSPA_URI("spectrum_analyzer_x8"),
            LSP_CLAP_URI("spectrum_analyzer_x8"),
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x8_ports,
            "analyzer/spectrum/x8.xml",
            NULL,
            NULL,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x12 =
        {
            "Spektrumanalysator x12",
            "Spectrum Analyzer x12",
            "SA12",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x12",
            LSP_LV2_URI("spectrum_analyzer_x12"),
            LSP_LV2UI_URI("spectrum_analyzer_x12"),
            "tj3l",
            LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 4,
            LSP_LADSPA_URI("spectrum_analyzer_x12"),
            LSP_CLAP_URI("spectrum_analyzer_x12"),
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x12_ports,
            "analyzer/spectrum/x12.xml",
            NULL,
            NULL,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x16 =
        {
            "Spektrumanalysator x16",
            "Spectrum Analyzer x16",
            "SA16",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x16",
            LSP_LV2_URI("spectrum_analyzer_x16"),
            LSP_LV2UI_URI("spectrum_analyzer_x16"),
            "nuzi",
            LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 5,
            LSP_LADSPA_URI("spectrum_analyzer_x16"),
            LSP_CLAP_URI("spectrum_analyzer_x16"),
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x16_ports,
            "analyzer/spectrum/x16.xml",
            NULL,
            NULL,
            &spectrum_analyzer_bundle
        };
    } /* namespace meta */
} /* namespace lsp */
