#include "touchswitch.hpp"
#include "touchswitch-icon-overlay.hpp"
#include "wayfire/core.hpp"
#include "wayfire/debug.hpp"
#include "wayfire/geometry.hpp"
#include "wayfire/output.hpp"
#include "wayfire/plugins/touchswitch-signal.hpp"
#include "wayfire/scene-operations.hpp"
#include "wayfire/signal-definitions.hpp"
#include "wayfire/view-helpers.hpp"
#include "wayfire/view-transform.hpp"
#include "INIReader.h"

#include <memory>
#include <sys/stat.h>
#include <librsvg/rsvg.h>
#include <wayfire/opengl.hpp>
#include <wayfire/util/log.hpp>
#include <wayfire/plugins/common/cairo-util.hpp>
#include <wayfire/scene.hpp>
#include <wayfire/scene-render.hpp>
#include <wayfire/render-manager.hpp>
static constexpr const char *TOUCHSWITCH_TRANSFORMER = "touchswitch";


/**
 * Class storing an overlay with a view's icon, only stored for parent views.
 */
struct view_icon_texture_t : public wf::custom_data_t
{
    std::string cached_app_id="";
    wayfire_toplevel_view view;
    wayfire_toplevel_view dialog; /* the texture should be rendered on top of this dialog */
    wf::owned_texture_t button_texture;
    wf::option_wrapper_t<int> icon_size{"touchswitch/icon_size"};
    wf::option_wrapper_t<std::string> theme_choice{"touchswitch/icon_theme"};

    /* Helper function to check a file exists */
    bool exists( std::string path ) 
    {
        struct stat statbuf;
        if ( stat( path.c_str(), &statbuf ) == 0 ) 
        {
            if ( S_ISDIR( statbuf.st_mode ) ) 
            {
                return (access( path.c_str(), R_OK | X_OK ) == 0);
            } else if ( S_ISREG( statbuf.st_mode ) ) 
            {
                return (access( path.c_str(), R_OK ) == 0);
            } else 
            {
                return false;
            }
        } else 
        {
            return false;
        }
    }

    /* Helper function to get directories to search through, use presets if XDG_DATA_DIRS is missing */
    std::string get_xdg_application_dirs()
    {
        char* xdg_data_dirs_raw = getenv("XDG_DATA_DIRS");
        if(xdg_data_dirs_raw == nullptr)
        {
            /* Fallback for missing XDG */
            std::string home = getenv("HOME");
            return home+"/.local/share/:/usr/local/share/:/usr/share/";
        } 
        return xdg_data_dirs_raw;
    }

    /* Find an icon path from a .desktop file for this appid */
    std::string get_icon_path_for_appid(std::string appid)
    {
        std::string data_dirs = get_xdg_application_dirs();
        std::stringstream ss(data_dirs);
        std::string path_prefix;
        char delim = ':';
        while (getline(ss, path_prefix, delim))
        {
            std::string desktop_path = path_prefix+ "/applications/" + appid + ".desktop";
            if (exists(desktop_path))
            {
                INIReader desktop( desktop_path );
                std::string icon_name = desktop.Get("Desktop Entry", "Icon", "");
                std::string icon_path = get_icon_path_from_icon(icon_name);
                if(icon_path!=""){
                    return icon_path;
                }
            }
        }
        
        return "";
    }

    std::string get_icon_path_from_icon(std::string icon)
    {
        /* Can't help here */
        if (icon == "")
        {
            return "";
        }
        /* Full direct path, use it exclusively */
        if (icon.substr(0,1) == "/")
        {
            return icon;
        }
        /* TODO Consider mapping icon:icon_path and emptying only if theme changes. */
        /* Search */
        std::string versions[] = {"scalable", "128x128", "96x96", "64x64", "48x48", "32x32"};
        std::string themes[] = {theme_choice, "hicolor", "locolor"};
        std::string extensions[] = {".svg", ".png"};
        std::string data_dirs = get_xdg_application_dirs();
        std::string path_prefix;
        char delim = ':';

        /* Expend every option in theme before moving along */
        for(std::string theme : themes)
        {
            for(std::string version: versions)
            {
                for(std::string extension: extensions)
                {
                    std::stringstream ss(data_dirs);

                    while (getline(ss, path_prefix, delim))
                    {
                        std::string icon_path = path_prefix+ "/icons/" + theme+ "/" + version +"/apps/" + icon + extension;
                        if(exists(icon_path)){
                            return icon_path;
                        }
                    }
                }
            }
        }
        /* Fallback to loose image */
        for(std::string extension: extensions)
        {
            std::stringstream ss(data_dirs);

            while (getline(ss, path_prefix, delim))
            {
                std::string icon_path = path_prefix+ "/icons/" + icon + extension;
                if(exists(icon_path)){
                    return icon_path;
                }
            }
        }
        return "";
    }

