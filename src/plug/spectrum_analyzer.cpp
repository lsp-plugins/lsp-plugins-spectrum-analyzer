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
#include <lsp-plug.in/common/alloc.h>
#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/stdlib/math.h>
#include <lsp-plug.in/dsp/dsp.h>

#include <lsp-plug.in/shared/id_colors.h>

#define TRACE_PORT(p)       lsp_trace("  port id=%s", (p)->metadata()->id);

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

        static plug::Module *spectrum_analyzer_factory(const meta::plugin_t *meta)
        {
            return new spectrum_analyzer(meta);
        }

        static plug::Factory factory(spectrum_analyzer_factory, plugins, 6);


    } // namespace plugins
} // namespace lsp



