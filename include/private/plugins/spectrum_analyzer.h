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

#ifndef PRIVATE_PLUGINS_SPECTRUM_ANALYZER_H_
#define PRIVATE_PLUGINS_SPECTRUM_ANALYZER_H_

#include <lsp-plug.in/dsp-units/ctl/Counter.h>
#include <lsp-plug.in/dsp-units/util/Analyzer.h>
#include <lsp-plug.in/plug-fw/plug.h>
#include <lsp-plug.in/plug-fw/core/IDBuffer.h>

#include <private/meta/spectrum_analyzer.h>

namespace lsp
{
    namespace plugins
    {
        class spectrum_analyzer: public plug::Module
        {
            protected:
                typedef struct sa_channel_t
                {
                    bool            bOn;                // Enabled flag
                    bool            bFreeze;            // Freeze flag
                    bool            bSolo;              // Soloing flag
                    bool            bSend;              // Send to UI flag
                    float           fGain;              // Makeup gain
                    float           fHue;               // Hue
                    float          *vIn;                // Input buffer pointer
                    float          *vOut;               // Output buffer pointer

                    // Port references
                    plug::IPort    *pIn;                // Input samples
                    plug::IPort    *pOut;               // Output samples
                    plug::IPort    *pOn;                // FFT on
                    plug::IPort    *pSolo;              // Soloing flag
                    plug::IPort    *pFreeze;            // Freeze flag
                    plug::IPort    *pHue;               // Hue of the graph color
                    plug::IPort    *pShift;             // Shift gain
                } sa_channel_t;

                typedef struct sa_spectralizer_t
                {
                    size_t          nPortId;            // Last port identifier
                    ssize_t         nChannelId;         // Channel identifier

                    plug::IPort    *pPortId;            // Port identifier
                    plug::IPort    *pFBuffer;           // Frame buffer port
                } sa_spectralizer_t;

                enum mode_t
                {
                    SA_ANALYZER,
                    SA_ANALYZER_STEREO,
                    SA_MASTERING,
                    SA_MASTERING_STEREO,
                    SA_SPECTRALIZER,
                    SA_SPECTRALIZER_STEREO
                };

                enum flags_t
                {
                    F_MASTERING     = 1 << 0,
                    F_SMOOTH_LOG    = 1 << 1,
                    F_LOG_SCALE     = 1 << 2,
                    F_BOOST         = 1 << 3
                };

            protected:
                bool                create_channels(size_t channels);
                mode_t              decode_mode(size_t mode);

            protected:
                dspu::Analyzer      sAnalyzer;
                dspu::Counter       sCounter;
                size_t              nChannels;
                sa_channel_t       *vChannels;
                float             **vAnalyze;           // Analysis buffers
                float              *vFrequences;
                float              *vMFrequences;
                uint32_t           *vIndexes;
                uint8_t            *pData;

                bool                bBypass;
                size_t              nChannel;
                float               fSelector;
                float               fMinFreq;
                float               fMaxFreq;
                float               fReactivity;        // Reactivity
                float               fTau;               // Time constant (dependent on reactivity)
                float               fPreamp;            // Preamplification level
                float               fZoom;              // Zoom
                mode_t              enMode;
                bool                bLogScale;

                plug::IPort        *pBypass;
                plug::IPort        *pMode;
                plug::IPort        *pTolerance;
                plug::IPort        *pWindow;
                plug::IPort        *pEnvelope;
                plug::IPort        *pPreamp;
                plug::IPort        *pZoom;
                plug::IPort        *pReactivity;
                plug::IPort        *pChannel;
                plug::IPort        *pSelector;
                plug::IPort        *pFrequency;
                plug::IPort        *pLevel;
                plug::IPort        *pLogScale;
                plug::IPort        *pFftData;

                plug::IPort        *pFreeze;
                plug::IPort        *pSpp;
                sa_spectralizer_t   vSpc[2];

                core::IDBuffer     *pIDisplay;          // Inline display buffer

            protected:
                void                update_multiple_settings();
                void                update_x2_settings(ssize_t ch1, ssize_t ch2);
                void                update_spectralizer_x2_settings(ssize_t ch1, ssize_t ch2);

                void                process_multiple();
                void                process_spectralizer();
                void                get_spectrum(float *dst, size_t channel, size_t flags);

            public:
                explicit spectrum_analyzer(const meta::plugin_t *metadata);
                virtual ~spectrum_analyzer();

            public:
                virtual void        init(plug::IWrapper *wrapper, plug::IPort **ports);
                virtual void        destroy();

                virtual void        update_settings();
                virtual void        update_sample_rate(long sr);

                virtual void        process(size_t samples);
                virtual bool        inline_display(plug::ICanvas *cv, size_t width, size_t height);

                virtual void        dump(dspu::IStateDumper *v) const;
        };
    }
}


#endif /* PRIVATE_PLUGINS_SPECTRUM_ANALYZER_H_ */
