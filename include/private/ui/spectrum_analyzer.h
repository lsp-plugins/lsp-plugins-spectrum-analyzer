/*
 * Copyright (C) 2023 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2023 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-plugins-spectrum-analyzer
 * Created on: 18 февр. 2023 г.
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

#ifndef PRIVATE_UI_SPECTRUM_ANALYZER_H_
#define PRIVATE_UI_SPECTRUM_ANALYZER_H_

#include <lsp-plug.in/plug-fw/ui.h>
#include <lsp-plug.in/lltl/darray.h>

namespace lsp
{
    namespace plugui
    {
        /**
         * UI for Spectrum Analyzer
         */
        class spectrum_analyzer_ui: public ui::Module, public ui::IPortListener
        {
            protected:
                enum key_flags_t
                {
                    K_LEFT_CTRL     = 1 << 0,
                    K_RIGHT_CTRL    = 1 << 1,
                };

                typedef struct channel_t
                {
                    spectrum_analyzer_ui   *pUI;
                    tk::GraphText          *wFrequency;         // The selector frequency widget
                } channel_t;

            protected:
                size_t                      nChannels;
                tk::Graph                  *wMainGraph;
                tk::Graph                  *wSpcGraphSingle;
                tk::Graph                  *wSpcGraphDual;

                tk::GraphText              *wMlValue;           // The Horizontal line value widget
                tk::GraphText              *wFrequency;         // The frequency widget
                tk::GraphText              *wFrequencySpc;      // The frequency widget for spectralizer mode
                tk::GraphText              *wFrequencySpcD1;    // The frequency widget for spectralizer dual mode
                tk::GraphText              *wFrequencySpcD2;    // The frequency widget for spectralizer dual mode
                ssize_t                     nXAxisIndex;
                ssize_t                     nXAxisIndexSpcS;
                ssize_t                     nXAxisIndexSpcD1;
                ssize_t                     nXAxisIndexSpcD2;
                size_t                      nGraphBtn;

                ui::IPort                  *pMode;              // Operating mode
                ui::IPort                  *pSelector;          // Selector frequency
                ui::IPort                  *pMlValue;           // Horizontal line value
                ui::IPort                  *pSelChannel;        // Selected channel
                ui::IPort                  *pFftFreq;           // Actual FFT frequency
                ui::IPort                  *pLevel;             // Actual level
                ui::IPort                  *pInspIndex;         // Inspection index
                ui::IPort                  *pInspOn;            // Inspection enable flag
                lltl::darray<channel_t>     vChannels;

            protected:
                template <class T>
                T              *find_widget(const char *prefix, size_t id);
                ui::IPort      *find_port(const char *prefix, size_t id);

            protected:
                static status_t slot_graph_key_down(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_graph_key_up(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_graph_mouse_down(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_graph_mouse_move(tk::Widget *sender, void *ptr, void *data);
                static status_t slot_graph_mouse_up(tk::Widget *sender, void *ptr, void *data);

            protected:
                void            update_selector_text();
                void            update_mlvalue_text();
                ssize_t         find_axis(tk::Graph *graph, const char *id);
                void            on_graph_mouse_down(tk::Widget *sender, const ws::event_t *ev);
                void            on_graph_mouse_move(tk::Widget *sender, const ws::event_t *ev);
                void            on_graph_mouse_up(tk::Widget *sender, const ws::event_t *ev);
                void            on_graph_key_down(tk::Widget *sender, const ws::event_t *ev);
                void            on_graph_key_up(tk::Widget *sender, const ws::event_t *ev);
                void            set_selector_text(tk::GraphText *fWidget, bool no_gain);
                bool            channels_selector_visible();
                void            enable_inspect(bool enable);
                size_t          get_keys(tk::Widget *sender);

            public:
                explicit spectrum_analyzer_ui(const meta::plugin_t *meta);
                virtual ~spectrum_analyzer_ui() override;

            public:
                virtual status_t    post_init() override;
                virtual status_t    pre_destroy() override;
                virtual void        notify(ui::IPort *port, size_t flags) override;
        };

    } /* namespace plugui */
} /* namespace lsp */


#endif /* PRIVATE_UI_SPECTRUM_ANALYZER_H_ */
