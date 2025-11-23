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

#include <wayfire/plugins/common/move-drag-interface.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/common/input-grab.hpp>

#include <linux/input-event-codes.h>

#include "wayfire/plugins/ipc/ipc-activator.hpp"
#include "touchswitch.hpp"
#include "touchswitch-title-overlay.hpp"
#include "touchswitch-icon-overlay.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/plugin.hpp"
#include "wayfire/plugins/common/util.hpp"
#include "wayfire/scene-input.hpp"
#include "wayfire/scene.hpp"
#include "wayfire/signal-provider.hpp"
#include "wayfire/toplevel-view.hpp"
#include "wayfire/view.hpp"

static constexpr const char *TOUCHSWITCH_TRANSFORMER = "touchswitch";
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
class wayfire_touchswitch : public wf::per_output_plugin_instance_t,
    public wf::keyboard_interaction_t,
    public wf::pointer_interaction_t,
    public wf::touch_interaction_t
{
    enum class swipe_direction_option
    {
        UNDECIDED,
        VERTICAL,
        HORIZONTAL,
    };
    /* helper class for optionally showing title overlays */
    touchswitch_show_title_t show_title;
    touchswitch_show_icon_t show_icon;
    bool hook_set;
    bool touch_held;
    uint32_t flick_timestamp = 0;
    bool travelled = false;
    wf::pointf_t last_touch, start_touch, start_flick, velocity;
    double touch_x_offset = std::numeric_limits<double>::quiet_NaN();
    double touch_y_offset = 0.0;
    /* View over which the last input press happened */
    wayfire_toplevel_view last_selected_view;
    std::map<wayfire_toplevel_view, view_scale_data> scale_data;
    swipe_direction_option swipe_direction=swipe_direction_option::UNDECIDED;
    wf::option_wrapper_t<int> spacing{"touchswitch/spacing"};
    wf::option_wrapper_t<bool> allow_scale_zoom{"touchswitch/allow_zoom"};
    wf::option_wrapper_t<double> window_scale{"touchswitch/window_scale"};
    wf::option_wrapper_t<bool> minimize_others{"touchswitch/minimize_others"};
    wf::option_wrapper_t<std::string> up_action{"touchswitch/pull_up"};
    wf::option_wrapper_t<std::string> down_action{"touchswitch/pull_down"};
    wf::option_wrapper_t<std::string> background_action{"touchswitch/background_touch"};
    wf::option_wrapper_t<double> flick_motion{"touchswitch/flick_motion"};

    double velocity_threshold = 0.1; /* The point at which movement is considered stopped, and velocity is zeroed */
    double flick_threshold_start = 50; /* The ammount of motion needed to start a flick gesture */
    double flick_threshold_end = 20;

    /* maximum scale -- 1.0 means we will not "zoom in" on a view */
    const double max_scale_factor = 1.0;
    /* maximum scale for child views (relative to their parents)
     * zero means unconstrained, 1.0 means child cannot be scaled
     * "larger" than the parent */
    const double max_scale_child = 1.0;

    std::unique_ptr<wf::input_grab_t> grab;

    wf::plugin_activation_data_t grab_interface{
        .name = TOUCHSWITCH_TRANSFORMER,
        .capabilities = wf::CAPABILITY_MANAGE_DESKTOP | wf::CAPABILITY_GRAB_INPUT,
        .cancel = [=] () { finalize(); },
    };

  public:
    bool active = false;

    void init() override
    {
        hook_set = false;
        grab     = std::make_unique<wf::input_grab_t>(TOUCHSWITCH_TRANSFORMER, output, this, this, this);

        allow_scale_zoom.set_callback(allow_scale_zoom_option_changed);



        show_title.init(output);
        show_icon.init(output);
        output->connect(&update_cb);
    }

    /* Helper to end swipe velocity */
    bool is_velocity_zero()
    {
        if (std::hypot(velocity.x, velocity.y) > velocity_threshold)
        {
            return false;
        }
        velocity = {0, 0};
        return true;
    }

    /* Variant to create a transform for a fully shown window, animate from current location in scene*/
    bool add_transformer(wayfire_toplevel_view view){
        if (view->get_transformed_node()->get_transformer(TOUCHSWITCH_TRANSFORMER))
        {
            return false;
        }
        auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
        
        scale_data[view].transformer = tr;
        view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D + 1,
            TOUCHSWITCH_TRANSFORMER);

        /* Transformers are added only once when scale is activated so
         * this is a good place to connect the geometry-changed handler */
        view->connect(&view_geometry_changed);
        view->connect(&view_unmapped);

        set_tiled_wobbly(view, true);

        /* signal that a transformer was added to this view */
        touchswitch_transformer_added_signal data;
        data.view = view;
        output->emit(&data);

        return true;
    }

    /* Add a transformer that will be used to scale the view, needs theoretical translation to start from, if minimized */
    bool add_transformer(wayfire_toplevel_view view, int start_x, int start_y)
    {
        if (view->get_transformed_node()->get_transformer(TOUCHSWITCH_TRANSFORMER))
        {
            return false;
        }
        /* If this is a previously unset transform, animate from bottom of display */
        /* TODO Animation Options */
        auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
        
        tr->translation_y=start_y;
        tr->translation_x=start_x;
        tr->scale_x = window_scale;
        tr->scale_y = window_scale;
        
        scale_data[view].transformer = tr;
        view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D + 1,
            TOUCHSWITCH_TRANSFORMER);

        /* Transformers are added only once when scale is activated so
         * this is a good place to connect the geometry-changed handler */
        view->connect(&view_geometry_changed);
        view->connect(&view_unmapped);

        set_tiled_wobbly(view, true);

        /* signal that a transformer was added to this view */
        touchswitch_transformer_added_signal data;
        data.view = view;
        output->emit(&data);

        return true;
    }

    /* Remove the scale transformer from the view */
    void pop_transformer(wayfire_toplevel_view view)
    {
        /* signal that a transformer was added to this view */
        touchswitch_transformer_removed_signal data;
        data.view = view;
        output->emit(&data);
        view->get_transformed_node()->rem_transformer(TOUCHSWITCH_TRANSFORMER);
        view->disconnect(&view_unmapped);
        set_tiled_wobbly(view, false);
    }

    /* Remove scale transformers from all views */
    void remove_transformers()
    {
        for (auto& e : scale_data)
        {
            for (auto& toplevel : e.first->enumerate_views(false))
            {
                pop_transformer(toplevel);
            }
        }
    }

    /* Activate scale, switch activator modes and deactivate */
    bool handle_toggle()
    {
        if (active)
        {
            deactivate();
            return true;
        }
        return activate();
    }

    wf::signal::connection_t<touchswitch_update_signal> update_cb = [=] (touchswitch_update_signal *ev)
    {
        if (active)
        {
            layout_slots(get_views());
            output->render->schedule_redraw();
        }
    };

    void handle_pointer_button(
        const wlr_pointer_button_event& event) override
    {
        process_input(event.button, event.state,
            wf::get_core().get_cursor_position(), event.time_msec);
    }

    void handle_touch_down(uint32_t time, int finger_id, wf::pointf_t pos) override
    {
        if (finger_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_PRESSED, pos, time);
        }
    }

    void handle_touch_up(uint32_t time, int finger_id,
        wf::pointf_t lift_off_position) override
    {
        if (finger_id == 0)
        {
            process_input(BTN_LEFT, WLR_BUTTON_RELEASED, lift_off_position, time);
        }
    }

    void handle_touch_motion(uint32_t time, int finger_id,
        wf::pointf_t position) override
    {
        if (finger_id == 0)
        {
            handle_pointer_motion(position, time);
        }
    }

    /* Updates initial view focus variables accordingly */
    void check_focus_view(wayfire_toplevel_view view)
    {
        if (view == last_selected_view)
        {
            last_selected_view = nullptr;
        }
    }

    /* Remove transformer from view and remove view from the scale_data map */
    void remove_view(wayfire_toplevel_view view)
    {
        if (!view || !scale_data.count(view))
        {
            return;
        }

        for (auto v : view->enumerate_views(false))
        {
            check_focus_view(v);
            pop_transformer(v);
            scale_data.erase(v);
        }
    }

    /* Process button event */
    void process_input(uint32_t button, uint32_t state, wf::pointf_t input_position, uint32_t time)
    {
        if (!active)
        {
            return;
        }
        if (button != BTN_LEFT)
        {
            return;
        }
        last_touch = input_position;
        start_touch = input_position;

        /* If a button press or touch-start */
        if (state == WLR_BUTTON_PRESSED)
        {
            swipe_direction = swipe_direction_option::UNDECIDED;
            travelled = false;
            touch_held = true;
            velocity = {0, 0};
            flick_timestamp = 0;
            auto view = touchswitch_find_view_at(input_position, output);
            if (view && should_scale_view(view))
            {
                /* Mark the view as the target of the next input release operation */
                last_selected_view = view;
            } else
            {
                last_selected_view = nullptr;
            }

            return;
        }

        touch_held = false;

        /* Drag or touch left the dead zone */
        if (travelled)
        {
            if (last_selected_view != nullptr)
            {
                auto workarea = output->workarea->get_workarea();
                std::string action;

                /* TODO User sensitivity? Currently swipe up or down 1/4 of the screen height */
                if (abs(touch_y_offset) > (workarea.height  / 4.0))
                {
                    if (touch_y_offset < 0)
                    {
                        action = up_action;
                    } else
                    {
                        action = down_action;
                    }
                }
                /* TODO other actions */
                if (action == "close")
                {
                    /* Set non-visible to avoid full-screen flicker of window as it dies */
                    wf::scene::set_node_enabled(last_selected_view->get_root_node(),false);
                    last_selected_view->close();
                } else if (action == "minimize")
                {
                    scale_data[last_selected_view].was_minimized = true;
                }
                /* TODO Consider logging unknown value */
            }
            /* Try for velocity */
            if (flick_timestamp > 0 && time > flick_timestamp)
            {
                uint32_t count_ms = time - flick_timestamp;
                velocity = start_flick - input_position;
                velocity = {velocity.x / (double)count_ms, velocity.y /(double)count_ms};
            }
            /* Set touch settings to no-touch */
            touch_y_offset = 0.0;
            

            /* Handle potential flick */
            if(flick_timestamp==0)
            {
                touch_x_offset = std::round(touch_x_offset);
            } else {
                double flick_duration = time - flick_timestamp;
                flick_timestamp = time;
                velocity = input_position - start_flick;
                velocity = {velocity.x /flick_duration, velocity.y / flick_duration};
            }
            start_flick = {0, 0};
            last_touch = {0, 0};
            start_touch = {0, 0};

            layout_slots(get_views());
            return;
        }
        
        if (last_selected_view != nullptr)
        {
            /* Touch a window directly, switch now! */
            touch_x_offset = get_view_index(last_selected_view);
            deactivate();
            return;
        } else
        {
            /* Touched background, optional actions */
            std::string bg_action = (std::string) background_action;
            if (bg_action == "ignore")
            {
                return;
            }
            /* Set to NaN to make sure no window is raised in finalize */
            if (bg_action == "showdesktop")
            {
                touch_x_offset = std::numeric_limits<double>::quiet_NaN();
            }
        }                
        deactivate();
    }

    /* Handle relative motion input. Should consider velocity of flick as well as mouse/touchscreen */
    void handle_relative_motion(wf::pointf_t diff, uint32_t time)
    {
        /* These actions should be animated mutually exclusively. Only show the axis with larger difference */
        if (swipe_direction == swipe_direction_option::VERTICAL)
        {
            /* Dragging up or down */
            touch_y_offset += diff.y;
            layout_slots(get_views());
        } else if (swipe_direction == swipe_direction_option::HORIZONTAL)
        {
            /* Dragging left or right */
            touch_y_offset = 0;
            auto workarea = output->workarea->get_workarea();
            const double scaled_width = std::max((double) workarea.width * window_scale, 1.0);
            /* Account for index-width not screen or window width */
            double motion_x = (diff.x) / (spacing + scaled_width);

            touch_x_offset -= motion_x;

            /* Force back into bounds if needed, also reset velocity if out of bounds */
            auto views = get_views();
            if (touch_x_offset < 0.0)
            {
                touch_x_offset = 0.0;
                velocity = {0, 0};
            } else if (touch_x_offset >= (double) (views.size() - 1))
            {
                touch_x_offset = (double) (views.size() - 1);
                velocity = {0, 0};
            }

            layout_slots(views);
        }

    }

    /* Handle motion input. Should only be mouse/touchscreen */
    void handle_pointer_motion(wf::pointf_t to_f, uint32_t time) override
    {
        if (!active)
        {
            return;
        }
        if (!touch_held)
        {
            return;
        }
        if (std::hypot(start_touch.x - to_f.x, start_touch.y - to_f.y) > 40.0)
        {
            travelled = true;
        }
        if (travelled){
            auto total_diff = to_f - start_touch;
            auto diff = to_f - last_touch;
            double total_distance = std::hypot(total_diff.x, total_diff.y);
            /* Check if we've commited to a gesture */
            if (swipe_direction == swipe_direction_option::UNDECIDED && total_distance > 50)
            {
                if (abs(total_diff.y) > abs(total_diff.x))
                {
                    swipe_direction = swipe_direction_option::VERTICAL;
                } else
                {
                    swipe_direction = swipe_direction_option::HORIZONTAL;
                }
            }
            double distance = std::hypot(diff.x, diff.y);
            if(distance > flick_threshold_start && flick_timestamp == 0)
            {
                /* Flick started */
                flick_timestamp = time;
                start_flick = to_f;
            } else if(distance <= flick_threshold_end)
            {
                /* Flick reset */
                flick_timestamp = 0;
                start_flick = {0,0};
            }
            handle_relative_motion(diff, time);
            last_touch = to_f;
        }
    }


    /* Returns the index of a given view, assert if not in get_views*/
    size_t get_view_index(wayfire_toplevel_view view)
    {
        auto views = get_views();
        auto iterator_focused = std::find(views.begin(), views.end(), view);
        wf::dassert(iterator_focused != views.end(), "Chosen view not in list!");
        return iterator_focused - views.begin();
    }

    /* Get the view at given index, returns nullptr if out of bounds */
    wayfire_toplevel_view get_view(size_t idx)
    {
        if (idx < 0 || idx >= get_views().size())
        {
            return nullptr;
        }
        return get_views().at(idx);
    }

    /* Return the selected current window, if offset is NaN at this point return null */
    wayfire_toplevel_view get_current_view()
    {
        if (std::isnan(touch_x_offset))
        {
            return nullptr;
        }
        return get_view(get_current_idx());
    }

    /* Get the current 'selected' middle slot index */
    size_t get_current_idx()
    {
        wf::dassert(!std::isnan(touch_x_offset), "X offset NaN");
        return std::roundl(touch_x_offset);
    }

    /* Process key event */
    void handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev) override
    {
        if ((ev.state != WLR_KEY_PRESSED) ||
            wf::get_core().seat->get_keyboard_modifiers())
        {
            return;
        }
        auto view_count = get_views().size();

        switch (ev.keycode)
        {

          case KEY_LEFT:
            touch_x_offset-=1.0;
            if (touch_x_offset < 0.0)
            {
                touch_x_offset = 0.0;
            }
            break;
          case KEY_RIGHT:
            touch_x_offset+=1.0;
            if(touch_x_offset >= (view_count - 1))
            {
                touch_x_offset = view_count - 1;
            }
            break;

          case KEY_ENTER:
            deactivate();
            return;

          default:
            return;
        }

        auto view = get_current_view();
        if (view)
        {
            layout_slots(get_views());
        }
    }

    /* Assign the transformer values to the view transformers */
    void transform_views()
    {
        for (auto& e : scale_data)
        {
            auto view = e.first;
            auto& view_data = e.second;
            if (!view || !view_data.transformer)
            {
                continue;
            }

            if (view_data.animation.scale_animation.running())
            {
                view->get_transformed_node()->begin_transform_update();
                view_data.transformer->scale_x =
                    view_data.animation.scale_animation.scale_x;
                view_data.transformer->scale_y =
                    view_data.animation.scale_animation.scale_y;
                view_data.transformer->translation_x =
                    view_data.animation.scale_animation.translation_x;
                view_data.transformer->translation_y =
                    view_data.animation.scale_animation.translation_y;

                view->get_transformed_node()->end_transform_update();
            }
        }
    }

    /* Returns a list of views to be scaled */
    std::vector<wayfire_toplevel_view> get_views()
    {
        std::vector<wayfire_toplevel_view> views = output->wset()->get_views(
            wf::WSET_MAPPED_ONLY);
        std::sort(views.begin(), views.end(), [] (auto a, auto b)
        {
            return a.get() < b.get();
        });
        return views;
    }

    /**
     * @return true if the view is to be scaled.
     */
    bool should_scale_view(wayfire_toplevel_view view)
    {
        auto views = get_views();

        return std::find(
            views.begin(), views.end(), wf::find_topmost_parent(view)) != views.end();
    }

    /* Convenience assignment function */
    void setup_view_transform(wayfire_toplevel_view view,
        view_scale_data& view_data,
        double scale_x,
        double scale_y,
        double translation_x,
        double translation_y)
    {
        /* If the user is actively dragging or flicking it then set it directly.
           Animating after the drag feels like really bad input lag */
        if (touch_held || !is_velocity_zero()){
            view->get_transformed_node()->begin_transform_update();
            view_data.transformer->scale_x = scale_x;
            view_data.transformer->scale_y = scale_y;
            view_data.transformer->translation_x = translation_x;
            view_data.transformer->translation_y = translation_y;
            view->get_transformed_node()->end_transform_update();
            return;
        }
        view_data.animation.scale_animation.scale_x.set(
            view_data.transformer->scale_x, scale_x);
        view_data.animation.scale_animation.scale_y.set(
            view_data.transformer->scale_y, scale_y);
        view_data.animation.scale_animation.translation_x.set(
            view_data.transformer->translation_x, translation_x);
        view_data.animation.scale_animation.translation_y.set(
            view_data.transformer->translation_y, translation_y);
        view_data.animation.scale_animation.start();
    }

    /* Compute target scale layout geometry for all the view transformers
     * and start animating. Initial code borrowed from the compiz scale
     * plugin algorithm */
    void layout_slots(std::vector<wayfire_toplevel_view> views)
    {
        wf::dassert(active || hook_set, "Touchswitch is not active");
        if (!views.size())
        {
            if (active)
            {
                deactivate();
            }

            return;
        }

        auto workarea = output->workarea->get_workarea();

        const double scaled_height = std::max((double)
            workarea.height * window_scale, 1.0);
        const double scaled_width = std::max((double)
            workarea.width * window_scale, 1.0);

        const double workarea_center_x = workarea.width / 2.0;
        const double workarea_center_y = workarea.height / 2.0;

        const double offset_x = workarea.x - (scaled_width / 2.0) + workarea_center_x;
        const double offset_y = workarea.y - (scaled_height / 2.0) + workarea_center_y;

        for (size_t j = 0; j < views.size(); j++)
        {

            wayfire_toplevel_view view = views[j];
            double index_position = (double)(j) - touch_x_offset;
            double x = offset_x +  (spacing + scaled_width) * index_position;
            double y = offset_y;
            if (last_selected_view != nullptr && view == last_selected_view)
            {
                y += touch_y_offset;
            }
            
            /* Calculate current transformation of the view, in order to
               ensure that new views in the view tree start directly at the
               correct position */
            double main_view_dx    = 0;
            double main_view_dy    = 0;
            double main_view_scale = 1.0;
            if (scale_data.count(view))
            {
                main_view_dx    = scale_data[view].transformer->translation_x;
                main_view_dy    = scale_data[view].transformer->translation_y;
                main_view_scale = scale_data[view].transformer->scale_x;

                if (view->minimized)
                {
                    view->set_minimized(false);
                    scale_data[view].was_minimized = true;
                }
            }

            /* Helper function to calculate the desired scale for a view */
            const auto& calculate_scale = [=] (wf::dimensions_t vg)
            {
                double w = std::max(1.0, scaled_width);
                double h = std::max(1.0, scaled_height);

                const double scale = std::min(w / vg.width, h / vg.height);
                if (!allow_scale_zoom)
                {
                    return std::min(scale, max_scale_factor);
                }

                return scale;
            };

            
            add_transformer(view, (spacing + scaled_width) * index_position, offset_y+workarea.height);
            auto geom = view->get_geometry();
            double view_scale = calculate_scale({geom.width, geom.height});
            for (auto& child : view->enumerate_views(true))
            {
                /* Ensure a transformer for the view, and make sure that
                   new views in the view tree start off with the correct
                   attributes set. */
                auto new_child   = add_transformer(child, (spacing + scaled_width) * index_position, offset_y+workarea.height);
                auto& child_data = scale_data[child];
                if (new_child)
                {
                    child_data.transformer->translation_x = main_view_dx;
                    child_data.transformer->translation_y = main_view_dy;
                    child_data.transformer->scale_x = main_view_scale;
                    child_data.transformer->scale_y = main_view_scale;
                }

                if (!active)
                {
                    /* On exit, we just animate towards normal state */
                    setup_view_transform(view, child_data, 1, 1, 0, 0);
                    continue;
                }

                auto vg = child->get_geometry();
                wf::pointf_t center = {vg.x + vg.width / 2.0, vg.y + vg.height / 2.0};

                /* Take padding into account */
                double scale = calculate_scale({vg.width, vg.height});
                /* Ensure child is not scaled more than parent */
                if (!allow_scale_zoom &&
                    (child != view) &&
                    (max_scale_child > 0.0))
                {
                    scale = std::min(max_scale_child * view_scale, scale);
                }

                /* Start the animation */
                const double dx = x - center.x + scaled_width / 2.0;
                const double dy = y - center.y + scaled_height / 2.0;
                setup_view_transform(view, child_data, scale, scale,
                    dx, dy);
            }
        }

        set_hook();
        transform_views();
    }

    /* Toggle between restricting maximum scale to 100% or allowing it
     * to become the greater. This is particularly noticeable when
     * scaling a single view or a view with child views. */
    wf::config::option_base_t::updated_callback_t allow_scale_zoom_option_changed =
        [=] ()
    {
        if (!output->is_plugin_active(grab_interface.name))
        {
            return;
        }

        layout_slots(get_views());
    };

    void handle_new_view(wayfire_toplevel_view view)
    {
        if (!should_scale_view(view))
        {
            return;
        }
        
        layout_slots(get_views());
    }

    wf::signal::connection_t<wf::view_mapped_signal> on_view_mapped = [=] (wf::view_mapped_signal *ev)
    {
        if (!active)
        {
            return;
        }
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            handle_new_view(toplevel);
        }
    };

    void handle_view_unmapped(wayfire_toplevel_view view)
    {
        if (!active)
        {
            return;
        }
        remove_view(view);
        if (scale_data.empty())
        {
            finalize();
        } else if (!view->parent)
        {
            /* If we're over the bounds now, move back in */
            if (touch_x_offset >= (double) (get_views().size() - 1))
            {
                touch_x_offset = (double) (get_views().size() - 1) ;
            }
            layout_slots(get_views());
        }
    }

    /* Workspace changed */
    wf::signal::connection_t<wf::workspace_changed_signal> workspace_changed =
        [=] (wf::workspace_changed_signal *ev)
    {
        if (!active)
        {
            return;
        }
        layout_slots(get_views());
    };

    wf::signal::connection_t<wf::workarea_changed_signal> workarea_changed =
        [=] (wf::workarea_changed_signal *ev)
    {
        if (!active)
        {
            return;
        }
        layout_slots(get_views());
    };

    /* View geometry changed. Also called when workspace changes */
    wf::signal::connection_t<wf::view_geometry_changed_signal> view_geometry_changed =
        [=] (wf::view_geometry_changed_signal *ev)
    {
        if (!active)
        {
            return;
        }
        auto views = get_views();
        if (!views.size())
        {
            deactivate();

            return;
        }

        layout_slots(std::move(views));
    };


    /* View unmapped */
    wf::signal::connection_t<wf::view_unmapped_signal> view_unmapped = [=] (wf::view_unmapped_signal *ev)
    {
        if (!active)
        {
            return;
        }
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            check_focus_view(toplevel);
            handle_view_unmapped(toplevel);
        }
    };

    /* Returns true if any scale animation is running */
    bool animation_running()
    {
        for (auto& e : scale_data)
        {
            if (e.second.animation.scale_animation.running())
            {
                return true;
            }
        }

        return false;
    }

    /* Assign transform values to the actual transformer */
    wf::effect_hook_t pre_hook = [=] ()
    {
        transform_views();
    };

    /* Keep rendering until all animation has finished */
    wf::effect_hook_t post_hook = [=] ()
    {
        bool running = animation_running() || !is_velocity_zero();
        if(!touch_held && !is_velocity_zero())
        {
            /* Apply friction */
            velocity = {velocity.x * flick_motion, velocity.y * flick_motion};
            if(is_velocity_zero())
            {
                /* Was moving, now isn't. */
                touch_x_offset = std::round(touch_x_offset);
                flick_timestamp = 0;
                start_flick = {0, 0};
                start_touch = {0, 0};
                layout_slots(get_views());
            } else
            {
                /* Count msec since last hook. Should be frame-length but let's not bet */
                uint32_t current_time = wf::get_current_time();
                double count_msec = current_time - flick_timestamp;
                flick_timestamp = current_time;
                wf::pointf_t movement = {velocity.x * count_msec, velocity.y * count_msec};
                handle_relative_motion(movement, current_time);
            }
        }

        if (running)
        {
            output->render->schedule_redraw();
        }

        if (active || running)
        {
            return;
        }

        finalize();
    };

    bool can_handle_drag()
    {
        return output->is_plugin_active(this->grab_interface.name);
    }

    /* Activate and start scale animation */
    bool activate()
    {
        if (active)
        {
            return false;
        }

        if (!output->activate_plugin(&grab_interface))
        {
            return false;
        }

        auto views = get_views();
        if (views.empty())
        {
            output->deactivate_plugin(&grab_interface);
            return false;
        }

        travelled = false;
        touch_held = false;

        wayfire_toplevel_view active_view = toplevel_cast(wf::get_active_view_for_output(output));
        if (active_view)
        {
            touch_x_offset = get_view_index(active_view);
        } else
        {
            touch_x_offset = 0.0;
        }

        /* Make sure no leftover events from the activation binding
           trigger an action in switcher */
        last_selected_view = nullptr;

        grab->grab_input(wf::scene::layer::WORKSPACE);

        active = true;

        /* For already visible views, transform from current location */
        for (auto& view : get_views())
        {
            if (!view->minimized)
            {
                add_transformer(view);
            }
        } 

        layout_slots(get_views());
        

        output->connect(&on_view_mapped);
        output->connect(&workspace_changed);
        output->connect(&workarea_changed);
        touchswitch_update_signal signal;
        output->emit(&signal);

        return true;
    }

    /* Deactivate and start unscale animation */
    void deactivate()
    {
        auto view = get_current_view();

        active = false;

        set_hook();
        on_view_mapped.disconnect();
        workspace_changed.disconnect();
        workarea_changed.disconnect();
        view_geometry_changed.disconnect();

        grab->ungrab_input();
        output->deactivate_plugin(&grab_interface);

        if (view != nullptr)
        {
            wf::get_core().default_wm->focus_raise_view(view);
            setup_view_transform(view, scale_data[view], 1, 1, 0, 0);
        }
        bool to_desktop = ((std::string)background_action)=="showdesktop" && view == nullptr;
        for (auto& e : scale_data)
        {
            if (e.first == view){
                continue;
            }
            if (e.second.was_minimized || minimize_others || to_desktop)
            {
                /* Animate downwards */
                /* TODO Custom direction? */
                setup_view_transform(e.first, e.second, window_scale, window_scale, e.second.transformer->translation_x, 1000);
            } else
            {
                setup_view_transform(e.first, e.second, 1, 1, 0, 0);
            }
        }

        touchswitch_end_signal signal;
        output->emit(&signal);
    }

    /* Completely end switcher, including animation */
    void finalize()
    {
        if (active)
        {
            /* only emit the signal if deactivate() was not called before */
            touchswitch_end_signal signal;
            output->emit(&signal);

        }
        active = false;
        auto view = get_current_view();
        std::string action = background_action;
        if (view != nullptr)
        {
            wf::get_core().default_wm->focus_raise_view(view);
        }
        for (auto& some_view : get_views())
        {
            /* Perform show desktop action */
            if (action == "showdesktop" && view == nullptr)
            {
                some_view->set_minimized(true);
                continue;
            }
            /* Skip newly chosen window */
            if (some_view == view)
            {
                continue;
            }
            /* Minimize others if user preference */
            if (minimize_others || scale_data[some_view].was_minimized)
            {
                some_view->set_minimized(true);
            }
        }

        unset_hook();
        remove_transformers();
        scale_data.clear();
        grab->ungrab_input();
        on_view_mapped.disconnect();
        workspace_changed.disconnect();
        workarea_changed.disconnect();
        view_geometry_changed.disconnect();
        output->deactivate_plugin(&grab_interface);
        touch_x_offset = std::numeric_limits<double>::quiet_NaN();
        touch_y_offset = 0.0;
        wf::scene::update(wf::get_core().scene(),
            wf::scene::update_flag::INPUT_STATE);
    }

    /* Utility hook setter */
    void set_hook()
    {
        if (hook_set)
        {
            return;
        }

        output->render->add_effect(&post_hook, wf::OUTPUT_EFFECT_POST);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        output->render->schedule_redraw();
        hook_set = true;
    }

    /* Utility hook unsetter */
    void unset_hook()
    {
        if (!hook_set)
        {
            return;
        }

        output->render->rem_effect(&post_hook);
        output->render->rem_effect(&pre_hook);
        hook_set = false;
    }

    void fini() override
    {
        finalize();
        show_title.fini();
        show_icon.fini();
    }
};

