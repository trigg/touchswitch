#pragma once

#include <string>
#include <memory>
#include <wayfire/geometry.hpp>
#include <wayfire/output.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/option-wrapper.hpp>
#include <cairo/cairo.h>
#include <gio/gio.h>
#include <librsvg/rsvg.h>
#include <wayfire/plugins/common/cairo-util.hpp>


namespace wf
{
namespace scene
{
enum class edge_overlay_position
{
    TOP,
    BOTTOM,
};

class touchswitch_edge_overlay_node_t : public node_t
{
  public:
    bool is_active = false;
    wf::output_t *output = nullptr;
    wf::geometry_t geometry{0, 0, 0, 0};
    wf::owned_texture_t button_texture;
    edge_overlay_position pos;
    bool overlay_shown = false;

    wf::option_wrapper_t<int> icon_size{"touchswitch/icon_size"};
    wf::option_wrapper_t<std::string> theme_choice{"touchswitch/icon_theme"};

    touchswitch_edge_overlay_node_t(wf::output_t *output_, edge_overlay_position position_);
    ~touchswitch_edge_overlay_node_t();

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *output) override;

    void do_push_damage(wf::region_t updated_region)
    {
        node_damage_signal ev;
        ev.region = updated_region;
        this->emit(&ev);
    }

    std::string stringify() const override
    {
        return "touchswitch-edge-overlay-node";
    }

    wf::geometry_t get_bounding_box() override
    {
        return geometry;
    }

    void update_icon();

    void update_geometry(wf::geometry_t new_geom);
    void set_visible(bool visible);

    wf::option_wrapper_t<std::string> action_name;

  private:
    bool exists(std::string path);
    std::string get_icon_path_from_icon(std::string icon);
    std::string get_icon_name_for_action(const std::string& action);
    std::string get_xdg_application_dirs();
    cairo_surface_t *get_surface(std::string path, int target_size);
    cairo_surface_t *get_surface_from_svg(std::string path, int target_size) const;
    cairo_surface_t *get_surface_from_png(std::string path, int target_size);
};

class touchswitch_edge_overlay_render_instance_t : public render_instance_t
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged = [=] (node_damage_signal *ev)
    {
        push_to_parent(ev->region);
    };
    std::shared_ptr<touchswitch_edge_overlay_node_t> self;
    damage_callback push_to_parent;

  public:
    touchswitch_edge_overlay_render_instance_t(touchswitch_edge_overlay_node_t *self_,
        damage_callback push_dmg)
    {
        this->self = std::dynamic_pointer_cast<touchswitch_edge_overlay_node_t>(self_->shared_from_this());
        this->push_to_parent = push_dmg;
        self_->connect(&on_node_damaged);
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        if (!self->overlay_shown || !self->button_texture.get_texture())
        {
            return;
        }

        instructions.push_back(render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = damage & self->get_bounding_box(),
                });
    }

    void render(const wf::scene::render_instruction_t& data) override
    {
        if (!self->button_texture.get_texture())
        {
            return;
        }

        data.pass->add_texture(self->button_texture.get_texture(), data.target, self->geometry, data.damage);
    }
};
} // namespace scene
} // namespace wf
