/**
 * Original code by: Scott Moreau, Daniel Kondor
 */
#include <map>
#include <memory>
#include <wayfire/workarea.hpp>
#include <wayfire/seat.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/workspace-set.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/vswitch.hpp>
#include <wayfire/touch/touch.hpp>
#include <wayfire/plugins/touchswitch-signal.hpp>
#include <wayfire/plugins/wobbly/wobbly-signal.hpp>
#include <wayfire/window-manager.hpp>

#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/input-grab.hpp>

#include <linux/input-event-codes.h>

#include "touchswitch-instance.hpp"
#include "wayfire/plugins/ipc/ipc-activator.hpp"
#include "touchswitch.hpp"

#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/plugin.hpp"

#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view.hpp"

using namespace wf::animation;
/**
 * Touchswitch is intended to be used by mouse or touch
 *
 * For debugging purposes it has the following hardcoded keys:
 * KEY_ENTER:
 * - Ends switcher, switching to the focused view
 * KEY_LEFT:
 * KEY_RIGHT:
 * - When switcher is active, change focus of the views
 *
 * Touch & Mouse bindings. Assume all actions are performed with a Left Click or One Finger Touch:
 * - Drag Left & Right anywhere on switcher to move between views
 * - Drag Up & Down starting on a view to perform a customisable action on it
 * - Tap a view to switch to it
 * - Tap background to perform customisable action
 */

void wayfire_touchswitch_global::init()
{
    this->init_output_tracking();

    activate.set_handler([this] (wf::output_t *output, wayfire_view)
    {
        return this->handle_activate(output);
    });

    on_view_set_output = [this] (wf::view_set_output_signal *ev)
    {
        this->handle_view_output_changed(ev);
    };
}

void wayfire_touchswitch_global::fini()
{
    this->fini_output_tracking();
}

void wayfire_touchswitch_global::handle_new_output(wf::output_t *output)
{
    per_output_tracker_mixin_t::handle_new_output(output);
    output->connect(&on_view_set_output);
}

void wayfire_touchswitch_global::handle_output_removed(wf::output_t *output)
{
    per_output_tracker_mixin_t::handle_output_removed(output);
    output->disconnect(&on_view_set_output);
}

bool wayfire_touchswitch_global::handle_activate(wf::output_t *output)
{
    if (this->output_instance[output]->handle_toggle())
    {
        output->render->schedule_redraw();
        return true;
    }

    return false;
}

void wayfire_touchswitch_global::handle_view_output_changed(wf::view_set_output_signal *ev)
{
    auto toplevel = wf::toplevel_cast(ev->view);
    if (!toplevel)
    {
        return;
    }

    if (ev->output && this->output_instance.count(ev->output))
    {
        this->output_instance[ev->output]->handle_view_unmapped(toplevel);
    }

    auto new_output = ev->view->get_output();
    if (new_output && this->output_instance.count(new_output))
    {
        auto& instance = this->output_instance[new_output];
        if (instance && instance->active)
        {
            instance->handle_new_view(toplevel);
        }
    }
}

DECLARE_WAYFIRE_PLUGIN(wayfire_touchswitch_global);
