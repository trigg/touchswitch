#pragma once

#include "touchswitch-edge-overlay.hpp"
#include "touchswitch-icon-overlay.hpp"
#include "touchswitch-title-overlay.hpp"
#include "touchswitch-notice-overlay.hpp"
#include "wayfire/plugins/touchswitch-signal.hpp"
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <linux/input-event-codes.h>

#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/core.hpp>
#include <wayfire/output.hpp>
#include <wayfire/plugins/common/input-grab.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/util.hpp>
#include <wayfire/scene-input.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/view.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/util/duration.hpp>
using namespace wf::animation;

class touchswitch_animation_t : public duration_t
{
  public:
    using duration_t::duration_t;
    timed_transition_t scale_x{*this};
    timed_transition_t scale_y{*this};
    timed_transition_t translation_x{*this};
    timed_transition_t translation_y{*this};
};

struct wf_scale_animation_attribs
{
    wf::option_wrapper_t<wf::animation_description_t> duration{"touchswitch/duration"};
    touchswitch_animation_t scale_animation{duration};
};

struct view_scale_data
{
    std::shared_ptr<wf::scene::view_2d_transformer_t> transformer;
    wf_scale_animation_attribs animation;
    bool was_minimized;
};

class wayfire_touchswitch : public wf::per_output_plugin_instance_t,
    public wf::keyboard_interaction_t,
    public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
  public:
    wayfire_touchswitch();
    bool active = false;

    void init() override;
    void fini() override;

    bool handle_toggle();
    bool activate();
    void deactivate();
    void finalize();

    void handle_pointer_button(const wlr_pointer_button_event& event) override;
    void handle_pointer_motion(wf::pointf_t position, uint32_t time_msec) override;
    void handle_keyboard_key(wf::seat_t *seat, wlr_keyboard_key_event ev) override;

    void handle_touch_down(uint32_t time, int finger_id, wf::pointf_t pos) override;
    void handle_touch_up(uint32_t time, int finger_id, wf::pointf_t lift_off_position) override;
    void handle_touch_motion(uint32_t time, int finger_id, wf::pointf_t position) override;

    bool add_transformer(wayfire_toplevel_view view);
    bool add_transformer(wayfire_toplevel_view view, int start_x, int start_y);
    void pop_transformer(wayfire_toplevel_view view);
    void remove_transformers();
    void remove_view(wayfire_toplevel_view view);
    void handle_new_view(wayfire_toplevel_view view);
    void handle_view_unmapped(wayfire_toplevel_view view);

  private:

    enum class swipe_direction_option
    {
        UNDECIDED,
        VERTICAL,
        HORIZONTAL,
    };

    const double boundary_elasticity = 0.35;
    double target_snap_index = 0.0;
    bool is_snapping = false;

    void update_snap_physics();
    void on_allow_scale_zoom_changed();
    void on_render_pre_hook();
    void on_render_post_hook();
    void on_view_mapped_event(wf::view_mapped_signal *ev);
    void on_view_unmapped_event(wf::view_unmapped_signal *ev);
    void on_workspace_changed_event(wf::workspace_changed_signal *ev);
    void on_workarea_changed_event(wf::workarea_changed_signal *ev);
    void on_view_geometry_changed_event(wf::view_geometry_changed_signal *ev);

    std::shared_ptr<wf::scene::touchswitch_edge_overlay_node_t> top_overlay;
    std::shared_ptr<wf::scene::touchswitch_edge_overlay_node_t> bottom_overlay;
    wf::touchswitch_notice_overlay_t workspace_notice;

    touchswitch_show_title_t show_title;
    touchswitch_show_icon_t show_icon;
    std::unique_ptr<wf::input_grab_t> grab;

    bool hook_set   = false;
    bool touch_held = false;
    uint32_t flick_timestamp = 0;
    wf::pointf_t last_touch{0, 0}, start_touch{0, 0}, start_flick{0, 0}, velocity{0, 0};
    double touch_x_offset = std::numeric_limits<double>::quiet_NaN();
    double touch_y_offset = 0.0;

    wayfire_toplevel_view last_selected_view = nullptr;
    std::map<wayfire_toplevel_view, view_scale_data> scale_data;
    swipe_direction_option swipe_direction = swipe_direction_option::UNDECIDED;

    wf::option_wrapper_t<int> spacing{"touchswitch/spacing"};
    wf::option_wrapper_t<bool> allow_scale_zoom{"touchswitch/allow_zoom"};
    wf::option_wrapper_t<double> window_scale{"touchswitch/window_scale"};
    wf::option_wrapper_t<bool> minimize_others{"touchswitch/minimize_others"};
    wf::option_wrapper_t<std::string> up_action{"touchswitch/pull_up"};
    wf::option_wrapper_t<std::string> down_action{"touchswitch/pull_down"};
    wf::option_wrapper_t<std::string> background_action{"touchswitch/background_touch"};
    wf::option_wrapper_t<double> flick_motion{"touchswitch/flick_motion"};
    wf::option_wrapper_t<int> icon_size{"touchswitch/icon_size"};

    const double velocity_threshold    = 0.1;
    const double flick_threshold_start = 50.0;
    const double flick_threshold_end   = 20.0;
    const double max_scale_factor = 1.0;
    const double max_scale_child  = 1.0;

    constexpr static const char *TOUCHSWITCH_TRANSFORMER = "touchswitch";
    wf::plugin_activation_data_t grab_interface;

    wf::effect_hook_t pre_hook;
    wf::effect_hook_t post_hook;
    wf::signal::connection_t<touchswitch_update_signal> update_cb;
    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped;
    wf::signal::connection_t<wf::workspace_changed_signal> workspace_changed;
    wf::signal::connection_t<wf::workarea_changed_signal> workarea_changed;
    wf::signal::connection_t<wf::view_geometry_changed_signal> view_geometry_changed;
    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped;

    bool is_velocity_zero();
    bool animation_running();
    void handle_window_swipe();
    void process_input(uint32_t button, uint32_t state, wf::pointf_t pos, uint32_t time);
    void handle_relative_motion(wf::pointf_t diff, uint32_t time);
    void check_focus_view(wayfire_toplevel_view view);
    void transform_views();
    void layout_slots(std::vector<wayfire_toplevel_view> views);
    void setup_view_transform(wayfire_toplevel_view view, view_scale_data& vd, double sx, double sy,
        double tx,
        double ty);
    void set_hook();
    void unset_hook();

    size_t get_view_index(wayfire_toplevel_view view);
    wayfire_toplevel_view get_view(size_t idx);
    wayfire_toplevel_view get_current_view();
    size_t get_current_idx();
    std::vector<wayfire_toplevel_view> get_views();
    bool should_scale_view(wayfire_toplevel_view view);
    void render_empty_notification_message();

    void set_tiled_wobbly(wayfire_toplevel_view v, bool b)
    {}
};