class wayfire_touchswitch_global : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_touchswitch>
{
    wf::ipc_activator_t activate{"touchswitch/activate"};

  public:
    void init() override
    {
        this->init_output_tracking();
        activate.set_handler(activate_cb);
    }

    void fini() override
    {
        this->fini_output_tracking();
    }

    void handle_new_output(wf::output_t *output) override
    {
        per_output_tracker_mixin_t::handle_new_output(output);
        output->connect(&on_view_set_output);
    }

    void handle_output_removed(wf::output_t *output) override
    {
        per_output_tracker_mixin_t::handle_output_removed(output);
        output->disconnect(&on_view_set_output);
    }

    wf::signal::connection_t<wf::view_set_output_signal> on_view_set_output =
        [=] (wf::view_set_output_signal *ev)
    {
        if (auto toplevel = wf::toplevel_cast(ev->view))
        {
            auto old_output = ev->output;
            if (old_output && output_instance.count(old_output))
            {
                this->output_instance[old_output]->handle_view_unmapped(toplevel);
            }

            auto new_output = ev->view->get_output();
            if (new_output && output_instance.count(new_output) && output_instance[new_output]->active)
            {
                this->output_instance[ev->view->get_output()]->handle_new_view(toplevel);
            }
        }
    };

    wf::ipc_activator_t::handler_t activate_cb = [=] (wf::output_t *output, wayfire_view)
    {
        if (this->output_instance[output]->handle_toggle())
        {
            output->render->schedule_redraw();
            return true;
        }

        return false;
    };
};

DECLARE_WAYFIRE_PLUGIN(wayfire_touchswitch_global);
