#include <plugin.hpp>
#include <output.hpp>
#include <core.hpp>
#include <debug.hpp>
#include <view.hpp>
#include <view-transform.hpp>
#include <render-manager.hpp>
#include <workspace-stream.hpp>
#include <workspace-manager.hpp>
#include <signal-definitions.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <animation.hpp>

#include <cmath>
#include <utility>
#include <animation.hpp>

#include "vswipe-processing.hpp"

class vswipe : public wf::plugin_interface_t
{
    private:
        struct {
            /* When the workspace is set to (-1, -1), means no such workspace */
            wf::workspace_stream_t prev, curr, next;
        } streams;

        enum swipe_direction_t
        {
            HORIZONTAL = 0,
            VERTICAL,
            UNKNOWN,
        };

        struct {
            bool swiping = false;
            swipe_direction_t direction;

            wf_pointf initial_deltas;

            double delta_sum = 0.0;
            double gap = 0.0;

            double delta_prev = 0.0;
            double delta_last = 0.0;

            int vx = 0;
            int vy = 0;
            int vw = 0;
            int vh = 0;
        } state;

        wf::render_hook_t renderer;

        wf_duration duration;
        wf_transition transition;

        wf_option animation_duration;
        wf_option background_color;
        wf_option enable;
        wf_option ignore_cancel;
        wf_option fingers;
        wf_option gap;
        wf_option threshold;
        wf_option delta_threshold;
        wf_option speed_factor;
        wf_option speed_cap;

    public:

    void init(wayfire_config *config)
    {
        grab_interface->name = "vswipe";
        grab_interface->capabilities = wf::CAPABILITY_MANAGE_COMPOSITOR;
        grab_interface->callbacks.cancel = [=] () { finalize_and_exit(); };

        auto section = config->get_section("vswipe");

        animation_duration = section->get_option("duration", "180");
        duration = wf_duration(animation_duration);

        enable = section->get_option("enable", "1");
        ignore_cancel = section->get_option("ignore_cancel", "1");
        fingers = section->get_option("fingers", "4");
        gap = section->get_option("gap", "32");
        threshold = section->get_option("threshold", "0.35");
        delta_threshold = section->get_option("delta_threshold", "24");
        speed_factor = section->get_option("speed_factor", "256");
        speed_cap = section->get_option("speed_cap", "0.05");
        wf::get_core().connect_signal("pointer-swipe-begin", &on_swipe_begin);
        wf::get_core().connect_signal("pointer-swipe-update", &on_swipe_update);
        wf::get_core().connect_signal("pointer-swipe-end", &on_swipe_end);

        background_color = section->get_option("background", "0 0 0 1");

        renderer = [=] (const wf_framebuffer& buffer) { render(buffer); };
    }

    /**
     * Get the translation matrix for a workspace at the given offset.
     * Depends on the swipe direction
     */
    glm::mat4 get_translation(double offset)
    {
        switch (state.direction)
        {
            case UNKNOWN:
                return glm::mat4(1.0);
            case HORIZONTAL:
                return glm::translate(glm::mat4(1.0),
                    glm::vec3(offset, 0.0, 0.0));
            case VERTICAL:
                return glm::translate(glm::mat4(1.0),
                    glm::vec3(0.0, -offset, 0.0));
        }

        assert(false); // not reached
    }

    void render(const wf_framebuffer &fb)
    {
        if (!duration.running() && !state.swiping)
            finalize_and_exit();

        if (duration.running())
            state.delta_sum = duration.progress(transition);

        update_stream(streams.prev);
        update_stream(streams.curr);
        update_stream(streams.next);

        OpenGL::render_begin(fb);
        OpenGL::clear(background_color->as_cached_color());
        fb.scissor(fb.framebuffer_box_from_geometry_box(fb.geometry));

        gl_geometry out_geometry = {
            .x1 = -1,
            .y1 = 1,
            .x2 = 1,
            .y2 = -1,
        };

        auto swipe = get_translation(state.delta_sum * 2);
        if (streams.prev.ws.x >= 0)
        {
            auto prev = get_translation(-2.0 - state.gap * 2.0);
            OpenGL::render_transformed_texture(streams.prev.buffer.tex,
                out_geometry, {}, fb.transform * prev * swipe);
        }

        OpenGL::render_transformed_texture(streams.curr.buffer.tex,
            out_geometry, {}, fb.transform * swipe);

        if (streams.next.ws.x >= 0)
        {
            auto next = get_translation(2.0 + state.gap * 2.0);
            OpenGL::render_transformed_texture(streams.next.buffer.tex,
                out_geometry, {}, fb.transform * next * swipe);
        }

        GL_CALL(glUseProgram(0));
        OpenGL::render_end();
    }

    inline void update_stream(wf::workspace_stream_t& s)
    {
        if (s.ws.x < 0 || s.ws.y < 0)
            return;

        if (!s.running)
            output->render->workspace_stream_start(s);
        else
            output->render->workspace_stream_update(s);
    }

