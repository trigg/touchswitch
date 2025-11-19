/* definition of filters for touchswitch plugin and activator */

#ifndef TOUCHSWITCH_SIGNALS_H
#define TOUCHSWITCH_SIGNALS_H

#include <wayfire/object.hpp>
#include <wayfire/view.hpp>
#include <vector>
#include <algorithm>

/**
 * name: touchswitch-end
 * on: output
 * when: When touchswitch ended / is deactivated.
 * argument: unused
 */
struct touchswitch_end_signal
{};

/**
 * name: touchswitch-update
 * on: output
 * when: A plugin can emit this signal to request touchswitch to be updated. This is
 *   intended for plugins that filter the views to request an update when
 *   the filter is changed. It is a no-op if touchswitch is not currently running.
 * argument: unused
 */
struct touchswitch_update_signal
{};

/**
 * name: touchswitch-transformer-added
 * on: output
 * when: This signal is emitted when touchswitch adds a transformer to a view, so
 *   plugins extending its functionality can add their overlays to it.
 * argument: pointer to the newly added transformer
 */
struct touchswitch_transformer_added_signal
{
    wayfire_toplevel_view view;
};

struct touchswitch_transformer_removed_signal
{
    wayfire_toplevel_view view;
};

#endif
