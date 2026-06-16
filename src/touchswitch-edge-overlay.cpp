#include "touchswitch-edge-overlay.hpp"
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <filesystem>

namespace wf
{
namespace scene
{
touchswitch_edge_overlay_node_t::touchswitch_edge_overlay_node_t(wf::output_t *output_,
    edge_overlay_position position_) :
    node_t(false),
    output(output_), pos(position_),
    action_name{(position_ == edge_overlay_position::TOP) ? "touchswitch/pull_up" : "touchswitch/pull_down"}
{
    action_name.set_callback([this] () {update_icon();});
    update_icon();
}

touchswitch_edge_overlay_node_t::~touchswitch_edge_overlay_node_t()
{}

void touchswitch_edge_overlay_node_t::gen_render_instances(std::vector<render_instance_uptr>& instances,
    damage_callback push_damage, wf::output_t *output)
{
    instances.push_back(std::make_unique<touchswitch_edge_overlay_render_instance_t>(this, push_damage));
}

void touchswitch_edge_overlay_node_t::update_geometry(wf::geometry_t new_geom)
{
    auto old_bbox = geometry;
    this->geometry = new_geom;
    this->do_push_damage(old_bbox);
    this->do_push_damage(geometry);
}

void touchswitch_edge_overlay_node_t::set_visible(bool visible)
{
    if (this->overlay_shown == visible)
    {
        return;
    }

    update_icon();

    this->overlay_shown = visible;
    this->do_push_damage(geometry);
    if (output)
    {
        output->render->schedule_redraw();
    }
}

bool touchswitch_edge_overlay_node_t::exists(std::string path)
{
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) == 0)
    {
        if (S_ISDIR(statbuf.st_mode))
        {
            return (access(path.c_str(), R_OK | X_OK) == 0);
        }

        if (S_ISREG(statbuf.st_mode))
        {
            return (access(path.c_str(), R_OK) == 0);
        }
    }

    return false;
}

/* Helper function to get directories to search through, use presets if XDG_DATA_DIRS is missing */
std::string touchswitch_edge_overlay_node_t::get_xdg_application_dirs()
{
    char *xdg_data_dirs_raw = getenv("XDG_DATA_DIRS");
    if (xdg_data_dirs_raw == nullptr)
    {
        /* Fallback for missing XDG */
        std::string home = getenv("HOME");
        return home + "/.local/share/:/usr/local/share/:/usr/share/";
    }

    return xdg_data_dirs_raw;
}

std::string touchswitch_edge_overlay_node_t::get_icon_path_from_icon(std::string icon)
{
    /* Can't help here */
    if (icon.empty())
    {
        return "";
    }

    /* Full direct path, use it exclusively */
    if (icon.substr(0, 1) == "/")
    {
        return icon;
    }

    /*
     * TODO Consider mapping icon:icon_path and emptying only if theme changes.
     * Search
     */
    std::string versions[] =
    {"scalable", "symbolic", "512x512", "256x256", "128x128", "96x96", "64x64", "48x48", "32x32", "24x24"};
    std::string theme_string = static_cast<std::string>(theme_choice);
    std::string themes[]     = {theme_string, "hicolor", "locolor"};
    std::string extensions[] = {".svg", ".png"};
    std::string data_dirs    = get_xdg_application_dirs();
    std::string path_prefix;
    char delim = ':';

    namespace fs = std::filesystem;
    /* Expend every option in theme before moving along */
    for (const std::string& theme : themes)
    {
        for (const std::string& version : versions)
        {
            std::stringstream ss(data_dirs);

            while (getline(ss, path_prefix, delim))
            {
                fs::path base_search_dir = fs::path(path_prefix) / "icons" / theme / version;
                if (!fs::exists(base_search_dir) || !fs::is_directory(base_search_dir))
                {
                    continue;
                }

                /* Recursively look at every file within this specific theme/version branch */
                for (const auto& entry :
                     fs::recursive_directory_iterator(base_search_dir,
                         fs::directory_options::skip_permission_denied))
                {
                    if (entry.is_regular_file())
                    {
                        fs::path file_path = entry.path();

                        if (file_path.stem().compare(icon) == 0)
                        {
                            std::string ext = file_path.extension().string();
                            if ((ext == ".svg") || (ext == ".png"))
                            {
                                return file_path.string();
                            }
                        }
                    }
                }
            }
        }
    }

    /* Fallback to loose image */
    for (const std::string& extension : extensions)
    {
        std::stringstream ss(data_dirs);
        while (getline(ss, path_prefix, delim))
        {
            std::string icon_path = path_prefix + "/icons/" + icon + extension;
            if (exists(icon_path))
            {
                return icon_path;
            }
        }
    }

    return "";
}

