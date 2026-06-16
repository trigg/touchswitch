#include "touchswitch-instance.hpp"
#include "touchswitch-edge-overlay.hpp"
#include "touchswitch.hpp"
#include <memory>
#include <wayfire/bindings.hpp>
#include <wayfire/option-wrapper.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/toplevel-view.hpp>
#include <wayfire/view-helpers.hpp>
#include <wayfire/window-manager.hpp>
#include <wayfire/workarea.hpp>
#include <wayfire/workspace-set.hpp>

void wayfire_touchswitch::init()
{
    top_overlay = std::make_shared<wf::scene::touchswitch_edge_overlay_node_t>(output,
        wf::scene::edge_overlay_position::TOP);
    bottom_overlay = std::make_shared<wf::scene::touchswitch_edge_overlay_node_t>(output,
        wf::scene::edge_overlay_position::BOTTOM);

    auto overlay_layer = output->node_for_layer(wf::scene::layer::OVERLAY);
    if (overlay_layer)
    {
        wf::scene::add_front(overlay_layer, top_overlay);
        wf::scene::add_front(overlay_layer, bottom_overlay);
        wf::scene::damage_node(overlay_layer, overlay_layer->get_bounding_box());
    }

    this->hook_set = false;
    this->grab_interface.name = TOUCHSWITCH_TRANSFORMER;
    this->grab_interface.capabilities = wf::CAPABILITY_MANAGE_DESKTOP | wf::CAPABILITY_GRAB_INPUT;
    this->grab_interface.cancel = [this] ()
    {
        this->finalize();
    };
    this->grab = std::make_unique<wf::input_grab_t>(TOUCHSWITCH_TRANSFORMER, output, this, this, this);

    allow_scale_zoom.set_callback([this] () {this->on_allow_scale_zoom_changed();});

    pre_hook  = [this] () { this->on_render_pre_hook(); };
    post_hook = [this] () { this->on_render_post_hook(); };
    on_view_mapped    = [this] (auto *ev) {this->on_view_mapped_event(ev);};
    view_unmapped     = [this] (auto *ev) {this->on_view_unmapped_event(ev);};
    workspace_changed = [this] (auto *ev) {this->on_workspace_changed_event(ev);};
    workarea_changed  = [this] (auto *ev) {this->on_workarea_changed_event(ev);};
    view_geometry_changed = [this] (auto *ev) {this->on_view_geometry_changed_event(ev);};

    show_title.init(output);
    show_icon.init(output);
    output->connect(&update_cb);
}

void wayfire_touchswitch::fini()
{
    finalize();

    if (top_overlay)
    {
        wf::scene::remove_child(top_overlay);
    }

    if (bottom_overlay)
    {
        wf::scene::remove_child(bottom_overlay);
    }

    show_title.fini();
    show_icon.fini();
}

wayfire_touchswitch::wayfire_touchswitch()
{}

bool wayfire_touchswitch::is_velocity_zero()
{
    if (std::hypot(velocity.x, velocity.y) > velocity_threshold)
    {
        return false;
    }

    velocity = {0, 0};
    return true;
}

void wayfire_touchswitch::handle_window_swipe()
{
    if ((swipe_direction != swipe_direction_option::VERTICAL) || !last_selected_view)
    {
        return;
    }

    auto workarea = output->workarea->get_workarea();
    std::string action;

    if (std::abs(touch_y_offset) > (workarea.height / 4.0))
    {
        action = (touch_y_offset < 0) ? (std::string)up_action : (std::string)down_action;
    }

    if (action == "close")
    {
        wf::scene::set_node_enabled(last_selected_view->get_root_node(), false);
        last_selected_view->close();
    } else if (action == "minimize")
    {
        scale_data[last_selected_view].was_minimized = true;
    }
}

