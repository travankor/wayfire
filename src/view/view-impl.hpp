#ifndef VIEW_IMPL_HPP
#define VIEW_IMPL_HPP

#include <nonstd/safe-list.hpp>
#include <view.hpp>
#include <opengl.hpp>

#include "surface-impl.hpp"

extern "C"
{
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
}

// for emit_map_*()
#include <compositor-view.hpp>
#include <compositor-surface.hpp>

struct wlr_seat;
namespace wf
{
struct view_transform_block_t : public noncopyable_t
{
    std::string plugin_name = "";
    std::unique_ptr<wf_view_transformer_t> transform;
    wf_framebuffer fb;

    view_transform_block_t();
    ~view_transform_block_t();
};

/** Private data used by the default view_interface_t implementation */
class view_interface_t::view_priv_impl
{
  public:
    wf::wl_idle_call idle_destruct;
    /**
     * A view is alive as long as it is possible for it to become mapped in the
     * future. For wlr views, this means that their role object hasn't been
     * destroyed and they still have the internal surface reference.
     */
    bool is_alive = true;

    bool keyboard_focus_enabled = true;

    /* For window restoration from maximized or fullscreen
     * -1 means that no such geometry has been stored. */
    wf_geometry last_windowed_geometry = {0, 0, -1, -1};
    wf_geometry last_maximized_geometry = {0, 0, -1, -1};

    /* those two point to the same object. Two fields are used to avoid
     * constant casting to and from types */
    surface_interface_t *decoration = NULL;
    wf_decorator_frame_t *frame = NULL;

    uint32_t edges = 0;
    int in_continuous_move = 0;
    int in_continuous_resize = 0;

    wf::safe_list_t<std::shared_ptr<view_transform_block_t>> transforms;

    struct offscreen_buffer_t : public wf_framebuffer
    {
        wf_region cached_damage;
        bool valid() { return this->fb != (uint32_t)-1; }
    } offscreen_buffer;
};

/**
 * Implementation of a view backed by a wlr_* shell struct.
 */
class wlr_view_t :
    public wlr_surface_base_t,
    public view_interface_t
{
  public:
    wlr_view_t();
    virtual ~wlr_view_t() {};

    /* Functions which are shell-independent */
    virtual void set_role(view_role_t new_role) override final;

    virtual std::string get_app_id() override final;
    virtual std::string get_title() override final;
    virtual wlr_box get_minimize_hint() override final;

    /* Subtract the opaque region of the surface from region, supposing
     * the surface is positioned at (x, y) */
    virtual void subtract_opaque(wf_region& region, int x, int y) override final;
    virtual void damage_surface_box(const wlr_box& box) override final;

    /* Functions which are further specialized for the different shells */
    virtual void move(int x, int y) override;
    virtual wf_geometry get_wm_geometry() override;
    virtual wf_geometry get_output_geometry() override;

    virtual wlr_surface *get_keyboard_focus_surface() override;

    virtual bool should_be_decorated() override;
    virtual void set_output(wf::output_t*) override;

    bool has_client_decoration = true;

  protected:
    std::string title, app_id;
    /** Used by view implementations when the app id changes */
    void handle_app_id_changed(std::string new_app_id);
    /** Used by view implementations when the title changes */
    void handle_title_changed(std::string new_title);
    /* Update the minimize hint */
    void handle_minimize_hint(wf::surface_interface_t *relative_to,
        const wlr_box& hint);

    /**
     * The bounding box of the view the last time it was rendered.
     *
     * This is used to damage the view when it is resized, because when a
     * transformer changes because the view is resized, we can't reliably
     * calculate the old view region to damage.
     */
    wf_geometry last_bounding_box {0, 0, 0, 0};

    /**
     * Adjust the view position when resizing the view so that its apparent
     * position doesn't change when resizing.
     */
    void adjust_anchored_edge(wf_size_t new_size);

    /** The output geometry of the view */
    wf_geometry geometry {100, 100, 0, 0};

    /** Set the view position and optionally send the geometry changed signal
     * @param old_geometry The geometry to report as previous, in case the
     * signal is sent. */
    virtual void set_position(int x, int y, wf_geometry old_geometry,
        bool send_geometry_signal);
    /** Update the view size to the actual dimensions of its surface */
    virtual void update_size();

    virtual void commit() override;
    virtual void map(wlr_surface *surface) override;
    virtual void unmap() override;

    /* Handle the destruction of the underlying wlroots object */
    virtual void destroy();

    /*
     * wlr_foreign_toplevel_v1 implementation functions
     */

    /* The toplevel is created by the individual view's mapping functions,
     * i.e in xdg-shell, xwayland, etc.
     * The handle is automatically destroyed when the view is unmapped */
    wlr_foreign_toplevel_handle_v1 *toplevel_handle = NULL;

    wf::wl_listener_wrapper toplevel_handle_v1_maximize_request,
        toplevel_handle_v1_activate_request,
        toplevel_handle_v1_minimize_request,
        toplevel_handle_v1_set_rectangle_request,
        toplevel_handle_v1_close_request;

    wlr_box minimize_hint;

    /* Create/destroy the toplevel_handle */
    virtual void create_toplevel();
    virtual void destroy_toplevel();

    /* The following are no-op if toplevel_handle == NULL */
    virtual void toplevel_send_title();
    virtual void toplevel_send_app_id();
    virtual void toplevel_send_state();
    virtual void toplevel_update_output(wf::output_t *output, bool enter);

    virtual void desktop_state_updated() override;

  public:
    /* Just pass to the default wlr surface implementation */
    virtual bool is_mapped() const override { return _is_mapped(); }
    virtual wf_size_t get_size() const override { return _get_size(); }
    virtual void simple_render(const wf_framebuffer& fb, int x, int y,
        const wf_region& damage) override {
        _simple_render(fb, x, y, damage);
    }
};

/** Emit the map signal for the given view */
void emit_view_map_signal(wayfire_view view, bool has_position);

wf::surface_interface_t* wf_surface_from_void(void *handle);
wf::view_interface_t* wf_view_from_void(void *handle);

void init_xdg_shell();
void init_xwayland();
void init_layer_shell();

void xwayland_set_seat(wlr_seat *seat);
int xwayland_get_display();

void init_desktop_apis(wayfire_config *conf);
}

#endif /* end of include guard: VIEW_IMPL_HPP */