    cairo_surface_t * get_surface_from_svg(std::string path) const {
	    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, icon_size, icon_size);
	    auto surface_rsvg = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, icon_size, icon_size);
	    auto cr = cairo_create(surface);
	    auto cr_rsvg = cairo_create(surface_rsvg);

	    GFile *file = g_file_new_for_path(path.c_str());
	    RsvgHandle *svg = rsvg_handle_new_from_gfile_sync(file, RSVG_HANDLE_FLAGS_NONE,
	                                                  NULL, NULL);
	    RsvgRectangle rect { 0, 0, (double)icon_size, (double)icon_size };
	    rsvg_handle_render_document(svg, cr_rsvg, &rect, nullptr);
	    cairo_destroy(cr_rsvg);

        cairo_translate(cr, (double)icon_size / 2, (double)icon_size / 2);
        cairo_scale(cr, 1.0, 1.0);
        cairo_translate(cr, -(double)icon_size / 2, -(double)icon_size / 2);

        cairo_set_source_surface(cr, surface_rsvg, 0, 0);
        cairo_paint(cr);
	    cairo_surface_destroy(surface_rsvg);
    
	    cairo_destroy(cr);

	    g_object_unref(svg);
	    g_object_unref(file);

	    return surface;
    }

    cairo_surface_t * get_surface_from_png(std::string path)
    {
        auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, icon_size, icon_size);
        auto cr = cairo_create(surface);

        auto image = cairo_image_surface_create_from_png(path.c_str());
        double width  = cairo_image_surface_get_width(image);
        double height = cairo_image_surface_get_height(image);

        cairo_translate(cr, (double)icon_size / 2, (double)icon_size / 2);
        cairo_scale(cr, (double)icon_size / width, -(double)icon_size / height);
        cairo_translate(cr, -(double)icon_size / 2, -(double)icon_size / 2);

        cairo_set_source_surface(cr, image, (icon_size - width) / 2, (icon_size - height) / 2);
        cairo_paint(cr);
        cairo_surface_destroy(image);
        cairo_destroy(cr);

        return surface; 
    }

    cairo_surface_t * get_surface(std::string path)
    {
        /* Skip if too short */
        if (path.size() <= 4)
        {
            return nullptr;
        }
        std::string end = path.substr(path.size()-4);
        if (end == ".png")
        {
            return get_surface_from_png(path);
        }
        if (end == ".svg")
        {
            return get_surface_from_svg(path);
        }
        LOGE("Could not get surface from file : ",path);
        return nullptr;
    }


    void update_overlay_texture(std::string app_id)
    {
        cached_app_id = app_id;
        update_overlay_texture();
    }

    void update_overlay_texture()
    {
        if (cached_app_id=="")
        {
            LOGE("Cached App Id blank");
            return;
        }
        auto icon_path = get_icon_path_for_appid(cached_app_id);
        if(icon_path=="")
        {
            LOGE("Icon Path blank : ",cached_app_id);
            return;
        }
        auto surface = get_surface(icon_path);
        if(surface==nullptr)
        {
            LOGE("Error getting surface : ",icon_path);
            return;
        }
        button_texture = wf::owned_texture_t{surface};
        cairo_surface_destroy(surface);
    }


    wf::signal::connection_t<wf::view_app_id_changed_signal> view_changed_icon =
        [=] (wf::view_app_id_changed_signal *ev)
    {
        LOGI("Got app id : ",ev->view->get_app_id());
        update_overlay_texture(ev->view->get_app_id());
    };

    view_icon_texture_t(wayfire_toplevel_view v, float output_scale) : view(v)
    {
        view->connect(&view_changed_icon);
        cached_app_id = view->get_app_id();
        update_overlay_texture();
    }
};

