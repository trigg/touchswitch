#pragma once


#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/signal-definitions.hpp>
#include "wayfire/toplevel-view.hpp"
#include "wayfire/plugins/common/util.hpp"
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/view.hpp>
#include <wayfire/workspace-set.hpp>

class wayfire_touchswitch;

class wayfire_touchswitch_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_touchswitch>
{
  public:
    void init() override;
    void fini() override;

    void handle_new_output(wf::output_t *output) override;
    void handle_output_removed(wf::output_t *output) override;

  private:
    wf::ipc_activator_t activate{"touchswitch/activate"};

    wf::signal::connection_t<wf::view_set_output_signal> on_view_set_output;

    bool handle_activate(wf::output_t *output);
    void handle_view_output_changed(wf::view_set_output_signal *ev);
};

inline wayfire_toplevel_view touchswitch_find_view_at(wf::pointf_t at, wf::output_t *output)
{
    auto offset = wf::origin(output->get_layout_geometry());
    at.x -= offset.x;
    at.y -= offset.y;
    return wf::find_output_view_at(output, at);
}
