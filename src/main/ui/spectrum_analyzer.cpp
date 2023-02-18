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

#include <lsp-plug.in/common/status.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/runtime/LSPString.h>
#include <private/meta/spectrum_analyzer.h>
#include <private/ui/spectrum_analyzer.h>

namespace lsp
{
    namespace plugui
    {
        //---------------------------------------------------------------------
        // Plugin UI factory
        static const meta::plugin_t *plugin_uis[] =
        {
            &meta::spectrum_analyzer_x1,
            &meta::spectrum_analyzer_x2,
            &meta::spectrum_analyzer_x4,
            &meta::spectrum_analyzer_x8,
            &meta::spectrum_analyzer_x12,
            &meta::spectrum_analyzer_x16
        };

        static ui::Module *ui_factory(const meta::plugin_t *meta)
        {
            return new spectrum_analyzer_ui(meta);
        }

        static ui::Factory factory(ui_factory, plugin_uis, 6);

        //---------------------------------------------------------------------
        static const char *note_names[] =
        {
            "c", "c#", "d", "d#", "e", "f", "f#", "g", "g#", "a", "a#", "b"
        };

        //---------------------------------------------------------------------
        spectrum_analyzer_ui::spectrum_analyzer_ui(const meta::plugin_t *meta):
            ui::Module(meta)
        {
            if (!strcmp(meta->uid, meta::spectrum_analyzer_x16.uid))
                nChannels   = 16;
            else if (!strcmp(meta->uid, meta::spectrum_analyzer_x12.uid))
                nChannels   = 12;
            else if (!strcmp(meta->uid, meta::spectrum_analyzer_x8.uid))
                nChannels   = 8;
            else if (!strcmp(meta->uid, meta::spectrum_analyzer_x4.uid))
                nChannels   = 4;
            else if (!strcmp(meta->uid, meta::spectrum_analyzer_x2.uid))
                nChannels   = 2;
            else
                nChannels   = 1;

            pSelector       = NULL;
            pSelChannel     = NULL;
            pFftFreq        = NULL;
        }

        spectrum_analyzer_ui::~spectrum_analyzer_ui()
        {
        }

        template <class T>
        T *spectrum_analyzer_ui::find_widget(const char *prefix, size_t id)
        {
            LSPString name;
            name.fmt_ascii("%s_%d", prefix, int(id));
            return pWrapper->controller()->widgets()->get<T>(&name);
        }

        ui::IPort *spectrum_analyzer_ui::find_port(const char *prefix, size_t id)
        {
            LSPString name;
            name.fmt_ascii("%s_%d", prefix, int(id));
            return pWrapper->port(&name);
        }

        status_t spectrum_analyzer_ui::post_init()
        {
            status_t res = ui::Module::post_init();
            if (res != STATUS_OK)
                return res;

            // Bind ports
            pSelector   = pWrapper->port("sel");
            if (pSelector != NULL)
                pSelector->bind(this);

            pSelChannel = pWrapper->port("chn");
            if (pSelChannel != NULL)
                pSelChannel->bind(this);

            pFftFreq = pWrapper->port("freq");
            if (pFftFreq != NULL)
                pFftFreq->bind(this);

            // Initialize channels
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *ch   = vChannels.add();
                if (ch == NULL)
                    continue;

                ch->pUI         = this;
                ch->wFrequency  = find_widget<tk::GraphText>("selector_freq", i);
            }

            // Update selector text after init
            update_selector_text();

            return res;
        }

        void spectrum_analyzer_ui::notify(ui::IPort *port)
        {
            if ((pSelector == port) || (pSelChannel == port) || (pFftFreq == port))
                update_selector_text();
        }

        void spectrum_analyzer_ui::update_selector_text()
        {
            if ((pSelChannel == NULL) || (pSelector == NULL) || (pFftFreq == NULL))
                return;

            // Get the channel to process
            ssize_t ch_idx = pSelChannel->value();
            channel_t *ch = vChannels.get(ch_idx);
            if (ch == NULL)
                return;

            float freq = pSelector->value();
            float fft_freq = pFftFreq->value();

            // Updatee the note name displayed in the text
            // Fill the parameters
            expr::Parameters params;
            tk::prop::String snote;
            LSPString text;
            snote.bind(ch->wFrequency->style(), pDisplay->dictionary());

            // Frequency
            text.fmt_ascii("%.2f", freq);
            params.set_string("frequency", &text);

            // FFT Frequency
            text.fmt_ascii("%.2f", fft_freq);
            params.set_string("fft_frequency", &text);

            // Note
            float note_full = dspu::frequency_to_note(freq);
            if (note_full != dspu::NOTE_OUT_OF_RANGE)
            {
                note_full += 0.5f;
                ssize_t note_number = ssize_t(note_full);

                // Note name
                ssize_t note        = note_number % 12;
                text.fmt_ascii("lists.notes.names.%s", note_names[note]);
                snote.set(&text);
                snote.format(&text);
                params.set_string("note", &text);

                // Octave number
                ssize_t octave      = (note_number / 12) - 1;
                params.set_int("octave", octave);

                // Cents
                ssize_t note_cents  = (note_full - float(note_number)) * 100 - 50;
                if (note_cents < 0)
                    text.fmt_ascii(" - %02d", -note_cents);
                else
                    text.fmt_ascii(" + %02d", note_cents);
                params.set_string("cents", &text);

                ch->wFrequency->text()->set("lists.spectrum.display.full", &params);
            }
            else
                ch->wFrequency->text()->set("lists.spectrum.display.unknown", &params);
        }

    } /* namespace plugui */
} /* namespace lsp */


