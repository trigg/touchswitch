#pragma once

#include <string>
#include <memory>
#include <vector>
#include <wayfire/geometry.hpp>
#include <wayfire/output.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>

namespace wf
{
namespace scene
{
class touchswitch_notice_overlay_node_t : public node_t
{
  public:
    wf::geometry_t geometry{0, 0, 0, 0};
    wf::cairo_text_t text_renderer;
    wf::cairo_text_t::params text_params;
    bool overlay_shown = false;

    touchswitch_notice_overlay_node_t(float output_scale);
    ~touchswitch_notice_overlay_node_t();

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
        return "touchswitch-notice-overlay-node";
    }

    wf::geometry_t get_bounding_box() override
    {
        return geometry;
    }

    void update_geometry(wf::geometry_t new_geom);
    void set_visible(bool visible, wf::output_t *output);
};

class touchswitch_notice_render_instance_t : public render_instance_t
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged =
        [=] (node_damage_signal *ev)
    {
        push_to_parent(ev->region);
    };

    std::shared_ptr<touchswitch_notice_overlay_node_t> self;
    damage_callback push_to_parent;

  public:
    touchswitch_notice_render_instance_t(touchswitch_notice_overlay_node_t *self_,
        damage_callback push_dmg)
    {
        this->self = std::dynamic_pointer_cast<touchswitch_notice_overlay_node_t>(self_->shared_from_this());
        this->push_to_parent = push_dmg;
        self_->connect(&on_node_damaged);
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        if (!self->overlay_shown || !self->text_renderer.get_texture())
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
        if (!self->text_renderer.get_texture())
        {
            return;
        }

        data.pass->add_texture(self->text_renderer.get_texture(), data.target, self->geometry, data.damage);
    }
};
} // namespace scene

class touchswitch_notice_overlay_t
{
  public:
    wf::output_t *output = nullptr;
    std::shared_ptr<wf::scene::touchswitch_notice_overlay_node_t> node;

    touchswitch_notice_overlay_t();
    ~touchswitch_notice_overlay_t();

    void init(wf::output_t *output);
    void fini();

    void show();
    void hide();

    bool is_active()
    {
        return node && node->overlay_shown;
    }
};
} // namespace wf
