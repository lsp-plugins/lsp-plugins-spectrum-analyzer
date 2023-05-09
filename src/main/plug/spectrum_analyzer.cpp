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

#include <private/meta/spectrum_analyzer.h>
#include <private/plugins/spectrum_analyzer.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/meta/ports.h>
#include <lsp-plug.in/plug-fw/meta/func.h>
#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/stdlib/math.h>
#include <lsp-plug.in/dsp/dsp.h>

#include <lsp-plug.in/shared/id_colors.h>

#define TRACE_PORT(p)       lsp_trace("  port id=%s", (p)->metadata()->id);
#define BUFFER_SIZE         0x1000u

namespace lsp
{
    namespace plugins
    {
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
            nChannels       = 0;
            vChannels       = NULL;
            vAnalyze        = NULL;
            pData           = NULL;
            vFrequences     = NULL;
            vMFrequences    = NULL;
            vIndexes        = NULL;

            bBypass         = false;
            nChannel        = 0;
            fSelector       = 0;
            fMinFreq        = 0;
            fMaxFreq        = 0;
            fReactivity     = 0.0f;
            fTau            = 0.0f;
            fPreamp         = 0.0f;
            fZoom           = 0.0f;
            enMode          = SA_ANALYZER;
            bLogScale       = false;
            bMSSwitch       = false;

            pBypass         = NULL;
            pMode           = NULL;
            pTolerance      = NULL;
            pWindow         = NULL;
            pEnvelope       = NULL;
            pPreamp         = NULL;
            pZoom           = NULL;
            pReactivity     = NULL;
            pChannel        = NULL;
            pSelector       = NULL;
            pFrequency      = NULL;
            pLevel          = NULL;
            pLogScale       = NULL;
            pFftData        = NULL;
            pMSSwitch       = NULL;

            pFreeze         = NULL;
            pSpp            = NULL;

            for (size_t i=0; i<2; ++i)
            {
                vSpc[i].nPortId     = -1;

                vSpc[i].pPortId     = NULL;
                vSpc[i].pFBuffer    = NULL;
            }

            pIDisplay       = NULL;
        }

        spectrum_analyzer::~spectrum_analyzer()
        {
            vChannels       = NULL;
            pIDisplay       = NULL;
        }