void wayfire_touchswitch::process_input(uint32_t button, uint32_t state, wf::pointf_t input_position,
    uint32_t time)
{
    if (!active || (button != BTN_LEFT))
    {
        return;
    }

    if ((state == WLR_BUTTON_RELEASED) && !touch_held)
    {
        return;
    }

    last_touch = start_touch = input_position;

    if (state == WLR_BUTTON_PRESSED)
    {
        is_snapping     = false;
        swipe_direction = swipe_direction_option::UNDECIDED;
        touch_held = true;
        velocity   = {0, 0};
        flick_timestamp = 0;
        auto view = touchswitch_find_view_at(input_position, output);
        last_selected_view = (view && should_scale_view(view)) ? view : nullptr;
        return;
    }

    touch_held = false;

    if ((swipe_direction == swipe_direction_option::VERTICAL) ||
        (swipe_direction == swipe_direction_option::HORIZONTAL))
    {
        auto views = get_views();
        double max_index = views.empty() ? 0.0 : static_cast<double>(views.size() - 1);

        if (swipe_direction == swipe_direction_option::HORIZONTAL)
        {
            if (touch_x_offset < 0.0)
            {
                target_snap_index = 0.0;
                is_snapping = true;
                velocity    = {0.0, 0.0};
            } else if (touch_x_offset > max_index)
            {
                target_snap_index = max_index;
                is_snapping = true;
                velocity    = {0.0, 0.0};
            } else if (std::abs(velocity.x) > velocity_threshold)
            {
                double predicted_landing = touch_x_offset - (velocity.x * 150.0);
                target_snap_index = std::clamp(std::round(predicted_landing), 0.0, max_index);
                is_snapping = true;
            } else
            {
                target_snap_index = std::clamp(std::round(touch_x_offset), 0.0, max_index);
                is_snapping = true;
            }
        } else if (swipe_direction == swipe_direction_option::VERTICAL)
        {
            if (flick_timestamp == 0)
            {
                handle_window_swipe();
                is_snapping = true;
            }

            top_overlay->set_visible(false);
            bottom_overlay->set_visible(false);
        }

        start_flick = {0.0, 0.0};
        last_touch  = {0.0, 0.0};
        start_touch = {0.0, 0.0};

        set_hook();
        layout_slots(get_views());
        return;
    }

    if (last_selected_view != nullptr)
    {
        touch_x_offset = static_cast<double>(get_view_index(last_selected_view));
    } else
    {
        std::string bg_action = (std::string)background_action;
        if (bg_action == "ignore")
        {
            return;
        }

        if (bg_action == "showdesktop")
        {
            touch_x_offset = std::numeric_limits<double>::quiet_NaN();
        }
    }

    deactivate();
}

void wayfire_touchswitch::handle_relative_motion(wf::pointf_t diff, uint32_t time)
{
    if (!active || !touch_held)
    {
        return;
    }

    auto workarea = output->workarea->get_workarea();

    if (swipe_direction == swipe_direction_option::VERTICAL)
    {
        touch_y_offset += diff.y;
        bool threshold_passed = (std::abs(touch_y_offset) > (workarea.height / 4.0));
        wf::geometry_t top_geom{
            workarea.x + (workarea.width / 2) - (icon_size / 2),
            workarea.y,
            icon_size,
            icon_size
        };

        wf::geometry_t bottom_geom{
            workarea.x + (workarea.width / 2) - (icon_size / 2),
            workarea.y + workarea.height - icon_size,
            icon_size,
            icon_size
        };

        top_overlay->update_geometry(top_geom);
        bottom_overlay->update_geometry(bottom_geom);

        top_overlay->set_visible(true);
        bottom_overlay->set_visible(true);
        if (touch_y_offset < 0)
        {
            if (threshold_passed && !top_overlay->is_active)
            {
                top_overlay->is_active = true;
                top_overlay->update_icon();
            } else if (!threshold_passed && top_overlay->is_active)
            {
                top_overlay->is_active = false;
                top_overlay->update_icon();
            }
        } else
        {
            if (threshold_passed && !bottom_overlay->is_active)
            {
                bottom_overlay->is_active = true;
                bottom_overlay->update_icon();
            } else if (!threshold_passed && bottom_overlay->is_active)
            {
                bottom_overlay->is_active = false;
                bottom_overlay->update_icon();
            }
        }

        layout_slots(get_views());
    } else if (swipe_direction == swipe_direction_option::HORIZONTAL)
    {
        top_overlay->set_visible(false);
        bottom_overlay->set_visible(false);
        touch_y_offset = 0;
        double scaled_width = std::max(static_cast<double>(workarea.width) * window_scale, 1.0);
        double motion_x     = diff.x / (static_cast<double>(spacing) + scaled_width);
        touch_x_offset -= motion_x;

        auto views = get_views();
        double max_index = views.empty() ? 0.0 : static_cast<double>(views.size() - 1);
        if ((touch_x_offset < 0.0) && (motion_x > 0.0))
        {
            motion_x *= boundary_elasticity / (1.0 + std::abs(touch_x_offset) * 2.0);
        } else if ((touch_x_offset > max_index) && (motion_x < 0.0))
        {
            motion_x *= boundary_elasticity / (1.0 + (touch_x_offset - max_index) * 2.0);
        }

        touch_x_offset -= motion_x;
        layout_slots(views);
    }
}

