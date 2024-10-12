/*
 * Copyright (C) 2024 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2024 Vladimir Sadovnikov <sadko4u@gmail.com>
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
#define LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION_MICRO         28

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
            { "32768", NULL },
            { NULL, NULL }
        };

        static const port_item_t line_thick_modes[]=
        {
            { "Thinnest",       "spectrum.line_thick.thinnest" },
            { "Thin",           "spectrum.line_thick.thin" },
            { "Normal",         "spectrum.line_thick.normal" },
            { "Semibold",       "spectrum.line_thick.semibold" },
            { "Bold",           "spectrum.line_thick.bold" },
            { NULL,             NULL }
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
            R("1"),
            R("2"),
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
            R("1"), R("2"), R("3"), R("4"),
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
            R("1"), R("2"), R("3"), R("4"),
            R("5"), R("6"), R("7"), R("8"),
            { NULL, NULL }
        };

        static const port_item_t *spectrum_analyzer_x8_modes = spectrum_analyzer_x4_modes;

        static const port_item_t spectrum_analyzer_x12_channels[]=
        {
            R("1"), R("2"), R("3"), R("4"),
            R("5"), R("6"), R("7"), R("8"),
            R("9"), R("10"), R("11"), R("12"),
            { NULL, NULL }
        };

        static const port_item_t *spectrum_analyzer_x12_modes = spectrum_analyzer_x4_modes;

        static const port_item_t spectrum_analyzer_x16_channels[]=
        {
            R("1"), R("2"), R("3"), R("4"),
            R("5"), R("6"), R("7"), R("8"),
            R("9"), R("10"), R("11"), R("12"),
            R("13"), R("14"), R("15"), R("16"),
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

        #define SA_CORRMETER(id, label) \
            METER(id, label, U_PERCENT, spectrum_analyzer::CORRELATION)

        #define SA_INPUT(x, total, active) \
            AUDIO_INPUT_N(x), \
            AUDIO_OUTPUT_N(x), \
            SWITCH("on_" #x, "Analyse " #x, active), \
            SWITCH("solo_" #x, "Solo " #x, 0.0f), \
            SWITCH("frz_" #x, "Freeze " #x, 0.0f), \
            { "hue_" #x, "Hue " #x, U_NONE, R_CONTROL, F_UPPER | F_LOWER | F_STEP | F_CYCLIC, 0.0f, 1.0f, (float(x) / float(total)), 0.25f/360.0f, NULL     }, \
            AMP_GAIN("sh_" #x, "Shift gain " #x, 1.0f, 1000.0f)

        #define SA_COMMON(c, channel) \
            BYPASS, \
            COMBO("mode", "Analyzer mode", 0, spectrum_analyzer_x ## c ## _modes), \
            COMBO("lthick", "Mesh thickness", 2, line_thick_modes), \
            COMBO("spm", "Spectralizer mode", 1, spectralizer_modes), \
            SWITCH("splog", "Spectralizer logarithmic scale", 1), \
            SWITCH("freeze", "Analyzer freeze", 0), \
            SWITCH("mline", "Horizontal measuring line", 0), \
            SWITCH("mtrack", "Track maximum values", 1), \
            TRIGGER("mreset", "Reset maximum values"), \
            { "tol", "FFT Tolerance", U_ENUM, R_CONTROL, 0, 0, 0, spectrum_analyzer::RANK_DFL - spectrum_analyzer::RANK_MIN, 0, fft_tolerance }, \
            { "wnd", "FFT Window", U_ENUM, R_CONTROL, 0, 0, 0, spectrum_analyzer::WND_DFL, 0, fft_windows }, \
            { "env", "FFT Envelope", U_ENUM, R_CONTROL, 0, 0, 0, spectrum_analyzer::ENV_DFL, 0, fft_envelopes }, \
            AMP_GAIN("pamp", "Preamp gain", spectrum_analyzer::PREAMP_DFL, 1000.0f), \
            LOG_CONTROL("zoom", "Graph zoom", U_GAIN_AMP, spectrum_analyzer::ZOOM), \
            { "react",          "Reactivity",       U_SEC,          R_CONTROL, F_UPPER | F_LOWER | F_STEP | F_LOG, \
                 spectrum_analyzer::REACT_TIME_MIN, spectrum_analyzer::REACT_TIME_MAX, spectrum_analyzer::REACT_TIME_DFL, spectrum_analyzer::REACT_TIME_STEP, NULL }, \
            channel(c) \
            LOG_CONTROL("sel", "Selector", U_HZ, spectrum_analyzer::SELECTOR), \
            LOG_CONTROL("mlval", "Horizontal measuring line level value", U_DB, spectrum_analyzer::HLINE), \
            { "freq", "Frequency", U_HZ, R_METER, F_UPPER | F_LOWER, \
                spectrum_analyzer::FREQ_MIN, spectrum_analyzer::FREQ_MAX, spectrum_analyzer::FREQ_DFL, 0, NULL }, \
            { "lvl", "Level", U_GAIN_AMP, R_METER, F_UPPER | F_LOWER, 0, 10000, 0, 0, NULL }, \
            MESH("spd", "Spectrum Data", c + 2, spectrum_analyzer::MESH_POINTS + 4)

        #define SA_SGROUP(id) \
            SWITCH("ms_" #id, "Mid/Side switch for channel pair " #id, 0), \
            SA_CORRMETER("cm_" #id, "Correlometer for stereo channel pair " #id)

        #define SA_CHANNEL(c)   { "chn", "Channel", U_ENUM, R_CONTROL, 0, 0, 0, 0, 0, spectrum_analyzer_x ## c ## _channels },
        #define SA_SKIP(c)

        static const port_t spectrum_analyzer_x1_ports[] =
        {
            SA_INPUT(0, 1, 1),
            SA_COMMON(1, SA_SKIP),
            FBUFFER("fb", "Spectralizer buffer", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x2_ports[] =
        {
            SA_INPUT(0, 2, 1),
            SA_INPUT(1, 2, 1),
            SA_SGROUP(0),
            SA_COMMON(2, SA_CHANNEL),
            SWITCH("ms", "Stereo analysis Mid/Side mode", 0),
            COMBO("spc", "Spectralizer channel", 0, spectrum_analyzer_x2_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x4_ports[] =
        {
            SA_INPUT(0, 4, 1),
            SA_INPUT(1, 4, 1),
            SA_INPUT(2, 4, 0),
            SA_INPUT(3, 4, 0),
            SA_SGROUP(0),
            SA_SGROUP(1),
            SA_COMMON(4, SA_CHANNEL),
            SA_CORRMETER("cccm", "Correlometer for selected channels"),
            SWITCH("ms", "Stereo analysis Mid/Side mode", 0),
            COMBO("spc0", "Spectralizer channel 0", 0, spectrum_analyzer_x4_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            COMBO("spc1", "Spectralizer channel 1", 1, spectrum_analyzer_x4_channels),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x8_ports[] =
        {
            SA_INPUT(0, 8, 1),
            SA_INPUT(1, 8, 1),
            SA_INPUT(2, 8, 0),
            SA_INPUT(3, 8, 0),
            SA_INPUT(4, 8, 0),
            SA_INPUT(5, 8, 0),
            SA_INPUT(6, 8, 0),
            SA_INPUT(7, 8, 0),
            SA_SGROUP(0),
            SA_SGROUP(1),
            SA_SGROUP(2),
            SA_SGROUP(3),
            SA_COMMON(8, SA_CHANNEL),
            SA_CORRMETER("cccm", "Correlometer for selected channels"),
            SWITCH("ms", "Stereo analysis Mid/Side mode", 0),
            COMBO("spc0", "Spectralizer channel 0", 0, spectrum_analyzer_x8_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            COMBO("spc1", "Spectralizer channel 1", 1, spectrum_analyzer_x8_channels),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x12_ports[] =
        {
            SA_INPUT(0, 12, 1),
            SA_INPUT(1, 12, 1),
            SA_INPUT(2, 12, 0),
            SA_INPUT(3, 12, 0),
            SA_INPUT(4, 12, 0),
            SA_INPUT(5, 12, 0),
            SA_INPUT(6, 12, 0),
            SA_INPUT(7, 12, 0),
            SA_INPUT(8, 12, 0),
            SA_INPUT(9, 12, 0),
            SA_INPUT(10, 12, 0),
            SA_INPUT(11, 12, 0),
            SA_SGROUP(0),
            SA_SGROUP(1),
            SA_SGROUP(2),
            SA_SGROUP(3),
            SA_SGROUP(4),
            SA_SGROUP(5),
            SA_COMMON(12, SA_CHANNEL),
            SA_CORRMETER("cccm", "Correlometer for selected channels"),
            SWITCH("ms", "Stereo analysis Mid/Side mode", 0),
            COMBO("spc0", "Spectralizer channel 0", 0, spectrum_analyzer_x12_channels),
            FBUFFER("fb0", "Spectralizer buffer 0", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            COMBO("spc1", "Spectralizer channel 1", 1, spectrum_analyzer_x12_channels),
            FBUFFER("fb1", "Spectralizer buffer 1", spectrum_analyzer::FB_ROWS, spectrum_analyzer::MESH_POINTS),
            PORTS_END
        };

        static const port_t spectrum_analyzer_x16_ports[] =
        {
            SA_INPUT(0, 16, 1),
            SA_INPUT(1, 16, 1),
            SA_INPUT(2, 16, 0),
            SA_INPUT(3, 16, 0),
            SA_INPUT(4, 16, 0),
            SA_INPUT(5, 16, 0),
            SA_INPUT(6, 16, 0),
            SA_INPUT(7, 16, 0),
            SA_INPUT(8, 16, 0),
            SA_INPUT(9, 16, 0),
            SA_INPUT(10, 16, 0),
            SA_INPUT(11, 16, 0),
            SA_INPUT(12, 16, 0),
            SA_INPUT(13, 16, 0),
            SA_INPUT(14, 16, 0),
            SA_INPUT(15, 16, 0),
            SA_SGROUP(0),
            SA_SGROUP(1),
            SA_SGROUP(2),
            SA_SGROUP(3),
            SA_SGROUP(4),
            SA_SGROUP(5),
            SA_SGROUP(6),
            SA_SGROUP(7),
            SA_COMMON(16, SA_CHANNEL),
            SA_CORRMETER("cccm", "Correlometer for selected channels"),
            SWITCH("ms", "Stereo analysis Mid/Side mode", 0),
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

        MONO_PORT_GROUP_PORT(mono_in, "in0");
        MONO_PORT_GROUP_PORT(mono_out, "out0");
        STEREO_PORT_GROUP_PORTS(stereo_in0, "in0", "in1");
        STEREO_PORT_GROUP_PORTS(stereo_in1, "in2", "in3");
        STEREO_PORT_GROUP_PORTS(stereo_in2, "in4", "in5");
        STEREO_PORT_GROUP_PORTS(stereo_in3, "in6", "in7");
        STEREO_PORT_GROUP_PORTS(stereo_in4, "in8", "in9");
        STEREO_PORT_GROUP_PORTS(stereo_in5, "in10", "in11");
        STEREO_PORT_GROUP_PORTS(stereo_in6, "in12", "in13");
        STEREO_PORT_GROUP_PORTS(stereo_in7, "in14", "in15");
        STEREO_PORT_GROUP_PORTS(stereo_out0, "out0", "out1");
        STEREO_PORT_GROUP_PORTS(stereo_out1, "out2", "out3");
        STEREO_PORT_GROUP_PORTS(stereo_out2, "out4", "out5");
        STEREO_PORT_GROUP_PORTS(stereo_out3, "out6", "out7");
        STEREO_PORT_GROUP_PORTS(stereo_out4, "out8", "out9");
        STEREO_PORT_GROUP_PORTS(stereo_out5, "out10", "out11");
        STEREO_PORT_GROUP_PORTS(stereo_out6, "out12", "out13");
        STEREO_PORT_GROUP_PORTS(stereo_out7, "out14", "out15");

        const port_group_t spectrum_analyzer_x1_port_groups[] =
        {
            { "in_0",           "Input 0",       GRP_MONO,      PGF_IN | PGF_MAIN,          mono_in_ports       },
            { "out_0",          "Output 0",      GRP_MONO,      PGF_OUT | PGF_MAIN,         mono_out_ports      },
            PORT_GROUPS_END
        };

        const port_group_t spectrum_analyzer_x2_port_groups[] =
        {
            { "in_0",           "Input 0",       GRP_STEREO,    PGF_IN | PGF_MAIN,          stereo_in0_ports    },
            { "out_0",          "Output 0",      GRP_STEREO,    PGF_OUT | PGF_MAIN,         stereo_out0_ports   },
            PORT_GROUPS_END
        };

        const port_group_t spectrum_analyzer_x4_port_groups[] =
        {
            { "in_0",           "Input 0",       GRP_STEREO,    PGF_IN | PGF_MAIN,          stereo_in0_ports    },
            { "out_0",          "Output 0",      GRP_STEREO,    PGF_OUT | PGF_MAIN,         stereo_out0_ports   },
            { "in_1",           "Input 1",       GRP_STEREO,    PGF_IN,                     stereo_in1_ports    },
            { "out_1",          "Output 1",      GRP_STEREO,    PGF_OUT,                    stereo_out1_ports   },
            PORT_GROUPS_END
        };

        const port_group_t spectrum_analyzer_x8_port_groups[] =
        {
            { "in_0",           "Input 0",       GRP_STEREO,    PGF_IN | PGF_MAIN,          stereo_in0_ports    },
            { "out_0",          "Output 0",      GRP_STEREO,    PGF_OUT | PGF_MAIN,         stereo_out0_ports   },
            { "in_1",           "Input 1",       GRP_STEREO,    PGF_IN,                     stereo_in1_ports    },
            { "out_1",          "Output 1",      GRP_STEREO,    PGF_OUT,                    stereo_out1_ports   },
            { "in_2",           "Input 2",       GRP_STEREO,    PGF_IN,                     stereo_in2_ports    },
            { "out_2",          "Output 2",      GRP_STEREO,    PGF_OUT,                    stereo_out2_ports   },
            { "in_3",           "Input 3",       GRP_STEREO,    PGF_IN,                     stereo_in3_ports    },
            { "out_3",          "Output 3",      GRP_STEREO,    PGF_OUT,                    stereo_out3_ports   },
            PORT_GROUPS_END
        };

        const port_group_t spectrum_analyzer_x12_port_groups[] =
        {
            { "in_0",           "Input 0",       GRP_STEREO,    PGF_IN | PGF_MAIN,          stereo_in0_ports    },
            { "out_0",          "Output 0",      GRP_STEREO,    PGF_OUT | PGF_MAIN,         stereo_out0_ports   },
            { "in_1",           "Input 1",       GRP_STEREO,    PGF_IN,                     stereo_in1_ports    },
            { "out_1",          "Output 1",      GRP_STEREO,    PGF_OUT,                    stereo_out1_ports   },
            { "in_2",           "Input 2",       GRP_STEREO,    PGF_IN,                     stereo_in2_ports    },
            { "out_2",          "Output 2",      GRP_STEREO,    PGF_OUT,                    stereo_out2_ports   },
            { "in_3",           "Input 3",       GRP_STEREO,    PGF_IN,                     stereo_in3_ports    },
            { "out_3",          "Output 3",      GRP_STEREO,    PGF_OUT,                    stereo_out3_ports   },
            { "in_4",           "Input 4",       GRP_STEREO,    PGF_IN,                     stereo_in4_ports    },
            { "out_4",          "Output 4",      GRP_STEREO,    PGF_OUT,                    stereo_out4_ports   },
            { "in_5",           "Input 5",       GRP_STEREO,    PGF_IN,                     stereo_in5_ports    },
            { "out_5",          "Output 5",      GRP_STEREO,    PGF_OUT,                    stereo_out5_ports   },
            PORT_GROUPS_END
        };

        const port_group_t spectrum_analyzer_x16_port_groups[] =
        {
            { "in_0",           "Input 0",       GRP_STEREO,    PGF_IN | PGF_MAIN,          stereo_in0_ports    },
            { "out_0",          "Output 0",      GRP_STEREO,    PGF_OUT | PGF_MAIN,         stereo_out0_ports   },
            { "in_1",           "Input 1",       GRP_STEREO,    PGF_IN,                     stereo_in1_ports    },
            { "out_1",          "Output 1",      GRP_STEREO,    PGF_OUT,                    stereo_out1_ports   },
            { "in_2",           "Input 2",       GRP_STEREO,    PGF_IN,                     stereo_in2_ports    },
            { "out_2",          "Output 2",      GRP_STEREO,    PGF_OUT,                    stereo_out2_ports   },
            { "in_3",           "Input 3",       GRP_STEREO,    PGF_IN,                     stereo_in3_ports    },
            { "out_3",          "Output 3",      GRP_STEREO,    PGF_OUT,                    stereo_out3_ports   },
            { "in_4",           "Input 4",       GRP_STEREO,    PGF_IN,                     stereo_in4_ports    },
            { "out_4",          "Output 4",      GRP_STEREO,    PGF_OUT,                    stereo_out4_ports   },
            { "in_5",           "Input 5",       GRP_STEREO,    PGF_IN,                     stereo_in5_ports    },
            { "out_5",          "Output 5",      GRP_STEREO,    PGF_OUT,                    stereo_out5_ports   },
            { "in_6",           "Input 6",       GRP_STEREO,    PGF_IN,                     stereo_in6_ports    },
            { "out_6",          "Output 6",      GRP_STEREO,    PGF_OUT,                    stereo_out6_ports   },
            { "in_7",           "Input 7",       GRP_STEREO,    PGF_IN,                     stereo_in7_ports    },
            { "out_7",          "Output 7",      GRP_STEREO,    PGF_OUT,                    stereo_out7_ports   },
            PORT_GROUPS_END
        };

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
            "Spectrum Analyzer x1",
            "SA1",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x1",
            {
                LSP_LV2_URI("spectrum_analyzer_x1"),
                LSP_LV2UI_URI("spectrum_analyzer_x1"),
                "qtez",
                LSP_VST3_UID("sa1     qtez"),
                LSP_VST3UI_UID("sa1     qtez"),
                LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 0,
                LSP_LADSPA_URI("spectrum_analyzer_x1"),
                LSP_CLAP_URI("spectrum_analyzer_x1"),
                LSP_GST_UID("spectrum_analyzer_x1"),
            },
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x1_ports,
            "analyzer/spectrum/x1.xml",
            NULL,
            spectrum_analyzer_x1_port_groups,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x2 =
        {
            "Spektrumanalysator x2",
            "Spectrum Analyzer x2",
            "Spectrum Analyzer x2",
            "SA2",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x2",
            {
                LSP_LV2_URI("spectrum_analyzer_x2"),
                LSP_LV2UI_URI("spectrum_analyzer_x2"),
                "aw7r",
                LSP_VST3_UID("sa2     aw7r"),
                LSP_VST3UI_UID("sa2     aw7r"),
                LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 1,
                LSP_LADSPA_URI("spectrum_analyzer_x2"),
                LSP_CLAP_URI("spectrum_analyzer_x2"),
                LSP_GST_UID("spectrum_analyzer_x2"),
            },
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x2_ports,
            "analyzer/spectrum/x2.xml",
            NULL,
            spectrum_analyzer_x2_port_groups,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x4 =
        {
            "Spektrumanalysator x4",
            "Spectrum Analyzer x4",
            "Spectrum Analyzer x4",
            "SA4",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x4",
            {
                LSP_LV2_URI("spectrum_analyzer_x4"),
                LSP_LV2UI_URI("spectrum_analyzer_x4"),
                "xzgo",
                LSP_VST3_UID("sa4     xzgo"),
                LSP_VST3UI_UID("sa4     xzgo"),
                LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 2,
                LSP_LADSPA_URI("spectrum_analyzer_x4"),
                LSP_CLAP_URI("spectrum_analyzer_x4"),
                LSP_GST_UID("spectrum_analyzer_x4"),
            },
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x4_ports,
            "analyzer/spectrum/x4.xml",
            NULL,
            spectrum_analyzer_x4_port_groups,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x8 =
        {
            "Spektrumanalysator x8",
            "Spectrum Analyzer x8",
            "Spectrum Analyzer x8",
            "SA8",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x8",
            {
                LSP_LV2_URI("spectrum_analyzer_x8"),
                LSP_LV2UI_URI("spectrum_analyzer_x8"),
                "e5hb",
                LSP_VST3_UID("sa8     e5hb"),
                LSP_VST3UI_UID("sa8     e5hb"),
                LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 3,
                LSP_LADSPA_URI("spectrum_analyzer_x8"),
                LSP_CLAP_URI("spectrum_analyzer_x8"),
                LSP_GST_UID("spectrum_analyzer_x8"),
            },
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x8_ports,
            "analyzer/spectrum/x8.xml",
            NULL,
            spectrum_analyzer_x8_port_groups,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x12 =
        {
            "Spektrumanalysator x12",
            "Spectrum Analyzer x12",
            "Spectrum Analyzer x12",
            "SA12",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x12",
            {
                LSP_LV2_URI("spectrum_analyzer_x12"),
                LSP_LV2UI_URI("spectrum_analyzer_x12"),
                "tj3l",
                LSP_VST3_UID("sa12    tj3l"),
                LSP_VST3UI_UID("sa12    tj3l"),
                LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 4,
                LSP_LADSPA_URI("spectrum_analyzer_x12"),
                LSP_CLAP_URI("spectrum_analyzer_x12"),
                LSP_GST_UID("spectrum_analyzer_x12"),
            },
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x12_ports,
            "analyzer/spectrum/x12.xml",
            NULL,
            spectrum_analyzer_x12_port_groups,
            &spectrum_analyzer_bundle
        };

        const plugin_t spectrum_analyzer_x16 =
        {
            "Spektrumanalysator x16",
            "Spectrum Analyzer x16",
            "Spectrum Analyzer x16",
            "SA16",
            &developers::v_sadovnikov,
            "spectrum_analyzer_x16",
            {
                LSP_LV2_URI("spectrum_analyzer_x16"),
                LSP_LV2UI_URI("spectrum_analyzer_x16"),
                "nuzi",
                LSP_VST3_UID("sa16    nuzi"),
                LSP_VST3UI_UID("sa16    nuzi"),
                LSP_LADSPA_SPECTRUM_ANALYZER_BASE + 5,
                LSP_LADSPA_URI("spectrum_analyzer_x16"),
                LSP_CLAP_URI("spectrum_analyzer_x16"),
                LSP_GST_UID("spectrum_analyzer_x16"),
            },
            LSP_PLUGINS_SPECTRUM_ANALYZER_VERSION,
            plugin_classes,
            clap_features,
            E_INLINE_DISPLAY | E_DUMP_STATE,
            spectrum_analyzer_x16_ports,
            "analyzer/spectrum/x16.xml",
            NULL,
            spectrum_analyzer_x16_port_groups,
            &spectrum_analyzer_bundle
        };
    } /* namespace meta */
} /* namespace lsp */