        bool spectrum_analyzer::create_channels(size_t channels)
        {
            lsp_trace("this=%p, channels = %d", this, int(channels));

            // Calculate header size
            size_t hdr_size         = align_size(sizeof(sa_channel_t) * channels, 64);
            size_t freq_buf_size    = align_size(sizeof(float) * meta::spectrum_analyzer::MESH_POINTS, 64);
            size_t mfreq_buf_size   = align_size(sizeof(float) * meta::spectrum_analyzer::MESH_POINTS, 64);
            size_t ind_buf_size     = align_size(sizeof(uint32_t) * meta::spectrum_analyzer::MESH_POINTS, 64);
            size_t analyze_size     = align_size(sizeof(float *) * channels, 16);
            size_t buffers          = BUFFER_SIZE * sizeof(float) * channels;
            size_t alloc            = hdr_size + freq_buf_size + mfreq_buf_size + ind_buf_size + analyze_size + buffers;

            lsp_trace("header_size      = %d", int(hdr_size));
            lsp_trace("freq_buf_size    = %d", int(freq_buf_size));
            lsp_trace("mfreq_buf_size   = %d", int(mfreq_buf_size));
            lsp_trace("ind_buf_size     = %d", int(ind_buf_size));
            lsp_trace("buffers          = %d", int(buffers));
            lsp_trace("alloc            = %d", int(alloc));

            // Allocate data
            uint8_t *ptr        = alloc_aligned<uint8_t>(pData, alloc, 64);
            if (ptr == NULL)
                return false;
            lsp_guard_assert( uint8_t *guard = ptr );

            // Initialize core
            nChannels       = channels;
            nChannel        = 0;
            fSelector       = meta::spectrum_analyzer::SELECTOR_DFL;
            fMinFreq        = meta::spectrum_analyzer::FREQ_MIN;
            fMaxFreq        = meta::spectrum_analyzer::FREQ_MAX;
            fReactivity     = meta::spectrum_analyzer::REACT_TIME_DFL;
            fTau            = 1.0f;
            fPreamp         = meta::spectrum_analyzer::PREAMP_DFL;

            // Initialize pointers and cleanup buffers
            vChannels       = reinterpret_cast<sa_channel_t *>(ptr);
            ptr            += hdr_size;
            lsp_trace("vChannels = %p", vChannels);

            vFrequences   = reinterpret_cast<float *>(ptr);
            ptr          += freq_buf_size;
            dsp::fill_zero(vFrequences, meta::spectrum_analyzer::MESH_POINTS);
            lsp_trace("vFrequences = %p", vFrequences);

            vMFrequences  = reinterpret_cast<float *>(ptr);
            ptr          += mfreq_buf_size;
            dsp::fill_zero(vMFrequences, meta::spectrum_analyzer::MESH_POINTS);
            lsp_trace("vMFrequences = %p", vMFrequences);

            vIndexes      = reinterpret_cast<uint32_t *>(ptr);
            ptr          += ind_buf_size;
            memset(vIndexes, 0, ind_buf_size);
            lsp_trace("vIndexes = %p", vIndexes);

            vAnalyze    = reinterpret_cast<float **>(ptr);
            ptr        += analyze_size;

            // Initialize channels
            for (size_t i=0; i<channels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];

                // Initialize fields
                c->bOn              = false;
                c->bFreeze          = false;
                c->bSolo            = false;
                c->bSend            = false;
                c->bMSSwitch        = false;
                c->fGain            = 1.0f;
                c->fHue             = 0.0f;
                c->vIn              = NULL;
                c->vOut             = NULL;
                c->vBuffer          = reinterpret_cast<float *>(ptr);
                ptr                += BUFFER_SIZE * sizeof(float);

                // Port references
                c->pIn              = NULL;
                c->pOut             = NULL;
                c->pMSSwitch        = NULL;
                c->pOn              = NULL;
                c->pFreeze          = NULL;
                c->pHue             = NULL;
                c->pShift           = NULL;

                // Clear the buffer
                dsp::fill_zero(c->vBuffer, BUFFER_SIZE);
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
                c->pIn              = ports[port_id++];
                c->pOut             = ports[port_id++];
                c->pOn              = ports[port_id++];
                c->pSolo            = ports[port_id++];
                c->pFreeze          = ports[port_id++];
                c->pHue             = ports[port_id++];
                c->pShift           = ports[port_id++];

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
                    sa_channel_t *l     = &vChannels[i];
                    sa_channel_t *r     = &vChannels[i+1];

                    l->pMSSwitch        = ports[port_id++];
                    r->pMSSwitch        = l->pMSSwitch;
                }
            }

            // Initialize basic ports
            pBypass         = ports[port_id++];
            pMode           = ports[port_id++];
            port_id++; // Skip spectralizer mode
            pLogScale       = ports[port_id++];
            pFreeze         = ports[port_id++];
            port_id++; // Skip horizontal line switch button
            pTolerance      = ports[port_id++];
            pWindow         = ports[port_id++];
            pEnvelope       = ports[port_id++];
            pPreamp         = ports[port_id++];
            pZoom           = ports[port_id++];
            pReactivity     = ports[port_id++];
            if (nChannels > 1)
                pChannel        = ports[port_id++];
            pSelector       = ports[port_id++];
            port_id++; // Skip horizontal line value
            pFrequency      = ports[port_id++];
            pLevel          = ports[port_id++];
            pFftData        = ports[port_id++];

            // Bind spectralizer ports
            if (nChannels >= 2)
            {
                pMSSwitch           = ports[port_id++];
                vSpc[0].pPortId     = ports[port_id++];
            }
            vSpc[0].pFBuffer    = ports[port_id++];
            vSpc[0].nChannelId  = -1;

            if (nChannels >= 2)
            {
                if (nChannels > 2)
                    vSpc[1].pPortId     = ports[port_id++];
                vSpc[1].pFBuffer    = ports[port_id++];
                vSpc[1].nChannelId  = -1;
            }

            // Initialize values
            fMinFreq        = pFrequency->metadata()->min;
            fMaxFreq        = pFrequency->metadata()->max;

            lsp_trace("this=%p, basic ports successful bound", this);
        }

        void spectrum_analyzer::destroy()
        {
            sAnalyzer.destroy();

            if (pData != NULL)
            {
                free_aligned(pData);
                pData           = NULL;
            }
            vFrequences     = NULL;
            vIndexes        = NULL;

            if (pIDisplay != NULL)
            {
                pIDisplay->destroy();
                pIDisplay       = NULL;
            }
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
            size_t has_solo         = 0;
            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];
                if (c->pSolo->value() >= 0.5f)
                    has_solo++;
            }

            // Process channel parameters
            bool freeze_all     = pFreeze->value() >= 0.5f;

            for (size_t i=0; i<nChannels; ++i)
            {
                sa_channel_t *c     = &vChannels[i];

                c->bOn              = c->pOn->value() >= 0.5f;
                c->bFreeze          = (freeze_all) || (c->pFreeze->value() >= 0.5f);
                c->bSolo            = c->pSolo->value() >= 0.5f;
                c->bSend            = (c->bOn) && ((has_solo == 0) || ((has_solo > 0) && (c->bSolo)));
                c->bMSSwitch        = (c->pMSSwitch != NULL) ? c->pMSSwitch->value() >= 0.5f : false;
                c->fGain            = c->pShift->value();
                c->fHue             = c->pHue->value();
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
                c->fHue             = c->pHue->value();
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
                c->fHue             = c->pHue->value();
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
            bBypass                 = pBypass->value();
            nChannel                = (pChannel != NULL) ? pChannel->value() : 0;
            fSelector               = lsp_limit((pSelector->value() * 2.0f) / fSampleRate, 0.0f, 1.0f);
            fPreamp                 = pPreamp->value();
            fZoom                   = pZoom->value();
            bLogScale               = (pLogScale != NULL) && (pLogScale->value() >= 0.5f);
            size_t rank             = pTolerance->value() + meta::spectrum_analyzer::RANK_MIN;

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
            enMode      = mode;

            // Update analysis parameters
            bool sync_freqs         = rank != sAnalyzer.get_rank();
            if (sync_freqs)
                sAnalyzer.set_rank(rank);

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
                    vFrequences, vIndexes,
                    fMinFreq, fMaxFreq,
                    meta::spectrum_analyzer::MESH_POINTS
                );
            }
        }

        void spectrum_analyzer::update_sample_rate(long sr)
        {
            lsp_trace("this=%p, sample_rate = %d", this, int(sr));
            sAnalyzer.set_sample_rate(sr);
            if (sAnalyzer.needs_reconfiguration())
                sAnalyzer.reconfigure();

            sAnalyzer.get_frequencies(
                vFrequences, vIndexes,
                fMinFreq, fMaxFreq,
                meta::spectrum_analyzer::MESH_POINTS
            );
            sCounter.set_sample_rate(sr, true);
        }

        void spectrum_analyzer::get_spectrum(float *dst, size_t channel, size_t flags)
        {
            float *v        = dst;
            size_t off      = 0;

            // Fetch original data
            if (flags & F_SMOOTH_LOG)
            {
                sAnalyzer.get_spectrum(channel, vMFrequences, vIndexes, meta::spectrum_analyzer::MESH_POINTS);
                size_t pi = 0, ni = meta::spectrum_analyzer::MMESH_STEP;

                for (; ni < meta::spectrum_analyzer::MESH_POINTS; ni += meta::spectrum_analyzer::MMESH_STEP)
                {
                    if (vIndexes[ni] == vIndexes[pi])
                        continue;

                    if (flags & F_SMOOTH_LOG)
                        dsp::smooth_cubic_log(&v[off], vMFrequences[pi], vMFrequences[ni], ni-pi);
                    else
                        dsp::smooth_cubic_linear(&v[off], vMFrequences[pi], vMFrequences[ni], ni-pi);

                    off        += ni-pi;
                    pi          = ni;
                }

                if (pi < meta::spectrum_analyzer::MESH_POINTS)
                {
                    if (flags & F_SMOOTH_LOG)
                        dsp::smooth_cubic_log(&v[off], vMFrequences[pi], vMFrequences[ni-1], ni-pi);
                    else
                        dsp::smooth_cubic_linear(&v[off], vMFrequences[pi], vMFrequences[ni-1], ni-pi);
                }
            }
            else
                sAnalyzer.get_spectrum(channel, v, vIndexes, meta::spectrum_analyzer::MESH_POINTS);

            // Apply gain
            float gain = (flags & F_BOOST) ?
                    vChannels[channel].fGain * meta::spectrum_analyzer::SPECTRALIZER_BOOST:
                    vChannels[channel].fGain ;
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

        void spectrum_analyzer::prepare_buffers(size_t count)
        {
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
                            dsp::lr_to_ms(l->vBuffer, r->vBuffer, l->vIn, r->vIn, count);
                            vAnalyze[i]         = l->vBuffer;
                            vAnalyze[i+1]       = r->vBuffer;
                        }
                        else
                        {
                            vAnalyze[i]     = l->vIn;
                            vAnalyze[i+1]   = r->vIn;
                        }
                    }
                }
                else
                {
                    // Copy pointers to all buffers as is
                    for (size_t i=0; i<nChannels; ++i)
                        vAnalyze[i]         = vChannels[i].vIn;

                    // Special case: do MS convert for two (or one) channels
                    ssize_t l_id        = vSpc[0].nPortId;
                    ssize_t r_id        = vSpc[1].nPortId;
                    if (r_id < 0)
                        r_id                = l_id;

                    sa_channel_t *l     = &vChannels[l_id];
                    sa_channel_t *r     = &vChannels[r_id];

                    if (l_id != r_id)
                    {
                        dsp::lr_to_ms(l->vBuffer, r->vBuffer, l->vIn, r->vIn, count);
                        vAnalyze[l_id]      = l->vBuffer;
                        vAnalyze[r_id]      = r->vBuffer;
                    }
                    else
                    {
                        dsp::lr_to_mid(l->vBuffer, l->vIn, r->vIn, count);
                        vAnalyze[l_id]      = l->vBuffer;
                    }
                }
            }
            else
                vAnalyze[0]     = vChannels[0].vIn;
        }

        void spectrum_analyzer::process(size_t samples)
        {
            // Always query for drawing
            pWrapper->query_display_draw();

            // Now process the channels
            size_t fft_size     = 1 << sAnalyzer.get_rank();

            for (size_t i=0; i<nChannels; ++i)
            {
                // Get channel pointers
                sa_channel_t *c     = &vChannels[i];
                c->vIn              = c->pIn->buffer<float>();
                c->vOut             = c->pOut->buffer<float>();
            }

            // Check that mesh request is pending
            plug::mesh_t *mesh      = pFftData->buffer<plug::mesh_t>();
            bool mesh_request   = (mesh != NULL) && (mesh->isEmpty());
            if ((enMode == SA_SPECTRALIZER) || (enMode == SA_SPECTRALIZER_STEREO))
                mesh_request        = false;

            if (mesh_request)
                dsp::copy(mesh->pvData[0], vFrequences, meta::spectrum_analyzer::MESH_POINTS);

            for (size_t n=samples; n > 0;)
            {
                // Get number of samples to process
                size_t count = lsp_min(sCounter.pending(), n, BUFFER_SIZE);
                bool fired = sCounter.submit(count);

                // Always bypass signal
                for (size_t i=0; i<nChannels; ++i)
                    dsp::copy(vChannels[i].vOut, vChannels[i].vIn, count);

                if (bBypass)
                {
                    // Bypass signal
                    pFrequency->set_value(0);
                    pLevel->set_value(0);

                    // For mesh request fill all data with zeros
                    if (mesh_request)
                    {
                        for (size_t i=0; i<nChannels; ++i)
                            dsp::fill_zero(mesh->pvData[i+1], meta::spectrum_analyzer::MESH_POINTS);
                    }
                }
                else
                {
                    // Prepare analysis data
                    prepare_buffers(count);

                    // Perform analysis
                    sAnalyzer.process(vAnalyze, count);

                    // Report values
                    sa_channel_t *c     = &vChannels[nChannel];
                    {
                        size_t idx  = fSelector * ((fft_size - 1) >> 1);
                        pFrequency->set_value(float(idx * fSampleRate) / float(fft_size));
                        float lvl = sAnalyzer.get_level(nChannel, idx);
                        pLevel->set_value(lvl * c->fGain * fPreamp );
                    }

                    // Mesh is requested?
                    if (mesh_request)
                    {
                        for (size_t i=0; i<nChannels; ++i)
                        {
                            c     = &vChannels[i];
                            if (c->bSend)
                            {
                                // Copy frequency points
                                size_t flags = 0;
                                if ((enMode == SA_MASTERING) || (enMode == SA_MASTERING_STEREO))
                                    flags |= F_SMOOTH_LOG | F_MASTERING;

                                get_spectrum(mesh->pvData[i+1], i, flags);
                            }
                            else
                                dsp::fill_zero(mesh->pvData[i+1], meta::spectrum_analyzer::MESH_POINTS);
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
                }

                // Synchronize buffer state
                if ((enMode == SA_SPECTRALIZER) || (enMode == SA_SPECTRALIZER_STEREO))
                {
                    // Update frame buffers if counter has fired
                    if ((fired) && (!bBypass))
                    {
                        size_t flags = 0;
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

            // Commit mesh data
            if (mesh_request)
                mesh->data(nChannels + 1, meta::spectrum_analyzer::MESH_POINTS);
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

            for (size_t j=0; j<width; ++j)
            {
                size_t k        = j*ni;
                idx[j]          = vIndexes[k];
                b->v[0][j]      = vFrequences[k];
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
                col.hue(c->fHue);
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
                        v->write("fHue", c->fHue);
                        v->write("vIn", c->vIn);
                        v->write("vOut", c->vOut);

                        v->write("pIn", c->pIn);
                        v->write("pOut", c->pOut);
                        v->write("pMSSwitch", c->pMSSwitch);
                        v->write("pOn", c->pOn);
                        v->write("pSolo", c->pSolo);
                        v->write("pFreeze", c->pFreeze);
                        v->write("pHue", c->pHue);
                        v->write("pShift", c->pShift);
                    }
                    v->end_object();
                }
            }
            v->end_array();

            v->write("vAnalyze", vAnalyze);
            v->write("vFrequences", vFrequences);
            v->write("vMFrequences", vMFrequences);
            v->write("vIndexes", vIndexes);
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
            v->write("pFftData", pFftData);
            v->write("pMSSwitch", pMSSwitch);

            v->write("pFreeze", pFreeze);
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