void wayfire_touchswitch::handle_pointer_motion(wf::pointf_t position, uint32_t time)
{
    if (!active || !touch_held)
    {
        return;
    }

    auto total_diff = position - start_touch;
    auto diff = position - last_touch;
    double total_distance = std::hypot(total_diff.x, total_diff.y);

    if (swipe_direction == swipe_direction_option::UNDECIDED)
    {
        if (total_distance > 40.0)
        {
            swipe_direction = (std::abs(total_diff.y) > std::abs(total_diff.x)) ?
                swipe_direction_option::VERTICAL : swipe_direction_option::HORIZONTAL;
        } else
        {
            last_touch = position;
            return;
        }
    }

    auto distance = std::hypot(diff.x, diff.y);

    if ((distance > flick_threshold_start) && (flick_timestamp == 0))
    {
        flick_timestamp = time;
        start_flick     = position;
    } else if (distance <= flick_threshold_end)
    {
        flick_timestamp = 0;
        start_flick     = {0, 0};
    }

    handle_relative_motion(diff, time);
    last_touch = position;
}

void wayfire_touchswitch::handle_pointer_button(const wlr_pointer_button_event& event)
{
    this->process_input(
        event.button,
        event.state,
        wf::get_core().get_cursor_position(),
        event.time_msec);
}

void wayfire_touchswitch::handle_keyboard_key(wf::seat_t*, wlr_keyboard_key_event ev)
{
    if ((ev.state != WLR_KEY_PRESSED) || wf::get_core().seat->get_keyboard_modifiers())
    {
        return;
    }

    auto views = get_views();
    if (views.empty())
    {
        return;
    }

    double max_index = static_cast<double>(views.size() - 1);

    switch (ev.keycode)
    {
      case KEY_LEFT:
        touch_x_offset = std::max(0.0, touch_x_offset - 1.0);
        break;

      case KEY_RIGHT:
        touch_x_offset = std::min(max_index, touch_x_offset + 1.0);
        break;

      case KEY_ENTER:
        deactivate();
        return;

      default:
        return;
    }

    if (get_current_view())
    {
        layout_slots(views);
    }
}

void wayfire_touchswitch::handle_touch_down(uint32_t time, int finger_id, wf::pointf_t pos)
{
    if (finger_id == 0)
    {
        process_input(BTN_LEFT, WLR_BUTTON_PRESSED, pos, time);
    }
}

void wayfire_touchswitch::handle_touch_up(uint32_t time, int finger_id, wf::pointf_t lift)
{
    if (finger_id == 0)
    {
        process_input(BTN_LEFT, WLR_BUTTON_RELEASED, lift, time);
    }
}

void wayfire_touchswitch::handle_touch_motion(uint32_t time, int finger_id, wf::pointf_t pos)
{
    if (finger_id == 0)
    {
        handle_pointer_motion(pos, time);
    }
}

bool wayfire_touchswitch::add_transformer(wayfire_toplevel_view view)
{
    if (view->get_transformed_node()->get_transformer(TOUCHSWITCH_TRANSFORMER))
    {
        return false;
    }

    auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
    scale_data[view].transformer = tr;
    view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D + 1, TOUCHSWITCH_TRANSFORMER);
    view->connect(&view_geometry_changed);
    view->connect(&view_unmapped);
    set_tiled_wobbly(view, true);
    touchswitch_transformer_added_signal data;
    data.view = view;
    output->emit(&data);
    return true;
}

bool wayfire_touchswitch::add_transformer(wayfire_toplevel_view view, int start_x, int start_y)
{
    if (view->get_transformed_node()->get_transformer(TOUCHSWITCH_TRANSFORMER))
    {
        return false;
    }

    auto tr = std::make_shared<wf::scene::view_2d_transformer_t>(view);
    tr->translation_y = start_y;
    tr->translation_x = start_x;
    tr->scale_x = window_scale;
    tr->scale_y = window_scale;
    scale_data[view].transformer = tr;
    view->get_transformed_node()->add_transformer(tr, wf::TRANSFORMER_2D + 1, TOUCHSWITCH_TRANSFORMER);
    view->connect(&view_geometry_changed);
    view->connect(&view_unmapped);
    set_tiled_wobbly(view, true);
    touchswitch_transformer_added_signal data;
    data.view = view;
    output->emit(&data);
    return true;
}