std::string touchswitch_edge_overlay_node_t::get_icon_name_for_action(const std::string& action)
{
    if (action == "close")
    {
        return "window-close";
    }

    if (action == "minimize")
    {
        return "window-minimize";
    }

    return "";
}

cairo_surface_t*touchswitch_edge_overlay_node_t::get_surface_from_svg(std::string path, int target_size) const
{
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_size, target_size);
    auto surface_rsvg = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_size, target_size);
    auto cr = cairo_create(surface);
    auto cr_rsvg = cairo_create(surface_rsvg);

    GFile *file     = g_file_new_for_path(path.c_str());
    RsvgHandle *svg = rsvg_handle_new_from_gfile_sync(file, RSVG_HANDLE_FLAGS_NONE, NULL, NULL);
    RsvgRectangle rect{0, 0, (double)target_size, (double)target_size};
    rsvg_handle_render_document(svg, cr_rsvg, &rect, nullptr);
    cairo_destroy(cr_rsvg);

    cairo_translate(cr, (double)target_size / 2, (double)target_size / 2);
    cairo_scale(cr, 1.0, 1.0);
    cairo_translate(cr, -(double)target_size / 2, -(double)target_size / 2);

    cairo_set_source_surface(cr, surface_rsvg, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surface_rsvg);

    cairo_destroy(cr);
    g_object_unref(svg);
    g_object_unref(file);
    return surface;
}

cairo_surface_t*touchswitch_edge_overlay_node_t::get_surface_from_png(std::string path, int target_size)
{
    auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, target_size, target_size);
    auto cr = cairo_create(surface);

    auto image    = cairo_image_surface_create_from_png(path.c_str());
    double width  = cairo_image_surface_get_width(image);
    double height = cairo_image_surface_get_height(image);

    cairo_translate(cr, (double)target_size / 2, (double)target_size / 2);
    cairo_scale(cr, (double)target_size / width, -(double)target_size / height);
    cairo_translate(cr, -(double)target_size / 2, -(double)target_size / 2);

    cairo_set_source_surface(cr, image, (target_size - width) / 2, (target_size - height) / 2);
    cairo_paint(cr);
    cairo_surface_destroy(image);
    cairo_destroy(cr);
    return surface;
}

void touchswitch_edge_overlay_node_t::update_icon()
{
    int target_size = icon_size;
    std::string icon_name = get_icon_name_for_action(action_name);
    std::string path = get_icon_path_from_icon(icon_name);
    if (path.empty())
    {
        return;
    }

    auto surface = get_surface(path, target_size);
    if (!surface)
    {
        return;
    }

    if (is_active)
    {
        /* I have an icon and I want to paint it red */
        int width  = cairo_image_surface_get_width(surface);
        int height = cairo_image_surface_get_height(surface);
        /* Render a colour, shaped like the icon */
        cairo_surface_t *mask_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        cairo_t *mask_cr = cairo_create(mask_surface);
        cairo_set_source_surface(mask_cr, surface, 0, 0);
        cairo_paint(mask_cr);
        cairo_destroy(mask_cr);
        auto cr = cairo_create(surface);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 1.0);
        cairo_mask_surface(cr, mask_surface, 0, 0);
        cairo_surface_destroy(mask_surface);
        cairo_destroy(cr);
        /* This is probably a stupid way to do this, PRs welcome */
    }

    button_texture = wf::owned_texture_t{surface};
    cairo_surface_destroy(surface);
    this->do_push_damage(geometry);
}

cairo_surface_t*touchswitch_edge_overlay_node_t::get_surface(std::string path, int target_size)
{
    if (path.size() <= 4)
    {
        return nullptr;
    }

    std::string end = path.substr(path.size() - 4);
    if (end == ".png")
    {
        return get_surface_from_png(path, target_size);
    }

    if (end == ".svg")
    {
        return get_surface_from_svg(path, target_size);
    }

    return nullptr;
}
} // namespace scene
} // namespace wf