    wf::signal_callback_t on_swipe_begin = [=] (wf::signal_data_t *data)
    {
        if (!enable->as_cached_int())
            return;

        if (output->is_plugin_active(grab_interface->name))
            return;

        auto ev = static_cast<wf::swipe_begin_signal*> (data)->ev;
        if (static_cast<int>(ev->fingers) != fingers->as_cached_int())
            return;

        // Plugins are per output, swipes are global, so we need to handle
        // the swipe only when the cursor is on *our* (plugin instance's) output
        if (!(output->get_relative_geometry() & output->get_cursor_position()))
            return;

        wf::get_core().focus_output(output);

        if (!output->activate_plugin(grab_interface))
            return;

        grab_interface->grab();

        state.swiping = true;
        state.direction = UNKNOWN;
        state.initial_deltas = {0.0, 0.0};
        state.delta_sum = 0;
        state.delta_last = 0;
        state.delta_prev = 0;

        state.gap = gap->as_cached_double() / output->get_screen_size().width;

        // We switch the actual workspace before the finishing animation,
        // so the rendering of the animation cannot dynamically query current
        // workspace again, so it's stored here
        auto grid = output->workspace->get_workspace_grid_size();
        auto ws = output->workspace->get_current_workspace();
        state.vw = grid.width;
        state.vh = grid.height;
        state.vx = ws.x;
        state.vy = ws.y;

        /* Invalid in the beginning, because we want a few swipe events to
         * determine whether swipe is horizontal or vertical */
        streams.prev.ws = {-1, -1};
        streams.next.ws = {-1, -1};
        streams.curr.ws = wf_point {ws.x, ws.y};

        output->render->set_renderer(renderer);
        output->render->damage_whole();
    };

    void start_swipe(swipe_direction_t direction)
    {
        assert(direction != UNKNOWN);
        state.direction = direction;

        auto ws = output->workspace->get_current_workspace();
        auto grid = output->workspace->get_workspace_grid_size();

        if (direction == HORIZONTAL)
        {
            if (ws.x > 0)
                streams.prev.ws = wf_point{ws.x - 1, ws.y};
            if (ws.x < grid.width - 1)
                streams.next.ws = wf_point{ws.x + 1, ws.y};
        } else //if (direction == VERTICAL)
        {
            if (ws.y > 0)
                streams.prev.ws = wf_point{ws.x, ws.y - 1};
            if (ws.y < grid.height - 1)
                streams.next.ws = wf_point{ws.x, ws.y + 1};
        }
    }

    wf::signal_callback_t on_swipe_update = [=] (wf::signal_data_t *data)
    {
        if (!state.swiping)
            return;

        auto ev = static_cast<wf::swipe_update_signal*> (data)->ev;

        if (state.direction == UNKNOWN)
        {
            auto grid = output->workspace->get_workspace_grid_size();

            // XXX: how to determine this??
            static constexpr double initial_direction_threshold = 0.05;
            state.initial_deltas.x +=
                std::abs(ev->dx) / speed_factor->as_double();
            state.initial_deltas.y +=
                std::abs(ev->dy) / speed_factor->as_double();

            bool horizontal =
                state.initial_deltas.x > initial_direction_threshold;
            bool vertical =
                state.initial_deltas.y > initial_direction_threshold;

            horizontal &= state.initial_deltas.x > state.initial_deltas.y;
            vertical &= state.initial_deltas.y > state.initial_deltas.x;

            if (horizontal || grid.height == 1)
            {
                start_swipe(HORIZONTAL);
            }
            else if (vertical || grid.width == 1)
            {
                start_swipe(VERTICAL);
            }

            if (state.direction == UNKNOWN)
                return;
        }

        const double cap = speed_cap->as_cached_double();
        const double fac = speed_factor->as_cached_double();

        state.delta_prev = state.delta_last;
        if (state.direction == HORIZONTAL)
        {
            state.delta_sum += vswipe_process_delta(ev->dx, state.delta_sum,
                state.vx, state.vw, cap, fac);
            state.delta_last = ev->dx;
        } else
        {
            state.delta_sum += vswipe_process_delta(ev->dy, state.delta_sum,
                state.vy, state.vh, cap, fac);
            state.delta_last = ev->dy;
        }

        output->render->damage_whole();
    };

    wf::signal_callback_t on_swipe_end = [=] (wf::signal_data_t *data)
    {
        if (!state.swiping)
            return;

        state.swiping = false;

        auto ev = static_cast<wf::swipe_end_signal*>(data)->ev;

        const double move_threshold =
            clamp(threshold->as_cached_double(), 0.0, 1.0);
        const double fast_threshold =
            clamp(delta_threshold->as_cached_double(), 0.0, 1000.0);

        int target_delta = 0;
        wf_point target_workspace = {state.vx, state.vy};

        if (!ev->cancelled || ignore_cancel->as_int())
        {
            switch (state.direction)
            {
                case UNKNOWN:
                    target_delta = 0;
                    break;
                case HORIZONTAL:
                    target_delta = vswipe_finish_target(state.delta_sum,
                        state.vx, state.vw, state.delta_prev + state.delta_last,
                        move_threshold, fast_threshold);
                    target_workspace.x -= target_delta;
                    break;
                case VERTICAL:
                    target_delta = vswipe_finish_target(state.delta_sum,
                        state.vy, state.vh, state.delta_prev + state.delta_last,
                        move_threshold, fast_threshold);
                    target_workspace.y -= target_delta;
                    break;
            }
        }

        transition = {state.delta_sum, target_delta + state.gap * target_delta};

        output->workspace->set_workspace(target_workspace);
        output->render->set_redraw_always();
        duration.start();
    };

    void finalize_and_exit()
    {
        state.swiping = false;
        grab_interface->ungrab();
        output->deactivate_plugin(grab_interface);

        if (streams.prev.running)
            output->render->workspace_stream_stop(streams.prev);
        output->render->workspace_stream_stop(streams.curr);
        if (streams.next.running)
            output->render->workspace_stream_stop(streams.next);

        output->render->set_renderer(nullptr);
        output->render->set_redraw_always(false);
    }

    void fini()
    {
        if (state.swiping)
            finalize_and_exit();

        OpenGL::render_begin();
        streams.prev.buffer.release();
        streams.curr.buffer.release();
        streams.next.buffer.release();
        OpenGL::render_end();
    }
};

DECLARE_WAYFIRE_PLUGIN(vswipe);