namespace wf
{
namespace scene
{
class touchswitch_icon_overlay_node_t : public node_t
{
  public:
    enum class position
    {
        ABOVE,
        TOP,
        CENTER,
        BOTTOM,
        BELOW,
    };

    /* save the transformed view, since we need it in the destructor */
    wayfire_toplevel_view view;
    /* the position on the screen we currently render to */
    wf::geometry_t geometry{0, 0, 0, 0};
    touchswitch_show_icon_t& parent;
    wf::option_wrapper_t<int> icon_size{"touchswitch/icon_size"};
    position pos = position::CENTER;
    /* Whether we are currently rendering the overlay by this transformer.
     * Set in the pre-render hook and used in the render function. */
    bool overlay_shown = false;
    wf::wl_idle_call idle_update_icon;

  private:
    /**
     * Gets the overlay texture stored with the given view.
     */
    view_icon_texture_t& get_overlay_texture(wayfire_toplevel_view view)
    {
        auto data = view->get_data<view_icon_texture_t>();
        if (!data)
        {
            auto new_data = new view_icon_texture_t(view, parent.output->handle->scale);
            view->store_data<view_icon_texture_t>(std::unique_ptr<view_icon_texture_t>(
                new_data));
            return *new_data;
        }

        return *data.get();
    }

    wf::geometry_t get_scaled_bbox(wayfire_toplevel_view v)
    {
        auto tr = v->get_transformed_node()->
            get_transformer<wf::scene::view_2d_transformer_t>(TOUCHSWITCH_TRANSFORMER);
        if (tr)
        {
            auto wm_geometry = v->get_geometry();
            return get_bbox_for_node(tr, wm_geometry);
        }

        return v->get_bounding_box();
    }

    
    /**
     * Check if this view should display an overlay.
     */
    bool should_have_overlay()
    {
        if (! this->parent.show_view_icon_overlay)
        {
            return false;
        }

        auto parent = find_topmost_parent(view);

        while (!parent->children.empty())
        {
            parent = parent->children[0];
        }

        return view == parent;
    }

    void update_app_id()
    {
        if (!should_have_overlay())
        {
            if (overlay_shown)
            {
                this->do_push_damage(get_bounding_box());
            }

            overlay_shown = false;
            return;
        }

        auto old_bbox = get_bounding_box();

        overlay_shown = true;
        wf::dimensions_t box = {icon_size, icon_size};
        auto output_scale = parent.output->handle->scale;

        auto& tex = get_overlay_texture(find_topmost_parent(view));

        geometry.width  = icon_size;
        geometry.height = icon_size;

        auto bbox = get_scaled_bbox(view);
        geometry.x = bbox.x + bbox.width / 2 - geometry.width / 2;
        switch (pos)
        {
          case position::ABOVE:
            geometry.y = bbox.y - geometry.height;
            break;

          case position::TOP:
            geometry.y = bbox.y;
            break;

          case position::CENTER:
            geometry.y = bbox.y + bbox.height / 2 - geometry.height / 2;
            break;

          case position::BOTTOM:
            geometry.y = bbox.y + bbox.height - geometry.height / 2;
            break;

          case position::BELOW:
            geometry.y = bbox.y + bbox.height;
        }

        this->do_push_damage(old_bbox);
        this->do_push_damage(get_bounding_box());
    }

  public:
    touchswitch_icon_overlay_node_t(
        wayfire_toplevel_view view_, position pos_, touchswitch_show_icon_t& parent_) :
        node_t(false), view(view_), parent(parent_), pos(pos_)
    {
        auto parent = find_topmost_parent(view);
        auto& icon = get_overlay_texture(parent);

        idle_update_icon.set_callback([=] () { update_app_id(); });
        idle_update_icon.run_once();
    }

    ~touchswitch_icon_overlay_node_t()
    {
        view->erase_data<view_icon_texture_t>();
    }

    void gen_render_instances(
        std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *output) override;

    void do_push_damage(wf::region_t updated_region)
    {
        node_damage_signal ev;
        ev.region = updated_region;
        this->emit(&ev);
    }

    std::string stringify() const override
    {
        return "touchswitch-icon-overlay";
    }