void wayfire_touchswitch::pop_transformer(wayfire_toplevel_view view)
{
    touchswitch_transformer_removed_signal data;
    data.view = view;
    output->emit(&data);
    view->get_transformed_node()->rem_transformer(TOUCHSWITCH_TRANSFORMER);
    view->disconnect(&view_unmapped);
    set_tiled_wobbly(view, false);
}

void wayfire_touchswitch::remove_transformers()
{
    for (auto& e : scale_data)
    {
        for (auto& toplevel : e.first->enumerate_views(false))
        {
            pop_transformer(toplevel);
        }
    }
}

void wayfire_touchswitch::remove_view(wayfire_toplevel_view view)
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

void wayfire_touchswitch::handle_new_view(wayfire_toplevel_view view)
{
    if (should_scale_view(view))
    {
        layout_slots(get_views());
    }
}

bool wayfire_touchswitch::handle_toggle()
{
    return active ? (deactivate(), true) : activate();
}

bool wayfire_touchswitch::activate()
{
    if (active || !output->activate_plugin(&grab_interface))
    {
        return false;
    }

    auto views = get_views();
    if (views.empty())
    {
        output->deactivate_plugin(&grab_interface);
        return false;
    }

    touch_held = false;
    swipe_direction = swipe_direction_option::UNDECIDED;
    auto active_view = toplevel_cast(wf::get_active_view_for_output(output));
    touch_x_offset     = active_view ? static_cast<double>(get_view_index(active_view)) : 0.0;
    last_selected_view = nullptr;
    grab->grab_input(wf::scene::layer::WORKSPACE);
    active = true;
    for (auto& view : views)
    {
        if (!view->minimized)
        {
            add_transformer(view);
        }
    }

    layout_slots(views);
    output->connect(&on_view_mapped);
    output->connect(&workspace_changed);
    output->connect(&workarea_changed);
    touchswitch_update_signal sig;
    output->emit(&sig);
    return true;
}

void wayfire_touchswitch::finalize()
{
    if (active)
    {
        touchswitch_end_signal sig;
        output->emit(&sig);
    }

    active = false;
    auto view = get_current_view();
    std::string action = background_action;
    if (view != nullptr)
    {
        view->set_minimized(false);
        wf::get_core().default_wm->focus_raise_view(view);
    }

    for (auto& some_view : get_views())
    {
        if ((action == "showdesktop") && (view == nullptr))
        {
            some_view->set_minimized(true);
            continue;
        }

        if (view && (some_view == view))
        {
            continue;
        }

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
    wf::scene::update(wf::get_core().scene(), wf::scene::update_flag::INPUT_STATE);
}

void wayfire_touchswitch::layout_slots(std::vector<wayfire_toplevel_view> views)
{
    wf::dassert(active || hook_set, "Touchswitch is not active");

    if (views.empty())
    {
        if (active)
        {
            deactivate();
        }

        return;
    }

    auto workarea = output->workarea->get_workarea();

    const double scaled_height = std::max(static_cast<double>(workarea.height) * window_scale, 1.0);
    const double scaled_width  = std::max(static_cast<double>(workarea.width) * window_scale, 1.0);

    const double workarea_center_x = workarea.width / 2.0;
    const double workarea_center_y = workarea.height / 2.0;

    const double offset_x = workarea.x - (scaled_width / 2.0) + workarea_center_x;
    const double offset_y = workarea.y - (scaled_height / 2.0) + workarea_center_y;
    for (size_t j = 0; j < views.size(); j++)
    {
        wayfire_toplevel_view view = views[j];
        double index_position = static_cast<double>(j) - touch_x_offset;
        double x = offset_x + (static_cast<double>(spacing) + scaled_width) * index_position;
        double y = offset_y;

        if ((last_selected_view != nullptr) && (view == last_selected_view))
        {
            y += touch_y_offset;
        }

        double main_view_dx    = 0.0;
        double main_view_dy    = 0.0;
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

        const auto calculate_scale = [this, scaled_width, scaled_height] (wf::dimensions_t vg)
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

        add_transformer(view, (static_cast<double>(spacing) + scaled_width) * index_position,
            offset_y + workarea.height);

        auto geom = view->get_geometry();
        double view_scale = calculate_scale({geom.width, geom.height});

        for (auto& child : view->enumerate_views(true))
        {
            auto new_child = add_transformer(child,
                (static_cast<double>(spacing) + scaled_width) * index_position,
                offset_y + workarea.height);

            auto& child_data = scale_data[child];
            if (new_child)
            {
                child_data.transformer->translation_x = main_view_dx;
                child_data.transformer->translation_y = main_view_dy;
                child_data.transformer->scale_x = main_view_scale;
                child_data.transformer->scale_y = main_view_scale;
            }

            if (!active && !hook_set)
            {
                setup_view_transform(view, child_data, 1.0, 1.0, 0.0, 0.0);
                continue;
            }

            auto vg = child->get_geometry();
            wf::pointf_t center = {vg.x + vg.width / 2.0, vg.y + vg.height / 2.0};

            double scale = calculate_scale({vg.width, vg.height});
            if (!allow_scale_zoom && (child != view) && (max_scale_child > 0.0))
            {
                scale = std::min(max_scale_child * view_scale, scale);
            }

            const double dx = x - center.x + scaled_width / 2.0;
            const double dy = y - center.y + scaled_height / 2.0;
            setup_view_transform(view, child_data, scale, scale, dx, dy);
        }
    }

    set_hook();
    transform_views();
}

void wayfire_touchswitch::handle_view_unmapped(wayfire_toplevel_view view)
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
        auto views     = get_views();
        double max_idx = views.empty() ? 0.0 : static_cast<double>(views.size() - 1);

        if (touch_x_offset >= max_idx)
        {
            touch_x_offset = max_idx;
        }

        layout_slots(views);
    }
}

