#include "touchswitch-notice-overlay.hpp"
#include <algorithm>
#include <cmath>
#include <wayfire/render-manager.hpp>
#include <wayfire/scene-operations.hpp>
#include <wayfire/workarea.hpp>

namespace wf
{
namespace scene
{
touchswitch_notice_overlay_node_t::touchswitch_notice_overlay_node_t(float output_scale) : node_t(false)
{
    text_params.font_size    = 16;
    text_params.bg_color     = {0.05f, 0.05f, 0.05f, 0.85f};
    text_params.text_color   = {1.0f, 1.0f, 1.0f, 0.95f};
    text_params.exact_size   = false;
    text_params.output_scale = output_scale;

    text_renderer.render_text("No Active Windows", text_params);
}

touchswitch_notice_overlay_node_t::~touchswitch_notice_overlay_node_t()
{}

void touchswitch_notice_overlay_node_t::gen_render_instances(
    std::vector<render_instance_uptr>& instances,
    damage_callback push_damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<touchswitch_notice_render_instance_t>(this, push_damage));
}

void touchswitch_notice_overlay_node_t::update_geometry(wf::geometry_t new_geom)
{
    auto old_bbox = geometry;
    this->geometry = new_geom;
    this->do_push_damage(old_bbox);
    this->do_push_damage(geometry);
}

void touchswitch_notice_overlay_node_t::set_visible(bool visible, wf::output_t *output)
{
    if (this->overlay_shown == visible)
    {
        return;
    }

    this->overlay_shown = visible;
    this->do_push_damage(geometry);
    if (output)
    {
        output->render->schedule_redraw();
    }
}
} // namespace scene


touchswitch_notice_overlay_t::touchswitch_notice_overlay_t()
{}

touchswitch_notice_overlay_t::~touchswitch_notice_overlay_t()
{
    fini();
}

void touchswitch_notice_overlay_t::init(wf::output_t *output_)
{
    this->output = output_;

    node = std::make_shared<wf::scene::touchswitch_notice_overlay_node_t>(output_->handle->scale);

    auto overlay_layer = output_->node_for_layer(wf::scene::layer::OVERLAY);
    if (overlay_layer)
    {
        wf::scene::add_front(overlay_layer, node);
    }
}

void touchswitch_notice_overlay_t::fini()
{
    if (node)
    {
        wf::scene::remove_child(node);
    }
}

void touchswitch_notice_overlay_t::show()
{
    if (!node || !output)
    {
        return;
    }

    auto workarea  = output->workarea->get_workarea();
    auto text_size = node->text_renderer.get_size();

    wf::geometry_t message_geometry = {
        workarea.x + (workarea.width / 2) - (text_size.width / 2),
        workarea.y + (workarea.height / 2) - (text_size.height / 2),
        text_size.width,
        text_size.height
    };

    node->update_geometry(message_geometry);
    node->set_visible(true, output);
}

void touchswitch_notice_overlay_t::hide()
{
    if (node)
    {
        node->set_visible(false, output);
    }
}
} // namespace wf
