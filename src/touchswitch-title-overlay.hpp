#pragma once

#include "wayfire/signal-definitions.hpp"
#include "wayfire/signal-provider.hpp"
#include <string>

#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/touchswitch-signal.hpp>

namespace wf
{
namespace scene
{
class touchswitch_overlay_node_t;
}
}


class touchswitch_show_title_t
{
  protected:
    /* Overlays for showing the title of each view */
    wf::option_wrapper_t<wf::color_t> bg_color{"touchswitch/bg_color"};
    wf::option_wrapper_t<wf::color_t> text_color{"touchswitch/text_color"};
    wf::option_wrapper_t<bool> show_view_title_overlay_opt{
        "touchswitch/title_overlay"};
    wf::option_wrapper_t<int> title_font_size{"touchswitch/title_font_size"};
    wf::option_wrapper_t<std::string> title_position{"touchswitch/title_position"};
    wf::output_t *output;

  public:
    touchswitch_show_title_t();

    void init(wf::output_t *output);

    void fini();

  protected:
    /* signals */
    wf::signal::connection_t<touchswitch_end_signal> touchswitch_end;
    wf::signal::connection_t<touchswitch_update_signal> touchswitch_update;
    wf::signal::connection_t<touchswitch_transformer_added_signal> add_title_overlay;
    wf::signal::connection_t<touchswitch_transformer_removed_signal> rem_title_overlay;

    enum class title_overlay_t
    {
        NEVER,
        ALL,
    };

    friend class wf::scene::touchswitch_overlay_node_t;

    title_overlay_t show_view_title_overlay;
    /* only used if title overlay is set to follow the mouse */
    wayfire_view last_title_overlay = nullptr;

    void update_title_overlay_opt();
};