bool wayfire_touchswitch::animation_running()
{
    for (auto& [view, view_data] : scale_data)
    {
        if (view_data.animation.scale_animation.running())
        {
            return true;
        }
    }

    return false;
}

void wayfire_touchswitch::deactivate()
{
    auto view = get_current_view();
    active = false;
    is_snapping = false;

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
        setup_view_transform(view, scale_data[view], 1.0, 1.0, 0.0, 0.0);
    }

    bool to_desktop = (static_cast<std::string>(background_action) == "showdesktop" && view == nullptr);
    for (auto& [some_view, view_data] : scale_data)
    {
        if (some_view == view)
        {
            continue;
        }

        if (view_data.was_minimized || minimize_others || to_desktop)
        {
            setup_view_transform(some_view, view_data, window_scale, window_scale,
                view_data.transformer->translation_x, 1000.0);
        } else
        {
            setup_view_transform(some_view, view_data, 1.0, 1.0, 0.0, 0.0);
        }
    }

    touchswitch_end_signal signal;
    output->emit(&signal);
}

void wayfire_touchswitch::set_hook()
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

void wayfire_touchswitch::unset_hook()
{
    if (!hook_set)
    {
        return;
    }

    output->render->rem_effect(&post_hook);
    output->render->rem_effect(&pre_hook);
    hook_set = false;
}

void wayfire_touchswitch::on_allow_scale_zoom_changed()
{
    if (!output->is_plugin_active(grab_interface.name))
    {
        return;
    }

    layout_slots(get_views());
}

void wayfire_touchswitch::on_render_pre_hook()
{
    this->transform_views();
}

void wayfire_touchswitch::on_render_post_hook()
{
    bool running = this->animation_running() || !this->is_velocity_zero() || is_snapping;

    if (!this->touch_held)
    {
        auto views = get_views();

        if (is_snapping)
        {
            if (swipe_direction == swipe_direction_option::HORIZONTAL)
            {
                double delta = target_snap_index - touch_x_offset;
                if (std::abs(delta) < 0.005)
                {
                    touch_x_offset = target_snap_index;
                    is_snapping    = false;
                    velocity = {0.0, 0.0};
                    swipe_direction = swipe_direction_option::UNDECIDED;
                } else
                {
                    touch_x_offset += delta * 0.15;
                }
            } else if (swipe_direction == swipe_direction_option::VERTICAL)
            {
                if (std::abs(touch_y_offset) < 1.0)
                {
                    touch_y_offset  = 0.0;
                    is_snapping     = false;
                    swipe_direction = swipe_direction_option::UNDECIDED;
                } else
                {
                    touch_y_offset -= touch_y_offset * 0.15;
                }
            }

            this->layout_slots(views);
        } else if (!this->is_velocity_zero())
        {
            this->velocity = {this->velocity.x * this->flick_motion, this->velocity.y * this->flick_motion};

            uint32_t current_time = wf::get_current_time();
            double count_msec     = static_cast<double>(current_time - this->flick_timestamp);
            this->flick_timestamp = current_time;

            wf::pointf_t movement = {this->velocity.x * count_msec, this->velocity.y * count_msec};

            double max_index = views.empty() ? 0.0 : static_cast<double>(views.size() - 1);
            if ((touch_x_offset < 0.0) || (touch_x_offset > max_index))
            {
                target_snap_index = touch_x_offset < 0.0 ? 0.0 : max_index;
                is_snapping = true;
            } else
            {
                this->handle_relative_motion(movement, current_time);
            }
        }
    }

    if (running)
    {
        this->output->render->schedule_redraw();
    }

    if (this->active || running)
    {
        return;
    }

    this->finalize();
}