    wf::geometry_t get_bounding_box() override
    {
        return geometry;
    }
};

class touchswitch_icon_overlay_render_instance_t : public render_instance_t
{
    wf::signal::connection_t<node_damage_signal> on_node_damaged =
        [=] (node_damage_signal *ev)
    {
        push_to_parent(ev->region);
    };

    std::shared_ptr<touchswitch_icon_overlay_node_t> self;
    damage_callback push_to_parent;

  public:
    touchswitch_icon_overlay_render_instance_t(touchswitch_icon_overlay_node_t *self,
        damage_callback push_dmg)
    {
        this->self = std::dynamic_pointer_cast<touchswitch_icon_overlay_node_t>(self->shared_from_this());
        this->push_to_parent = push_dmg;
        self->connect(&on_node_damaged);
    }

    void schedule_instructions(std::vector<render_instruction_t>& instructions,
        const wf::render_target_t& target, wf::region_t& damage) override
    {
        if (!self->overlay_shown || !self->view->has_data<view_icon_texture_t>())
        {
            return;
        }

        /* We want to render ourselves only, the node does not have children */
        instructions.push_back(render_instruction_t{
                    .instance = this,
                    .target   = target,
                    .damage   = damage & self->get_bounding_box(),
                });
    }

    void render(const wf::scene::render_instruction_t& data) override
    {
        auto& icon = *self->view->get_data<view_icon_texture_t>();
        if (! icon.button_texture.get_texture().texture)
        {
            LOGE("Null texture");
            return;
        }
        data.pass->add_texture(icon.button_texture.get_texture(), data.target, self->geometry, data.damage);
        self->idle_update_icon.run_once();
    }
};

void touchswitch_icon_overlay_node_t::gen_render_instances(
    std::vector<render_instance_uptr>& instances,
    damage_callback push_damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<touchswitch_icon_overlay_render_instance_t>(
        this, push_damage));
}
}
}

touchswitch_show_icon_t::touchswitch_show_icon_t() :
    touchswitch_update{[this] (auto)
    {
        update_icon_overlay_opt();
    }},
    touchswitch_end{[this] (auto)
    {
        show_view_icon_overlay = false;
    }
},

add_icon_overlay{[this] (touchswitch_transformer_added_signal *signal)
    {
        using namespace wf::scene;

        const std::string& pos_opt = icon_position;
        touchswitch_icon_overlay_node_t::position pos = touchswitch_icon_overlay_node_t::position::CENTER;
        if (pos_opt == "top")
        {
            pos = touchswitch_icon_overlay_node_t::position::TOP;
        } else if (pos_opt == "bottom")
        {
            pos = touchswitch_icon_overlay_node_t::position::BOTTOM;
        } else if (pos_opt == "below")
        {
            pos = touchswitch_icon_overlay_node_t::position::BELOW;
        } else if (pos_opt == "above")
        {
            pos = touchswitch_icon_overlay_node_t::position::ABOVE;
        }

        auto tr     = signal->view->get_transformed_node()->get_transformer(TOUCHSWITCH_TRANSFORMER);
        auto parent = std::dynamic_pointer_cast<wf::scene::floating_inner_node_t>(
            tr->parent()->shared_from_this());

        auto node = std::make_shared<touchswitch_icon_overlay_node_t>(signal->view, pos, *this);
        wf::scene::add_front(parent, node);
        wf::scene::damage_node(parent, parent->get_bounding_box());
    }
},

rem_icon_overlay{[] (touchswitch_transformer_removed_signal *signal)
    {
        using namespace wf::scene;
        node_t *tr = signal->view->get_transformed_node()->get_transformer(TOUCHSWITCH_TRANSFORMER).get();

        while (tr)
        {
            for (auto& ch : tr->get_children())
            {
                if (dynamic_cast<touchswitch_icon_overlay_node_t*>(ch.get()))
                {
                    remove_child(ch);
                    break;
                }
            }

            tr = tr->parent();
        }
    }
}
{}

void touchswitch_show_icon_t::init(wf::output_t *output)
{
    this->output = output;
    output->connect(&add_icon_overlay);
    output->connect(&rem_icon_overlay);
    output->connect(&touchswitch_end);
    output->connect(&touchswitch_update);

}

void touchswitch_show_icon_t::fini()
{
}

void touchswitch_show_icon_t::update_icon_overlay_opt()
{
    show_view_icon_overlay = show_view_icon_overlay_opt;
}
