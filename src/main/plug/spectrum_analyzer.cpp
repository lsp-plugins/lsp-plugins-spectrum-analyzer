/*
 * Copyright (C) 2025 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2025 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <private/meta/spectrum_analyzer.h>
#include <private/plugins/spectrum_analyzer.h>

#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/dsp/dsp.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/core/AudioBuffer.h>
#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <lsp-plug.in/stdlib/math.h>

#include <lsp-plug.in/shared/debug.h>
#include <lsp-plug.in/shared/id_colors.h>

#define BUFFER_SIZE         0x1000u

namespace lsp
{
    namespace plugins
    {
        static constexpr size_t EQ_SMOOTH_STEP      = 32;

        //---------------------------------------------------------------------
        // Plugin factory
        static const meta::plugin_t *plugins[] =
        {
            &meta::spectrum_analyzer_x1,
            &meta::spectrum_analyzer_x2,
            &meta::spectrum_analyzer_x4,
            &meta::spectrum_analyzer_x8,
            &meta::spectrum_analyzer_x12,
            &meta::spectrum_analyzer_x16
        };

        static plug::Module *plugin_factory(const meta::plugin_t *meta)
        {
            return new spectrum_analyzer(meta);
        }

        static plug::Factory factory(plugin_factory, plugins, 6);

        //-------------------------------------------------------------------------
        spectrum_analyzer::spectrum_analyzer(const meta::plugin_t *metadata): plug::Module(metadata)
        {
            nChannels           = 0;
            nCorrelometers      = 0;
            vChannels           = NULL;
            vCorrelometers      = NULL;

            for (size_t i=0; i<2; ++i)
            {
                dspu::filter_params_t *fp = &vFP[i];
                fp->nType           = dspu::FLT_NONE;
                fp->nSlope          = 0;
                fp->fFreq           = 1000.0f;
                fp->fFreq2          = 1000.0f;
                fp->fGain           = 1.0f;
                fp->fQuality        = 1.0f;
            }

            vAnalyze            = NULL;
            pData               = NULL;
            vFrequences[0]      = NULL;
            vFrequences[1]      = NULL;
            vMaxValues[0]       = NULL;
            vMaxValues[1]       = NULL;
            vMaxValues[2]       = NULL;
            vMaxValues[3]       = NULL;
            vMFrequences        = NULL;
            vIndexes[0]         = NULL;
            vIndexes[1]         = NULL;

            bBypass             = false;
            nChannel            = 0;
            fSelector           = 0;
            fMinFreq            = 0;
            fMaxFreq            = 0;
            fReactivity         = 0.0f;
            fTau                = 0.0f;
            fPreamp             = 0.0f;
            fZoom               = 0.0f;
            enMode              = SA_ANALYZER;
            bLogScale           = false;
            bLinFreq            = false;
            bMSSwitch           = false;
            bInspOn             = false;
            bSyncInspFilter     = false;
            bSmoothInspFilter   = false;

            fWndState           = 0.0f;
            fEnvState           = 0.0f;

            pBypass             = NULL;
            pMode               = NULL;
            pTolerance          = NULL;
            pWindow             = NULL;
            pEnvelope           = NULL;
            pPreamp             = NULL;
            pZoom               = NULL;
            pReactivity         = NULL;
            pChannel            = NULL;
            pSelector           = NULL;
            pFrequency          = NULL;
            pInspSwitch         = NULL;
            pInspRange          = NULL;
            pLevel              = NULL;
            pLogScale           = NULL;
            pLinFreq            = NULL;
            pFftData            = NULL;
            pInspMesh           = NULL;
            pMSSwitch           = NULL;

            pFreeze             = NULL;
            pMaxReset           = NULL;
            pSpp                = NULL;

            for (size_t i=0; i<2; ++i)
            {
                vSpc[i].nPortId     = -1;
                vSpc[i].nChannelId  = 0;
                vSpc[i].bLinFreq    = false;

                vSpc[i].pPortId     = NULL;
                vSpc[i].pFBuffer    = NULL;
            }

            pIDisplay           = NULL;
        }

        spectrum_analyzer::~spectrum_analyzer()
        {
            do_destroy();
        }

        bool spectrum_analyzer::create_channels(size_t channels)
        {
            lsp_trace("this=%p, channels = %d", this, int(channels));

            // Calculate header size
            size_t n_correlometers  = (channels >= 4) ? (channels / 2) + 1 : (channels >= 2) ? 1 : 0;
            size_t hdr_size         = align_size(sizeof(sa_channel_t) * channels, 64);
            size_t freq_buf_size    = align_size(sizeof(float) * meta::spectrum_analyzer::MESH_POINTS, 64);
            size_t mfreq_buf_size   = align_size(sizeof(float) * meta::spectrum_analyzer::MESH_POINTS, 64);
            size_t ind_buf_size     = align_size(sizeof(uint32_t) * meta::spectrum_analyzer::MESH_POINTS, 64);
            size_t analyze_size     = align_size(sizeof(float *) * channels, 16);
            size_t buffers          = BUFFER_SIZE * sizeof(float) * channels;
            size_t szof_corrs       = align_size(sizeof(sa_correlometer_t) * n_correlometers, 64);
            size_t alloc            = hdr_size +
                                      freq_buf_size * 2 +               // vFrequences[2]
                                      freq_buf_size * 4 +               // vMaxValues[4]
                                      mfreq_buf_size +
                                      ind_buf_size * 2 +                // vIndexes[2]
                                      analyze_size +
                                      buffers +
                                      freq_buf_size * channels * 8 +    // vSpc[4], vMax[4]
                                      szof_corrs;

            lsp_trace("header_size      = %d", int(hdr_size));
            lsp_trace("freq_buf_size    = %d", int(freq_buf_size));
            lsp_trace("mfreq_buf_size   = %d", int(mfreq_buf_size));
            lsp_trace("ind_buf_size     = %d", int(ind_buf_size));
            lsp_trace("buffers          = %d", int(buffers));
            lsp_trace("corrs            = %d", int(szof_corrs));
            lsp_trace("alloc            = %d", int(alloc));

            // Allocate data
            uint8_t *ptr        = alloc_aligned<uint8_t>(pData, alloc, 64);
            if (ptr == NULL)
                return false;
            lsp_guard_assert( uint8_t *guard = ptr );

            // Initialize core
            nChannels       = channels;
            nCorrelometers  = n_correlometers;
            nChannel        = 0;
            fSelector       = meta::spectrum_analyzer::SELECTOR_DFL;
            fMinFreq        = meta::spectrum_analyzer::FREQ_MIN;
            fMaxFreq        = meta::spectrum_analyzer::FREQ_MAX;
            fReactivity     = meta::spectrum_analyzer::REACT_TIME_DFL;
            fTau            = 1.0f;
            fPreamp         = meta::spectrum_analyzer::PREAMP_DFL;

            // Initialize pointers and cleanup buffers
            vChannels       = advance_ptr_bytes<sa_channel_t>(ptr, hdr_size);
            vCorrelometers  = (n_correlometers > 0) ? advance_ptr_bytes<sa_correlometer_t>(ptr, szof_corrs) : NULL;
            vFrequences[0]  = advance_ptr_bytes<float>(ptr, freq_buf_size);
            vFrequences[1]  = advance_ptr_bytes<float>(ptr, freq_buf_size);
            for (size_t j=0; j<4; ++j)
                vMaxValues[j]   = advance_ptr_bytes<float>(ptr, freq_buf_size);
            vMFrequences    = advance_ptr_bytes<float>(ptr, mfreq_buf_size);
            vIndexes[0]     = advance_ptr_bytes<uint32_t>(ptr, ind_buf_size);
            vIndexes[1]     = advance_ptr_bytes<uint32_t>(ptr, ind_buf_size);
            vAnalyze        = advance_ptr_bytes<float *>(ptr, analyze_size);

            dsp::fill_zero(vFrequences[0], meta::spectrum_analyzer::MESH_POINTS);
            dsp::fill_zero(vFrequences[1], meta::spectrum_analyzer::MESH_POINTS);
            dsp::fill_zero(vMFrequences, meta::spectrum_analyzer::MESH_POINTS);
            for (size_t j=0; j<4; ++j)
                dsp::fill_zero(vMaxValues[j], meta::spectrum_analyzer::MESH_POINTS);
            memset(vIndexes[0], 0, ind_buf_size);
            memset(vIndexes[1], 0, ind_buf_size);

            // Initialize channels
            for (size_t i=0; i<channels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];

                // Construct inspection equalizer
                c->sInspEq.construct();
                if (!c->sInspEq.init(1, 0))
                    return false;
                c->sInspEq.set_mode(dspu::EQM_IIR);

                // Initialize fields
                c->bOn              = false;
                c->bFreeze          = false;
                c->bSolo            = false;
                c->bSend            = false;
                c->bMSSwitch        = false;
                c->fGain            = 1.0f;
                c->vIn              = NULL;
                c->vOut             = NULL;
                c->vRet             = NULL;
                c->vBuffer          = advance_ptr_bytes<float>(ptr, BUFFER_SIZE * sizeof(float));
                for (size_t j=0; j<4; ++j)
                {
                    c->vSpc[j]          = advance_ptr_bytes<float>(ptr, freq_buf_size);
                    c->vMax[j]          = advance_ptr_bytes<float>(ptr, freq_buf_size);
                }

                // Port references
                c->pIn              = NULL;
                c->pOut             = NULL;
                c->pRet             = NULL;
                c->pMSSwitch        = NULL;
                c->pOn              = NULL;
                c->pFreeze          = NULL;
                c->pShift           = NULL;

                // Clear the buffer
                dsp::fill_zero(c->vBuffer, BUFFER_SIZE);
                for (size_t j=0; j<4; ++j)
                {
                    dsp::fill_zero(c->vSpc[j], meta::spectrum_analyzer::MESH_POINTS);
                    dsp::fill_zero(c->vMax[j], meta::spectrum_analyzer::MESH_POINTS);
                }
            }

            // Initialize correlometers
            for (size_t i=0; i<n_correlometers; ++i)
            {
                sa_correlometer_t *cm   = &vCorrelometers[i];
                cm->sCorr.construct();
                cm->fCorrelation        = 0.0f;
                cm->pCorrelometer       = NULL;
            }

            lsp_assert(ptr <= &guard[alloc]);

            return true;
        }

        void spectrum_analyzer::init(plug::IWrapper *wrapper, plug::IPort **ports)
        {
            // Pass wrapper
            plug::Module::init(wrapper, ports);

            // Determine number of channels
            size_t channels     = 0;
            if (pMetadata == NULL)
                return;
            for (const meta::port_t *p=pMetadata->ports; p->id != NULL; ++p)
                if (meta::is_audio_port(p) && (meta::is_in_port(p)))
                    ++channels;

            // Initialize analyzer
            sAnalyzer.init(
                channels, meta::spectrum_analyzer::RANK_MAX,
                MAX_SAMPLE_RATE, meta::spectrum_analyzer::REFRESH_RATE
            );
            sAnalyzer.set_rate(meta::spectrum_analyzer::REFRESH_RATE);

            // Initialize counter
            sCounter.set_frequency(meta::spectrum_analyzer::FB_ROWS / meta::spectrum_analyzer::FB_TIME, true);

            // Allocate channels
            if (!create_channels(channels))
                return;

            // Seek for first input port
            size_t port_id = 0;

            // Now we are available to map the ports for channels
            for (size_t i=0; i<nChannels; ++i)
            {
                lsp_trace("binding channel %d", int(i));

                plug::IPort *vp = ports[port_id];
                if (vp == NULL)
                    break;
                const meta::port_t *p = vp->metadata();
                if (p == NULL)
                    break;
                if ((p->id == NULL) || (!meta::is_audio_port(p)) || (!meta::is_in_port(p)))
                    break;

                sa_channel_t *c     = &vChannels[i];
                BIND_PORT(c->pIn);
                BIND_PORT(c->pOut);
                BIND_PORT(c->pOn);
                BIND_PORT(c->pSolo);
                BIND_PORT(c->pFreeze);
                BIND_PORT(c->pShift);

                // Sync metadata
                const meta::port_t *meta  = c->pSolo->metadata();
                if (meta != NULL)
                    c->bSolo        = meta->start >= 0.5f;
                meta                = c->pShift->metadata();
                if (meta != NULL)
                    c->fGain            = meta->start;

                lsp_trace("channel %d successful bound", int(i));
            }

            // Bind mid/side switches
            if (nChannels > 1)
            {
                for (size_t i=0; i<nChannels; i += 2)
                {
                    sa_channel_t *l         = &vChannels[i];
                    sa_channel_t *r         = &vChannels[i+1];
                    sa_correlometer_t *cm   = &vCorrelometers[i >> 1];

                    SKIP_PORT("Return name");
                    BIND_PORT(l->pRet);
                    BIND_PORT(r->pRet);
                    BIND_PORT(l->pMSSwitch);
                    r->pMSSwitch        = l->pMSSwitch;

                    BIND_PORT(cm->pCorrelometer);
                }
            }
            else
            {
                SKIP_PORT("Return name");
                BIND_PORT(vChannels[0].pRet);
            }

            // Initialize basic ports
            BIND_PORT(pBypass);
            BIND_PORT(pMode);
            SKIP_PORT("Mesh thickness");
            SKIP_PORT("Spectralizer mode");
            BIND_PORT(pLogScale);
            BIND_PORT(pFreeze);
            BIND_PORT(pLinFreq);
            SKIP_PORT("Horizontal line switch button");
            SKIP_PORT("All maximum tracking");
            if (nChannels > 1)
                SKIP_PORT("Channels maximum tracking");
            BIND_PORT(pMaxReset);
            BIND_PORT(pTolerance);
            BIND_PORT(pWindow);
            BIND_PORT(pEnvelope);
            BIND_PORT(pPreamp);
            BIND_PORT(pZoom);
            BIND_PORT(pReactivity);
            if (nChannels > 1)
                BIND_PORT(pChannel);
            BIND_PORT(pSelector);
            SKIP_PORT("Horizontal line value");
            BIND_PORT(pInspSwitch);
            BIND_PORT(pInspRange);
            SKIP_PORT("Automatic frequency range inspect");
            BIND_PORT(pFrequency);
            BIND_PORT(pLevel);
            BIND_PORT(pFftData);
            BIND_PORT(pInspMesh);

            // Bind global correlometer ports
            if (nChannels >= 4)
            {
                BIND_PORT(vCorrelometers[nCorrelometers-1].pCorrelometer);
            }

            // Bind spectralizer ports
            if (nChannels >= 2)
            {
                BIND_PORT(pMSSwitch);
                BIND_PORT(vSpc[0].pPortId);
            }
            BIND_PORT(vSpc[0].pFBuffer);
            vSpc[0].nChannelId  = -1;

            if (nChannels >= 2)
            {
                if (nChannels > 2)
                    BIND_PORT(vSpc[1].pPortId);
                BIND_PORT(vSpc[1].pFBuffer);
                vSpc[1].nChannelId  = -1;
            }

            // Initialize values
            fMinFreq        = pFrequency->metadata()->min;
            fMaxFreq        = pFrequency->metadata()->max;

            lsp_trace("this=%p, basic ports successful bound", this);
        }

        void spectrum_analyzer::destroy()
        {
            plug::Module::destroy();
            do_destroy();
        }

        void spectrum_analyzer::do_destroy()
        {
            if (vChannels != NULL)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    sa_channel_t *c     = &vChannels[i];
                    c->sInspEq.destroy();
                }
                vChannels       = NULL;
            }

            if (vCorrelometers != NULL)
            {
                for (size_t i=0; i<nCorrelometers; ++i)
                {
                    sa_correlometer_t *cm = &vCorrelometers[i];
                    cm->sCorr.destroy();
                }
                vCorrelometers  = NULL;
            }

            sAnalyzer.destroy();

            if (pData != NULL)
            {
                free_aligned(pData);
                pData           = NULL;
            }
            vFrequences[0]  = NULL;
            vFrequences[1]  = NULL;
            vIndexes[0]     = NULL;
            vIndexes[1]     = NULL;

            if (pIDisplay != NULL)
            {
                pIDisplay->destroy();
                pIDisplay       = NULL;
            }

            vChannels       = NULL;
        }

        spectrum_analyzer::mode_t spectrum_analyzer::decode_mode(size_t mode)
        {
            if (nChannels == 1)
            {
                switch (mode)
                {
                    case 0: return SA_ANALYZER;
                    case 1: return SA_MASTERING;
                    case 2: return SA_SPECTRALIZER;
                    default:
                        return SA_ANALYZER;
                }
            }
            else if (nChannels == 2)
            {
                switch (mode)
                {
                    case 0: return SA_ANALYZER;
                    case 1: return SA_MASTERING;
                    case 2: return SA_SPECTRALIZER;
                    case 3: return SA_SPECTRALIZER_STEREO;
                    default:
                        return SA_ANALYZER;
                }
            }
            else
            {
                switch (mode)
                {
                    case 0: return SA_ANALYZER;
                    case 1: return SA_ANALYZER_STEREO;
                    case 2: return SA_MASTERING;
                    case 3: return SA_MASTERING_STEREO;
                    case 4: return SA_SPECTRALIZER;
                    case 5: return SA_SPECTRALIZER_STEREO;
                    default:
                        return SA_ANALYZER;
                }
            }
        }

        void spectrum_analyzer::update_multiple_settings()
        {
            // Check that there are soloing channels
            bool has_solo           = false;
            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];
                if (c->pSolo->value() >= 0.5f)
                {
                    has_solo            = true;
                    break;
                }
            }

            // Process channel parameters
            bool freeze_all     = pFreeze->value() >= 0.5f;

            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];

                c->bOn              = c->pOn->value() >= 0.5f;
                c->bFreeze          = (freeze_all) || (c->pFreeze->value() >= 0.5f);
                c->bSolo            = c->pSolo->value() >= 0.5f;
                c->bSend            = (c->bOn) && ((!has_solo) || (c->bSolo));
                c->bMSSwitch        = (c->pMSSwitch != NULL) ? c->pMSSwitch->value() >= 0.5f : false;
                c->fGain            = c->pShift->value();
            }

            bMSSwitch               = false;
            vSpc[0].nChannelId      = -1;
            vSpc[1].nChannelId      = -1;

        }

        void spectrum_analyzer::update_x2_settings(ssize_t ch1, ssize_t ch2)
        {
            bool freeze_all     = pFreeze->value() >= 0.5f;
            ssize_t nc          = nChannels;

            if (ch1 >= nc)
                ch1 -= nc;
            if (ch2 >= nc)
                ch2 -= nc;

            for (ssize_t i=0; i<nc; ++i)
            {
                sa_channel_t *c     = &vChannels[i];

                c->bOn              = (i == ch1) || (i == ch2);
                c->bFreeze          = (freeze_all) || (c->pFreeze->value() >= 0.5f);
                c->bSolo            = false;
                c->bSend            = c->bOn;
                c->bMSSwitch        = false;
                c->fGain            = c->pShift->value();
            }

            bMSSwitch               = (pMSSwitch != NULL) ? pMSSwitch->value() >= 0.5f : false;
            vSpc[0].nPortId         = ch1;
            vSpc[1].nPortId         = ch2;
            vSpc[0].nChannelId      = -1;
            vSpc[1].nChannelId      = -1;
        }

        void spectrum_analyzer::update_spectralizer_x2_settings(ssize_t ch1, ssize_t ch2)
        {
            bool freeze_all     = pFreeze->value() >= 0.5f;
            ssize_t nc          = nChannels;

            if (ch1 >= nc)
                ch1 -= nc;
            if (ch2 >= nc)
                ch2 -= nc;

            for (ssize_t i=0; i<nc; ++i)
            {
                sa_channel_t *c     = &vChannels[i];

                c->bOn              = (i == ch1) || (i == ch2);
                c->bFreeze          = (freeze_all) || (c->pFreeze->value() >= 0.5f);
                c->bSolo            = false;
                c->bSend            = false; // We do not need to send mesh data because utilizing framebuffer ports
                c->bMSSwitch        = false;
                c->fGain            = c->pShift->value();
            }

            bMSSwitch               = (pMSSwitch != NULL) ? pMSSwitch->value() >= 0.5f : false;
            vSpc[0].nPortId         = ch1;
            vSpc[1].nPortId         = ch2;
            vSpc[0].nChannelId      = ch1;
            vSpc[1].nChannelId      = ch2;
        }

        void spectrum_analyzer::update_settings()
        {
            // Update global settings
            const float freq        = pSelector->value();
            bBypass                 = pBypass->value();
            nChannel                = (pChannel != NULL) ? pChannel->value() : 0;
            fSelector               = lsp_limit((freq * 2.0f) / fSampleRate, 0.0f, 1.0f);
            fPreamp                 = pPreamp->value();
            fZoom                   = pZoom->value();
            bLogScale               = (pLogScale != NULL) && (pLogScale->value() >= 0.5f);
            size_t rank             = pTolerance->value() + meta::spectrum_analyzer::RANK_MIN;

            bool res_state          = false;  // Automatic reset maximum values

            if (pMaxReset->value() >= 0.5f)
                res_state = true;

            lsp_trace("rank         = %d",     int(rank));
            lsp_trace("channel      = %d",     int(nChannel));
            lsp_trace("selector     = %.3f",   fSelector);
            lsp_trace("preamp       = %.3f",   fPreamp);
            lsp_trace("reactivity   = %.3f",   fReactivity);
            lsp_trace("tau          = %.5f",   fTau);

            // Update channel state depending on the mode
            mode_t mode = decode_mode(pMode->value());

            switch (mode)
            {
                case SA_ANALYZER:
                case SA_MASTERING:
                    update_multiple_settings();
                    break;

                case SA_ANALYZER_STEREO:
                case SA_MASTERING_STEREO:
                    if (nChannels > 2)
                        update_x2_settings(vSpc[0].pPortId->value(), vSpc[1].pPortId->value());
                    else if (nChannels == 2)
                        update_x2_settings(0, 1);
                    else
                        update_x2_settings(0, -1); // This should not happen, but... let's do a special scenario
                    break;

                case SA_SPECTRALIZER:
                    if (nChannels > 2)
                        update_spectralizer_x2_settings(vSpc[0].pPortId->value(), -1);
                    else if (nChannels == 2)
                        update_spectralizer_x2_settings(vSpc[0].pPortId->value(), -1);
                    else
                        update_spectralizer_x2_settings(0, -1); // This should not happen, but... let's do a special scenario
                    break;

                case SA_SPECTRALIZER_STEREO:
                    if (nChannels > 2)
                        update_spectralizer_x2_settings(vSpc[0].pPortId->value(), vSpc[1].pPortId->value());
                    else if (nChannels == 2)
                        update_spectralizer_x2_settings(0, 1);
                    else
                        update_spectralizer_x2_settings(0, -1); // This should not happen, but... let's do a special scenario
                    break;

                default:
                    break;
            }

            // Update mode
            enMode       = mode;

            // Update analysis parameters
            bool sync_freqs         = rank != sAnalyzer.get_rank();
            if (sync_freqs)
            {
                res_state    = true;
                sAnalyzer.set_rank(rank);
            }
            const bool lin_freq     = pLinFreq->value() >= 0.5f;
            if (lin_freq != bLinFreq)
                bSyncInspFilter         = true;
            bLinFreq                = lin_freq;

            sAnalyzer.set_reactivity(pReactivity->value());
            sAnalyzer.set_window(pWindow->value());
            sAnalyzer.set_envelope(pEnvelope->value());

            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];
                sAnalyzer.enable_channel(i, c->bOn);
                sAnalyzer.freeze_channel(i, c->bFreeze);
            }

            // Reconfigure analyzer if required
            if (sAnalyzer.needs_reconfiguration())
                sAnalyzer.reconfigure();

            if (sync_freqs)
            {
                sAnalyzer.get_frequencies(
                    vFrequences[0], vIndexes[0],
                    fMinFreq, fMaxFreq,
                    meta::spectrum_analyzer::MESH_POINTS,
                    false);
                sAnalyzer.get_frequencies(
                    vFrequences[1], vIndexes[1],
                    fMinFreq, fMaxFreq,
                    meta::spectrum_analyzer::MESH_POINTS,
                    true);
            }

            // Check if the state of the switches has not changed
            if (pWindow->value() != fWndState)
            {
                res_state    = true;
                fWndState    = pWindow->value();
            }
            if (pEnvelope->value() != fEnvState)
            {
                res_state    = true;
                fEnvState    = pEnvelope->value();
            }

            // Check inspection enabled
            const float insp_on     = pInspSwitch->value() >= 0.5f;
            dspu::filter_params_t *old_fp = &vFP[0];
            dspu::filter_params_t *fp = &vFP[1];
            fp->nType               = (insp_on) ? dspu::FLT_BT_BWC_BANDPASS : dspu::FLT_NONE;

            if (insp_on)
            {
                const float f_range = expf(M_LN2 * 0.5f * pInspRange->value());

                fp->nSlope          = 4;
                fp->fFreq           = freq / f_range;
                fp->fFreq2          = freq * f_range;
                fp->fGain           = 1.0f;
                fp->fQuality        = 0.707f;

                if (!bInspOn)
                {
                    bSyncInspFilter     = true;
                    bSmoothInspFilter   = false;
                    *old_fp             = *fp;

                    for (size_t i=0; i<nChannels; ++i)
                    {
                        sa_channel_t *c = & vChannels[i];
                        c->sInspEq.set_params(0, fp);
                        c->sInspEq.reset();
                    }
                }
                else if ((old_fp->nType != fp->nType) ||
                    (old_fp->nSlope != fp->nSlope) ||
                    (old_fp->fFreq != fp->fFreq) ||
                    (old_fp->fGain != fp->fGain) ||
                    (old_fp->fQuality != fp->fQuality))
                {
                    bSmoothInspFilter   = true;
                    bSyncInspFilter     = true;
                }
            }

            if (insp_on != bInspOn)
                bSyncInspFilter     = true;
            bInspOn             = insp_on;

            lsp_trace("insp_on = %s, sync = %s, smooth = %s",
                (bInspOn) ? "true" : "false",
                (bSyncInspFilter) ? "true" : "false",
                (bSmoothInspFilter) ? "true" : "false");

            // if the state has changed
            if (res_state)
            {
                // Reset Tracking Mesh and channel meshes
                for (size_t j=0; j<4; ++j)
                    dsp::fill_zero(vMaxValues[j], meta::spectrum_analyzer::MESH_POINTS);

                for (size_t i=0; i<nChannels; ++i)
                {
                    sa_channel_t *c     = &vChannels[i];
                    for (size_t j=0; j<4; ++j)
                        dsp::fill_zero(c->vMax[j], meta::spectrum_analyzer::MESH_POINTS);
                }
            }
        }

        void spectrum_analyzer::update_sample_rate(long sr)
        {
            lsp_trace("this=%p, sample_rate = %d", this, int(sr));
            const size_t corr_period = dspu::millis_to_samples(sr, meta::spectrum_analyzer::CORR_PERIOD);

            for (size_t i=0; i<nCorrelometers; ++i)
            {
                sa_correlometer_t *cm   = &vCorrelometers[i];
                cm->sCorr.init(corr_period);
                cm->sCorr.set_period(corr_period);
                cm->sCorr.clear();
            }

            for (size_t i=0; i<nChannels; ++i)
                vChannels[i].sInspEq.set_sample_rate(sr);
            bSyncInspFilter = true;

            sAnalyzer.set_sample_rate(sr);
            if (sAnalyzer.needs_reconfiguration())
                sAnalyzer.reconfigure();

            sAnalyzer.get_frequencies(
                vFrequences[0], vIndexes[0],
                fMinFreq, fMaxFreq,
                meta::spectrum_analyzer::MESH_POINTS,
                false);
            sAnalyzer.get_frequencies(
                vFrequences[1], vIndexes[1],
                fMinFreq, fMaxFreq,
                meta::spectrum_analyzer::MESH_POINTS,
                true);
            sCounter.set_sample_rate(sr, true);
        }

        void spectrum_analyzer::ui_activated()
        {
            bSyncInspFilter = true;
        }

        void spectrum_analyzer::get_spectrum(float *dst, size_t channel, size_t flags)
        {
            float *v        = dst;
            size_t off      = 0;

            // Fetch original data
            const uint32_t *indexes = (flags & F_LIN_FREQ) ? vIndexes[1] : vIndexes[0];
            if (flags & F_MASTERING)
            {
                sAnalyzer.get_spectrum(channel, vMFrequences, indexes, meta::spectrum_analyzer::MESH_POINTS);
                size_t pi = 0, ni = meta::spectrum_analyzer::MMESH_STEP;

                for (; ni < meta::spectrum_analyzer::MESH_POINTS; ni += meta::spectrum_analyzer::MMESH_STEP)
                {
                    if (indexes[ni] == indexes[pi])
                        continue;

                    if (flags & F_SMOOTH_LOG)
                    {
                        const float v_pi = lsp_max(vMFrequences[pi], GAIN_AMP_M_160_DB);
                        const float v_ni = lsp_max(vMFrequences[ni], GAIN_AMP_M_160_DB);
                        dsp::smooth_cubic_log(&v[off], v_pi, v_ni, ni-pi);
                    }
                    else
                        dsp::smooth_cubic_linear(&v[off], vMFrequences[pi], vMFrequences[ni], ni-pi);

                    off        += ni-pi;
                    pi          = ni;
                }

                if (pi < meta::spectrum_analyzer::MESH_POINTS)
                {
                    if (flags & F_SMOOTH_LOG)
                    {
                        const float v_pi = lsp_max(vMFrequences[pi], GAIN_AMP_M_160_DB);
                        const float v_ni = lsp_max(vMFrequences[ni-1], GAIN_AMP_M_160_DB);
                        dsp::smooth_cubic_log(&v[off], v_pi, v_ni, ni-pi);
                    }
                    else
                        dsp::smooth_cubic_linear(&v[off], vMFrequences[pi], vMFrequences[ni-1], ni-pi);
                }
            }
            else
                sAnalyzer.get_spectrum(channel, v, indexes, meta::spectrum_analyzer::MESH_POINTS);

            // Apply gain
            float gain = (flags & F_BOOST) ?
                    vChannels[channel].fGain * meta::spectrum_analyzer::SPECTRALIZER_BOOST:
                    vChannels[channel].fGain * meta::spectrum_analyzer::ANALYZER_BOOST;
            dsp::mul_k2(dst, gain * fPreamp, meta::spectrum_analyzer::MESH_POINTS);

            // Apply log scale if necessary
            if (flags & F_LOG_SCALE)
            {
                dsp::logd1(dst, meta::spectrum_analyzer::MESH_POINTS);
                float k = 10.0f / (meta::spectrum_analyzer::THRESH_HI_DB - meta::spectrum_analyzer::THRESH_LO_DB);
                float s = 0.1f * meta::spectrum_analyzer::THRESH_LO_DB;

                for (size_t i=0; i<meta::spectrum_analyzer::MESH_POINTS; ++i)
                    dst[i] = k * (dst[i] - s);
            }
        }

        void spectrum_analyzer::measure_correlation(size_t count)
        {
            // Do the correlation measurements
            if (nCorrelometers > 0)
            {
                for (size_t i=0; i<nChannels; i += 2)
                {
                    sa_channel_t *l         = &vChannels[i];
                    sa_channel_t *r         = &vChannels[i + 1];
                    sa_correlometer_t *cm   = &vCorrelometers[i >> 1];

                    float min               = 0.0f;
                    float max               = 0.0f;
                    cm->sCorr.process(l->vBuffer, l->vIn, r->vIn, count);
                    dsp::minmax(l->vBuffer, count, &min, &max);
                    max                     = (fabs(min) > fabs(max)) ? min : max;

                    if (fabs(cm->fCorrelation) < fabs(max))
                        cm->fCorrelation        = max;
                }

                if (nChannels >= 4)
                {
                    sa_correlometer_t *cm   = &vCorrelometers[nCorrelometers-1];
                    sa_channel_t *a         = (vSpc[0].nPortId >= 0) ? &vChannels[vSpc[0].nPortId] : NULL;
                    sa_channel_t *b         = (vSpc[1].nPortId >= 0) ? &vChannels[vSpc[1].nPortId] : NULL;

                    float min               = 0.0f;
                    float max               = 0.0f;
                    if ((a != NULL) && (b != NULL))
                    {
                        cm->sCorr.process(a->vBuffer, a->vIn, b->vIn, count);
                        dsp::minmax(a->vBuffer, count, &min, &max);
                    }

                    if (fabs(cm->fCorrelation) < fabs(max))
                        cm->fCorrelation        = max;
                }
            }
        }

        void spectrum_analyzer::prepare_buffers(size_t count)
        {
            // Mix with return if it is routed
            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];

                vAnalyze[i]         = c->vIn;
                if (c->vRet != NULL)
                {
                    dsp::add3(c->vBuffer, c->vIn, c->vRet, count);
                    vAnalyze[i]         = c->vBuffer;
                }
            }

            if (nChannels > 1)
            {
                if (!bMSSwitch)
                {
                    for (size_t i=0; i<nChannels; i += 2)
                    {
                        sa_channel_t *l     = &vChannels[i];
                        sa_channel_t *r     = &vChannels[i + 1];

                        if ((l->bMSSwitch) || (r->bMSSwitch))
                        {
                            dsp::lr_to_ms(l->vBuffer, r->vBuffer, vAnalyze[i], vAnalyze[i+1], count);
                            vAnalyze[i]         = l->vBuffer;
                            vAnalyze[i+1]       = r->vBuffer;
                        }
                    }
                }
                else
                {
                    // Special case: do MS convert for two (or one) channels
                    ssize_t l_id        = vSpc[0].nPortId;
                    ssize_t r_id        = vSpc[1].nPortId;
                    if (r_id < 0)
                        r_id                = l_id;

                    sa_channel_t *l     = &vChannels[l_id];
                    sa_channel_t *r     = &vChannels[r_id];

                    if (l_id != r_id)
                    {
                        dsp::lr_to_ms(l->vBuffer, r->vBuffer, vAnalyze[l_id], vAnalyze[r_id], count);
                        vAnalyze[l_id]      = l->vBuffer;
                        vAnalyze[r_id]      = r->vBuffer;
                    }
                    else
                    {
                        dsp::lr_to_mid(l->vBuffer, vAnalyze[l_id], vAnalyze[r_id], count);
                        vAnalyze[l_id]      = l->vBuffer;
                    }
                }
            }
        }

        void spectrum_analyzer::analyze_data(size_t samples)
        {
            const size_t fft_size   = 1 << sAnalyzer.get_rank();

            for (size_t n=samples; n > 0;)
            {
                // Get number of samples to process
                const size_t count  = lsp_min(sCounter.pending(), n, BUFFER_SIZE);
                const bool fired    = sCounter.submit(count);

                // Measure correlation
                measure_correlation(count);

                if (bBypass)
                {
                    // Bypass signal
                    pFrequency->set_value(0);
                    pLevel->set_value(0);
                }
                else
                {
                    // Prepare analysis data
                    prepare_buffers(count);

                    // Perform analysis
                    sAnalyzer.process(vAnalyze, count);

                    // Report values
                    sa_channel_t *c     = &vChannels[nChannel];

                    size_t idx          = fSelector * ((fft_size - 1) >> 1);
                    pFrequency->set_value(float(idx * fSampleRate) / float(fft_size));
                    float lvl = sAnalyzer.get_level(nChannel, idx) * meta::spectrum_analyzer::ANALYZER_BOOST;
                    pLevel->set_value(lvl * c->fGain * fPreamp);

                    // Copy frequency points
                    if ((enMode == SA_MASTERING) || (enMode == SA_MASTERING_STEREO))
                    {
                        for (size_t i=0; i<nChannels; ++i)
                        {
                            c           = &vChannels[i];

                            get_spectrum(c->vSpc[1], i, F_MASTERING | F_SMOOTH_LOG);
                            dsp::pmax2(c->vMax[1], c->vSpc[1], meta::spectrum_analyzer::MESH_POINTS);
                            dsp::pmax2(vMaxValues[1], c->vSpc[1], meta::spectrum_analyzer::MESH_POINTS);

                            get_spectrum(c->vSpc[3], i, F_MASTERING | F_LIN_FREQ);
                            dsp::pmax2(c->vMax[3], c->vSpc[3], meta::spectrum_analyzer::MESH_POINTS);
                            dsp::pmax2(vMaxValues[3], c->vSpc[3], meta::spectrum_analyzer::MESH_POINTS);
                        }
                    }
                    else
                    {
                        for (size_t i=0; i<nChannels; ++i)
                        {
                            c           = &vChannels[i];

                            get_spectrum(c->vSpc[0], i, 0);
                            dsp::pmax2(c->vMax[0], c->vSpc[0], meta::spectrum_analyzer::MESH_POINTS);
                            dsp::pmax2(vMaxValues[0], c->vSpc[0], meta::spectrum_analyzer::MESH_POINTS);

                            get_spectrum(c->vSpc[2], i, F_LIN_FREQ);
                            dsp::pmax2(c->vMax[2], c->vSpc[2], meta::spectrum_analyzer::MESH_POINTS);
                            dsp::pmax2(vMaxValues[2], c->vSpc[2], meta::spectrum_analyzer::MESH_POINTS);
                        }
                    }
                }

                // Update pointers
                for (size_t i=0; i<nChannels; ++i)
                {
                    // Get channel pointer
                    sa_channel_t *c     = &vChannels[i];
                    c->vIn             += count;
                    c->vOut            += count;
                    if (c->vRet != NULL)
                        c->vRet            += count;
                }

                // Synchronize buffer state
                if ((enMode == SA_SPECTRALIZER) || (enMode == SA_SPECTRALIZER_STEREO))
                {
                    // Update frame buffers if counter has fired
                    if ((fired) && (!bBypass))
                    {
                        size_t flags = (bLinFreq) ? F_LIN_FREQ : 0;
                        if (bLogScale)
                            flags      |= F_LOG_SCALE;
                        else
                            flags      |= F_BOOST;

                        for (size_t i=0; i<2; ++i)
                        {
                            ssize_t cid = vSpc[i].nChannelId;
                            if (cid < 0)
                                continue;
                            plug::IPort *p = vSpc[i].pFBuffer;
                            if (p == NULL)
                                continue;
                            plug::frame_buffer_t *fb = p->buffer<plug::frame_buffer_t>();
                            if (fb == NULL)
                                continue;

                            // Check that frequency presentation has changed
                            if (vSpc[i].bLinFreq != bLinFreq)
                            {
                                fb->clear();
                                vSpc[i].bLinFreq    = bLinFreq;
                            }

                            // Get row and commit it
                            sa_channel_t *c     = &vChannels[cid];
                            if (c->bFreeze) // Do not report new data in 'Hold' state
                                continue;

                            // Output data
                            float *v            = fb->next_row();
                            get_spectrum(v, cid, flags);
                            fb->write_row(); // Mark row as written
                        }
                    }
                }

                // Update state
                n -= count;
                if (fired)
                    sCounter.commit();
            } // for n
        }

        void spectrum_analyzer::pass_signal(size_t samples)
        {
            // Just copy data to output if inspection is disabled
            if (!bInspOn)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    sa_channel_t *c = &vChannels[i];
                    dsp::copy(c->vOut, c->vIn, samples);
                }
                return;
            }

            // Apply inspection filter
            if (!bSmoothInspFilter)
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    sa_channel_t *c = &vChannels[i];
                    c->sInspEq.process(c->vOut, c->vIn, samples);
                }
                return;
            }

            // Smooth inspection filter
            dspu::filter_params_t fp;
            const float den                 = 1.0f / samples;
            dspu::filter_params_t *old_fp   = &vFP[0];
            dspu::filter_params_t *new_fp   = &vFP[1];

            fp.nType                        = new_fp->nType;
            fp.nSlope                       = new_fp->nSlope;

            // In smooth mode, we need to update filter parameters for each sample
            for (size_t offset=0; offset<samples; )
            {
                const size_t count          = lsp_min(samples - offset, EQ_SMOOTH_STEP);
                const float k               = offset * den;

                // Tune filter parameters
                fp.fFreq                    = old_fp->fFreq * expf(logf(new_fp->fFreq / old_fp-> fFreq)*k);
                fp.fFreq2                   = old_fp->fFreq2 * expf(logf(new_fp->fFreq2 / old_fp->fFreq2)*k);
                fp.fGain                    = old_fp->fGain * expf(logf(new_fp->fGain / old_fp->fGain)*k);
                fp.fQuality                 = old_fp->fQuality + (new_fp->fQuality - old_fp->fQuality)*k;

                for (size_t i=0; i<nChannels; ++i)
                {
                    sa_channel_t *c             = &vChannels[i];
                    c->sInspEq.set_params(0, &fp);
                    c->sInspEq.process(&c->vOut[offset], &c->vIn[offset], count);
                }

                // Update position
                offset                     += count;
            }

            // Store new filter parameters
            *old_fp                 = *new_fp;
            bSyncInspFilter         = true;
        }

        void spectrum_analyzer::process(size_t samples)
        {
            // Always query for drawing
            pWrapper->query_display_draw();

            // Now process the channels
            for (size_t i=0; i<nChannels; ++i)
            {
                // Get channel pointers
                sa_channel_t *c     = &vChannels[i];
                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();

                core::AudioBuffer *ret  = c->pRet->buffer<core::AudioBuffer>();
                c->vRet             = ((ret!= NULL) && (ret->active())) ? ret->buffer() : NULL;
            }

            // Cleanup correlometers
            for (size_t i=0; i<nCorrelometers; ++i)
                vCorrelometers[i].fCorrelation      = 0.0f;

            analyze_data(samples);

            // Pass the sigal to the output
            pass_signal(samples);

            // Output spectrum
            output_spectrum();
            output_inpect_filter();

            // Report correlometers
            for (size_t i=0; i<nCorrelometers; ++i)
            {
                sa_correlometer_t *cm = &vCorrelometers[i];
                cm->pCorrelometer->set_value(cm->fCorrelation * 100.0f);
            }
        }

        void spectrum_analyzer::output_spectrum()
        {
            // Check that mesh request is pending
            plug::mesh_t *mesh      = pFftData->buffer<plug::mesh_t>();
            bool mesh_request       = (mesh != NULL) && (mesh->isEmpty());
            if ((enMode == SA_SPECTRALIZER) || (enMode == SA_SPECTRALIZER_STEREO))
                mesh_request        = false;

            if (!mesh_request)
                return;

            // Frequencies
            size_t rows = 0;
            float *v    = mesh->pvData[rows++];
            const bool linear   = bLinFreq;
            const bool mastering= (enMode == SA_MASTERING) || (enMode == SA_MASTERING_STEREO);
            dsp::copy(&v[2], (linear) ? vFrequences[1] : vFrequences[0], meta::spectrum_analyzer::MESH_POINTS);
            const size_t sub    = ((linear) ? 2 : 0) + ((mastering) ? 1 : 0);

            v[0]    = SPEC_FREQ_MIN * 0.5f;
            v[1]    = SPEC_FREQ_MIN * 0.5f;
            v      += meta::spectrum_analyzer::MESH_POINTS + 2;
            v[0]    = SPEC_FREQ_MAX * 2.0f;
            v[1]    = SPEC_FREQ_MAX * 2.0f;

            // Output current values
            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c = &vChannels[i];
                v               = mesh->pvData[rows++];

                if (c->bSend)
                {
                    // Current value
                    if (bBypass)
                        dsp::fill_zero(&v[2], meta::spectrum_analyzer::MESH_POINTS);
                    else
                        dsp::copy(&v[2], c->vSpc[sub], meta::spectrum_analyzer::MESH_POINTS);

                    v[0]    = GAIN_AMP_M_INF_DB;
                    v[1]    = v[2];
                    v      += meta::spectrum_analyzer::MESH_POINTS + 2;
                    v[0]    = v[-1];
                    v[1]    = GAIN_AMP_M_INF_DB;
                }
                else
                    dsp::fill_zero(v, meta::spectrum_analyzer::MESH_POINTS + 4);
            }

            // Output maximums
            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c = &vChannels[i];
                v               = mesh->pvData[rows++];

                if (c->bSend)
                {
                    // Current value
                    if (bBypass)
                        dsp::fill_zero(&v[2], meta::spectrum_analyzer::MESH_POINTS);
                    else
                        dsp::copy(&v[2], c->vMax[sub], meta::spectrum_analyzer::MESH_POINTS);

                    v[0]    = GAIN_AMP_M_INF_DB;
                    v[1]    = v[2];
                    v      += meta::spectrum_analyzer::MESH_POINTS + 2;
                    v[0]    = v[-1];
                    v[1]    = GAIN_AMP_M_INF_DB;
                }
                else
                    dsp::fill_zero(v, meta::spectrum_analyzer::MESH_POINTS + 4);
            }

            // Output common maximum
            v       = mesh->pvData[rows++];
            if (bBypass)
                dsp::fill_zero(&v[2], meta::spectrum_analyzer::MESH_POINTS);
            else
                dsp::copy(&v[2], vMaxValues[sub], meta::spectrum_analyzer::MESH_POINTS);

            v[0]    = GAIN_AMP_M_INF_DB;
            v[1]    = v[2];
            v      += meta::spectrum_analyzer::MESH_POINTS + 2;
            v[0]    = v[-1];
            v[1]    = GAIN_AMP_M_INF_DB;

            // Commit mesh data
            mesh->data(rows, meta::spectrum_analyzer::MESH_POINTS + 4);
        }

        void spectrum_analyzer::output_inpect_filter()
        {
            if (!bSyncInspFilter)
                return;

            plug::mesh_t *mesh  = (pInspMesh != NULL) ? pInspMesh->buffer<plug::mesh_t>() : NULL;
            if ((mesh == NULL) || (!mesh->isEmpty()))
                return;

            float *v            = mesh->pvData[0];
            const bool linear   = bLinFreq;
            dsp::copy(&v[2], (linear) ? vFrequences[1] : vFrequences[0], meta::spectrum_analyzer::MESH_POINTS);
            v[0]                = SPEC_FREQ_MIN;
            v[1]                = SPEC_FREQ_MIN;
            v                  += meta::spectrum_analyzer::MESH_POINTS + 2;
            v[0]                = SPEC_FREQ_MAX;
            v[1]                = SPEC_FREQ_MAX;

            v                   = mesh->pvData[1];
            if (bInspOn)
            {
                for (size_t i=0, samples=meta::spectrum_analyzer::MESH_POINTS + 4; i<samples; ++i)
                {
                    const size_t count  = lsp_min(samples - i, meta::spectrum_analyzer::MESH_POINTS / 2);
                    vChannels[0].sInspEq.freq_chart(vMFrequences, &mesh->pvData[0][i + 2], count);
                    dsp::pcomplex_mod(&v[i], vMFrequences, count);
                }
            }
            else
                dsp::fill_zero(v, meta::spectrum_analyzer::MESH_POINTS + 4);

            // Commit mesh data
            mesh->data(2, meta::spectrum_analyzer::MESH_POINTS + 4);
            bSyncInspFilter     = false;
        }

        bool spectrum_analyzer::inline_display(plug::ICanvas *cv, size_t width, size_t height)
        {
            // Check proportions
            if (height > (M_RGOLD_RATIO * width))
                height  = M_RGOLD_RATIO * width;

            // Init canvas
            if (!cv->init(width, height))
                return false;
            width   = cv->width();
            height  = cv->height();

            // Clear background
            bool bypass = bBypass;
            cv->set_color_rgb((bypass) ? CV_DISABLED : CV_BACKGROUND);
            cv->paint();

            // Draw axis
            cv->set_line_width(1.0);

            float zx    = 1.0f/SPEC_FREQ_MIN;
            float zy    = fZoom/GAIN_AMP_M_72_DB;
            float dx    = width/(logf(SPEC_FREQ_MAX)-logf(SPEC_FREQ_MIN));
            float dy    = height/(logf(GAIN_AMP_M_72_DB/fZoom)-logf(GAIN_AMP_P_24_DB*fZoom));

            // Draw vertical lines
            cv->set_color_rgb(CV_YELLOW, 0.5f);
            for (float i=100.0f; i<SPEC_FREQ_MAX; i *= 10.0f)
            {
                float ax = dx*(logf(i*zx));
                cv->line(ax, 0, ax, height);
            }

            // Draw horizontal lines
            cv->set_color_rgb(CV_WHITE, 0.5f);
            for (float i=GAIN_AMP_M_60_DB; i<GAIN_AMP_P_24_DB; i *= GAIN_AMP_P_12_DB)
            {
                float ay = height + dy*(logf(i*zy));
                cv->line(0, ay, width, ay);
            }

            // Allocate buffer: f, a(f), x, y
            pIDisplay               = core::IDBuffer::reuse(pIDisplay, 4, width);
            core::IDBuffer *b       = pIDisplay;
            if (b == NULL)
                return false;

            if (bypass)
                return true;

            Color col(CV_MESH);
            cv->set_line_width(2);

            float ni        = float(meta::spectrum_analyzer::MESH_POINTS) / width; // Normalizing index
            uint32_t *idx   = reinterpret_cast<uint32_t *>(alloca(width * sizeof(uint32_t))); //vIndexes;

            const bool linear           = bLinFreq;
            const uint32_t *vindexes    = (linear) ? vIndexes[1] : vIndexes[0];
            const float *vfrequences    = (linear) ? vFrequences[1] : vFrequences[0];

            for (size_t j=0; j<width; ++j)
            {
                size_t k        = j*ni;
                idx[j]          = vindexes[k];
                b->v[0][j]      = vfrequences[k];
            }

            for (size_t i=0; i<nChannels; ++i)
            {
                // Output only active channel
                sa_channel_t *c = &vChannels[i];
                if (!c->bOn)
                    continue;

                sAnalyzer.get_spectrum(i, b->v[1], idx, width);

                dsp::mul_k2(b->v[1], c->fGain * fPreamp, width);

                dsp::fill(b->v[2], 0.0f, width);
                dsp::fill(b->v[3], height, width);
                dsp::axis_apply_log1(b->v[2], b->v[0], zx, dx, width);
                dsp::axis_apply_log1(b->v[3], b->v[1], zy, dy, width);

                // Draw mesh
                col.hue(float(i) / float(nChannels));
                cv->set_color(col);
                cv->draw_lines(b->v[2], b->v[3], width);
            }

            return true;
        }


        void spectrum_analyzer::dump(dspu::IStateDumper *v) const
        {
            v->write_object("sAnalyzer", &sAnalyzer);
            v->write_object("sCounter", &sCounter);

            v->write("nChannels", nChannels);
            v->write("nCorrelometers", nCorrelometers);
            v->begin_array("vChannels", vChannels, nChannels);
            {
                for (size_t i=0; i<nChannels; ++i)
                {
                    const sa_channel_t *c = &vChannels[i];

                    v->begin_object(c, sizeof(sa_channel_t));
                    {
                        v->write("bOn", c->bOn);
                        v->write("bFreeze", c->bFreeze);
                        v->write("bSolo", c->bSolo);
                        v->write("bSend", c->bSend);
                        v->write("bMSSwitch", c->bMSSwitch);
                        v->write("fGain", c->fGain);
                        v->write("vIn", c->vIn);
                        v->write("vOut", c->vOut);
                        v->write("vRet", c->vRet);
                        v->write("vBuffer", c->vBuffer);
                        v->writev("vSpc", c->vSpc, 4);
                        v->writev("vMax", c->vMax, 4);

                        v->write("pIn", c->pIn);
                        v->write("pOut", c->pOut);
                        v->write("pMSSwitch", c->pMSSwitch);
                        v->write("pOn", c->pOn);
                        v->write("pSolo", c->pSolo);
                        v->write("pFreeze", c->pFreeze);
                        v->write("pShift", c->pShift);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->begin_array("vCorrelometers", vCorrelometers, nCorrelometers);
            {
                for (size_t i=0; i<nCorrelometers; ++i)
                {
                    const sa_correlometer_t *cm = &vCorrelometers[i];

                    v->begin_object(cm, sizeof(sa_correlometer_t));
                    {
                        v->write_object("sCorr", &cm->sCorr);
                        v->write("fCorrelation", cm->fCorrelation);
                        v->write("pCorrelometer", cm->pCorrelometer);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->write("vAnalyze", vAnalyze);
            v->writev("vFrequences", vFrequences, 2);
            v->writev("vMaxValues", vMaxValues, 4);
            v->write("vMFrequences", vMFrequences);
            v->writev("vIndexes", vIndexes, 2);
            v->write("pData", pData);

            v->write("bBypass", bBypass);
            v->write("nChannel", nChannel);
            v->write("fSelector", fSelector);
            v->write("fMinFreq", fMinFreq);
            v->write("fMaxFreq", fMaxFreq);
            v->write("fReactivity", fReactivity);
            v->write("fTau", fTau);
            v->write("fPreamp", fPreamp);
            v->write("fZoom", fZoom);
            v->write("enMode", enMode);
            v->write("bLogScale", bLogScale);
            v->write("bLinFreq", bLinFreq);
            v->write("bMSSwitch", bMSSwitch);

            v->write("fWndState", fWndState);
            v->write("fEnvState", fEnvState);

            v->write("pBypass", pBypass);
            v->write("pMode", pMode);
            v->write("pTolerance", pTolerance);
            v->write("pWindow", pWindow);
            v->write("pEnvelope", pEnvelope);
            v->write("pPreamp", pPreamp);
            v->write("pZoom", pZoom);
            v->write("pReactivity", pReactivity);
            v->write("pChannel", pChannel);
            v->write("pSelector", pSelector);
            v->write("pFrequency", pFrequency);
            v->write("pLevel", pLevel);
            v->write("pLogScale", pLogScale);
            v->write("pLinFreq", pLinFreq);
            v->write("pFftData", pFftData);
            v->write("pMSSwitch", pMSSwitch);

            v->write("pFreeze", pFreeze);
            v->write("pMaxReset", pMaxReset);
            v->write("pSpp", pSpp);

            v->begin_array("vSpc", vSpc, 2);
            {
                for (size_t i=0; i<2; ++i)
                {
                    const sa_spectralizer_t *s = &vSpc[i];

                    v->begin_object(s, sizeof(sa_spectralizer_t));
                    {
                        v->write("nPortId", s->nPortId);
                        v->write("nChannelId", s->nChannelId);
                        v->write("pPortId", s->pPortId);
                        v->write("pFBuffer", s->pFBuffer);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->write_object("pIDisplay", pIDisplay);
        }

    } /* namespace plugins */
} /* namespace lsp */