void wayfire_touchswitch::on_view_mapped_event(wf::view_mapped_signal *ev)
{
    if (auto toplevel = wf::toplevel_cast(ev->view))
    {
        this->handle_new_view(toplevel);
    }
}

void wayfire_touchswitch::on_view_unmapped_event(wf::view_unmapped_signal *ev)
{
    if (auto toplevel = wf::toplevel_cast(ev->view))
    {
        this->check_focus_view(toplevel);
        this->handle_view_unmapped(toplevel);
    }
}

void wayfire_touchswitch::on_workspace_changed_event(wf::workspace_changed_signal *ev)
{
    this->layout_slots(this->get_views());
}

void wayfire_touchswitch::on_workarea_changed_event(wf::workarea_changed_signal *ev)
{
    this->layout_slots(this->get_views());
}

void wayfire_touchswitch::on_view_geometry_changed_event(wf::view_geometry_changed_signal *ev)
{
    auto views = this->get_views();
    if (views.empty())
    {
        this->deactivate();
        return;
    }

    this->layout_slots(std::move(views));
}

std::vector<wayfire_toplevel_view> wayfire_touchswitch::get_views()
{
    std::vector<wayfire_toplevel_view> views = output->wset()->get_views(wf::WSET_MAPPED_ONLY);

    std::sort(views.begin(), views.end(), [] (const auto& a, const auto& b)
    {
        return a->get_id() < b->get_id();
    });

    return views;
}

size_t wayfire_touchswitch::get_view_index(wayfire_toplevel_view view)
{
    auto views = this->get_views();
    auto it    = std::find(views.begin(), views.end(), view);
    wf::dassert(it != views.end(), "Chosen view not in list!");
    return static_cast<size_t>(std::distance(views.begin(), it));
}

void wayfire_touchswitch::setup_view_transform(wayfire_toplevel_view view,
    view_scale_data& view_data,
    double scale_x,
    double scale_y,
    double translation_x,
    double translation_y)
{
    /* If the user is actively dragging or flicking it then set it directly.
     * Animating after the drag feels like really bad input lag */
    if (touch_held || !is_velocity_zero())
    {
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

void wayfire_touchswitch::transform_views()
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

            view_data.transformer->scale_x = view_data.animation.scale_animation.scale_x;
            view_data.transformer->scale_y = view_data.animation.scale_animation.scale_y;
            view_data.transformer->translation_x = view_data.animation.scale_animation.translation_x;
            view_data.transformer->translation_y = view_data.animation.scale_animation.translation_y;

            view->get_transformed_node()->end_transform_update();
        }
    }
}

bool wayfire_touchswitch::should_scale_view(wayfire_toplevel_view view)
{
    auto views = get_views();

    return std::find(
        views.begin(),
        views.end(),
        wf::find_topmost_parent(view)) != views.end();
}

wayfire_toplevel_view wayfire_touchswitch::get_current_view()
{
    if (std::isnan(touch_x_offset))
    {
        return nullptr;
    }

    return get_view(get_current_idx());
}

size_t wayfire_touchswitch::get_current_idx()
{
    wf::dassert(!std::isnan(touch_x_offset), "X offset NaN");
    return static_cast<size_t>(std::roundl(std::max(0.0, touch_x_offset)));
}

wayfire_toplevel_view wayfire_touchswitch::get_view(size_t idx)
{
    auto views = get_views();

    if (idx >= views.size())
    {
        return nullptr;
    }

    return views.at(idx);
}

void wayfire_touchswitch::check_focus_view(wayfire_toplevel_view view)
{
    if (view == last_selected_view)
    {
        last_selected_view = nullptr;
    }
}
