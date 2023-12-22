/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
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

#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/common/status.h>
#include <lsp-plug.in/dsp-units/units.h>
#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/runtime/LSPString.h>
#include <lsp-plug.in/stdlib/locale.h>
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

            wMainGraph      = NULL;
            wMlValue        = NULL;
            nXAxisIndex     = -1;
            nMainGraphBtn   = 0;

            pSelector       = NULL;
            pMlValue        = NULL;
            pSelChannel     = NULL;
            pFftFreq        = NULL;
            pLevel          = NULL;
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

            pMlValue   = pWrapper->port("mlval");
            if (pMlValue != NULL)
                pMlValue->bind(this);

            pSelChannel = pWrapper->port("chn");
            if (pSelChannel != NULL)
                pSelChannel->bind(this);

            pFftFreq = pWrapper->port("freq");
            if (pFftFreq != NULL)
                pFftFreq->bind(this);

            pLevel = pWrapper->port("lvl");
            if (pLevel != NULL)
                pLevel->bind(this);

            // Initialize channels
            for (size_t i=0; i<nChannels; ++i)
            {
                channel_t *ch   = vChannels.add();
                if (ch == NULL)
                    continue;

                ch->pUI         = this;
                ch->wFrequency  = find_widget<tk::GraphText>("selector_freq", i);
            }

            // Bind graph click handler
            wMainGraph          = pWrapper->controller()->widgets()->get<tk::Graph>("main_graph");
            if (wMainGraph != NULL)
            {
                wMainGraph->slots()->bind(tk::SLOT_MOUSE_DOWN, slot_main_graph_mouse_down, this);
                wMainGraph->slots()->bind(tk::SLOT_MOUSE_MOVE, slot_main_graph_mouse_move, this);
                wMainGraph->slots()->bind(tk::SLOT_MOUSE_UP, slot_main_graph_mouse_up, this);
                nXAxisIndex         = find_axis(wMainGraph, "main_graph_ox");
            }

            // Bind horizontal line
            wMlValue            = pWrapper->controller()->widgets()->get<tk::GraphText>("mline_level");

            // Update selector and horizontal line text after init
            update_selector_text();
            update_mlvalue_text();

            return res;
        }

        void spectrum_analyzer_ui::notify(ui::IPort *port, size_t flags)
        {
            if ((pSelector == port) ||
                (pSelChannel == port) ||
                (pFftFreq == port) ||
                (pLevel == port))
                update_selector_text();

            if (pMlValue == port)
                update_mlvalue_text();
        }

        void spectrum_analyzer_ui::update_mlvalue_text()
        {
            if (pMlValue == NULL || wMlValue == NULL)
                return;

            float mlvalue = pMlValue->value();
            LSPString text;

            SET_LOCALE_SCOPED(LC_NUMERIC, "C");
            text.fmt_ascii("%.1f", dspu::gain_to_db(mlvalue));

            wMlValue->text()->params()->set_string("value", &text);
            wMlValue->text()->set_key("labels.values.x_db");
        }

        void spectrum_analyzer_ui::update_selector_text()
        {
            if ((pSelector == NULL) ||
                (pFftFreq == NULL) ||
                (pLevel == NULL))
                return;

            // Get the channel to process
            ssize_t ch_idx = (pSelChannel != NULL) ? pSelChannel->value() : 0;
            channel_t *ch = vChannels.get(ch_idx);
            if (ch == NULL)
                return;
            if (ch->wFrequency == NULL)
                return;

            float freq = pSelector->value();
            float fft_freq = pFftFreq->value();
            float level = pLevel->value();

            // Update the note name displayed in the text
            // Fill the parameters
            expr::Parameters params;
            tk::prop::String snote;
            LSPString text;
            snote.bind(ch->wFrequency->style(), pDisplay->dictionary());
            SET_LOCALE_SCOPED(LC_NUMERIC, "C");

            // Frequency
            text.fmt_ascii("%.2f", freq);
            params.set_string("frequency", &text);

            // FFT Frequency
            text.fmt_ascii("%.2f", fft_freq);
            params.set_string("fft_frequency", &text);

            // Gain Level
            params.set_float("level", level);
            params.set_float("level_db", dspu::gain_to_db(level));

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

        ssize_t spectrum_analyzer_ui::find_axis(tk::Graph *graph, const char *id)
        {
            if (graph == NULL)
                return -1;

            ctl::Window *wnd    = pWrapper->controller();
            tk::GraphAxis *axis = tk::widget_cast<tk::GraphAxis>(wnd->widgets()->find(id));
            if (axis == NULL)
                return -1;

            for (size_t i=0; ; ++i)
            {
                tk::GraphAxis *ax = graph->axis(i);
                if (ax == NULL)
                    break;
                else if (ax == axis)
                    return i;
            }

            return -1;
        }

        status_t spectrum_analyzer_ui::slot_main_graph_mouse_down(tk::Widget *sender, void *ptr, void *data)
        {
            spectrum_analyzer_ui *_this = static_cast<spectrum_analyzer_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            ws::event_t *ev = static_cast<ws::event_t *>(data);
            _this->on_main_graph_mouse_down(sender, ev);

            return STATUS_OK;
        }

        status_t spectrum_analyzer_ui::slot_main_graph_mouse_move(tk::Widget *sender, void *ptr, void *data)
        {
            spectrum_analyzer_ui *_this = static_cast<spectrum_analyzer_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            ws::event_t *ev = static_cast<ws::event_t *>(data);
            _this->on_main_graph_mouse_move(sender, ev);

            return STATUS_OK;
        }

        status_t spectrum_analyzer_ui::slot_main_graph_mouse_up(tk::Widget *sender, void *ptr, void *data)
        {
            spectrum_analyzer_ui *_this = static_cast<spectrum_analyzer_ui *>(ptr);
            if (_this == NULL)
                return STATUS_BAD_STATE;

            ws::event_t *ev = static_cast<ws::event_t *>(data);
            _this->on_main_graph_mouse_up(sender, ev);

            return STATUS_OK;
        }

        void spectrum_analyzer_ui::on_main_graph_mouse_down(tk::Widget *sender, const ws::event_t *ev)
        {
            nMainGraphBtn |= (size_t(1) << ev->nCode);
            on_main_graph_mouse_move(sender, ev);
        }

        void spectrum_analyzer_ui::on_main_graph_mouse_up(tk::Widget *sender, const ws::event_t *ev)
        {
            nMainGraphBtn &= ~(size_t(1) << ev->nCode);
        }

        void spectrum_analyzer_ui::on_main_graph_mouse_move(tk::Widget *sender, const ws::event_t *ev)
        {
            if ((wMainGraph == NULL) || (nXAxisIndex < 0))
                return;
            if (nMainGraphBtn != (size_t(1) << ws::MCB_LEFT))
                return;

            // Check that channel is enabled
            LSPString port_id;
            ssize_t channel = (pSelChannel != NULL) ? pSelChannel->value() : 0;
            port_id.fmt_ascii("on_%d", int(channel));
            ui::IPort *channel_on = pWrapper->port(&port_id);
            bool channel_enabled = (channel_on != NULL) ? channel_on->value() >= 0.5f : true;
            if (!channel_enabled)
                return;

            // Translate coordinates
            float freq = 0.0f;
            if (wMainGraph->xy_to_axis(nXAxisIndex, &freq, ev->nLeft, ev->nTop) != STATUS_OK)
                return;

            lsp_trace("Graph apply: x=%d, y=%d, freq=%.2f", ev->nLeft, ev->nTop, freq);

            // Obtain which port set (left/right/mid/side) should be updated
            if (pSelector != NULL)
            {
                pSelector->set_value(freq);
                pSelector->notify_all(ui::PORT_USER_EDIT);
            }
        }

    } /* namespace plugui */
} /* namespace lsp */


