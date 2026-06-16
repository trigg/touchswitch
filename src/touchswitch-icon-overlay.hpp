#pragma once

#include "wayfire/signal-provider.hpp"
#include <string>

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/touchswitch-signal.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

namespace wf
{
namespace scene
{
class touchswitch_icon_overlay_node_t;
}
}


class touchswitch_show_icon_t
{
  protected:
    wf::option_wrapper_t<bool> show_view_icon_overlay_opt{
        "touchswitch/icon_overlay"};
    wf::option_wrapper_t<std::string> icon_position{"touchswitch/icon_position"};
    wf::output_t *output;

  public:
    touchswitch_show_icon_t();

    void init(wf::output_t *output);

    void fini();

  protected:
    wf::signal::connection_t<touchswitch_end_signal> touchswitch_end;
    wf::signal::connection_t<touchswitch_update_signal> touchswitch_update;
    wf::signal::connection_t<touchswitch_transformer_added_signal> add_icon_overlay;
    wf::signal::connection_t<touchswitch_transformer_removed_signal> rem_icon_overlay;

    friend class wf::scene::touchswitch_icon_overlay_node_t;

    bool show_view_icon_overlay;

    void update_icon_overlay_opt();
};
